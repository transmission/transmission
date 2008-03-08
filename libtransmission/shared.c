/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <errno.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>

#include "transmission.h"
#include "natpmp.h"
#include "net.h"
#include "peer-mgr.h"
#include "shared.h"
#include "torrent.h"
#include "trevent.h"
#include "upnp.h"
#include "utils.h"

static const char * getKey( void ) { return _( "Port Forwarding" ); }

struct tr_shared
{
    tr_handle * h;
    tr_timer  * pulseTimer;

    /* Incoming connections */
    int bindPort;
    int bindSocket;

    /* port forwarding */
    int isEnabled;
    int publicPort;
    tr_nat_traversal_status natStatus;
    tr_upnp * upnp;
    tr_natpmp  * natpmp;

    int isShuttingDown;
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
        case TR_NAT_TRAVERSAL_MAPPING:   return _( "starting" );
        /* we've successfully forwarded the port */
        case TR_NAT_TRAVERSAL_MAPPED:    return _( "forwarded" );
        /* we're cancelling the port forwarding */
        case TR_NAT_TRAVERSAL_UNMAPPING: return _( "stopping" );
        /* the port isn't forwarded */
        case TR_NAT_TRAVERSAL_UNMAPPED:  return _( "not forwarded" );
        case TR_NAT_TRAVERSAL_ERROR:     return "???";
    }

    return "notfound";
}

static void
natPulse( tr_shared * s )
{
    tr_nat_traversal_status status;
    const int port = s->publicPort;
    const int isEnabled = s->isEnabled && !s->isShuttingDown;

    status = tr_natpmpPulse( s->natpmp, port, isEnabled );
    if( status == TR_NAT_TRAVERSAL_ERROR )
        status = tr_upnpPulse( s->upnp, port, isEnabled );
    if( status != s->natStatus ) {
        tr_inf( _( "%s: state changed from \"%s\" to \"%s\"" ), getKey(), getNatStateStr(s->natStatus), getNatStateStr(status) );
        s->natStatus = status;
    }
}

static void
incomingPeersPulse( tr_shared * s )
{
    if( s->bindSocket >= 0 && ( s->bindPort != s->publicPort ) )
    {
        tr_inf( _( "%s: Closing port %d" ), getKey(), s->bindPort );
        tr_netClose( s->bindSocket );
        s->bindSocket = -1;
    }

    if( s->bindPort != s->publicPort )
    {
        int socket;
        errno = 0;
        socket = tr_netBindTCP( s->publicPort );
        if( socket >= 0 ) {
            tr_inf( _( "%s: Opened port %d to listen for incoming peer connections" ), getKey(), s->publicPort );
            s->bindPort = s->publicPort;
            s->bindSocket = socket;
            listen( s->bindSocket, 5 );
        } else {
            tr_err( _( "%s: Couldn't open port %d to listen for incoming peer connections (errno %d - %s)" ),
                    getKey(), s->publicPort, errno, tr_strerror(errno) );
            s->bindPort = -1;
            s->bindSocket = -1;
        }
    }
    
    for( ;; ) /* check for new incoming peer connections */    
    {
        int socket;
        uint16_t port;
        struct in_addr addr;

        if( s->bindSocket < 0 )
            break;

        socket = tr_netAccept( s->bindSocket, &addr, &port );
        if( socket < 0 )
            break;

        tr_peerMgrAddIncoming( s->h->peerMgr, &addr, port, socket );
    }
}

static int
sharedPulse( void * vshared )
{
    int keepPulsing = 1;
    tr_shared * shared = vshared;

    natPulse( shared );

    if( !shared->isShuttingDown )
    {
        incomingPeersPulse( shared );
    }
    else if( ( shared->natStatus == TR_NAT_TRAVERSAL_ERROR ) || ( shared->natStatus == TR_NAT_TRAVERSAL_UNMAPPED ) )
    {
        tr_dbg( _( "%s: stopped" ), getKey() );
        shared->h->shared = NULL;
        tr_netClose( shared->bindSocket );
        tr_natpmpClose( shared->natpmp );
        tr_upnpClose( shared->upnp );
        tr_free( shared );
        keepPulsing = 0;
    }

    return keepPulsing;
}

/***
****
***/

tr_shared *
tr_sharedInit( tr_handle * h, int isEnabled, int publicPort )
{
    tr_shared * s = tr_new0( tr_shared, 1 );

    s->h            = h;
    s->publicPort   = publicPort;
    s->bindPort     = -1;
    s->bindSocket   = -1;
    s->natpmp       = tr_natpmpInit();
    s->upnp         = tr_upnpInit();
    s->pulseTimer   = tr_timerNew( h, sharedPulse, s, 500 );
    s->isEnabled    = isEnabled ? 1 : 0;
    s->natStatus    = TR_NAT_TRAVERSAL_UNMAPPED;

    return s;
}

void
tr_sharedShuttingDown( tr_shared * s )
{
    s->isShuttingDown = 1;
}

void
tr_sharedSetPort( tr_shared * s, int port )
{
    tr_torrent * tor;

    s->publicPort = port;

    for( tor = s->h->torrentList; tor; tor = tor->next )
        tr_torrentChangeMyPort( tor );
}

int
tr_sharedGetPublicPort( const tr_shared * s )
{
    return s->publicPort;
}

void
tr_sharedTraversalEnable( tr_shared * s, int isEnabled )
{
    s->isEnabled = isEnabled;
}

int
tr_sharedTraversalStatus( const tr_shared * s )
{
    return s->natStatus;
}
