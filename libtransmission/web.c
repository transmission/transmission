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
#define HAVE_CURL_MULTI_SOCKET
#else
#define PULSE_MSEC 200
static void pulse( int socket UNUSED, short action UNUSED, void * vweb );
#endif

struct tr_web
{
    CURLM * cm;
    tr_session * session;
    int remain;
    struct event timer;
};

struct tr_web_task
{
    unsigned int tag;
    struct evbuffer * response;
    tr_web_done_func * done_func;
    void * done_func_user_data;
};

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * vtask )
{
    const size_t byteCount = size * nmemb;
    struct tr_web_task * task = vtask;
    evbuffer_add( task->response, ptr, byteCount );
    return byteCount;
}

static void
pump( tr_web * web )
{
    CURLMcode rc;
    do {
#ifdef HAVE_CURL_MULTI_SOCKET
        rc = curl_multi_socket_all( web->cm, &web->remain );
#else
        rc = curl_multi_perform( web->cm, &web->remain );
#endif
fprintf( stderr, "remaining: %d\n", web->remain );
    } while( rc == CURLM_CALL_MULTI_PERFORM );
}

void
tr_webRun( tr_session         * session,
           const char         * url,
           tr_web_done_func   * done_func,
           void               * done_func_user_data )
{
    static unsigned int tag = 0;
    struct tr_web_task * task;
    struct tr_web * web = session->web;
    CURL * ch;

    task = tr_new0( struct tr_web_task, 1 );
    task->done_func = done_func;
    task->done_func_user_data = done_func_user_data;
    task->tag = ++tag;
    task->response = evbuffer_new( );

fprintf( stderr, "new web tag %u [%s]\n", task->tag, url );
    ++web->remain;

    ch = curl_easy_init( );
    curl_easy_setopt( ch, CURLOPT_PRIVATE, task );
    curl_easy_setopt( ch, CURLOPT_URL, url );
    curl_easy_setopt( ch, CURLOPT_WRITEFUNCTION, writeFunc );
    curl_easy_setopt( ch, CURLOPT_WRITEDATA, task );
    curl_easy_setopt( ch, CURLOPT_USERAGENT, TR_NAME "/" LONG_VERSION_STRING );

    curl_multi_add_handle( web->cm, ch );

    pump( web );

#ifndef HAVE_CURL_MULTI_SOCKET
    if( !evtimer_initialized( &web->timer ) )
    {
        struct timeval tv = tr_timevalMsec( PULSE_MSEC );
        evtimer_set( &web->timer, pulse, web );
        fprintf( stderr, "no timer running yet... starting one\n" );
        evtimer_add( &web->timer, &tv );
    }
#endif
}

static void
responseHandler( tr_web * web )
{
    int remaining = 0;

    do {
        CURLMsg * msg = curl_multi_info_read( web->cm, &remaining );
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

fprintf( stderr, "web task %u done\n", task->tag );
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
    while( remaining );

    /* remove timeout if there are no transfers left */
    if( !web->remain && evtimer_initialized( &web->timer ) )
        evtimer_del( &web->timer );
}

#ifdef HAVE_CURL_MULTI_SOCKET
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
    fprintf( stderr, "Event on socket %d (%s)\n", sock, strings[action] );
#endif

    switch (action & (EV_READ|EV_WRITE)) {
        case EV_READ: mask = CURL_CSELECT_IN; break;
        case EV_WRITE: mask = CURL_CSELECT_OUT; break;
        case EV_READ|EV_WRITE: mask = CURL_CSELECT_IN|CURL_CSELECT_OUT; break;
        default: tr_err( "Unknown event %d\n", (int)action ); return;
    }

    do
        rc = curl_multi_socket_action( web->cm, sock, mask, &web->remain );
    while( rc == CURLM_CALL_MULTI_PERFORM );

    if ( rc != CURLM_OK  )
        tr_err( "%s (%d)", curl_multi_strerror(rc), (int)sock );

    responseHandler( web );
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

#if 0
    {
        static const char *actions[] = {"NONE", "IN", "OUT", "INOUT", "REMOVE"};
        fprintf( stderr, "Callback on socket %d (%s)\n", (int)sock, actions[action]);
    }
#endif

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
}

/* libcurl wants us to tell it when timeout_ms have passed */
static void
timer_callback( CURLM *multi UNUSED, long timeout_ms, void * vweb )
{
    tr_web * web = vweb;
    struct timeval tv = tr_timevalMsec( timeout_ms );

    if( evtimer_initialized( &web->timeout ) )
        evtimer_del( &web->timeout );

    evtimer_set( &web->timer, timeout_callback, vweb );
    evtimer_add( &web->timer, &tv );
}
#else

static void
pulse( int socket UNUSED, short action UNUSED, void * vweb )
{
    tr_web * web = vweb;

    pump( web );
    responseHandler( web );

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
    web->remain = 0;

#ifdef HAVE_CURL_MULTI_SOCKET
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
