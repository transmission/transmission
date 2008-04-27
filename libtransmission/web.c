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

#include <stdlib.h> /* bsearch */

#include <event.h>
#include <curl/curl.h>

#include "transmission.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

#define CURL_CHECK_VERSION(major,minor,micro)    \
    (LIBCURL_VERSION_MAJOR > (major) || \
     (LIBCURL_VERSION_MAJOR == (major) && LIBCURL_VERSION_MINOR > (minor)) || \
     (LIBCURL_VERSION_MAJOR == (major) && LIBCURL_VERSION_MINOR == (minor) && \
      LIBCURL_VERSION_PATCH >= (micro)))

//#if CURL_CHECK_VERSION(7,16,0)
//#define USE_CURL_MULTI_SOCKET
//#else
#define PULSE_MSEC 150
//#endif

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
    char * url;
    tr_session * session;
    tr_web_done_func * done_func;
    void * done_func_user_data;
};

static void
processCompletedTasks( tr_web * web )
{
    CURL * easy;
    CURLMsg * msg;
    CURLcode res;

    do {
        /* this convoluted loop is from the "hiperinfo.c" sample which
         * hints that removing an easy handle in curl_multi_info_read's
         * loop may be unsafe */
        int more;
        easy = NULL;
        while(( msg = curl_multi_info_read( web->cm, &more ))) {
            if( msg->msg == CURLMSG_DONE ) {
                easy = msg->easy_handle;
                res = msg->data.result;
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
            tr_free( task->url );
            tr_free( task );
        }
    } while( easy );
}

static void
pump( tr_web * web )
{
    int unused;
    CURLMcode rc;
    do {
#ifdef USE_CURL_MULTI_SOCKET
        rc = curl_multi_socket_all( web->cm, &unused );
#else
        rc = curl_multi_perform( web->cm, &unused );
#endif
    } while( rc == CURLM_CALL_MULTI_PERFORM );
    if ( rc == CURLM_OK  )
        processCompletedTasks( web );
    else
        tr_err( "%s", curl_multi_strerror(rc) );
}

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * task )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( ((struct tr_web_task*)task)->response, ptr, byteCount );
    return byteCount;
}

static void
addTask( void * vtask )
{
    struct tr_web_task * task = vtask;

    if( task->session && task->session->web )
    {
        struct tr_web * web = task->session->web;
        CURL * ch;

        ++web->remain;
        dbgmsg( "adding task #%lu [%s] (%d remain)", task->tag, task->url, web->remain );

        ch = curl_easy_init( );
        curl_easy_setopt( ch, CURLOPT_PRIVATE, task );
        curl_easy_setopt( ch, CURLOPT_URL, task->url );
        curl_easy_setopt( ch, CURLOPT_WRITEFUNCTION, writeFunc );
        curl_easy_setopt( ch, CURLOPT_WRITEDATA, task );
        curl_easy_setopt( ch, CURLOPT_USERAGENT, TR_NAME "/" LONG_VERSION_STRING );
        curl_easy_setopt( ch, CURLOPT_SSL_VERIFYPEER, 0 );
        curl_easy_setopt( ch, CURLOPT_FORBID_REUSE, 1 );
        curl_easy_setopt( ch, CURLOPT_NOSIGNAL, 1 );
        curl_easy_setopt( ch, CURLOPT_FOLLOWLOCATION, 1 );
        curl_easy_setopt( ch, CURLOPT_MAXREDIRS, 5 );
        curl_easy_setopt( ch, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
        curl_easy_setopt( ch, CURLOPT_ENCODING, "" );
        curl_multi_add_handle( web->cm, ch );

        pump( web );
    }
}

void
tr_webRun( tr_session         * session,
           const char         * url,
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
        task->done_func = done_func;
        task->done_func_user_data = done_func_user_data;
        task->tag = ++tag;
        task->response = evbuffer_new( );

        tr_runInEventThread( session, addTask, task );
    }
}

#ifdef USE_CURL_MULTI_SOCKET

/* libevent says that sock is ready to be processed, so tell libcurl */
static void
ev_sock_cb( int sock, short action, void * vweb )
{
    tr_web * web = vweb;
    CURLMcode rc;
    int mask, unused;

    switch (action & (EV_READ|EV_WRITE)) {
        case EV_READ: mask = CURL_CSELECT_IN; break;
        case EV_WRITE: mask = CURL_CSELECT_OUT; break;
        case EV_READ|EV_WRITE: mask = CURL_CSELECT_IN|CURL_CSELECT_OUT; break;
        default: tr_err( "Unknown event %hd\n", action ); return;
    }

    do {
        rc = curl_multi_socket_action( web->cm, sock, mask, &unused );
    } while( rc == CURLM_CALL_MULTI_PERFORM );
    if ( rc == CURLM_OK  )
        processCompletedTasks( web );
    else
        tr_err( "%s (%d)", curl_multi_strerror(rc), (int)sock );

}

/* CURLMPOPT_SOCKETFUNCTION */
/* libcurl wants us to tell it when sock is ready to be processed */
static void
multi_sock_cb( CURL            * easy UNUSED,
               curl_socket_t     sock,
               int               action,
               void            * vweb,
               void            * assigndata )
{
    tr_web * web = vweb;
    struct event * ev = assigndata;

    if( action == CURL_POLL_REMOVE ) {
        if( ev ) {
            dbgmsg( "deleting libevent socket polling" );
            event_del( ev );
            tr_free( ev );
            curl_multi_assign( web->cm, sock, NULL );
        }
    } else {
        int kind;
        if( ev ) {
            event_del( ev );
        } else {
            ev = tr_new0( struct event, 1 );
            curl_multi_assign( web->cm, sock, ev );
        }
        kind = EV_PERSIST;
        if( action & CURL_POLL_IN ) kind |= EV_READ;
        if( action & CURL_POLL_OUT ) kind |= EV_WRITE;
        event_set( ev, sock, kind, ev_sock_cb, web );
        event_add( ev, NULL );
    }
}

/* libevent says that timeout_ms have passed, so tell libcurl */
static void
event_timer_cb( int socket UNUSED, short action UNUSED, void * vweb )
{
    int unused;
    CURLMcode rc;
    tr_web * web = vweb;

    do {
        rc = curl_multi_socket( web->cm, CURL_SOCKET_TIMEOUT, &unused );
    } while( rc == CURLM_CALL_MULTI_PERFORM );
    if ( rc == CURLM_OK  )
        processCompletedTasks( web );
    else
        tr_err( "%s", curl_multi_strerror(rc) );
}

/* CURLMPOPT_TIMERFUNCTION */
static void
multi_timer_cb( CURLM *multi UNUSED, long timeout_ms, void * vweb )
{
    tr_web * web = vweb;
    struct timeval tv = tr_timevalMsec( timeout_ms );
    evtimer_add( &web->timer, &tv );
}

#else

static void
pulse( int socket UNUSED, short action UNUSED, void * vweb )
{
    tr_web * web = vweb;
    struct timeval tv = tr_timevalMsec( PULSE_MSEC );

    pump( web );

    evtimer_del( &web->timer );
    evtimer_add( &web->timer, &tv );
}

#endif

tr_web*
tr_webInit( tr_session * session )
{
#ifndef USE_CURL_MULTI_SOCKET
    struct timeval tv = tr_timevalMsec( PULSE_MSEC );
#endif
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
    evtimer_set( &web->timer, event_timer_cb, web );
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_SOCKETFUNCTION, multi_sock_cb );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERDATA, web );
    curl_multi_setopt( web->cm, CURLMOPT_TIMERFUNCTION, multi_timer_cb );
#else
    evtimer_set( &web->timer, pulse, web );
    evtimer_add( &web->timer, &tv );
#endif
#if CURL_CHECK_VERSION(7,16,3)
    curl_multi_setopt( web->cm, CURLMOPT_MAXCONNECTS, 10 );
#endif
    pump( web );

    return web;
}

void
tr_webClose( tr_web * web )
{
    dbgmsg( "deleting web->timer" );
    evtimer_del( &web->timer );
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
