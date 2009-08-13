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

#include <event.h>
#include <evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "crypto.h"
#include "list.h"
#include "net.h"
#include "platform.h"
#include "ptrarray.h"
#include "rpcimpl.h"
#include "rpc-server.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

/* session-id is used to make cross-site request forgery attacks difficult.
 * Don't disable this feature unless you really know what you're doing!
 * http://en.wikipedia.org/wiki/Cross-site_request_forgery
 * http://shiflett.org/articles/cross-site-request-forgeries
 * http://www.webappsec.org/lists/websecurity/archive/2008-04/msg00037.html */
#define REQUIRE_SESSION_ID

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
    struct in_addr     bindAddress;
    struct evhttp *    httpd;
    tr_session *       session;
    char *             username;
    char *             password;
    char *             whitelistStr;
    tr_list *          whitelist;

    char *             sessionId;
    time_t             sessionIdExpiresAt;

#ifdef HAVE_ZLIB
    tr_bool            isStreamInitialized;
    z_stream           stream;
#endif
};

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, MY_NAME, __VA_ARGS__ ); \
    } while( 0 )


/***
****
***/

static char*
get_current_session_id( struct tr_rpc_server * server )
{
    const time_t now = time( NULL );

    if( !server->sessionId || ( now >= server->sessionIdExpiresAt ) )
    {
        int i;
        const int n = 48;
        const char * pool = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        const size_t pool_size = strlen( pool );
        char * buf = tr_new( char, n+1 );

        for( i=0; i<n; ++i )
            buf[i] = pool[ tr_cryptoRandInt( pool_size ) ];
        buf[n] = '\0';

        tr_free( server->sessionId );
        server->sessionId = buf;
        server->sessionIdExpiresAt = now + (60*60); /* expire in an hour */
    }

    return server->sessionId;
}


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

struct tr_mimepart
{
    char * headers;
    int headers_len;
    char * body;
    int body_len;
};

static void
tr_mimepart_free( struct tr_mimepart * p )
{
    tr_free( p->body );
    tr_free( p->headers );
    tr_free( p );
}

static void
extract_parts_from_multipart( const struct evkeyvalq * headers,
                              const struct evbuffer * body,
                              tr_ptrArray * setme_parts )
{
    const char * content_type = evhttp_find_header( headers, "Content-Type" );
    const char * in = (const char*) EVBUFFER_DATA( body );
    size_t inlen = EVBUFFER_LENGTH( body );

    const char * boundary_key = "boundary=";
    const char * boundary_key_begin = strstr( content_type, boundary_key );
    const char * boundary_val = boundary_key_begin ? boundary_key_begin + strlen( boundary_key ) : "arglebargle";
    char * boundary = tr_strdup_printf( "--%s", boundary_val );
    const size_t boundary_len = strlen( boundary );

    const char * delim = tr_memmem( in, inlen, boundary, boundary_len );
    while( delim )
    {
        size_t part_len;
        const char * part = delim + boundary_len;

        inlen -= ( part - in );
        in = part;

        delim = tr_memmem( in, inlen, boundary, boundary_len );
        part_len = delim ? (size_t)( delim - part ) : inlen;

        if( part_len )
        {
            const char * rnrn = tr_memmem( part, part_len, "\r\n\r\n", 4 );
            if( rnrn )
            {
                struct tr_mimepart * p = tr_new( struct tr_mimepart, 1 );
                p->headers_len = rnrn - part;
                p->headers = tr_strndup( part, p->headers_len );
                p->body_len = (part+part_len) - (rnrn + 4);
                p->body = tr_strndup( rnrn+4, p->body_len );
                tr_ptrArrayAppend( setme_parts, p );
            }
        }
    }

    tr_free( boundary );
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
        int i;
        int n;
        tr_bool hasSessionId = FALSE;
        tr_ptrArray parts = TR_PTR_ARRAY_INIT;

        const char * query = strchr( req->uri, '?' );
        const tr_bool paused = query && strstr( query + 1, "paused=true" );

        extract_parts_from_multipart( req->input_headers, req->input_buffer, &parts );
        n = tr_ptrArraySize( &parts );

        /* first look for the session id */
        for( i=0; i<n; ++i ) {
            struct tr_mimepart * p = tr_ptrArrayNth( &parts, i );
            if( tr_memmem( p->headers, p->headers_len, TR_RPC_SESSION_ID_HEADER, strlen( TR_RPC_SESSION_ID_HEADER ) ) )
                break;
        }
        if( i<n ) {
            const struct tr_mimepart * p = tr_ptrArrayNth( &parts, i );
            const char * ours = get_current_session_id( server );
            const int ourlen = strlen( ours );
            hasSessionId = ourlen<=p->body_len && !memcmp( p->body, ours, ourlen );
        }

        if( !hasSessionId )
        {
            send_simple_response( req, 409, NULL );
        }
        else for( i=0; i<n; ++i )
        {
            struct tr_mimepart * p = tr_ptrArrayNth( &parts, i );
            if( strstr( p->headers, "filename=\"" ) )
            {
                char * b64;
                int body_len = p->body_len;
                tr_benc top, *args;
                const char * body = p->body;
                struct evbuffer * json = evbuffer_new( );

                if( body_len >= 2 && !memcmp( &body[body_len - 2], "\r\n", 2 ) )
                    body_len -= 2;

                tr_bencInitDict( &top, 2 );
                args = tr_bencDictAddDict( &top, "arguments", 2 );
                tr_bencDictAddStr( &top, "method", "torrent-add" );
                b64 = tr_base64_encode( body, body_len, NULL );
                tr_bencDictAddStr( args, "metainfo", b64 );
                tr_bencDictAddBool( args, "paused", paused );
                tr_bencToBuf( &top, TR_FMT_JSON_LEAN, json );
                tr_rpc_request_exec_json( server->session,
                                          EVBUFFER_DATA( json ),
                                          EVBUFFER_LENGTH( json ),
                                          NULL, NULL );

                evbuffer_free( json );
                tr_free( b64 );
                tr_bencFree( &top );
            }
        }

        tr_ptrArrayDestruct( &parts, (PtrArrayForeachFunc)tr_mimepart_free );

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

        /* FIXME(libevent2): this won't compile under libevent2.
           but we can use evbuffer_reserve_space() + evbuffer_commit_space() instead */

        if( !server->isStreamInitialized )
        {
            server->isStreamInitialized = TRUE;
            server->stream.zalloc = (alloc_func) Z_NULL;
            server->stream.zfree = (free_func) Z_NULL;
            server->stream.opaque = (voidpf) Z_NULL;
            deflateInit( &server->stream, Z_BEST_COMPRESSION );
        }

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
add_time_header( struct evkeyvalq * headers, const char * key, time_t value )
{
    /* According to RFC 2616 this must follow RFC 1123's date format,
       so use gmtime instead of localtime... */
    char buf[128];
    struct tm tm = *gmtime( &value );
    strftime( buf, sizeof( buf ), "%a, %d %b %Y %H:%M:%S GMT", &tm );
    evhttp_add_header( headers, key, buf );
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
            char * tmp = tr_strdup_printf( "%s (%s)", filename, tr_strerror( errno ) );
            send_simple_response( req, HTTP_NOTFOUND, tmp );
            tr_free( tmp );
        }
        else
        {
            struct evbuffer * out;
            const time_t now = time( NULL );

            errno = error;
            out = evbuffer_new( );
            evhttp_add_header( req->output_headers, "Content-Type", mimetype_guess( filename ) );
            add_time_header( req->output_headers, "Date", now );
            add_time_header( req->output_headers, "Expires", now+(24*60*60) );
            add_response( req, server, out, content, content_len );
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
            "set the TRANSMISSION_WEB_HOME environment "
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

        subpath = tr_strdup( req->uri + 18 );
        if(( pch = strchr( subpath, '?' )))
            *pch = '\0';

        if( strstr( subpath, ".." ) )
        {
            send_simple_response( req, HTTP_NOTFOUND, "<p>Tsk, tsk.</p>" );
        }
        else
        {
            char * filename = tr_strdup_printf( "%s%s%s",
                                 clutchDir,
                                 TR_PATH_DELIMITER_STR,
                                 subpath && *subpath ? subpath : "index.html" );
            serve_file( req, server, filename );
            tr_free( filename );
        }

        tr_free( subpath );
    }
}

struct rpc_response_data
{
    struct evhttp_request * req;
    struct tr_rpc_server  * server;
};

/* FIXME(libevent2): make "response" an evbuffer and remove response_len */
static void
rpc_response_func( tr_session      * session UNUSED,
                   const char      * response,
                   size_t            response_len,
                   void            * user_data )
{
    struct rpc_response_data * data = user_data;
    struct evbuffer * buf = evbuffer_new( );

    add_response( data->req, data->server, buf, response, response_len );
    evhttp_add_header( data->req->output_headers,
                           "Content-Type", "application/json; charset=UTF-8" );
    evhttp_send_reply( data->req, HTTP_OK, "OK", buf );

    evbuffer_free( buf );
    tr_free( data );
}


static void
handle_rpc( struct evhttp_request * req,
            struct tr_rpc_server  * server )
{
    struct rpc_response_data * data = tr_new0( struct rpc_response_data, 1 );

    data->req = req;
    data->server = server;

    if( req->type == EVHTTP_REQ_GET )
    {
        const char * q;
        if( ( q = strchr( req->uri, '?' ) ) )
            tr_rpc_request_exec_uri( server->session, q+1, -1, rpc_response_func, data );
    }
    else if( req->type == EVHTTP_REQ_POST )
    {
        tr_rpc_request_exec_json( server->session,
                                  EVBUFFER_DATA( req->input_buffer ),
                                  EVBUFFER_LENGTH( req->input_buffer ),
                                  rpc_response_func, data );
    }

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

static tr_bool
test_session_id( struct tr_rpc_server * server, struct evhttp_request * req )
{
    const char * ours = get_current_session_id( server );
    const char * theirs = evhttp_find_header( req->input_headers, TR_RPC_SESSION_ID_HEADER );
    const tr_bool success =  theirs && !strcmp( theirs, ours );
    return success;
}

static void
handle_request( struct evhttp_request * req, void * arg )
{
    struct tr_rpc_server * server = arg;

    if( req && req->evcon )
    {
        const char * auth;
        char * user = NULL;
        char * pass = NULL;

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
            send_simple_response( req, 403,
                "<p>Unauthorized IP Address.</p>"
                "<p>Either disable the IP address whitelist or add your address to it.</p>"
                "<p>If you're editing settings.json, see the 'rpc-whitelist' and 'rpc-whitelist-enabled' entries.</p>"
                "<p>If you're still using ACLs, use a whitelist instead.  See the transmission-daemon manpage for details.</p>" );
        }
        else if( server->isPasswordEnabled
                 && ( !pass || !user || strcmp( server->username, user )
                                     || !tr_ssha1_matches( server->password,
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
            const char * protocol = "http";
            const char * host = evhttp_find_header( req->input_headers, "Host" );
            const char * uri = "transmission/web/";
            char * location = tr_strdup_printf( "%s://%s/%s", protocol, host, uri );
            evhttp_add_header( req->output_headers, "Location", location );
            send_simple_response( req, HTTP_MOVEPERM, NULL );
            tr_free( location );
        }
        else if( !strncmp( req->uri, "/transmission/web/", 18 ) )
        {
            handle_clutch( req, server );
        }
        else if( !strncmp( req->uri, "/transmission/upload", 20 ) )
        {
            handle_upload( req, server );
        }
#ifdef REQUIRE_SESSION_ID
        else if( !test_session_id( server, req ) )
        {
            const char * sessionId = get_current_session_id( server );
            char * tmp = tr_strdup_printf(
                "<p>Your request had an invalid session-id header.</p>"
                "<p>To fix this, follow these steps:"
                "<ol><li> When reading a response, get its X-Transmission-Session-Id header and remember it"
                "<li> Add the updated header to your outgoing requests"
                "<li> When you get this 409 error message, resend your request with the updated header"
                "</ol></p>"
                "<p>This requirement has been added to help prevent "
                "<a href=\"http://en.wikipedia.org/wiki/Cross-site_request_forgery\">CSRF</a> "
                "attacks.</p>"
                "<p><code>%s: %s</code></p>",
                TR_RPC_SESSION_ID_HEADER, sessionId );
            evhttp_add_header( req->output_headers, TR_RPC_SESSION_ID_HEADER, sessionId );
            send_simple_response( req, 409, tmp );
            tr_free( tmp );
        }
#endif
        else if( !strncmp( req->uri, "/transmission/rpc", 17 ) )
        {
            handle_rpc( req, server );
        }
        else
        {
            send_simple_response( req, HTTP_NOTFOUND, req->uri );
        }

        tr_free( user );
    }
}

static void
startServer( void * vserver )
{
    tr_rpc_server * server  = vserver;
    tr_address addr;

    if( !server->httpd )
    {
        addr.type = TR_AF_INET;
        addr.addr.addr4 = server->bindAddress;
        server->httpd = evhttp_new( tr_eventGetBase( server->session ) );
        evhttp_bind_socket( server->httpd, tr_ntop_non_ts( &addr ),
                            server->port );
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

const char*
tr_rpcGetWhitelist( const tr_rpc_server * server )
{
    return server->whitelistStr ? server->whitelistStr : "";
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

const char*
tr_rpcGetUsername( const tr_rpc_server * server )
{
    return server->username ? server->username : "";
}

void
tr_rpcSetPassword( tr_rpc_server * server,
                   const char *    password )
{
    tr_free( server->password );
    if( *password != '{' )
        server->password = tr_ssha1( password );
    else
        server->password = strdup( password );
    dbgmsg( "setting our Password to [%s]", server->password );
}

const char*
tr_rpcGetPassword( const tr_rpc_server * server )
{
    return server->password ? server->password : "" ;
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

const char *
tr_rpcGetBindAddress( const tr_rpc_server * server )
{
    tr_address addr;
    addr.type = TR_AF_INET;
    addr.addr.addr4 = server->bindAddress;
    return tr_ntop_non_ts( &addr );
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
    if( s->isStreamInitialized )
        deflateEnd( &s->stream );
#endif
    tr_free( s->sessionId );
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
            tr_benc * settings )
{
    tr_rpc_server * s;
    tr_bool found;
    tr_bool boolVal;
    int64_t i;
    const char *str;
    tr_address address;

    s = tr_new0( tr_rpc_server, 1 );
    s->session = session;

    found = tr_bencDictFindBool( settings, TR_PREFS_KEY_RPC_ENABLED, &boolVal );
    assert( found );
    s->isEnabled = boolVal;

    found = tr_bencDictFindInt( settings, TR_PREFS_KEY_RPC_PORT, &i );
    assert( found );
    s->port = i;

    found = tr_bencDictFindBool( settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, &boolVal );
    assert( found );
    s->isWhitelistEnabled = boolVal;

    found = tr_bencDictFindBool( settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, &boolVal );
    assert( found );
    s->isPasswordEnabled = boolVal;

    found = tr_bencDictFindStr( settings, TR_PREFS_KEY_RPC_WHITELIST, &str );
    assert( found );
    tr_rpcSetWhitelist( s, str ? str : "127.0.0.1" );

    found = tr_bencDictFindStr( settings, TR_PREFS_KEY_RPC_USERNAME, &str );
    assert( found );
    s->username = tr_strdup( str );

    found = tr_bencDictFindStr( settings, TR_PREFS_KEY_RPC_PASSWORD, &str );
    assert( found );
    if( *str != '{' )
        s->password = tr_ssha1( str );
    else
        s->password = strdup( str );

    found = tr_bencDictFindStr( settings, TR_PREFS_KEY_RPC_BIND_ADDRESS, &str );
    assert( found );
    if( tr_pton( str, &address ) == NULL ) {
        tr_err( _( "%s is not a valid address" ), str );
        address = tr_inaddr_any;
    } else if( address.type != TR_AF_INET ) {
        tr_err( _( "%s is not an IPv4 address. RPC listeners must be IPv4" ),
                   str );
        address = tr_inaddr_any;
    }
    s->bindAddress = address.addr.addr4;

    if( s->isEnabled )
    {
        tr_ninf( MY_NAME, _( "Serving RPC and Web requests on port %d" ), (int) s->port );
        tr_runInEventThread( session, startServer, s );

        if( s->isWhitelistEnabled )
            tr_ninf( MY_NAME, "%s", _( "Whitelist enabled" ) );

        if( s->isPasswordEnabled )
            tr_ninf( MY_NAME, "%s", _( "Password required" ) );
    }

    return s;
}
