/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
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

#ifdef HAVE_ZLIB
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

#ifdef WIN32
#define strncasecmp _strnicmp
#endif

struct tr_rpc_server
{
    tr_bool            isEnabled;
    tr_bool            isPasswordEnabled;
    tr_bool            isWhitelistEnabled;
    tr_port            port;
    struct evhttp *    httpd;
    tr_session *       session;
    char *             username;
    char *             password;
    char *             whitelistStr;
    tr_list *          whitelist;

#ifdef HAVE_ZLIB
    z_stream           stream;
#endif
};

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, MY_NAME, __VA_ARGS__ ); \
    } while( 0 )


/**
***
**/

static void
send_simple_response( struct evhttp_request * req,
                      int                     code,
                      const char *            text )
{
    const char *      code_text = tr_webGetResponseStr( code );
    struct evbuffer * body = tr_getBuffer( );

    evbuffer_add_printf( body, "<h1>%d: %s</h1>", code, code_text );
    if( text )
        evbuffer_add_printf( body, "%s", text );
    evhttp_send_reply( req, code, code_text, body );

    tr_releaseBuffer( body );
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
                        char * b64;
                        size_t  body_len;
                        tr_benc top, *args;
                        struct evbuffer * json = tr_getBuffer( );

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
                        tr_bencSaveAsJSON( &top, json );
                        tr_rpc_request_exec_json( server->session,
                                                  EVBUFFER_DATA( json ),
                                                  EVBUFFER_LENGTH( json ),
                                                  NULL );

                        tr_releaseBuffer( json );
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

static void
add_response( struct evhttp_request * req,
              struct tr_rpc_server *  server,
              struct evbuffer *       out,
              const void *            content,
              size_t                  content_len )
{
#ifndef HAVE_ZLIB
    evbuffer_add( out, content, content_len );
#else
    const char * key = "Accept-Encoding";
    const char * encoding = evhttp_find_header( req->input_headers, key );
    const int do_deflate = encoding && strstr( encoding, "deflate" );

    if( !do_deflate )
    {
        evbuffer_add( out, content, content_len );
    }
    else
    {
        int state;

        server->stream.next_in = (Bytef*) content;
        server->stream.avail_in = content_len;

        /* allocate space for the raw data and call deflate() just once --
         * we won't use the deflated data if it's longer than the raw data,
         * so it's okay to let deflate() run out of output buffer space */
        evbuffer_expand( out, content_len );
        server->stream.next_out = EVBUFFER_DATA( out );
        server->stream.avail_out = content_len;

        state = deflate( &server->stream, Z_FINISH );

        if( state == Z_STREAM_END )
        {
            EVBUFFER_LENGTH( out ) = content_len - server->stream.avail_out;

            /* http://carsten.codimi.de/gzip.yaws/
               It turns out that some browsers expect deflated data without
               the first two bytes (a kind of header) and and the last four
               bytes (an ADLER32 checksum). This format can of course
               be produced by simply stripping these off. */
            if( EVBUFFER_LENGTH( out ) >= 6 ) {
                EVBUFFER_LENGTH( out ) -= 4;
                evbuffer_drain( out, 2 );
            }

#if 0
            tr_ninf( MY_NAME, _( "Deflated response from %zu bytes to %zu" ),
                              content_len,
                              EVBUFFER_LENGTH( out ) );
#endif
            evhttp_add_header( req->output_headers,
                               "Content-Encoding", "deflate" );
        }
        else
        {
            evbuffer_drain( out, EVBUFFER_LENGTH( out ) );
            evbuffer_add( out, content, content_len );
        }

        deflateReset( &server->stream );
    }
#endif
}

static void
serve_file( struct evhttp_request * req,
            struct tr_rpc_server *  server,
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
            out = tr_getBuffer( );
            evhttp_add_header( req->output_headers, "Content-Type",
                               mimetype_guess( filename ) );
            add_response( req, server, out, content, content_len );
            evhttp_send_reply( req, HTTP_OK, "OK", out );

            tr_releaseBuffer( out );
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

        filename = tr_strdup_printf( "%s%s%s",
                       clutchDir,
                       TR_PATH_DELIMITER_STR,
                       subpath && *subpath ? subpath : "index.html" );

        serve_file( req, server, filename );

        tr_free( filename );
        tr_free( subpath );
    }
}

static void
handle_rpc( struct evhttp_request * req,
            struct tr_rpc_server  * server )
{
    struct evbuffer * response = tr_getBuffer( );

    if( req->type == EVHTTP_REQ_GET )
    {
        const char * q;
        if( ( q = strchr( req->uri, '?' ) ) )
            tr_rpc_request_exec_uri( server->session, q + 1, strlen( q + 1 ), response );
    }
    else if( req->type == EVHTTP_REQ_POST )
    {
        tr_rpc_request_exec_json( server->session,
                                  EVBUFFER_DATA( req->input_buffer ),
                                  EVBUFFER_LENGTH( req->input_buffer ),
                                  response );
    }

    {
        struct evbuffer * buf = tr_getBuffer( );
        add_response( req, server, buf,
                      EVBUFFER_DATA( response ),
                      EVBUFFER_LENGTH( response ) );
        evhttp_add_header( req->output_headers, "Content-Type",
                                                "application/json; charset=UTF-8" );
        evhttp_send_reply( req, HTTP_OK, "OK", buf );
        tr_releaseBuffer( buf );
    }

    /* cleanup */
    tr_releaseBuffer( response );
}

static tr_bool
isAddressAllowed( const tr_rpc_server * server,
                  const char *          address )
{
    tr_list * l;

    if( !server->isWhitelistEnabled )
        return TRUE;

    for( l=server->whitelist; l!=NULL; l=l->next )
        if( tr_wildmat( address, l->data ) )
            return TRUE;

    return FALSE;
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
            send_simple_response( req, 401,
                "<p>Unauthorized IP Address.</p>"
                "<p>Either disable the IP address whitelist or add your address to it.</p>"
                "<p>If you're editing settings.json, see the 'rpc-whitelist' and 'rpc-whitelist-enabled' entries.</p>"
                "<p>If you're still using ACLs, use a whitelist instead.  See the transmission-daemon manpage for details.</p>" );
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
                  tr_bool         isEnabled )
{
    server->isEnabled = isEnabled;

    tr_runInEventThread( server->session, onEnabledChanged, server );
}

tr_bool
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
               tr_port         port )
{
    if( server->port != port )
    {
        server->port = port;

        if( server->isEnabled )
            tr_runInEventThread( server->session, restartServer, server );
    }
}

tr_port
tr_rpcGetPort( const tr_rpc_server * server )
{
    return server->port;
}

void
tr_rpcSetWhitelist( tr_rpc_server * server,
                    const char    * whitelistStr )
{
    void * tmp;
    const char * walk;

    /* keep the string */
    tr_free( server->whitelistStr );
    server->whitelistStr = tr_strdup( whitelistStr );

    /* clear out the old whitelist entries */
    while(( tmp = tr_list_pop_front( &server->whitelist )))
        tr_free( tmp );

    /* build the new whitelist entries */
    for( walk=whitelistStr; walk && *walk; ) {
        const char * delimiters = " ,;";
        const size_t len = strcspn( walk, delimiters );
        char * token = tr_strndup( walk, len );
        tr_list_append( &server->whitelist, token );
        if( strcspn( token, "+-" ) < len )
            tr_ninf( MY_NAME, "Adding address to whitelist: %s (And it has a '+' or '-'!  Are you using an old ACL by mistake?)", token );
        else
            tr_ninf( MY_NAME, "Adding address to whitelist: %s", token );
        
        if( walk[len]=='\0' )
            break;
        walk += len + 1;
    }
}

char*
tr_rpcGetWhitelist( const tr_rpc_server * server )
{
    return tr_strdup( server->whitelistStr ? server->whitelistStr : "" );
}

void
tr_rpcSetWhitelistEnabled( tr_rpc_server  * server,
                           tr_bool          isEnabled )
{
    server->isWhitelistEnabled = isEnabled != 0;
}

tr_bool
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
                          tr_bool          isEnabled )
{
    server->isPasswordEnabled = isEnabled;
    dbgmsg( "setting 'password enabled' to %d", (int)isEnabled );
}

tr_bool
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
    void * tmp;
    tr_rpc_server * s = vserver;

    stopServer( s );
    while(( tmp = tr_list_pop_front( &s->whitelist )))
        tr_free( tmp );
#ifdef HAVE_ZLIB
    deflateEnd( &s->stream );
#endif
    tr_free( s->whitelistStr );
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
tr_rpcInit( tr_session  * session,
            tr_bool       isEnabled,
            tr_port       port,
            tr_bool       isWhitelistEnabled,
            const char  * whitelist,
            tr_bool       isPasswordEnabled,
            const char  * username,
            const char  * password )
{
    tr_rpc_server * s;

    s = tr_new0( tr_rpc_server, 1 );
    s->session = session;
    s->port = port;
    s->username = tr_strdup( username );
    s->password = tr_strdup( password );
    s->isWhitelistEnabled = isWhitelistEnabled;
    s->isPasswordEnabled = isPasswordEnabled;
    s->isEnabled = isEnabled != 0;
    tr_rpcSetWhitelist( s, whitelist ? whitelist : "127.0.0.1" );

#ifdef HAVE_ZLIB
    s->stream.zalloc = (alloc_func) Z_NULL;
    s->stream.zfree = (free_func) Z_NULL;
    s->stream.opaque = (voidpf) Z_NULL;
    deflateInit( &s->stream, Z_BEST_COMPRESSION );
#endif

    if( isEnabled )
        tr_runInEventThread( session, startServer, s );

    if( isEnabled )
    {
        tr_ninf( MY_NAME, _( "Serving RPC and Web requests on port %d" ), (int)port );

        if( isWhitelistEnabled )
            tr_ninf( MY_NAME, _( "Whitelist enabled" ) );

        if( isPasswordEnabled )
            tr_ninf( MY_NAME, _( "Password required" ) );
    }

    return s;
}
