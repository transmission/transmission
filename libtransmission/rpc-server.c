/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
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
#include <ctype.h> /* isdigit */
#include <errno.h>
#include <stdlib.h> /* strtol */
#include <string.h>

#include <unistd.h> /* unlink */

#include <libevent/event.h>
#include <shttpd/defs.h> /* edit_passwords */
#include <shttpd/shttpd.h>

#include "transmission.h"
#include "rpc.h"
#include "rpc-server.h"
#include "utils.h"

#define MY_NAME "RPC Server"
#define MY_REALM "Transmission RPC Server"

#define BUSY_INTERVAL_MSEC 30
#define IDLE_INTERVAL_MSEC 100
#define UNUSED_INTERVAL_MSEC 1000

struct tr_rpc_server
{
    int port;
    time_t lastRequestTime;
    struct shttpd_ctx * ctx;
    tr_handle * session;
    struct evbuffer * in;
    struct evbuffer * out;
    struct event timer;
    int isPasswordEnabled;
    char * username;
    char * password;
    char * acl;
};

#define dbgmsg(fmt...) tr_deepLog(__FILE__, __LINE__, MY_NAME, ##fmt )

static void
handle_rpc( struct shttpd_arg * arg )
{
    struct tr_rpc_server * s = arg->user_data;
    s->lastRequestTime = time( NULL );

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
            if( arg->flags & SHTTPD_MORE_POST_DATA )
                return;
            response = tr_rpc_request_exec_json( s->session,
                                                 EVBUFFER_DATA( s->in ),
                                                 EVBUFFER_LENGTH( s->in ),
                                                 &len );
            evbuffer_drain( s->in, EVBUFFER_LENGTH( s->in ) );
        }

        evbuffer_add_printf( s->out, "HTTP/1.1 200 OK\r\n"
                                     "Content-Type: application/json\r\n"
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
    const time_t now = time( NULL );

    assert( server );

    shttpd_poll( server->ctx, 1 );

    /* set a timer for the next pulse */
    if( EVBUFFER_LENGTH( server->in ) || EVBUFFER_LENGTH( server->out ) )
        interval = BUSY_INTERVAL_MSEC;
    else if( now - server->lastRequestTime < 300 )
        interval = IDLE_INTERVAL_MSEC;
    else
        interval = UNUSED_INTERVAL_MSEC;
    tv = tr_timevalMsec( interval );
    evtimer_add( &server->timer, &tv );
}

static void
getPasswordFile( tr_rpc_server * server, char * buf, int buflen )
{
    tr_buildPath( buf, buflen, tr_sessionGetConfigDir( server->session ),
                               "htpasswd",
                               NULL );
}

static void
startServer( tr_rpc_server * server )
{
    dbgmsg( "in startServer; current context is %p", server->ctx );

    if( !server->ctx )
    {
        char ports[128];
        char passwd[MAX_PATH_LENGTH];
        struct timeval tv = tr_timevalMsec( UNUSED_INTERVAL_MSEC );

        getPasswordFile( server, passwd, sizeof( passwd ) );
        if( !server->isPasswordEnabled )
            unlink( passwd );
        else
            edit_passwords( passwd, MY_REALM, server->username, server->password );

        server->ctx = shttpd_init( );
        snprintf( ports, sizeof( ports ), "%d", server->port );
        shttpd_register_uri( server->ctx, "/transmission", handle_rpc, server );
        shttpd_set_option( server->ctx, "ports", ports );
        shttpd_set_option( server->ctx, "dir_list", "0" );
        shttpd_set_option( server->ctx, "root", "/dev/null" );
        shttpd_set_option( server->ctx, "auth_realm", MY_REALM );
        if( server->acl ) {
            dbgmsg( "setting acl [%s]", server->acl );
            shttpd_set_option( server->ctx, "acl", server->acl );
        }
        if( server->isPasswordEnabled ) {
            char * buf = tr_strdup_printf( "/transmission=%s", passwd );
            shttpd_set_option( server->ctx, "protect", buf );
            tr_free( buf );
        }

        evtimer_set( &server->timer, rpcPulse, server );
        evtimer_add( &server->timer, &tv );
    }
}

static void
stopServer( tr_rpc_server * server )
{
    if( server->ctx )
    {
        char passwd[MAX_PATH_LENGTH];
        getPasswordFile( server, passwd, sizeof( passwd ) );
        unlink( passwd );

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

/****
*****  ACL
****/

/*
 * DELIM_CHARS, FOR_EACH_WORD_IN_LIST, isbyte, and testACL are from, or modified from,
 * shttpd, written by Sergey Lyubka under this license:
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

#define  DELIM_CHARS "," /* Separators for lists */

#define FOR_EACH_WORD_IN_LIST(s,len)                                    \
        for (; s != NULL && (len = strcspn(s, DELIM_CHARS)) != 0;       \
                        s += len, s+= strspn(s, DELIM_CHARS))

static int isbyte(int n) { return (n >= 0 && n <= 255); }

static char*
testACL( const char * s )
{
    int len;

    FOR_EACH_WORD_IN_LIST(s, len)
    {

        char flag;
        int  a, b, c, d, n, mask;

        if( sscanf(s, "%c%d.%d.%d.%d%n",&flag,&a,&b,&c,&d,&n) != 5 )
            return tr_strdup_printf( _( "[%s]: subnet must be [+|-]x.x.x.x[/x]" ), s );
        if( flag != '+' && flag != '-')
            return tr_strdup_printf( _( "[%s]: flag must be + or -" ), s );
        if( !isbyte(a) || !isbyte(b) || !isbyte(c) || !isbyte(d) )
            return tr_strdup_printf( _( "[%s]: bad ip address" ), s );
        if( sscanf(s + n, "/%d", &mask) == 1 && ( mask<0 || mask>32 ) )
            return tr_strdup_printf( _( "[%s]: bad subnet mask %d" ), s, n );
    }

    return NULL;
}

/* 192.*.*.* --> 192.0.0.0/8
   192.64.*.* --> 192.64.0.0/16
   192.64.1.* --> 192.64.1.0/24
   192.64.1.2 --> 192.64.1.2/32 */
static void
cidrizeOne( const char * in, int len, struct evbuffer * out )
{
    int stars = 0;
    const char * pch;
    const char * end;
    char zero = '0';
    char huh = '?';

    for( pch=in, end=pch+len; pch!=end; ++pch ) {
        if( stars && isdigit(*pch) )
            evbuffer_add( out, &huh, 1 ); 
        else if( *pch!='*' )
            evbuffer_add( out, pch, 1 );
        else {
            evbuffer_add( out, &zero, 1 );
            ++stars;
        }
    }

    evbuffer_add_printf( out, "/%d", (32-(stars*8)));
}

char*
cidrize( const char * acl )
{
    int len;
    const char * walk = acl;
    char * ret;
    struct evbuffer * out = evbuffer_new( );

    FOR_EACH_WORD_IN_LIST( walk, len )
    {
        cidrizeOne( walk, len, out );
        evbuffer_add_printf( out, "," );
    }

    /* the -1 is to eat the final ", " */
    ret = tr_strndup( (char*) EVBUFFER_DATA(out), EVBUFFER_LENGTH(out)-1 );
    evbuffer_free( out );
    return ret;
}

int
tr_rpcTestACL( const tr_rpc_server  * server UNUSED,
               const char           * acl,
               char                ** setme_errmsg )
{
    int err = 0;
    char * cidr = cidrize( acl );
    char * errmsg = testACL( cidr );
    if( errmsg )
    {
        if( setme_errmsg )
            *setme_errmsg = errmsg;
        else
            tr_free( errmsg );
        err = -1;
    }
    tr_free( cidr );
    return err;
}

int
tr_rpcSetACL( tr_rpc_server   * server,
              const char      * acl,
              char           ** setme_errmsg )
{
    char * cidr = cidrize( acl );
    const int err = tr_rpcTestACL( server, cidr, setme_errmsg );

    if( !err )
    {
        const int isRunning = server->ctx != NULL;

        if( isRunning )
            stopServer( server );

        tr_free( server->acl );
        server->acl = tr_strdup( cidr );
        dbgmsg( "setting our ACL to [%s]", server->acl );

        if( isRunning )
            startServer( server );
    }
    tr_free( cidr );

    return err;
}

char*
tr_rpcGetACL( const tr_rpc_server * server )
{
    return tr_strdup( server->acl ? server->acl : "" );
}

/****
*****  PASSWORD
****/

void
tr_rpcSetUsername( tr_rpc_server        * server,
                   const char           * username )
{
    const int isRunning = server->ctx != NULL;

    if( isRunning )
        stopServer( server );

    tr_free( server->username );
    server->username = tr_strdup( username );
    dbgmsg( "setting our Username to [%s]", server->username );

    if( isRunning )
        startServer( server );
}

char*
tr_rpcGetUsername( const tr_rpc_server  * server )
{
    return tr_strdup( server->username ? server->username : "" );
}

void
tr_rpcSetPassword( tr_rpc_server        * server,
                   const char           * password )
{
    const int isRunning = server->ctx != NULL;

    if( isRunning )
        stopServer( server );

    tr_free( server->password );
    server->password = tr_strdup( password );
    dbgmsg( "setting our Password to [%s]", server->password );

    if( isRunning )
        startServer( server );
}

char*
tr_rpcGetPassword( const tr_rpc_server  * server )
{
    return tr_strdup( server->password ? server->password : "" );
}

void
tr_rpcSetPasswordEnabled( tr_rpc_server  * server,
                          int              isEnabled )
{
    const int isRunning = server->ctx != NULL;

    if( isRunning )
        stopServer( server );

    server->isPasswordEnabled = isEnabled;
    dbgmsg( "setting 'password enabled' to %d", isEnabled );

    if( isRunning )
        startServer( server );
}

int
tr_rpcIsPasswordEnabled( const tr_rpc_server * server )
{
    return server->isPasswordEnabled;
}

/****
*****  LIFE CYCLE
****/

void
tr_rpcClose( tr_rpc_server ** ps )
{
    tr_rpc_server * s = *ps;
    *ps = NULL;

    stopServer( s );
    evbuffer_free( s->in );
    evbuffer_free( s->out );
    tr_free( s->acl );
    tr_free( s );
}

tr_rpc_server *
tr_rpcInit( tr_handle   * session,
            int           isEnabled,
            int           port,
            const char  * acl,
            int           isPasswordEnabled,
            const char  * username,
            const char  * password )
{
    char * errmsg;
    tr_rpc_server * s;

    if(( errmsg = testACL ( acl )))
    {
        tr_nerr( MY_NAME, errmsg );
        tr_free( errmsg );
        acl = TR_DEFAULT_RPC_ACL;
        tr_nerr( MY_NAME, "using fallback ACL \"%s\"", acl );
    }

    s = tr_new0( tr_rpc_server, 1 );
    s->session = session;
    s->port = port;
    s->in = evbuffer_new( );
    s->out = evbuffer_new( );
    s->acl = tr_strdup( acl );
    s->username = tr_strdup( username );
    s->password = tr_strdup( password );
    s->isPasswordEnabled = isPasswordEnabled;
   
    if( isEnabled )
        startServer( s );
    return s;   
}
