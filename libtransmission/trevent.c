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

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/queue.h> /* for evhttp */
#include <sys/types.h> /* for evhttp */

#ifdef WIN32
  #include <fcntl.h>
  #define pipe(f) _pipe(f, 1000, _O_BINARY)
#else
  #include <unistd.h>
#endif

#include <event.h>
#include <evhttp.h>

#include "transmission.h"
#include "platform.h"
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

typedef struct tr_event_handle_s
{
    int fds[2];
    tr_lock_t * lock;
    tr_handle_t * h;
    tr_thread_t * thread;
    struct event_base * base;
    struct event pipeEvent;
}
tr_event_handle_t;

#ifdef DEBUG
static int reads = 0;
static int writes = 0;
#endif


void
readFromPipe( int fd, short eventType UNUSED, void * unused UNUSED )
{
    char ch;
    int ret;
    struct event * event;
    struct timeval interval;
    struct evhttp_connection * evcon;
    struct evhttp_request * req;
    enum evhttp_cmd_type type;
    char * uri;
    char * buf;
    size_t buflen;
    struct bufferevent * bufev;
    short mode;

#ifdef DEBUG
    fprintf( stderr, "reading...reads: [%d] writes: [%d]\n", ++reads, writes );
#endif

    ch = '\0';
    do {
        ret = read( fd, &ch, 1 );
    } while ( ret<0 && errno==EAGAIN );

    if( ret < 0 )
    {
        tr_err( "Couldn't read from libevent pipe: %s", strerror(errno) );
    }
    else switch( ch )
    {
        case 'd': /* event_del */
            read( fd, &event, sizeof(struct event*) );
            event_del( event );
            tr_free( event );
            break;

        case 'e': /* event_add */
            read( fd, &event, sizeof(struct event*) );
            read( fd, &interval, sizeof(struct timeval) );
            event_add( event, &interval );
            break;

        case 'h': /* http_make_request */
            ret = read( fd, &evcon, sizeof(struct evhttp_connection*) );
            read( fd, &req, sizeof(struct evhttp_request*) );
            read( fd, &type, sizeof(enum evhttp_cmd_type) );
            read( fd, &uri, sizeof(char*) );
            evhttp_make_request( evcon, req, type, uri );
            tr_free( uri );
            break;

        case 'm': /* set bufferevent mode */
            read( fd, &bufev, sizeof(struct evhttp_request*) );
            mode = 0;
            read( fd, &mode, sizeof(short) );
            bufferevent_enable( bufev, mode );
            mode = 0;
            read( fd, &mode, sizeof(short) );
            bufferevent_disable( bufev, mode );
fprintf( stderr, "after enable/disable, the mode is %hd\n", bufev->enabled );
            break;

        case 'w': /* bufferevent_write */
            read( fd, &bufev, sizeof(struct bufferevent*) );
            read( fd, &buf, sizeof(char*) );
            read( fd, &buflen, sizeof(size_t) );
            bufferevent_write( bufev, buf, buflen );
            tr_free( buf );
            break;

        default:
            assert( 0 && "unhandled event pipe condition!" );
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
    tr_event_handle_t * eh = (tr_event_handle_t *) veh;
    tr_dbg( "Starting libevent thread" );

    eh->base = event_init( );
    event_set_log_callback( logFunc );

    /* listen to the pipe's read fd */
    event_set( &eh->pipeEvent, eh->fds[0], EV_READ|EV_PERSIST, readFromPipe, NULL );
    event_add( &eh->pipeEvent, NULL );

    event_dispatch( );

    event_del( &eh->pipeEvent );
    tr_lockFree( eh->lock );
    event_base_free( eh->base );
    tr_free( eh );

    tr_dbg( "Closing libevent thread" );
}

void
tr_eventInit( tr_handle_t * handle )
{
    tr_event_handle_t * eh;

    eh = tr_new0( tr_event_handle_t, 1 );
    eh->lock = tr_lockNew( );
    pipe( eh->fds );
    eh->h = handle;
    handle->events = eh;
    eh->thread = tr_threadNew( libeventThreadFunc, eh, "libeventThreadFunc" );
}

void
tr_eventClose( tr_handle_t * handle )
{
    tr_event_handle_t * eh = handle->events;

    event_base_loopexit( eh->base, NULL );
}

void
tr_event_add( tr_handle_t    * handle,
              struct event   * event,
              struct timeval * interval )
{
    if( tr_amInThread( handle->events->thread ) )
    {
        event_add( event, interval );
    }
    else
    {
        const char ch = 'e';
        int fd = handle->events->fds[1];
        tr_lock_t * lock = handle->events->lock;

        tr_lockLock( lock );
        tr_dbg( "writing event to pipe: event.ev_arg is %p", event->ev_arg );
        write( fd, &ch, 1 );
        write( fd, &event, sizeof(struct event*) );
        write( fd, interval, sizeof(struct timeval) );
        tr_lockUnlock( lock );
    }
}

void
tr_event_del( tr_handle_t    * handle,
              struct event   * event )
{
    if( tr_amInThread( handle->events->thread ) )
    {
        event_del( event );
        tr_free( event );
    }
    else
    {
        const char ch = 'd';
        int fd = handle->events->fds[1];
        tr_lock_t * lock = handle->events->lock;

        tr_lockLock( lock );
        tr_dbg( "writing event to pipe: del event %p", event );
        write( fd, &ch, 1 );
        write( fd, &event, sizeof(struct event*) );
        tr_lockUnlock( lock );
    }
}

void
tr_evhttp_make_request (tr_handle_t               * handle,
                        struct evhttp_connection  * evcon,
                        struct evhttp_request     * req,
                        enum   evhttp_cmd_type      type,
                        char                      * uri)
{
    if( tr_amInThread( handle->events->thread ) )
    {
        evhttp_make_request( evcon, req, type, uri );
        tr_free( uri );
    }
    else
    {
        const char ch = 'h';
        int fd = handle->events->fds[1];
        tr_lock_t * lock = handle->events->lock;

        tr_lockLock( lock );
        tr_dbg( "writing HTTP req to pipe: req.cb_arg is %p", req->cb_arg );
        write( fd, &ch, 1 );
        write( fd, &evcon, sizeof(struct evhttp_connection*) );
        write( fd, &req, sizeof(struct evhttp_request*) );
        write( fd, &type, sizeof(enum evhttp_cmd_type) );
        write( fd, &uri, sizeof(char*) );
        tr_lockUnlock( lock );
    }
}

void
tr_bufferevent_write( tr_handle_t           * handle,
                      struct bufferevent    * bufev,
                      const void            * buf,
                      size_t                  buflen )
{
    if( tr_amInThread( handle->events->thread ) )
    {
        bufferevent_write( bufev, (void*)buf, buflen );
    }
    else
    {
        const char ch = 'w';
        int fd = handle->events->fds[1];
        tr_lock_t * lock = handle->events->lock;
        char * local = tr_strndup( buf, buflen );

        tr_lockLock( lock );
        tr_dbg( "writing bufferevent_write pipe" );
        write( fd, &ch, 1 );
        write( fd, &bufev, sizeof(struct bufferevent*) );
        write( fd, &local, sizeof(char*) );
        write( fd, &buflen, sizeof(size_t) );
        tr_lockUnlock( lock );
    }
}

void
tr_setBufferEventMode( struct tr_handle_s   * handle,
                       struct bufferevent * bufev,
                       short                mode_enable,
                       short                mode_disable )
{
    if( tr_amInThread( handle->events->thread ) )
    {
        bufferevent_enable( bufev, mode_enable );
        bufferevent_disable( bufev, mode_disable );
    }
    else
    {
        const char ch = 'm';
        int fd = handle->events->fds[1];
        tr_lock_t * lock = handle->events->lock;

        tr_lockLock( lock );
        write( fd, &ch, 1 );
        write( fd, &bufev, sizeof(struct bufferevent*) );
        write( fd, &mode_enable, sizeof(short) );
        write( fd, &mode_disable, sizeof(short) );
        tr_lockUnlock( lock );
    }
}
