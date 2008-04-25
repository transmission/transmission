/* * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <stdlib.h> /* bsearch */

#include <event.h>
#include <curl/curl.h>

#include "transmission.h"
#include "utils.h"
#include "web.h"

#define CURL_CHECK_VERSION(major,minor,micro)    \
    (LIBCURL_VERSION_MAJOR > (major) || \
     (LIBCURL_VERSION_MAJOR == (major) && LIBCURL_VERSION_MINOR > (minor)) || \
     (LIBCURL_VERSION_MAJOR == (major) && LIBCURL_VERSION_MINOR == (minor) && \
      LIBCURL_VERSION_PATCH >= (micro)))

#if CURL_CHECK_VERSION(7,16,0)
#define USE_CURL_MULTI_SOCKET
#else
#define PULSE_MSEC 200
static void pulse( int socket UNUSED, short action UNUSED, void * vweb );
#endif

#define dbgmsg(fmt...) tr_deepLog( __FILE__, __LINE__, "web", ##fmt )

struct tr_web
{
    CURLM * cm;
    tr_session * session;
    int remain;
    struct event timer;
};

struct tr_web_task
{
    unsigned long tag;
    struct evbuffer * response;
    tr_web_done_func * done_func;
    void * done_func_user_data;
};

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * task )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( ((struct tr_web_task*)task)->response, ptr, byteCount );
    return byteCount;
}

static void
pump( tr_web * web )
{
    CURLMcode rc;
    do
#ifdef USE_CURL_MULTI_SOCKET
        rc = curl_multi_socket_all( web->cm, &web->remain );
#else
        rc = curl_multi_perform( web->cm, &web->remain );
#endif
    while( rc == CURLM_CALL_MULTI_PERFORM );
    dbgmsg( "%d tasks remain", web->remain );
    if ( rc != CURLM_OK  )
        tr_err( "%s", curl_multi_strerror(rc) );
}

void
tr_webRun( tr_session         * session,
           const char         * url,
           tr_web_done_func   * done_func,
           void               * done_func_user_data )
{
    static unsigned long tag = 0;
    struct tr_web_task * task;
    struct tr_web * web = session->web;
    CURL * ch;

    task = tr_new0( struct tr_web_task, 1 );
    task->done_func = done_func;
    task->done_func_user_data = done_func_user_data;
    task->tag = ++tag;
    task->response = evbuffer_new( );

    dbgmsg( "adding task #%lu [%s]", task->tag, url );
    ++web->remain;

    ch = curl_easy_init( );
    curl_easy_setopt( ch, CURLOPT_PRIVATE, task );
    curl_easy_setopt( ch, CURLOPT_URL, url );
    curl_easy_setopt( ch, CURLOPT_WRITEFUNCTION, writeFunc );
    curl_easy_setopt( ch, CURLOPT_WRITEDATA, task );
    curl_easy_setopt( ch, CURLOPT_USERAGENT, TR_NAME "/" LONG_VERSION_STRING );
    curl_easy_setopt( ch, CURLOPT_SSL_VERIFYPEER, 0 );
    curl_easy_setopt( ch, CURLOPT_TIMEOUT, 30 );

    curl_multi_add_handle( web->cm, ch );

#ifdef USE_CURL_MULTI_SOCKET
    pump( web );
#else
    if( !evtimer_initialized( &web->timer ) )
        evtimer_set( &web->timer, pulse, web );
    if( !evtimer_pending( &web->timer, NULL ) ) {
        struct timeval tv = tr_timevalMsec( PULSE_MSEC );
        evtimer_add( &web->timer, &tv );
    }
#endif
}

static void
processCompletedTasks( tr_web * web )
{
    int more = 0;

    do {
        CURLMsg * msg = curl_multi_info_read( web->cm, &more );
        if( msg && ( msg->msg == CURLMSG_DONE ) )
        {
            CURL * ch;
            struct tr_web_task * task;
            long response_code;

            if( msg->data.result != CURLE_OK )
                tr_err( "%s", curl_easy_strerror( msg->data.result ) );
            
            ch = msg->easy_handle;
            curl_easy_getinfo( ch, CURLINFO_PRIVATE, &task );
            curl_easy_getinfo( ch, CURLINFO_RESPONSE_CODE, &response_code );

            dbgmsg( "task #%lu done", task->tag );
            task->done_func( web->session,
                             response_code,
                             EVBUFFER_DATA(task->response),
                             EVBUFFER_LENGTH(task->response),
                             task->done_func_user_data );

            curl_multi_remove_handle( web->cm, ch );
            curl_easy_cleanup( ch );

            evbuffer_free( task->response );
            tr_free( task );
        }
    }
    while( more );

    /* remove timeout if there are no transfers left */
    if( !web->remain && evtimer_initialized( &web->timer ) )
        evtimer_del( &web->timer );
}

#ifdef USE_CURL_MULTI_SOCKET
/* libevent says that sock is ready to be processed, so tell libcurl */
static void
event_callback( int sock, short action, void * vweb )
{
    tr_web * web = vweb;
    CURLMcode rc;
    int mask;

    switch (action & (EV_READ|EV_WRITE)) {
        case EV_READ: mask = CURL_CSELECT_IN; break;
        case EV_WRITE: mask = CURL_CSELECT_OUT; break;
        case EV_READ|EV_WRITE: mask = CURL_CSELECT_IN|CURL_CSELECT_OUT; break;
        default: tr_err( "Unknown event %hd\n", action ); return;
    }

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
        curl_multi_assign( web->cm, sock, ev );
    }

    switch (action) {
        case CURL_POLL_IN: events |= EV_READ; break;
        case CURL_POLL_OUT: events |= EV_WRITE; break;
        case CURL_POLL_INOUT: events |= EV_READ|EV_WRITE; break;
        case CURL_POLL_REMOVE: tr_free( ev ); /* fallthrough */
        case CURL_POLL_NONE: return 0;
        default: tr_err( "Unknown socket action %d", action ); return -1;
    }

    event_set( ev, sock, events, event_callback, web );
    event_add( ev, NULL );
    return 0;
}

/* libevent says that timeout_ms have passed, so tell libcurl */
static void
timeout_callback( int socket UNUSED, short action UNUSED, void * vweb )
{
    tr_web * web = vweb;
    CURLMcode rc;

    do rc = curl_multi_socket( web->cm, CURL_SOCKET_TIMEOUT, &web->remain );
    while( rc == CURLM_CALL_MULTI_PERFORM );
    if( rc != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( rc ) );
}

/* libcurl wants us to tell it when timeout_ms have passed */
static void
timer_callback( CURLM *multi UNUSED, long timeout_ms, void * vweb )
{
    tr_web * web = vweb;
    struct timeval tv = tr_timevalMsec( timeout_ms );

    if( evtimer_initialized( &web->timer ) )
        evtimer_del( &web->timer );

    evtimer_set( &web->timer, timeout_callback, vweb );
    evtimer_add( &web->timer, &tv );
}
#else

static void
pulse( int socket UNUSED, short action UNUSED, void * vweb )
{
    tr_web * web = vweb;

    pump( web );
    processCompletedTasks( web );

    if( web->remain > 0 ) {
        struct timeval tv = tr_timevalMsec( PULSE_MSEC );
        evtimer_add( &web->timer, &tv );
    }
}

#endif

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

#ifdef USE_CURL_MULTI_SOCKET
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETFUNCTION, socket_callback );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERFUNCTION, timer_callback );
#endif
#if CURL_CHECK_VERSION(7,16,3)
    curl_multi_setopt( web->cm, CURLMOPT_MAXCONNECTS, 20 );
#endif
#if CURL_CHECK_VERSION(7,16,0)
    curl_multi_setopt( web->cm, CURLMOPT_PIPELINING, 1 );
#endif

    return web;
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
