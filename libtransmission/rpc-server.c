/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
#include <string.h>

#include <third-party/libevent/event.h>
#include <third-party/shttpd/shttpd.h>

#include "transmission.h"
#include "rpc.h"
#include "rpc-server.h"
#include "utils.h"

#define BUSY_INTERVAL_MSEC 30
#define IDLE_INTERVAL_MSEC 1000

struct tr_rpc_server
{
    int port;
    struct shttpd_ctx * ctx;
    tr_handle * session;
    struct evbuffer * in;
    struct evbuffer * out;
    struct event timer;
    char * acl;
};

static void
handle_rpc( struct shttpd_arg * arg )
{
    struct tr_rpc_server * s = arg->user_data;

    if( !EVBUFFER_LENGTH( s->out ) )
    {
        int len = 0;
        char * response = NULL;
        const char * request_method = shttpd_get_env( arg, "REQUEST_METHOD" );
        const char * query_string = shttpd_get_env( arg, "QUERY_STRING" );

        if( query_string && *query_string )
            response = tr_rpc_request_exec_uri( s->session,
                                                query_string,
                                                strlen( query_string ),
                                                &len );
        else if( !strcmp( request_method, "POST" ) )
        {
            evbuffer_add( s->in, arg->in.buf, arg->in.len );
            arg->in.num_bytes = arg->in.len;
            
            if( ! ( arg->flags & SHTTPD_MORE_POST_DATA ) )
                return;
            response = tr_rpc_request_exec_uri( s->session,
                                                EVBUFFER_DATA( s->in ),
                                                EVBUFFER_LENGTH( s->in ),
                                                &len );
            evbuffer_drain( s->in, EVBUFFER_LENGTH( s->in ) );
        }

        evbuffer_add_printf( s->out, "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: text/x-json\r\n"
                                     "Content-Length: %d\r\n"
                                     "\r\n"
                                     "%*.*s\r\n", len, len, len, response );
        tr_free( response );
    }

    if( EVBUFFER_LENGTH( s->out ) )
    {
        const int n = MIN( ( int )EVBUFFER_LENGTH( s->out ), arg->out.len );
        memcpy( arg->out.buf, EVBUFFER_DATA( s->out ), n );
        evbuffer_drain( s->out, n );
        arg->out.num_bytes = n;
    }

    if( !EVBUFFER_LENGTH( s->out ) )
        arg->flags |= SHTTPD_END_OF_OUTPUT;
}

static void
rpcPulse( int socket UNUSED, short action UNUSED, void * vserver )
{
    int interval;
    struct timeval tv;
    tr_rpc_server * server = vserver;

    assert( server );

    shttpd_poll( server->ctx, 1 );

    /* set a timer for the next pulse */
    if( EVBUFFER_LENGTH( server->in ) || EVBUFFER_LENGTH( server->out ) )
        interval = BUSY_INTERVAL_MSEC;
    else
        interval = IDLE_INTERVAL_MSEC;
    tv = tr_timevalMsec( interval );
    evtimer_add( &server->timer, &tv );
}

static void
startServer( tr_rpc_server * server )
{
    if( !server->ctx )
    {
        char ports[128];
        struct timeval tv = tr_timevalMsec( IDLE_INTERVAL_MSEC );

        server->ctx = shttpd_init( );
        snprintf( ports, sizeof( ports ), "%d", server->port );
        shttpd_register_uri( server->ctx, "/transmission", handle_rpc, server );
        shttpd_set_option( server->ctx, "ports", ports );
        shttpd_set_option( server->ctx, "dir_list", "0" );
        shttpd_set_option( server->ctx, "root", "/dev/null" );
        if( server->acl )
            shttpd_set_option( server->ctx, "acl", server->acl );

        evtimer_set( &server->timer, rpcPulse, server );
        evtimer_add( &server->timer, &tv );
    }
}

static void
stopServer( tr_rpc_server * server )
{
    if( server->ctx )
    {
        evtimer_del( &server->timer );
        shttpd_fini( server->ctx );
        server->ctx = NULL;
    }
}

void
tr_rpcSetEnabled( tr_rpc_server * server, int isEnabled )
{
    if( !isEnabled && server->ctx )
        stopServer( server );

    if( isEnabled && !server->ctx )
        startServer( server );
}

int
tr_rpcIsEnabled( const tr_rpc_server * server )
{
    return server->ctx != NULL;
}

void
tr_rpcSetPort( tr_rpc_server * server, int port )
{
    if( server->port != port )
    {
        server->port = port;

        if( server->ctx )
        {
            stopServer( server );
            startServer( server );
        }
    }
}

int
tr_rpcGetPort( const tr_rpc_server * server )
{
    return server->port;
}

void
tr_rpcSetACL( tr_rpc_server * server, const char * acl )
{
    const int isRunning = server->ctx != NULL;

    if( isRunning )
        stopServer( server );

    tr_free( server->acl );
    server->acl = tr_strdup( acl );

    if( isRunning )
        startServer( server );
}

const char*
tr_rpcGetACL( const tr_rpc_server * server )
{
    return server->acl ? server->acl : "";
}

void
tr_rpcClose( tr_rpc_server ** ps )
{
    tr_rpc_server * s = *ps;
    *ps = NULL;

    stopServer( s );
    evbuffer_free( s->in );
    evbuffer_free( s->out );
    tr_free( s );
}

tr_rpc_server *
tr_rpcInit( tr_handle   * session,
            int           isEnabled,
            int           port,
            const char  * acl )
{
    tr_rpc_server * s = tr_new0( tr_rpc_server, 1 );
    s->session = session;
    s->port = port;
    s->in = evbuffer_new( );
    s->out = evbuffer_new( );
    s->acl = tr_strdup( acl );
   
    if( isEnabled )
        startServer( s );
    return s;   
}
