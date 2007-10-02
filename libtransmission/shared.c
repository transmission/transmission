/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/types.h>

#include "transmission.h"
#include "handshake.h"
#include "natpmp.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "platform.h"
#include "shared.h"
#include "trevent.h"
#include "upnp.h"
#include "utils.h"

struct tr_shared
{
    tr_handle    * h;
    tr_timer     * pulseTimer;

    /* Incoming connections */
    int publicPort;
    int bindPort;
    int bindSocket;

    /* NAT-PMP/UPnP */
    tr_natpmp_t  * natpmp;
    tr_upnp_t    * upnp;
};

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static int SharedLoop( void * );
static void SetPublicPort( tr_shared *, int );
static void AcceptPeers( tr_shared * );


/***********************************************************************
 * tr_sharedInit
 ***********************************************************************
 *
 **********************************************************************/
tr_shared * tr_sharedInit( tr_handle * h )
{
    tr_shared * s = calloc( 1, sizeof( tr_shared ) );

    s->h          = h;
    s->publicPort = -1;
    s->bindPort   = -1;
    s->bindSocket = -1;
    s->natpmp     = tr_natpmpInit();
    s->upnp       = tr_upnpInit();
    s->pulseTimer   = tr_timerNew( h, SharedLoop, s, 250 );

    return s;
}

/***********************************************************************
 * tr_sharedClose
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedClose( tr_shared * s )
{
    tr_timerFree( &s->pulseTimer );

    tr_netClose( s->bindSocket );
    tr_natpmpClose( s->natpmp );
    tr_upnpClose( s->upnp );
    free( s );
}

/***********************************************************************
 * tr_sharedSetPort
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedSetPort( tr_shared * s, int port )
{
#ifdef BEOS_NETSERVER
    /* BeOS net_server seems to be unable to set incoming connections
     * to non-blocking. Too bad. */
    return;
#endif

    tr_globalLock( s->h );

    if( port == s->bindPort )
    {
        tr_globalUnlock( s->h );
        return;
    }
    s->bindPort = port;

    /* Close the previous accept socket, if any */
    if( s->bindSocket > -1 )
    {
        tr_netClose( s->bindSocket );
    }

    /* Create the new one */
    s->bindSocket = tr_netBindTCP( port );

    /* Notify the trackers */
    SetPublicPort( s, port );

    /* XXX should handle failure here in a better way */
    if( s->bindSocket < 0 )
    {
        /* Remove the forwarding for the old port */
        tr_natpmpRemoveForwarding( s->natpmp );
        tr_upnpRemoveForwarding( s->upnp );
    }
    else
    {
        tr_inf( "Bound listening port %d", port );
        listen( s->bindSocket, 5 );
        /* Forward the new port */
        tr_natpmpForwardPort( s->natpmp, port );
        tr_upnpForwardPort( s->upnp, port );
    }

    tr_globalUnlock( s->h );
}

int
tr_sharedGetPublicPort( const tr_shared * s )
{
    return s->publicPort;
}

/***********************************************************************
 * tr_sharedTraversalEnable, tr_sharedTraversalStatus
 ***********************************************************************
 *
 **********************************************************************/
void tr_sharedTraversalEnable( tr_shared * s, int enable )
{
    if( enable )
    {
        tr_natpmpStart( s->natpmp );
        tr_upnpStart( s->upnp );
    }
    else
    {
        tr_natpmpStop( s->natpmp );
        tr_upnpStop( s->upnp );
    }
}

int tr_sharedTraversalStatus( const tr_shared * s )
{
    const int statuses[] = {
        TR_NAT_TRAVERSAL_MAPPED,
        TR_NAT_TRAVERSAL_MAPPING,
        TR_NAT_TRAVERSAL_UNMAPPING,
        TR_NAT_TRAVERSAL_ERROR,
        TR_NAT_TRAVERSAL_NOTFOUND,
        TR_NAT_TRAVERSAL_DISABLED,
        -1,
    };
    int natpmp, upnp, ii;

    natpmp = tr_natpmpStatus( s->natpmp );
    upnp = tr_upnpStatus( s->upnp );

    for( ii = 0; 0 <= statuses[ii]; ii++ )
    {
        if( statuses[ii] == natpmp || statuses[ii] == upnp )
        {
            return statuses[ii];
        }
    }

    assert( 0 );

    return TR_NAT_TRAVERSAL_ERROR;

}


/***********************************************************************
 * Local functions
 **********************************************************************/

/***********************************************************************
 * SharedLoop
 **********************************************************************/
static int
SharedLoop( void * vs )
{
    int newPort;
    tr_shared * s = vs;

    tr_globalLock( s->h );

    /* NAT-PMP and UPnP pulses */
    newPort = -1;
    tr_natpmpPulse( s->natpmp, &newPort );
    if( 0 < newPort && newPort != s->publicPort )
        SetPublicPort( s, newPort );
    tr_upnpPulse( s->upnp );

    /* Handle incoming connections */
    AcceptPeers( s );

    tr_globalUnlock( s->h );

    return TRUE;
}

/***********************************************************************
 * SetPublicPort
 **********************************************************************/
static void SetPublicPort( tr_shared * s, int port )
{
    tr_handle * h = s->h;
    tr_torrent * tor;

    s->publicPort = port;

    for( tor = h->torrentList; tor; tor = tor->next )
        tr_torrentChangeMyPort( tor );
}

/***********************************************************************
 * AcceptPeers
 ***********************************************************************
 * Check incoming connections and add the peers to our local list
 **********************************************************************/

static void
AcceptPeers( tr_shared * s )
{
    for( ;; )
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
