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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

//#define DEBUG
#ifdef DEBUG
#undef tr_dbg
#define tr_dbg( a, b... ) fprintf(stderr, a "\n", ##b )
#endif

/***
****
***/

typedef struct tr_event_handle_s
{
    int fds[2];
    int isShuttingDown;
    tr_lock_t * lock;
    tr_handle_t * h;
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
            tr_dbg( "read del event from pipe: event is %p", event );
            event_del( event );
            break;

        case 'e': /* event_add */
            read( fd, &event, sizeof(struct event*) );
            read( fd, &interval, sizeof(struct timeval) );
            tr_dbg( "read event from pipe: event.ev_arg is %p", event->ev_arg );
            event_add( event, &interval );
            break;

        case 'h': /* http_make_request */
            ret = read( fd, &evcon, sizeof(struct evhttp_connection*) );
            read( fd, &req, sizeof(struct evhttp_request*) );
            read( fd, &type, sizeof(enum evhttp_cmd_type) );
            read( fd, &uri, sizeof(char*) );
            tr_dbg( "read http req from pipe: req.cb_arg is %p", req->cb_arg );
            evhttp_make_request( evcon, req, type, uri );
            tr_free( uri );
            break;

        default:
            assert( 0 && "unhandled event pipe condition!" );
    }
}

static void
libeventThreadFunc( void * veh )
{
    tr_event_handle_t * eh = (tr_event_handle_t *) veh;
    tr_dbg( "libevent thread starting" );

    event_init( );

    /* listen to the pipe's read fd */
    event_set( &eh->pipeEvent, eh->fds[0], EV_READ|EV_PERSIST, readFromPipe, NULL );
    event_add( &eh->pipeEvent, NULL );

    while( !eh->isShuttingDown ) {
        event_dispatch( );
        tr_wait( 50 ); /* 1/20th of a second */
    }

    tr_dbg( "libevent thread exiting" );
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
    tr_threadNew( libeventThreadFunc, eh, "libeventThreadFunc" );
}

void
tr_eventClose( tr_handle_t * handle UNUSED )
{
    fprintf( stderr, "%s:%d FIXME\n", __FILE__, __LINE__ );
}

void
tr_event_add( tr_handle_t    * handle,
              struct event   * event,
              struct timeval * interval )
{
    const char ch = 'e';
    int fd = handle->events->fds[1];
    tr_lock_t * lock = handle->events->lock;

    tr_lockLock( lock );
    tr_dbg( "writing event to pipe: event.ev_arg is %p", event->ev_arg );
#ifdef DEBUG
    fprintf( stderr, "reads: [%d] writes: [%d]\n", reads, ++writes );
#endif
    write( fd, &ch, 1 );
    write( fd, &event, sizeof(struct event*) );
    write( fd, interval, sizeof(struct timeval) );
    tr_lockUnlock( lock );
}

void
tr_event_del( tr_handle_t    * handle,
              struct event   * event )
{
    const char ch = 'd';
    int fd = handle->events->fds[1];
    tr_lock_t * lock = handle->events->lock;

    tr_lockLock( lock );
    tr_dbg( "writing event to pipe: del event %p", event );
#ifdef DEBUG
    fprintf( stderr, "reads: [%d] writes: [%d]\n", reads, ++writes );
#endif
    write( fd, &ch, 1 );
    write( fd, &event, sizeof(struct event*) );
    tr_lockUnlock( lock );
}

void
tr_evhttp_make_request (tr_handle_t               * handle,
                        struct evhttp_connection  * evcon,
                        struct evhttp_request     * req,
                        enum   evhttp_cmd_type      type,
                        const char                * uri)
{
    const char ch = 'h';
    int fd = handle->events->fds[1];
    tr_lock_t * lock = handle->events->lock;

    tr_lockLock( lock );
    tr_dbg( "writing HTTP req to pipe: req.cb_arg is %p", req->cb_arg );
#ifdef DEBUG
    fprintf( stderr, "reads: [%d] writes: [%d]\n", reads, ++writes );
#endif
    write( fd, &ch, 1 );
    write( fd, &evcon, sizeof(struct evhttp_connection*) );
    write( fd, &req, sizeof(struct evhttp_request*) );
    write( fd, &type, sizeof(enum evhttp_cmd_type) );
    write( fd, &uri, sizeof(char*) );
    tr_lockUnlock( lock );
}
