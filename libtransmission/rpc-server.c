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
#include "bencode.h"
#include "list.h"
#include "platform.h"
#include "rpc.h"
#include "rpc-server.h"
#include "utils.h"

#define MY_NAME "RPC Server"
#define MY_REALM "Transmission RPC Server"

#define ACTIVE_INTERVAL_MSEC 40
#define INACTIVE_INTERVAL_MSEC 200

struct tr_rpc_server
{
    int port;
    time_t lastRequestTime;
    struct shttpd_ctx * ctx;
    tr_handle * session;
    struct event timer;
    int isPasswordEnabled;
    char * username;
    char * password;
    char * acl;
    tr_list * connections;
};

#define dbgmsg(fmt...) tr_deepLog(__FILE__, __LINE__, MY_NAME, ##fmt )

static const char*
tr_memmem( const char * s1, size_t l1,
           const char * s2, size_t l2 )
{
    if (!l2) return s1;
    while (l1 >= l2) {
        l1--;
        if (!memcmp(s1,s2,l2))
            return s1;
        s1++;
    }
    return NULL;
}

/**
***
**/

struct ConnBuf
{
    char * key;
    time_t lastActivity;
    struct evbuffer * in;
    struct evbuffer * out;
};

static char*
buildKey( struct shttpd_arg * arg )
{
    return tr_strdup_printf( "%s %s",
                             shttpd_get_env( arg, "REMOTE_ADDR" ),
                             shttpd_get_env( arg, "REQUEST_URI" ) );
}

static struct ConnBuf*
getBuffer( tr_rpc_server * server, struct shttpd_arg * arg )
{
    tr_list * l;
    char * key = buildKey( arg );
    struct ConnBuf * found = NULL;

    for( l=server->connections; l && !found; l=l->next )
    {
        struct ConnBuf * buf = l->data;
        if( !strcmp( key, buf->key ) )
            found = buf;
    }

    if( found == NULL )
    {
        found = tr_new0( struct ConnBuf, 1 );
        found->lastActivity = time( NULL );
        found->key = tr_strdup( key );
        found->in = evbuffer_new( );
        found->out = evbuffer_new( );
        tr_list_append( &server->connections, found );
    }

    tr_free( key );
    return found;
}

static void
pruneBuf( tr_rpc_server * server, struct ConnBuf * buf )
{
    tr_list_remove_data( &server->connections, buf );

    evbuffer_free( buf->in );
    evbuffer_free( buf->out );
    tr_free( buf->key );
    tr_free( buf );
}

/**
***
**/

static void
handle_upload( struct shttpd_arg * arg )
{
    struct tr_rpc_server * s;
    struct ConnBuf * cbuf;

    s = arg->user_data;
    s->lastRequestTime = time( NULL );
    cbuf = getBuffer( s, arg );

    /* if we haven't parsed the POST, do that now */
    if( !EVBUFFER_LENGTH( cbuf->out ) )
    {
        const char * query_string;
        const char * content_type;
        const char * delim;
        const char * in;
        size_t inlen;
        char * boundary;
        size_t boundary_len;
        char buf[64];
        int paused;

        /* if we haven't finished reading the POST, read more now */
        evbuffer_add( cbuf->in, arg->in.buf, arg->in.len );
        arg->in.num_bytes = arg->in.len;
        if( arg->flags & SHTTPD_MORE_POST_DATA )
            return;

        query_string = shttpd_get_env( arg, "QUERY_STRING" );
        content_type = shttpd_get_header( arg, "Content-Type" );
        in = (const char *) EVBUFFER_DATA( cbuf->in );
        inlen = EVBUFFER_LENGTH( cbuf->in );
        boundary = tr_strdup_printf( "--%s", strstr( content_type, "boundary=" ) + strlen( "boundary=" ) );
        boundary_len = strlen( boundary );
        paused = ( query_string != NULL )
              && ( shttpd_get_var( "paused", query_string, strlen( query_string ), buf, sizeof( buf ) ) == 4 )
              && ( !strcmp( buf, "true" ) );

        delim = tr_memmem( in, inlen, boundary, boundary_len );
        if( delim ) do
        {
            size_t part_len;
            const char * part = delim + boundary_len;
            inlen -= ( part - in );
            in = part;
            delim = tr_memmem( in, inlen, boundary, boundary_len );
            part_len = delim ? (size_t)(delim-part) : inlen;

            if( part_len )
            {
                char * text = tr_strndup( part, part_len );
                if( strstr( text, "filename=\"" ) )
                {
                    const char * body = strstr( text, "\r\n\r\n" );
                    if( body )
                    {
                        char * b64, *json, *freeme;
                        int json_len;
                        size_t body_len;
                        tr_benc top, *args;

                        body += 4;
                        body_len = part_len - ( body - text );
                        if( body_len >= 2 && !memcmp(&body[body_len-2],"\r\n",2) )
                            body_len -= 2;

                        tr_bencInitDict( &top, 2 );
                        args = tr_bencDictAddDict( &top, "arguments", 2 );
                        tr_bencDictAddStr( &top, "method", "torrent-add" );
                        b64 = tr_base64_encode( body, body_len, NULL );
                        tr_bencDictAddStr( args, "metainfo", b64 );
                        tr_bencDictAddInt( args, "paused", paused );
                        json = tr_bencSaveAsJSON( &top, &json_len );
                        freeme = tr_rpc_request_exec_json( s->session, json, json_len, NULL );

                        tr_free( freeme );
                        tr_free( json );
                        tr_free( b64 );
                        tr_bencFree( &top );
                    }
                }
                tr_free( text );
            }
        }
        while( delim );

        evbuffer_drain( cbuf->in, EVBUFFER_LENGTH( cbuf->in ) );
        tr_free( boundary );

        {
            /* use xml here because json responses to file uploads is trouble.
             * see http://www.malsup.com/jquery/form/#sample7 for details */
            const char * response = "<result>success</result>";
            const int len = strlen( response );
            evbuffer_add_printf( cbuf->out, "HTTP/1.1 200 OK\r\n"
                                            "Content-Type: text/xml; charset=UTF-8\r\n"
                                            "Content-Length: %d\r\n"
                                            "\r\n"
                                           "%s\r\n", len, response );
        }
    }

    if( EVBUFFER_LENGTH( cbuf->out ) )
    {
        const int n = MIN( ( int )EVBUFFER_LENGTH( cbuf->out ), arg->out.len );
        memcpy( arg->out.buf, EVBUFFER_DATA( cbuf->out ), n );
        evbuffer_drain( cbuf->out, n );
        arg->out.num_bytes = n;
    }

    if( !EVBUFFER_LENGTH( cbuf->out ) )
    {
        arg->flags |= SHTTPD_END_OF_OUTPUT;
        pruneBuf( s, cbuf );
    }
}

static void
handle_root( struct shttpd_arg * arg )
{
    const char * redirect = "HTTP/1.1 200 OK""\r\n"
                            "Content-Type: text/html; charset=UTF-8" "\r\n"
                            "\r\n"
                            "<html><head>" "\r\n"
                            "  <meta http-equiv=\"Refresh\" content=\"2; url=/transmission/web/\">" "\r\n"
                            "</head><body>" "\r\n"
                            "  <p>redirecting to <a href=\"/transmission/web\">/transmission/web/</a></p>" "\r\n"
                            "</body></html>" "\r\n";
    const size_t n = strlen( redirect );
    memcpy( arg->out.buf, redirect, n );
    arg->in.num_bytes = arg->in.len;
    arg->out.num_bytes = n;
    arg->flags |= SHTTPD_END_OF_OUTPUT;
}

static void
handle_rpc( struct shttpd_arg * arg )
{
    struct tr_rpc_server * s;
    struct ConnBuf * cbuf;

    s = arg->user_data;
    s->lastRequestTime = time( NULL );
    cbuf = getBuffer( s, arg );

    if( !EVBUFFER_LENGTH( cbuf->out ) )
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
            evbuffer_add( cbuf->in, arg->in.buf, arg->in.len );
            arg->in.num_bytes = arg->in.len;
            if( arg->flags & SHTTPD_MORE_POST_DATA )
                return;
            response = tr_rpc_request_exec_json( s->session,
                                                 EVBUFFER_DATA( cbuf->in ),
                                                 EVBUFFER_LENGTH( cbuf->in ),
                                                 &len );
            evbuffer_drain( cbuf->in, EVBUFFER_LENGTH( cbuf->in ) );
        }

        evbuffer_add_printf( cbuf->out, "HTTP/1.1 200 OK\r\n"
                                        "Content-Type: application/json; charset=UTF-8\r\n"
                                        "Content-Length: %d\r\n"
                                        "\r\n"
                                        "%*.*s", len, len, len, response );
        tr_free( response );
    }

    if( EVBUFFER_LENGTH( cbuf->out ) )
    {
        const int n = MIN( ( int )EVBUFFER_LENGTH( cbuf->out ), arg->out.len );
        memcpy( arg->out.buf, EVBUFFER_DATA( cbuf->out ), n );
        evbuffer_drain( cbuf->out, n );
        arg->out.num_bytes = n;
    }

    if( !EVBUFFER_LENGTH( cbuf->out ) )
    {
        arg->flags |= SHTTPD_END_OF_OUTPUT;
        pruneBuf( s, cbuf );
    }
}

static void
rpcPulse( int socket UNUSED, short action UNUSED, void * vserver )
{
    int interval;
    struct timeval tv;
    tr_rpc_server * server = vserver;
    const time_t now = time( NULL );

    assert( server );

    if( server->ctx )
        shttpd_poll( server->ctx, 1 );

    /* set a timer for the next pulse */
    if( now - server->lastRequestTime < 300 )
        interval = ACTIVE_INTERVAL_MSEC;
    else
        interval = INACTIVE_INTERVAL_MSEC;
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
        int i;
        int argc = 0;
        char * argv[100];
        char passwd[MAX_PATH_LENGTH];
        const char * clutchDir = tr_getClutchDir( server->session );
        struct timeval tv = tr_timevalMsec( INACTIVE_INTERVAL_MSEC );

        getPasswordFile( server, passwd, sizeof( passwd ) );
        if( !server->isPasswordEnabled )
            unlink( passwd );
        else
            edit_passwords( passwd, MY_REALM, server->username, server->password );

        argv[argc++] = tr_strdup( "appname-unused" );

        argv[argc++] = tr_strdup( "-ports" );
        argv[argc++] = tr_strdup_printf( "%d", server->port );

        argv[argc++] = tr_strdup( "-dir_list" );
        argv[argc++] = tr_strdup( "0" );

        argv[argc++] = tr_strdup( "-auth_realm" );
        argv[argc++] = tr_strdup( MY_REALM );

        argv[argc++] = tr_strdup( "-root" );
        argv[argc++] = tr_strdup( "/dev/null" );

        if( server->acl )
        {
            argv[argc++] = tr_strdup( "-acl" );
            argv[argc++] = tr_strdup( server->acl );
        }
        if( server->isPasswordEnabled )
        {
            argv[argc++] = tr_strdup( "-protect" );
            argv[argc++] = tr_strdup_printf( "/transmission=%s", passwd );
        }
        if( clutchDir && *clutchDir )
        {
            tr_inf( _( "Serving the web interface files from \"%s\"" ), clutchDir );
            argv[argc++] = tr_strdup( "-aliases" );
            argv[argc++] = tr_strdup_printf( "%s=%s,%s=%s",
                                             "/transmission/clutch", clutchDir,
                                             "/transmission/web", clutchDir );
        }

        argv[argc] = NULL; /* shttpd_init() wants it null-terminated */

        server->ctx = shttpd_init( argc, argv );
        shttpd_register_uri( server->ctx, "/transmission/rpc", handle_rpc, server );
        shttpd_register_uri( server->ctx, "/transmission/upload", handle_upload, server );
        shttpd_register_uri( server->ctx, "/", handle_root, server );

        evtimer_set( &server->timer, rpcPulse, server );
        evtimer_add( &server->timer, &tv );

        for( i=0; i<argc; ++i )
            tr_free( argv[i] );
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
 * FOR_EACH_WORD_IN_LIST, isbyte, and testACL are from, or modified from,
 * shttpd, written by Sergey Lyubka under this license:
 * "THE BEER-WARE LICENSE" (Revision 42):
 * Sergey Lyubka wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.
 */

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
    tr_free( s->username );
    tr_free( s->password );
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
    s->acl = tr_strdup( acl );
    s->username = tr_strdup( username );
    s->password = tr_strdup( password );
    s->isPasswordEnabled = isPasswordEnabled;
   
    if( isEnabled )
        startServer( s );
    return s;   
}
