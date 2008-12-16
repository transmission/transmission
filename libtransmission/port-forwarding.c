/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>

#include "transmission.h"
#include "natpmp.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "port-forwarding.h"
#include "torrent.h"
#include "trevent.h"
#include "upnp.h"
#include "utils.h"

static const char *
getKey( void ) { return _( "Port Forwarding" ); }

struct tr_shared
{
    tr_bool               isEnabled;
    tr_bool               isShuttingDown;

    tr_port_forwarding    natpmpStatus;
    tr_port_forwarding    upnpStatus;

    tr_bool               shouldChange;
    tr_socketList       * bindSockets;
    tr_port               publicPort;

    tr_timer            * pulseTimer;

    tr_upnp             * upnp;
    tr_natpmp           * natpmp;
    tr_session          * session;
};

/***
****
***/

static const char*
getNatStateStr( int state )
{
    switch( state )
    {
        /* we're in the process of trying to set up port forwarding */
        case TR_PORT_MAPPING:
            return _( "Starting" );

        /* we've successfully forwarded the port */
        case TR_PORT_MAPPED:
            return _( "Forwarded" );

        /* we're cancelling the port forwarding */
        case TR_PORT_UNMAPPING:
            return _( "Stopping" );

        /* the port isn't forwarded */
        case TR_PORT_UNMAPPED:
            return _( "Not forwarded" );

        case TR_PORT_ERROR:
            return "???";
    }

    return "notfound";
}

static void
natPulse( tr_shared * s )
{
    const tr_port port = s->publicPort;
    const int     isEnabled = s->isEnabled && !s->isShuttingDown;
    int           oldStatus;
    int           newStatus;

    oldStatus = tr_sharedTraversalStatus( s );
    s->natpmpStatus = tr_natpmpPulse( s->natpmp, port, isEnabled );
    s->upnpStatus = tr_upnpPulse( s->upnp, port, isEnabled );
    newStatus = tr_sharedTraversalStatus( s );

    if( newStatus != oldStatus )
        tr_ninf( getKey( ), _( "State changed from \"%1$s\" to \"%2$s\"" ),
                getNatStateStr( oldStatus ),
                getNatStateStr( newStatus ) );
}

/*
 * Callbacks for socket list
 */
static void
closeCb( int * const socket,
         tr_address * const addr,
         void * const userData )
{
    tr_shared * s = ( tr_shared * )userData;
    if( *socket >= 0 )
    {
        tr_ninf( getKey( ), _( "Closing port %d on %s" ), s->publicPort,
                tr_ntop_non_ts( addr ) );
        tr_netClose( *socket );
    }
}

static void
acceptCb( int        * const socket,
          tr_address * const addr UNUSED,
          void       * const userData )
{
    tr_shared * s = ( tr_shared * )userData;
    tr_address clientAddr;
    tr_port clientPort;
    int clientSocket;
    clientSocket = tr_netAccept( s->session, *socket, &clientAddr, &clientPort );
    if( clientSocket > 0 )
    {
        tr_deepLog( __FILE__, __LINE__, NULL,
                   "New INCOMING connection %d (%s)",
                   clientSocket, tr_peerIoAddrStr( &clientAddr, clientPort ) );
        
        tr_peerMgrAddIncoming( s->session->peerMgr, &clientAddr, clientPort,
                              clientSocket );
    }
}

static void
bindCb( int * const socket,
        tr_address * const addr,
        void * const userData )
{
    tr_shared * s = ( tr_shared * )userData;
    *socket = tr_netBindTCP( addr, s->publicPort, FALSE );
    if( *socket >= 0 )
    {
        tr_ninf( getKey( ),
                _( "Opened port %d on %s to listen for incoming peer connections" ),
                s->publicPort, tr_ntop_non_ts( addr ) );
        listen( *socket, 10 );
    }
    else
    {
        tr_nerr( getKey( ),
                _(
                  "Couldn't open port %d on %s to listen for incoming peer connections (errno %d - %s)" ),
                s->publicPort, tr_ntop_non_ts( addr ), errno, tr_strerror( errno ) );
    }
}

static void
incomingPeersPulse( tr_shared * s )
{
    int allPaused;
    tr_torrent * tor;
    
    if( s->shouldChange )
    {
        tr_socketListForEach( s->bindSockets, &closeCb, s );
        s->shouldChange = FALSE;
        if( s->publicPort > 0 )
            tr_socketListForEach( s->bindSockets, &bindCb, s );
    }
    
    /* see if any torrents aren't paused */
    allPaused = 1;
    tor = NULL;
    while( ( tor = tr_torrentNext( s->session, tor ) ) )
    {
        if( TR_STATUS_IS_ACTIVE( tr_torrentGetActivity( tor ) ) )
        {
            allPaused = 0;
            break;
        }
    }
    
    /* if we have any running torrents, check for new incoming peer connections */
    /* (jhujhiti):
     * This has been changed from a loop that will end when the listener queue
     * is exhausted to one that will only check for one connection at a time.
     * I think it unlikely that we get many more than one connection in the
     * time between pulses (currently one second). However, just to be safe,
     * I have increased the length of the listener queue from 5 to 10
     * (see acceptCb() above). */
    if( !allPaused )
        tr_socketListForEach( s->bindSockets, &acceptCb, s );
}

static int
sharedPulse( void * vshared )
{
    tr_bool keepPulsing = 1;
    tr_shared * shared = vshared;

    natPulse( shared );

    if( !shared->isShuttingDown )
    {
        incomingPeersPulse( shared );
    }
    else
    {
        tr_ninf( getKey( ), _( "Stopped" ) );
        tr_timerFree( &shared->pulseTimer );
        tr_socketListForEach( shared->bindSockets, &closeCb, shared );
        tr_socketListFree( shared->bindSockets );
        tr_natpmpClose( shared->natpmp );
        tr_upnpClose( shared->upnp );
        shared->session->shared = NULL;
        tr_free( shared );
        keepPulsing = 0;
    }

    return keepPulsing;
}

/***
****
***/

static tr_socketList *
setupBindSockets( tr_port port )
{
    /* Do we care if an address is in use? Probably not, since it will be
     * caught later. This will only set up the list of sockets to bind. */
    int s4, s6;
    tr_socketList * socks = NULL;
    s6 = tr_netBindTCP( &tr_in6addr_any, port, TRUE );
    if( s6 >= 0 || -s6 != EAFNOSUPPORT ) /* we support ipv6 */
    {
        socks = tr_socketListNew( &tr_in6addr_any );
        listen( s6, 1 );
    }
    s4 = tr_netBindTCP( &tr_inaddr_any, port, TRUE );
    if( s4 >= 0 ) /* we bound *with* the ipv6 socket bound (need both)
                   * or only have ipv4 */
    {
        if( socks )
            tr_socketListAppend( socks, &tr_inaddr_any );
        else
            socks = tr_socketListNew( &tr_inaddr_any );
        tr_netClose( s4 );
    }
    if( s6 >= 0 )
        tr_netClose( s6 );
    return socks;
}

tr_shared *
tr_sharedInit( tr_session  * session,
               tr_bool       isEnabled,
               tr_port       publicPort )
{
    tr_shared * s = tr_new0( tr_shared, 1 );

    s->session      = session;
    s->publicPort   = publicPort;
    s->shouldChange = TRUE;
    s->bindSockets  = setupBindSockets( publicPort );
    s->shouldChange = TRUE;
    s->natpmp       = tr_natpmpInit( );
    s->upnp         = tr_upnpInit( );
    s->pulseTimer   = tr_timerNew( session, sharedPulse, s, 1000 );
    s->isEnabled    = isEnabled;
    s->upnpStatus   = TR_PORT_UNMAPPED;
    s->natpmpStatus = TR_PORT_UNMAPPED;

    return s;
}

void
tr_sharedShuttingDown( tr_shared * s )
{
    s->isShuttingDown = 1;
}

void
tr_sharedSetPort( tr_shared * s, tr_port  port )
{
    tr_torrent * tor = NULL;

    s->publicPort   = port;
    s->shouldChange = TRUE;

    while( ( tor = tr_torrentNext( s->session, tor ) ) )
        tr_torrentChangeMyPort( tor );
}

tr_port
tr_sharedGetPeerPort( const tr_shared * s )
{
    return s->publicPort;
}

void
tr_sharedTraversalEnable( tr_shared * s, tr_bool isEnabled )
{
    s->isEnabled = isEnabled;
}

tr_bool
tr_sharedTraversalIsEnabled( const tr_shared * s )
{
    return s->isEnabled;
}

int
tr_sharedTraversalStatus( const tr_shared * s )
{
    return MAX( s->natpmpStatus, s->upnpStatus );
}

