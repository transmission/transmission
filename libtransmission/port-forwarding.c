/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
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
#include "session.h"
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
    tr_bool               doPortCheck;

    tr_port_forwarding    natpmpStatus;
    tr_port_forwarding    upnpStatus;

    tr_upnp             * upnp;
    tr_natpmp           * natpmp;
    tr_session          * session;

    struct event        * timer;
};

/***
****
***/

static const char*
getNatStateStr( int state )
{
    switch( state )
    {
        case TR_PORT_MAPPING:   return _( "Starting" );
        case TR_PORT_MAPPED:    return _( "Forwarded" );
        case TR_PORT_UNMAPPING: return _( "Stopping" );
        case TR_PORT_UNMAPPED:  return _( "Not forwarded" );
        default:                return "???";
    }
}

static void
natPulse( tr_shared * s, tr_bool doPortCheck )
{
    const tr_port port = s->session->peerPort;
    const int isEnabled = s->isEnabled && !s->isShuttingDown;
    int oldStatus;
    int newStatus;

    if( s->natpmp == NULL )
        s->natpmp = tr_natpmpInit( );
    if( s->upnp == NULL )
        s->upnp = tr_upnpInit( );

    oldStatus = tr_sharedTraversalStatus( s );
    s->natpmpStatus = tr_natpmpPulse( s->natpmp, port, isEnabled );
    s->upnpStatus = tr_upnpPulse( s->upnp, port, isEnabled, doPortCheck );
    newStatus = tr_sharedTraversalStatus( s );

    if( newStatus != oldStatus )
        tr_ninf( getKey( ), _( "State changed from \"%1$s\" to \"%2$s\"" ),
                getNatStateStr( oldStatus ),
                getNatStateStr( newStatus ) );
}

static void
onTimer( int fd UNUSED, short what UNUSED, void * vshared )
{
    tr_shared * s = vshared;
    struct timeval interval;

    assert( s );
    assert( s->timer );

    /* do something */
    natPulse( s, s->doPortCheck );
    s->doPortCheck = FALSE;

    /* when to wake up again */
    switch( tr_sharedTraversalStatus( s ) )
    {
        case TR_PORT_MAPPED:
            /* if we're mapped, everything is fine... check back in 20 minutes
             * to renew the port forwarding if it's expired */
            s->doPortCheck = TRUE;
            interval.tv_sec = 60*20;
            break;

        case TR_PORT_ERROR:
            /* some kind of an error.  wait 60 seconds and retry */
            interval.tv_sec = 60;
            break;

        default:
            /* in progress.  pulse frequently. */
            interval.tv_sec = 0;
            interval.tv_usec = 333000;
            break;
    }

    evtimer_add( s->timer, &interval );
}

/***
****
***/

tr_shared *
tr_sharedInit( tr_session  * session, tr_bool isEnabled )
{
    tr_shared * s = tr_new0( tr_shared, 1 );

    s->session      = session;
    s->isEnabled    = isEnabled;
    s->upnpStatus   = TR_PORT_UNMAPPED;
    s->natpmpStatus = TR_PORT_UNMAPPED;

    if( isEnabled )
    {
        struct timeval timeval;
        timeval.tv_sec = 0;
        timeval.tv_usec = 333000;
        s->timer = tr_new0( struct event, 1 );
        evtimer_set( s->timer, onTimer, s );
        evtimer_add( s->timer, &timeval );
    }

    return s;
}

static void
stop_timer( tr_shared * s )
{
    if( s->timer != NULL )
    {
        evtimer_del( s->timer );
        tr_free( s->timer );
        s->timer = NULL;
    }
}

static void
stop_forwarding( tr_shared * s )
{
    tr_ninf( getKey( ), _( "Stopped" ) );
    natPulse( s, FALSE );
    tr_natpmpClose( s->natpmp );
    s->natpmp = NULL;
    tr_upnpClose( s->upnp );
    s->upnp = NULL;
    stop_timer( s );
}

void
tr_sharedClose( tr_session * session )
{
    tr_shared * s = session->shared;

    s->isShuttingDown = TRUE;
    stop_forwarding( s );
    s->session->shared = NULL;
    tr_free( s );
}

static void
start_timer( tr_shared * s )
{
    s->timer = tr_new0( struct event, 1 );
    evtimer_set( s->timer, onTimer, s );
    onTimer( 0, 0, s );
}

void
tr_sharedTraversalEnable( tr_shared * s, tr_bool isEnabled )
{
    if(( s->isEnabled = isEnabled ))
        start_timer( s );
    else
        stop_forwarding( s );
}

void
tr_sharedPortChanged( tr_session * session )
{
    tr_shared * s = session->shared;

    if( s->isEnabled )
    {
        stop_timer( s );
        start_timer( s );
    }
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
