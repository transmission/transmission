/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <signal.h>
#include <sys/queue.h> /* for evhttp */
#include <sys/types.h> /* for evhttp */

#include <event.h>
#include <evdns.h>
#include <evhttp.h>

#include "transmission.h"
#include "list.h"
#include "platform.h"
#include "trevent.h"
#include "utils.h"

/* #define DEBUG */
#ifdef DEBUG
#include <stdio.h>
#undef tr_dbg
#define tr_dbg( a, b... ) fprintf(stderr, a "\n", ##b )
#endif

/***
****
***/

typedef struct tr_event_handle
{
    tr_lock * lock;
    tr_handle * h;
    tr_thread * thread;
    tr_list * commands;
    struct event_base * base;
    struct event pulse;
    struct timeval pulseInterval;
    uint8_t die;

    int timerCount; 
}
tr_event_handle;

#ifdef DEBUG
static int reads = 0;
static int writes = 0;
#endif

enum mode
{
    TR_EV_EVHTTP_MAKE_REQUEST,
    TR_EV_BUFFEREVENT_SET,
    TR_EV_BUFFEREVENT_WRITE,
    TR_EV_TIMER_ADD,
    TR_EV_TIMER_DEL,
    TR_EV_EXEC
};

typedef int timer_func(void*);

struct tr_timer
{
    struct event event;
    struct timeval tv;
    timer_func * func;
    void * user_data;
    struct tr_event_handle * eh;
    uint8_t inCallback;
};

struct tr_event_command
{
    int mode;

    struct tr_timer * timer;

    struct evhttp_connection * evcon;
    struct evhttp_request * req;
    enum evhttp_cmd_type evtype;
    char * uri;

    struct bufferevent * bufev;
    short enable;
    short disable;
    char * buf;
    size_t buflen;

    void (*func)( void* );
    void * user_data;
};

static void
pumpList( int i UNUSED, short s UNUSED, void * veh )
{
    tr_event_handle * eh = veh;
    int doDie;

    for( ;; )
    {
        struct tr_event_command * cmd;

        doDie = eh->die && !eh->timerCount;
        if( doDie )
            break;

        /* get the next command */
        tr_lockLock( eh->lock );
        cmd = tr_list_pop_front( &eh->commands );
        tr_lockUnlock( eh->lock );
        if( cmd == NULL )
            break;

        /* process the command */
        switch( cmd->mode )
        {
            case TR_EV_TIMER_ADD:
                timeout_add( &cmd->timer->event, &cmd->timer->tv );
                ++eh->timerCount;
                break;

            case TR_EV_TIMER_DEL:
                event_del( &cmd->timer->event );
                tr_free( cmd->timer );
                --eh->timerCount;
                break;

            case TR_EV_EVHTTP_MAKE_REQUEST:
                evhttp_make_request( cmd->evcon, cmd->req, cmd->evtype, cmd->uri );
                tr_free( cmd->uri );
                break;

           case TR_EV_BUFFEREVENT_SET:
                bufferevent_enable( cmd->bufev, cmd->enable );
                bufferevent_disable( cmd->bufev, cmd->disable );
                break;

            case TR_EV_BUFFEREVENT_WRITE:
                bufferevent_write( cmd->bufev, cmd->buf, cmd->buflen );
                tr_free( cmd->buf );
                break;

            case TR_EV_EXEC:
                (cmd->func)( cmd->user_data );
                break;

            default:
                assert( 0 && "unhandled command type!" );
        }

        /* cleanup */
        tr_free( cmd );
    }

    if( !doDie )
        timeout_add( &eh->pulse, &eh->pulseInterval );
    else {
        assert( eh->timerCount ==  0 );
        evdns_shutdown( FALSE );
        event_del( &eh->pulse );
    }
}

static void
logFunc( int severity, const char * message )
{
    switch( severity )
    {
        case _EVENT_LOG_DEBUG: 
            tr_dbg( "%s", message );
            break;
        case _EVENT_LOG_ERR:
            tr_err( "%s", message );
            break;
        default:
            tr_inf( "%s", message );
            break;
    }
}

static void
libeventThreadFunc( void * veh )
{
    tr_event_handle * eh = (tr_event_handle *) veh;
    tr_dbg( "Starting libevent thread" );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    eh->base = event_init( );
    evdns_init( );
    timeout_set( &eh->pulse, pumpList, veh );
    timeout_add( &eh->pulse, &eh->pulseInterval );
    eh->h->events = eh;

    event_dispatch( );

    tr_lockFree( eh->lock );
    event_base_free( eh->base );

    eh->h->events = NULL;

    tr_free( eh );
    tr_dbg( "Closing libevent thread" );
}

void
tr_eventInit( tr_handle * handle )
{
    tr_event_handle * eh;

    eh = tr_new0( tr_event_handle, 1 );
    eh->lock = tr_lockNew( );
    eh->h = handle;
    eh->pulseInterval = timevalMsec( 20 );
    eh->thread = tr_threadNew( libeventThreadFunc, eh, "libeventThreadFunc" );
}

void
tr_eventClose( tr_handle * handle )
{
    tr_event_handle * eh = handle->events;

    tr_lockLock( eh->lock );
    tr_list_free( &eh->commands, tr_free );
    eh->die = TRUE;
    tr_lockUnlock( eh->lock );
}

/**
***
**/

static void
pushList( struct tr_event_handle * eh, struct tr_event_command * command )
{
    tr_lockLock( eh->lock );
    tr_list_append( &eh->commands, command );
    tr_lockUnlock( eh->lock );
}

void
tr_evhttp_make_request (tr_handle                 * handle,
                        struct evhttp_connection  * evcon,
                        struct evhttp_request     * req,
                        enum   evhttp_cmd_type      type,
                        char                      * uri)
{
    if( tr_amInThread( handle->events->thread ) ) {
        evhttp_make_request( evcon, req, type, uri );
        tr_free( uri );
    } else {
        struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
        cmd->mode = TR_EV_EVHTTP_MAKE_REQUEST;
        cmd->evcon = evcon;
        cmd->req = req;
        cmd->evtype = type;
        cmd->uri = uri;
        pushList( handle->events, cmd );
    }
}

void
tr_bufferevent_write( tr_handle             * handle,
                      struct bufferevent    * bufev,
                      const void            * buf,
                      size_t                  buflen )
{
    if( tr_amInThread( handle->events->thread ) )
        bufferevent_write( bufev, (void*)buf, buflen );
    else {
        struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
        cmd->mode = TR_EV_BUFFEREVENT_WRITE;
        cmd->bufev = bufev;
        cmd->buf = tr_strndup( buf, buflen );
        cmd->buflen = buflen;
        pushList( handle->events, cmd );
    }
}

void
tr_setBufferEventMode( struct tr_handle   * handle,
                       struct bufferevent * bufev,
                       short                mode_enable,
                       short                mode_disable )
{
    if( tr_amInThread( handle->events->thread ) ) {
        bufferevent_enable( bufev, mode_enable );
        bufferevent_disable( bufev, mode_disable );
    } else {
        struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
        cmd->mode = TR_EV_BUFFEREVENT_SET;
        cmd->bufev = bufev;
        cmd->enable = mode_enable;
        cmd->disable = mode_disable;
        pushList( handle->events, cmd );
    }
}

/**
***
**/

static int
timerCompareFunc( const void * va, const void * vb )
{
    const struct tr_event_command * a = va;
    const struct tr_timer * b = vb;
    return a->timer == b ? 0 : 1;
}

static void
timerCallback( int fd UNUSED, short event UNUSED, void * vtimer )
{
    int more;
    struct tr_timer * timer = vtimer;
    void * del;

    del = tr_list_remove( &timer->eh->commands, timer, timerCompareFunc );

    if( del != NULL ) /* there's a TIMER_DEL command queued for this timer... */
        more = FALSE;
    else {
        timer->inCallback = 1;
        more = (*timer->func)( timer->user_data );
        timer->inCallback = 0;
    }

    if( more )
        timeout_add( &timer->event, &timer->tv );
    else
        tr_timerFree( &timer );

    tr_free( del );
}

void
tr_timerFree( tr_timer ** ptimer )
{
    tr_timer * timer;

    /* zero out the argument passed in */
    assert( ptimer );
    timer = *ptimer;
    *ptimer = NULL;

    /* destroy the timer directly or via the command queue */
    if( timer!=NULL && !timer->inCallback ) {
        if( tr_amInThread( timer->eh->thread ) ) {
            void * del = tr_list_remove( &timer->eh->commands, timer, timerCompareFunc );
            --timer->eh->timerCount;
            event_del( &timer->event );
            tr_free( timer );
            tr_free( del );
        } else {
            struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
            cmd->mode = TR_EV_TIMER_DEL;
            cmd->timer = timer;
            pushList( timer->eh, cmd );
        }
    }
}

tr_timer*
tr_timerNew( struct tr_handle * handle,
             timer_func         func,
             void             * user_data,
             uint64_t           timeout_milliseconds )
{
    tr_timer * timer = tr_new0( tr_timer, 1 );
    timer->tv = timevalMsec( timeout_milliseconds );
    timer->func = func;
    timer->user_data = user_data;
    timer->eh = handle->events;
    timeout_set( &timer->event, timerCallback, timer );

    if( tr_amInThread( handle->events->thread ) ) {
        timeout_add( &timer->event,  &timer->tv );
        ++handle->events->timerCount;
    } else {
        struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
        cmd->mode = TR_EV_TIMER_ADD;
        cmd->timer = timer;
        pushList( handle->events, cmd );
    }

    return timer;
}

void
tr_runInEventThread( struct tr_handle * handle,
                     void               func( void* ),
                     void             * user_data )
{
    if( tr_amInThread( handle->events->thread ) )
        (func)( user_data );
    else {
        struct tr_event_command * cmd = tr_new0( struct tr_event_command, 1 );
        cmd->mode = TR_EV_EXEC;
        cmd->func = func;
        cmd->user_data = user_data;
        pushList( handle->events, cmd );
    }
}
