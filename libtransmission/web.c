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
#include <stdlib.h> /* bsearch */

#include <event.h>
#include <curl/curl.h>

#include "transmission.h"
#include "list.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

#define dbgmsg(fmt...) tr_deepLog( __FILE__, __LINE__, "web", ##fmt )

struct tr_web
{
    int remain;
    CURLM * cm;
    tr_session * session;
    tr_list * socket_events;
    struct event timer;
};

struct tr_web_task
{
    unsigned long tag;
    struct evbuffer * response;
    char * url;
    char * range;
    tr_session * session;
    tr_web_done_func * done_func;
    void * done_func_user_data;
};

static void
processCompletedTasks( tr_web * web )
{
    CURL * easy;
    CURLMsg * msg;

    do {
        /* this convoluted loop is from the "hiperinfo.c" sample which
         * hints that removing an easy handle in curl_multi_info_read's
         * loop may be unsafe */
        int more;
        easy = NULL;
        while(( msg = curl_multi_info_read( web->cm, &more ))) {
            if( msg->msg == CURLMSG_DONE ) {
                easy = msg->easy_handle;
                break;
            }
        }
        if( easy ) {
            struct tr_web_task * task;
            long response_code;
            curl_easy_getinfo( easy, CURLINFO_PRIVATE, &task );
            curl_easy_getinfo( easy, CURLINFO_RESPONSE_CODE, &response_code );
            --web->remain;
            dbgmsg( "task #%lu done (%d remain)", task->tag, web->remain );
            task->done_func( web->session,
                             response_code,
                             EVBUFFER_DATA(task->response),
                             EVBUFFER_LENGTH(task->response),
                             task->done_func_user_data );
            curl_multi_remove_handle( web->cm, easy );
            curl_easy_cleanup( easy );
            evbuffer_free( task->response );
            tr_free( task->range );
            tr_free( task->url );
            tr_free( task );
        }
    } while( easy );
}

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * task )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( ((struct tr_web_task*)task)->response, ptr, byteCount );
    return byteCount;
}

static int
getCurlProxyType( tr_proxy_type t )
{
    switch( t )
    {
        case TR_PROXY_SOCKS4: return CURLPROXY_SOCKS4;
        case TR_PROXY_SOCKS5: return CURLPROXY_SOCKS5;
        default:              return CURLPROXY_HTTP;
    }
}



static void
addTask( void * vtask )
{
    struct tr_web_task * task = vtask;
    const tr_handle * session = task->session;

    if( session && session->web )
    {
        struct tr_web * web = session->web;
        CURL * ch;

        ++web->remain;
        dbgmsg( "adding task #%lu [%s] (%d remain)", task->tag, task->url, web->remain );

        ch = curl_easy_init( );

        if( getenv( "TRANSMISSION_LIBCURL_VERBOSE" ) != 0 )
            curl_easy_setopt( ch, CURLOPT_VERBOSE, 1 );

        if( !task->range && session->isProxyEnabled ) {
            curl_easy_setopt( ch, CURLOPT_PROXY, session->proxy );
            curl_easy_setopt( ch, CURLOPT_PROXYPORT, session->proxyPort );
            curl_easy_setopt( ch, CURLOPT_PROXYTYPE, getCurlProxyType( session->proxyType ) );
            curl_easy_setopt( ch, CURLOPT_PROXYAUTH, CURLAUTH_ANY );
        }
        if( !task->range && session->isProxyAuthEnabled ) {
            char * str = tr_strdup_printf( "%s:%s", session->proxyUsername, session->proxyPassword );
            curl_easy_setopt( ch, CURLOPT_PROXYUSERPWD, str );
            tr_free( str );
        }

        curl_easy_setopt( ch, CURLOPT_PRIVATE, task );
        curl_easy_setopt( ch, CURLOPT_URL, task->url );
        curl_easy_setopt( ch, CURLOPT_WRITEFUNCTION, writeFunc );
        curl_easy_setopt( ch, CURLOPT_WRITEDATA, task );
        curl_easy_setopt( ch, CURLOPT_USERAGENT, TR_NAME "/" LONG_VERSION_STRING );
        curl_easy_setopt( ch, CURLOPT_SSL_VERIFYHOST, 0 );
        curl_easy_setopt( ch, CURLOPT_SSL_VERIFYPEER, 0 );
        curl_easy_setopt( ch, CURLOPT_FORBID_REUSE, 1 );
        curl_easy_setopt( ch, CURLOPT_NOSIGNAL, 1 );
        curl_easy_setopt( ch, CURLOPT_FOLLOWLOCATION, 1 );
        curl_easy_setopt( ch, CURLOPT_MAXREDIRS, 5 );
        curl_easy_setopt( ch, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
        if( task->range )
            curl_easy_setopt( ch, CURLOPT_RANGE, task->range );
        else /* don't set encoding if range is sent; it messes up binary data */
            curl_easy_setopt( ch, CURLOPT_ENCODING, "" );
        curl_multi_add_handle( web->cm, ch );
    }
}

void
tr_webRun( tr_session         * session,
           const char         * url,
           const char         * range,
           tr_web_done_func   * done_func,
           void               * done_func_user_data )
{
    if( session->web )
    {
        static unsigned long tag = 0;
        struct tr_web_task * task;

        task = tr_new0( struct tr_web_task, 1 );
        task->session = session;
        task->url = tr_strdup( url );
        task->range = tr_strdup( range );
        task->done_func = done_func;
        task->done_func_user_data = done_func_user_data;
        task->tag = ++tag;
        task->response = evbuffer_new( );

        tr_runInEventThread( session, addTask, task );
    }
}

/* libevent says that sock is ready to be processed, so wake up libcurl */
static void
event_callback( int sock, short action, void * vweb )
{
    tr_web * web = vweb;
    CURLMcode rc;
    int mask;

#if 0
    static const char *strings[] = {
        "NONE","TIMEOUT","READ","TIMEOUT|READ","WRITE","TIMEOUT|WRITE",
        "READ|WRITE","TIMEOUT|READ|WRITE","SIGNAL" };
     fprintf( stderr, "%s:%d (%s) event on socket %d (%s)\n",
              __FILE__, __LINE__, __FUNCTION__,
              sock, strings[action] );
#endif

    mask = 0;
    if( action & EV_READ  ) mask |= CURL_CSELECT_IN;
    if( action & EV_WRITE ) mask |= CURL_CSELECT_OUT;

    do
        rc = curl_multi_socket_action( web->cm, sock, mask, &web->remain );
    while( rc == CURLM_CALL_MULTI_PERFORM );

    if ( rc != CURLM_OK  )
        tr_err( "%s (%d)", curl_multi_strerror(rc), (int)sock );

    processCompletedTasks( web );
}

/* libcurl wants us to tell it when sock is ready to be processed */
static int
socket_callback( CURL            * easy UNUSED,
                 curl_socket_t     sock,
                 int               action,
                 void            * vweb,
                 void            * assigndata )
{
    tr_web * web = vweb;
    int events = EV_PERSIST;
    struct event * ev = assigndata;

    if( ev )
        event_del( ev );
    else {
        ev = tr_new0( struct event, 1 );
        tr_list_prepend( &web->socket_events, ev );
        curl_multi_assign( web->cm, sock, ev );
    }

#if 0
    {
        static const char *actions[] = {"NONE", "IN", "OUT", "INOUT", "REMOVE"};
        fprintf( stderr, "%s:%d (%s) callback on socket %d (%s)\n",
                 __FILE__, __LINE__, __FUNCTION__,
                 (int)sock, actions[action]);
    }
#endif

    switch (action) {
        case CURL_POLL_IN: events |= EV_READ; break;
        case CURL_POLL_OUT: events |= EV_WRITE; break;
        case CURL_POLL_INOUT: events |= EV_READ|EV_WRITE; break;
        case CURL_POLL_REMOVE: tr_list_remove_data( &web->socket_events, ev );
                               tr_free( ev );
                               /* fallthrough */
        case CURL_POLL_NONE: return 0;
        default: tr_err( "Unknown socket action %d", action ); return -1;
    }

    event_set( ev, sock, events, event_callback, web );
    event_add( ev, NULL );
    return 0;
}

/* libevent says that timeout_ms have passed, so wake up libcurl */
static void
timeout_callback( int socket UNUSED, short action UNUSED, void * vweb )
{
    CURLMcode rc;
    tr_web * web = vweb;

    do
        rc = curl_multi_socket( web->cm, CURL_SOCKET_TIMEOUT, &web->remain );
    while( rc == CURLM_CALL_MULTI_PERFORM );

    if( rc != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( rc ) );

    processCompletedTasks( web );
}

/* libcurl wants us to tell it when timeout_ms have passed */
static void
timer_callback( CURLM *multi UNUSED, long timeout_ms, void * vweb )
{
    tr_web * web = vweb;
    struct timeval timeout = tr_timevalMsec( timeout_ms );
    timeout_add( &web->timer, &timeout );
}

tr_web*
tr_webInit( tr_session * session )
{
    static int curlInited = FALSE;
    tr_web * web;

    /* call curl_global_init if we haven't done it already.
     * try to enable ssl for https support; but if that fails,
     * try a plain vanilla init */ 
    if( curlInited == FALSE ) {
        curlInited = TRUE;
        if( curl_global_init( CURL_GLOBAL_SSL ) )
            curl_global_init( 0 );
    }
   
    web = tr_new0( struct tr_web, 1 );
    web->cm = curl_multi_init( );
    web->session = session;

    timeout_set( &web->timer, timeout_callback, web );
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETFUNCTION, socket_callback );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERFUNCTION, timer_callback );

    return web;
}

static void
event_del_and_free( void * e )
{
    event_del( e );
    tr_free( e );
}

void
tr_webClose( tr_web ** web_in )
{
    tr_web * web = *web_in;
    *web_in = NULL;

    timeout_del( &web->timer );
    tr_list_free( &web->socket_events, event_del_and_free );
    curl_multi_cleanup( web->cm );
    tr_free( web );
}

/***
****
***/

static struct http_msg {
    long code;
    const char * text;
} http_msg[] = {
    { 101, "Switching Protocols" },
    { 200, "OK" },
    { 201, "Created" },
    { 202, "Accepted" },
    { 203, "Non-Authoritative Information" },
    { 204, "No Content" },
    { 205, "Reset Content" },
    { 206, "Partial Content" },
    { 300, "Multiple Choices" },
    { 301, "Moved Permanently" },
    { 302, "Found" },
    { 303, "See Other" },
    { 304, "Not Modified" },
    { 305, "Use Proxy" },
    { 306, "(Unused)" },
    { 307, "Temporary Redirect" },
    { 400, "Bad Request" },
    { 401, "Unauthorized" },
    { 402, "Payment Required" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
    { 405, "Method Not Allowed" },
    { 406, "Not Acceptable" },
    { 407, "Proxy Authentication Required" },
    { 408, "Request Timeout" },
    { 409, "Conflict" },
    { 410, "Gone" },
    { 411, "Length Required" },
    { 412, "Precondition Failed" },
    { 413, "Request Entity Too Large" },
    { 414, "Request-URI Too Long" },
    { 415, "Unsupported Media Type" },
    { 416, "Requested Range Not Satisfiable" },
    { 417, "Expectation Failed" },
    { 500, "Internal Server Error" },
    { 501, "Not Implemented" },
    { 502, "Bad Gateway" },
    { 503, "Service Unavailable" },
    { 504, "Gateway Timeout" },
    { 505, "HTTP Version Not Supported" },
    { 0, NULL }
};

static int
compareResponseCodes( const void * va, const void * vb )
{
    const long a = *(const long*) va;
    const struct http_msg * b = vb;
    return a - b->code;
}

const char *
tr_webGetResponseStr( long code )
{
    struct http_msg * msg = bsearch( &code,
                                     http_msg, 
                                     sizeof( http_msg ) / sizeof( http_msg[0] ),
                                     sizeof( http_msg[0] ),
                                     compareResponseCodes );
    return msg ? msg->text : "Unknown Error";
}
