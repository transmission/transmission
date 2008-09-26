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
#include <string.h> /* memcpy */
#include <limits.h> /* INT_MAX */

#include <sys/types.h> /* open */
#include <sys/stat.h>  /* open */
#include <fcntl.h>     /* open */
#include <unistd.h>    /* close */

#include <libevent/event.h>
#include <libevent/evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "list.h"
#include "platform.h"
#include "rpcimpl.h"
#include "rpc-server.h"
#include "utils.h"
#include "web.h"

#define MY_NAME "RPC Server"
#define MY_REALM "Transmission"
#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

struct tr_rpc_server
{
    unsigned int     isEnabled         : 1;
    unsigned int     isPasswordEnabled : 1;
    uint16_t         port;
    struct evhttp *  httpd;
    tr_handle *      session;
    char *           username;
    char *           password;
    char *           acl;
};

#define dbgmsg( fmt... ) tr_deepLog( __FILE__, __LINE__, MY_NAME, ## fmt )

/**
***
**/

static void
send_simple_response( struct evhttp_request * req,
                      int                     code,
                      const char *            text )
{
    const char *      code_text = tr_webGetResponseStr( code );
    struct evbuffer * body = evbuffer_new( );

    evbuffer_add_printf( body, "<h1>%s</h1>", code_text );
    if( text )
        evbuffer_add_printf( body, "<h2>%s</h2>", text );
    evhttp_send_reply( req, code, code_text, body );
    evbuffer_free( body );
}

static const char*
tr_memmem( const char * s1,
           size_t       l1,
           const char * s2,
           size_t       l2 )
{
    if( !l2 ) return s1;
    while( l1 >= l2 )
    {
        l1--;
        if( !memcmp( s1, s2, l2 ) )
            return s1;
        s1++;
    }

    return NULL;
}

static void
handle_upload( struct evhttp_request * req,
               struct tr_rpc_server *  server )
{
    if( req->type != EVHTTP_REQ_POST )
    {
        send_simple_response( req, 405, NULL );
    }
    else
    {
        const char * content_type = evhttp_find_header( req->input_headers,
                                                        "Content-Type" );

        const char * query = strchr( req->uri, '?' );
        const int    paused = query && strstr( query + 1, "paused=true" );

        const char * in = (const char *) EVBUFFER_DATA( req->input_buffer );
        size_t       inlen = EVBUFFER_LENGTH( req->input_buffer );

        const char * boundary_key = "boundary=";
        const char * boundary_key_begin = strstr( content_type,
                                                  boundary_key );
        const char * boundary_val =
            boundary_key_begin ? boundary_key_begin +
            strlen( boundary_key ) : "arglebargle";

        char *       boundary = tr_strdup_printf( "--%s", boundary_val );
        const size_t boundary_len = strlen( boundary );

        const char * delim = tr_memmem( in, inlen, boundary, boundary_len );
        while( delim )
        {
            size_t       part_len;
            const char * part = delim + boundary_len;
            inlen -= ( part - in );
            in = part;
            delim = tr_memmem( in, inlen, boundary, boundary_len );
            part_len = delim ? (size_t)( delim - part ) : inlen;

            if( part_len )
            {
                char * text = tr_strndup( part, part_len );
                if( strstr( text, "filename=\"" ) )
                {
                    const char * body = strstr( text, "\r\n\r\n" );
                    if( body )
                    {
                        char *  b64, *json, *freeme;
                        int     json_len;
                        size_t  body_len;
                        tr_benc top, *args;

                        body += 4;
                        body_len = part_len - ( body - text );
                        if( body_len >= 2
                          && !memcmp( &body[body_len - 2], "\r\n", 2 ) )
                            body_len -= 2;

                        tr_bencInitDict( &top, 2 );
                        args = tr_bencDictAddDict( &top, "arguments", 2 );
                        tr_bencDictAddStr( &top, "method", "torrent-add" );
                        b64 = tr_base64_encode( body, body_len, NULL );
                        tr_bencDictAddStr( args, "metainfo", b64 );
                        tr_bencDictAddInt( args, "paused", paused );
                        json = tr_bencSaveAsJSON( &top, &json_len );
                        freeme = tr_rpc_request_exec_json( server->session,
                                                           json, json_len,
                                                           NULL );

                        tr_free( freeme );
                        tr_free( json );
                        tr_free( b64 );
                        tr_bencFree( &top );
                    }
                }
                tr_free( text );
            }
        }

        tr_free( boundary );

        /* use xml here because json responses to file uploads is trouble.
         * see http://www.malsup.com/jquery/form/#sample7 for details */
        evhttp_add_header( req->output_headers, "Content-Type",
                           "text/xml; charset=UTF-8" );
        send_simple_response( req, HTTP_OK, NULL );
    }
}

static const char*
mimetype_guess( const char * path )
{
    unsigned int i;

    const struct
    {
        const char *  suffix;
        const char *  mime_type;
    } types[] = {
        /* these are just the ones we need for serving clutch... */
        { "css",  "text/css"                   },
        { "gif",  "image/gif"                  },
        { "html", "text/html"                  },
        { "ico",  "image/vnd.microsoft.icon"   },
        { "js",   "application/javascript"     },
        { "png",  "image/png"                  }
    };
    const char * dot = strrchr( path, '.' );

    for( i = 0; dot && i < TR_N_ELEMENTS( types ); ++i )
        if( !strcmp( dot + 1, types[i].suffix ) )
            return types[i].mime_type;

    return "application/octet-stream";
}

static void
serve_file( struct evhttp_request * req,
            const char *            path )
{
    if( req->type != EVHTTP_REQ_GET )
    {
        evhttp_add_header( req->output_headers, "Allow", "GET" );
        send_simple_response( req, 405, NULL );
    }
    else
    {
        const int fd = open( path, O_RDONLY, 0 );
        if( fd == -1 )
        {
            send_simple_response( req, HTTP_NOTFOUND, NULL );
        }
        else
        {
            struct evbuffer * buf = evbuffer_new( );
            evbuffer_read( buf, fd, INT_MAX );
            evhttp_add_header( req->output_headers, "Content-Type",
                              mimetype_guess(
                                  path ) );
            evhttp_send_reply( req, HTTP_OK, "OK", buf );
            evbuffer_free( buf );
            close( fd );
        }
    }
}

static void
handle_clutch( struct evhttp_request * req,
               struct tr_rpc_server *  server )
{
    const char *      uri;
    struct evbuffer * buf = evbuffer_new( );

    assert( !strncmp( req->uri, "/transmission/web/", 18 ) );

    evbuffer_add_printf( buf, "%s%s", tr_getClutchDir(
                             server->session ), TR_PATH_DELIMITER_STR );
    uri = req->uri + 18;
    if( ( *uri == '?' ) || ( *uri == '\0' ) )
        evbuffer_add_printf( buf, "index.html" );
    else
    {
        const char * pch = strchr( uri, '?' );
        if( pch )
            evbuffer_add_printf( buf, "%*.*s", (int)( pch - uri ),
                                 (int)( pch - uri ), uri );
        else
            evbuffer_add_printf( buf, "%s", uri );
    }

    if( strstr( (const char *)EVBUFFER_DATA( buf ), ".." ) )
        send_simple_response( req, 401, NULL );
    else
        serve_file( req, (const char *)EVBUFFER_DATA( buf ) );

    evbuffer_free( buf );
}

static void
handle_rpc( struct evhttp_request * req,
            struct tr_rpc_server *  server )
{
    int               len = 0;
    char *            response = NULL;
    struct evbuffer * buf;

    if( req->type == EVHTTP_REQ_GET )
    {
        const char * q;
        if( ( q = strchr( req->uri, '?' ) ) )
            response = tr_rpc_request_exec_uri( server->session,
                                                q + 1,
                                                strlen( q + 1 ),
                                                &len );
    }
    else if( req->type == EVHTTP_REQ_POST )
    {
        response = tr_rpc_request_exec_json( server->session,
                                             EVBUFFER_DATA( req->
                                                            input_buffer ),
                                             EVBUFFER_LENGTH( req->
                                                              input_buffer ),
                                             &len );
    }

    buf = evbuffer_new( );
    evbuffer_add( buf, response, len );
    evhttp_add_header( req->output_headers, "Content-Type",
                       "application/json; charset=UTF-8" );
    evhttp_send_reply( req, HTTP_OK, "OK", buf );
    evbuffer_free( buf );
}

static int
isAddressAllowed( const tr_rpc_server * server,
                  const char *          address )
{
    const char * acl;

    for( acl = server->acl; acl && *acl; )
    {
        const char * delimiter = strchr( acl, ',' );
        const int    len = delimiter ? delimiter - acl : (int)strlen( acl );
        char *       token = tr_strndup( acl, len );
        const int    match = tr_wildmat( address, token + 1 );
        tr_free( token );
        if( match )
            return *acl == '+';
        if( !delimiter )
            break;
        acl = delimiter + 1;
    }

    return 0;
}

static void
handle_request( struct evhttp_request * req,
                void *                  arg )
{
    struct tr_rpc_server * server = arg;

    if( req && req->evcon )
    {
        const char * auth;
        char *       user = NULL;
        char *       pass = NULL;

        evhttp_add_header( req->output_headers, "Server", MY_REALM );

        auth = evhttp_find_header( req->input_headers, "Authorization" );

        if( auth && !strncasecmp( auth, "basic ", 6 ) )
        {
            int    plen;
            char * p = tr_base64_decode( auth + 6, 0, &plen );
            if( p && plen && ( ( pass = strchr( p, ':' ) ) ) )
            {
                user = p;
                *pass++ = '\0';
            }
        }

        if( server->acl && !isAddressAllowed( server, req->remote_host ) )
        {
            send_simple_response( req, 401, "Unauthorized IP Address" );
        }
        else if( server->isPasswordEnabled && ( !pass
                                              || !user
                                              || strcmp( server->username,
                                                         user )
                                              || strcmp( server->password,
                                                         pass ) ) )
        {
            evhttp_add_header( req->output_headers,
                               "WWW-Authenticate",
                               "Basic realm=\"" MY_REALM "\"" );
            send_simple_response( req, 401, "Unauthorized User" );
        }
        else if( !strcmp( req->uri, "/transmission/web" )
               || !strcmp( req->uri, "/transmission/clutch" )
               || !strcmp( req->uri, "/" ) )
        {
            evhttp_add_header( req->output_headers, "Location",
                               "/transmission/web/" );
            send_simple_response( req, HTTP_MOVEPERM, NULL );
        }
        else if( !strncmp( req->uri, "/transmission/web/", 18 ) )
        {
            handle_clutch( req, server );
        }
        else if( !strncmp( req->uri, "/transmission/rpc", 17 ) )
        {
            handle_rpc( req, server );
        }
        else if( !strncmp( req->uri, "/transmission/upload", 20 ) )
        {
            handle_upload( req, server );
        }
        else
        {
            send_simple_response( req, HTTP_NOTFOUND, NULL );
        }

        tr_free( user );
    }
}

static void
startServer( tr_rpc_server * server )
{
    dbgmsg( "in startServer; current context is %p", server->httpd );

    if( !server->httpd )
        if( ( server->httpd = evhttp_start( NULL, server->port ) ) )
            evhttp_set_gencb( server->httpd, handle_request, server );
}

static void
stopServer( tr_rpc_server * server )
{
    if( server->httpd )
    {
        evhttp_free( server->httpd );
        server->httpd = NULL;
    }
}

void
tr_rpcSetEnabled( tr_rpc_server * server,
                  int             isEnabled )
{
    server->isEnabled = isEnabled != 0;

    if( !isEnabled )
        stopServer( server );
    else
        startServer( server );
}

int
tr_rpcIsEnabled( const tr_rpc_server * server )
{
    return server->isEnabled;
}

void
tr_rpcSetPort( tr_rpc_server * server,
               uint16_t        port )
{
    if( server->port != port )
    {
        server->port = port;

        if( server->isEnabled )
        {
            stopServer( server );
            startServer( server );
        }
    }
}

uint16_t
tr_rpcGetPort( const tr_rpc_server * server )
{
    return server->port;
}

void
tr_rpcSetACL( tr_rpc_server * server,
              const char *    acl )
{
    tr_free( server->acl );
    server->acl = tr_strdup( acl );
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
tr_rpcSetUsername( tr_rpc_server * server,
                   const char *    username )
{
    tr_free( server->username );
    server->username = tr_strdup( username );
    dbgmsg( "setting our Username to [%s]", server->username );
}

char*
tr_rpcGetUsername( const tr_rpc_server * server )
{
    return tr_strdup( server->username ? server->username : "" );
}

void
tr_rpcSetPassword( tr_rpc_server * server,
                   const char *    password )
{
    tr_free( server->password );
    server->password = tr_strdup( password );
    dbgmsg( "setting our Password to [%s]", server->password );
}

char*
tr_rpcGetPassword( const tr_rpc_server * server )
{
    return tr_strdup( server->password ? server->password : "" );
}

void
tr_rpcSetPasswordEnabled( tr_rpc_server * server,
                          int             isEnabled )
{
    server->isPasswordEnabled = isEnabled != 0;
    dbgmsg( "setting 'password enabled' to %d", isEnabled );
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
    tr_free( s->acl );
    tr_free( s->username );
    tr_free( s->password );
    tr_free( s );
}

tr_rpc_server *
tr_rpcInit( tr_handle *  session,
            int          isEnabled,
            uint16_t     port,
            const char * acl,
            int          isPasswordEnabled,
            const char * username,
            const char * password )
{
    tr_rpc_server * s;

    s = tr_new0( tr_rpc_server, 1 );
    s->session = session;
    s->port = port;
    s->acl = tr_strdup( acl && *acl ? acl : TR_DEFAULT_RPC_ACL );
    s->username = tr_strdup( username );
    s->password = tr_strdup( password );
    s->isPasswordEnabled = isPasswordEnabled != 0;
    s->isEnabled = isEnabled != 0;

    if( isEnabled )
        startServer( s );
    return s;
}

