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
#include <errno.h>
#include <string.h> /* memcpy */
#include <limits.h> /* INT_MAX */

#include <sys/types.h> /* open */
#include <sys/stat.h>  /* open */
#include <fcntl.h>     /* open */
#include <unistd.h>    /* close */

#ifdef HAVE_LIBZ
 #include <zlib.h>
#endif

#include <libevent/event.h>
#include <libevent/evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "list.h"
#include "platform.h"
#include "rpcimpl.h"
#include "rpc-server.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

#define MY_NAME "RPC Server"
#define MY_REALM "Transmission"
#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

struct tr_rpc_server
{
    unsigned int       isEnabled          : 1;
    unsigned int       isPasswordEnabled  : 1;
    unsigned int       isWhitelistEnabled : 1;
    uint16_t           port;
    struct evhttp *    httpd;
    tr_handle *        session;
    char *             username;
    char *             password;
    char *             whitelist;
};

#define dbgmsg( fmt ... ) tr_deepLog( __FILE__, __LINE__, MY_NAME, ## fmt )

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

    evbuffer_add_printf( body, "<h1>%d: %s</h1>", code, code_text );
    if( text )
        evbuffer_add_printf( body, "%s", text );
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

                        body += 4; /* walk past the \r\n\r\n */
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
        const char *    suffix;
        const char *    mime_type;
    } types[] = {
        /* these are just the ones we need for serving clutch... */
        { "css",  "text/css"                  },
        { "gif",  "image/gif"                 },
        { "html", "text/html"                 },
        { "ico",  "image/vnd.microsoft.icon"  },
        { "js",   "application/javascript"    },
        { "png",  "image/png"                 }
    };
    const char * dot = strrchr( path, '.' );

    for( i = 0; dot && i < TR_N_ELEMENTS( types ); ++i )
        if( !strcmp( dot + 1, types[i].suffix ) )
            return types[i].mime_type;

    return "application/octet-stream";
}

#ifdef HAVE_LIBZ
static int
compress_response( struct evbuffer  * out,
                   const void       * content,
                   size_t             content_len )
{
    int err = 0;
    z_stream stream;

    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
    deflateInit( &stream, Z_BEST_COMPRESSION );

    stream.next_in = (Bytef*) content;
    stream.avail_in = content_len;

    while( !err ) {
        unsigned char buf[1024];
        int state;
        stream.next_out = buf;
        stream.avail_out = sizeof( buf );
        state = deflate( &stream, Z_FINISH );
        if( ( state != Z_OK ) && ( state != Z_STREAM_END ) ) {
            tr_nerr( MY_NAME, _( "Error deflating file: %s" ), zError( err ) );
            err = state;
            break;
        }
        evbuffer_add( out, buf, sizeof(buf) - stream.avail_out );
        if( state == Z_STREAM_END )
            break;
    }

    /* if the deflated form is larger, then just use the original */
    if( !err && ( EVBUFFER_LENGTH( out ) >= content_len ) )
        err = -1;

    if( err )
        evbuffer_add( out, content, content_len );
    else
        tr_ninf( MY_NAME, "deflated response from %zu bytes to %zu",
                          content_len,
                          EVBUFFER_LENGTH( out ) );

    deflateEnd( &stream );
    return err;
}
#endif

static void
add_response( struct evhttp_request * req,
              struct evbuffer *       response,
              const void *            content,
              size_t                  content_len )
{
#ifdef HAVE_LIBZ
    const char * accept_encoding = evhttp_find_header( req->input_headers,
                                                       "Accept-Encoding" );
    const int    do_deflate = accept_encoding && strstr( accept_encoding,
                                                         "deflate" );
    if( do_deflate && !compress_response( response, content, content_len ) )
        evhttp_add_header( req->output_headers, "Content-Encoding", "deflate" );
#else
    evbuffer_add( response, content, content_len );
#endif
}

static void
serve_file( struct evhttp_request * req,
            const char *            filename )
{
    if( req->type != EVHTTP_REQ_GET )
    {
        evhttp_add_header( req->output_headers, "Allow", "GET" );
        send_simple_response( req, 405, NULL );
    }
    else
    {
        size_t content_len;
        uint8_t * content;
        const int error = errno;

        errno = 0;
        content_len = 0;
        content = tr_loadFile( filename, &content_len );

        if( errno )
        {
            send_simple_response( req, HTTP_NOTFOUND, NULL );
        }
        else
        {
            struct evbuffer * out;

            errno = error;
            out = evbuffer_new( );
            evhttp_add_header( req->output_headers, "Content-Type",
                               mimetype_guess( filename ) );
            add_response( req, out, content, content_len );
            evhttp_send_reply( req, HTTP_OK, "OK", out );

            evbuffer_free( out );
            tr_free( content );
        }
    }
}

static void
handle_clutch( struct evhttp_request * req,
               struct tr_rpc_server *  server )
{
    const char * clutchDir = tr_getClutchDir( server->session );

    assert( !strncmp( req->uri, "/transmission/web/", 18 ) );

    if( !clutchDir || !*clutchDir )
    {
        send_simple_response( req, HTTP_NOTFOUND,
            "<p>Couldn't find Transmission's web interface files!</p>"
            "<p>Users: to tell Transmission where to look, "
            "set the TRANSMISSION_WEB_HOME environmental "
            "variable to the folder where the web interface's "
            "index.html is located.</p>"
            "<p>Package Builders: to set a custom default at compile time, "
            "#define PACKAGE_DATA_DIR in libtransmission/platform.c "
            "or tweak tr_getClutchDir() by hand.</p>" );
    }
    else
    {
        char * pch;
        char * subpath;
        char * filename;

        subpath = tr_strdup( req->uri + 18 );
        if(( pch = strchr( subpath, '?' )))
            *pch = '\0';

        filename = *subpath
            ? tr_strdup_printf( "%s%s%s", clutchDir, TR_PATH_DELIMITER_STR, subpath )
            : tr_strdup_printf( "%s%s%s", clutchDir, TR_PATH_DELIMITER_STR, "index.html" );

        serve_file( req, filename );

        tr_free( filename );
        tr_free( subpath );
    }
}

static void
handle_rpc( struct evhttp_request * req,
            struct tr_rpc_server *  server )
{
    int               len = 0;
    char *            out = NULL;
    struct evbuffer * buf;

    if( req->type == EVHTTP_REQ_GET )
    {
        const char * q;
        if( ( q = strchr( req->uri, '?' ) ) )
            out = tr_rpc_request_exec_uri( server->session,
                                           q + 1,
                                           strlen( q + 1 ),
                                           &len );
    }
    else if( req->type == EVHTTP_REQ_POST )
    {
        out = tr_rpc_request_exec_json( server->session,
                                        EVBUFFER_DATA( req->input_buffer ),
                                        EVBUFFER_LENGTH( req->input_buffer ),
                                        &len );
    }

    buf = evbuffer_new( );
    add_response( req, buf, out, len );
    evhttp_add_header( req->output_headers, "Content-Type",
                       "application/json; charset=UTF-8" );
    evhttp_send_reply( req, HTTP_OK, "OK", buf );

    /* cleanup */
    evbuffer_free( buf );
    tr_free( out );
}

static int
isAddressAllowed( const tr_rpc_server * server,
                  const char *          address )
{
    const char * str;

    if( !server->isWhitelistEnabled )
        return 1;

    for( str = server->whitelist; str && *str; )
    {
        const char * delimiter = strchr( str, ',' );
        const int    len = delimiter ? delimiter - str : (int)strlen( str );
        char *       token = tr_strndup( str, len );
        const int    match = tr_wildmat( address, token );
        tr_free( token );
        if( match )
            return 1;
        if( !delimiter )
            break;
        str = delimiter + 1;
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

        if( !isAddressAllowed( server, req->remote_host ) )
        {
            send_simple_response( req, 401, "Unauthorized IP Address" );
        }
        else if( server->isPasswordEnabled
                 && ( !pass || !user || strcmp( server->username, user )
                                     || strcmp( server->password, pass ) ) )
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
startServer( void * vserver )
{
    tr_rpc_server * server  = vserver;

    if( !server->httpd )
    {
        server->httpd = evhttp_new( tr_eventGetBase( server->session ) );
        evhttp_bind_socket( server->httpd, "0.0.0.0", server->port );
        evhttp_set_gencb( server->httpd, handle_request, server );
    }
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

static void
onEnabledChanged( void * vserver )
{
    tr_rpc_server * server = vserver;

    if( !server->isEnabled )
        stopServer( server );
    else
        startServer( server );
}

void
tr_rpcSetEnabled( tr_rpc_server * server,
                  int             isEnabled )
{
    server->isEnabled = isEnabled != 0;

    tr_runInEventThread( server->session, onEnabledChanged, server );
}

int
tr_rpcIsEnabled( const tr_rpc_server * server )
{
    return server->isEnabled;
}

static void
restartServer( void * vserver )
{
    tr_rpc_server * server = vserver;

    if( server->isEnabled )
    {
        stopServer( server );
        startServer( server );
    }
}

void
tr_rpcSetPort( tr_rpc_server * server,
               uint16_t        port )
{
    if( server->port != port )
    {
        server->port = port;

        if( server->isEnabled )
            tr_runInEventThread( server->session, restartServer, server );
    }
}

uint16_t
tr_rpcGetPort( const tr_rpc_server * server )
{
    return server->port;
}

void
tr_rpcSetWhitelist( tr_rpc_server * server,
                    const char *    whitelist )
{
    tr_free( server->whitelist );
    server->whitelist = tr_strdup( whitelist );
}

char*
tr_rpcGetWhitelist( const tr_rpc_server * server )
{
    return tr_strdup( server->whitelist ? server->whitelist : "" );
}

void
tr_rpcSetWhitelistEnabled( tr_rpc_server  * server,
                           int              isEnabled )
{
    server->isWhitelistEnabled = isEnabled != 0;
}

int
tr_rpcGetWhitelistEnabled( const tr_rpc_server * server )
{
    return server->isWhitelistEnabled;
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

static void
closeServer( void * vserver )
{
    tr_rpc_server * s = vserver;

    stopServer( s );
    tr_free( s->whitelist );
    tr_free( s->username );
    tr_free( s->password );
    tr_free( s );
}

void
tr_rpcClose( tr_rpc_server ** ps )
{
    tr_runInEventThread( ( *ps )->session, closeServer, *ps );
    *ps = NULL;
}

tr_rpc_server *
tr_rpcInit( tr_handle *  session,
            int          isEnabled,
            uint16_t     port,
            int          isWhitelistEnabled,
            const char * whitelist,
            int          isPasswordEnabled,
            const char * username,
            const char * password )
{
    tr_rpc_server * s;

    s = tr_new0( tr_rpc_server, 1 );
    s->session = session;
    s->port = port;
    s->whitelist = tr_strdup( whitelist && *whitelist
                              ? whitelist
                              : TR_DEFAULT_RPC_WHITELIST );
    s->username = tr_strdup( username );
    s->password = tr_strdup( password );
    s->isWhitelistEnabled = isWhitelistEnabled != 0;
    s->isPasswordEnabled = isPasswordEnabled != 0;
    s->isEnabled = isEnabled != 0;
    if( isEnabled )
        tr_runInEventThread( session, startServer, s );
    return s;
}
