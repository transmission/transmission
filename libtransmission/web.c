/*
 * This file Copyright (C) 2008-2009 Mnemosyne LLC
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

#define CURL_DISABLE_TYPECHECK /* otherwise -Wunreachable-code goes insane */
#include <curl/curl.h>

#include "transmission.h"
#include "net.h" /* socklen_t */
#include "session.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "version.h"
#include "web.h"

enum
{
    /* arbitrary number */
    DEFAULT_TIMER_MSEC = 1500
};

static void
tr_multi_perform( tr_web * g, int fd );

#if 0
#define dbgmsg(...) \
    do { \
        fprintf( stderr, __VA_ARGS__ ); \
        fprintf( stderr, "\n" ); \
    } while( 0 )
#else
#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, "web", __VA_ARGS__ ); \
    } while( 0 )
#endif

struct tr_web
{
    tr_bool closing;
    int prev_running;
    int still_running;
    long timer_msec;
    CURLM * multi;
    tr_session * session;
    tr_bool haveAddr;
    tr_address addr;
    struct event timer_event;
};

/***
****
***/

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

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * vtask )
{
    const size_t byteCount = size * nmemb;
    struct tr_web_task * task = vtask;
    evbuffer_add( task->response, ptr, byteCount );
    dbgmsg( "wrote %zu bytes to task %p's buffer", byteCount, task );
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
sockoptfunction( void * vtask, curl_socket_t fd, curlsocktype purpose UNUSED )
{
    struct tr_web_task * task = vtask;
    const tr_bool isScrape = strstr( task->url, "scrape" ) != NULL;
    const tr_bool isAnnounce = strstr( task->url, "announce" ) != NULL;

    /* announce and scrape requests have tiny payloads... 
     * which have very small payloads */
    if( isScrape || isAnnounce )
    {
        int sndbuf = 1024;
        int rcvbuf = isScrape ? 2048 : 3072;

        if( setsockopt( fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf) ) )
            tr_inf( "Unable to set SO_SNDBUF on socket %d: %s", fd, tr_strerror( sockerrno ) );

        if( setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf) ) )
            tr_inf( "Unable to set SO_RCVBUF on socket %d: %s", fd, tr_strerror( sockerrno ) );
    }
}

static void
addTask( void * vtask )
{
    struct tr_web_task * task = vtask;
    const tr_session * session = task->session;

    if( session && session->web )
    {
        struct tr_web * web = session->web;
        CURL * easy;
        long timeout;

        dbgmsg( "adding task #%lu [%s]", task->tag, task->url );

        easy = curl_easy_init( );

        if( !task->range && session->isProxyEnabled ) {
            curl_easy_setopt( easy, CURLOPT_PROXY, session->proxy );
            curl_easy_setopt( easy, CURLOPT_PROXYAUTH, CURLAUTH_ANY );
            curl_easy_setopt( easy, CURLOPT_PROXYPORT, session->proxyPort );
            curl_easy_setopt( easy, CURLOPT_PROXYTYPE,
                                      getCurlProxyType( session->proxyType ) );
        }
        if( !task->range && session->isProxyAuthEnabled ) {
            char * str = tr_strdup_printf( "%s:%s", session->proxyUsername,
                                                    session->proxyPassword );
            curl_easy_setopt( easy, CURLOPT_PROXYUSERPWD, str );
            tr_free( str );
        }

        curl_easy_setopt( easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );

        /* set a time limit for announces & scrapes */
        if( strstr( task->url, "scrape" ) != NULL )
            timeout = 20L;
        else if( strstr( task->url, "announce" ) != NULL )
            timeout = 30L;
        else
            timeout = 240L;
        curl_easy_setopt( easy, CURLOPT_TIMEOUT, timeout );
        curl_easy_setopt( easy, CURLOPT_CONNECTTIMEOUT, timeout-5 );
        dbgmsg( "new task's timeout is %ld\n", timeout );

        curl_easy_setopt( easy, CURLOPT_SOCKOPTFUNCTION, sockoptfunction );
        curl_easy_setopt( easy, CURLOPT_SOCKOPTDATA, task );
        curl_easy_setopt( easy, CURLOPT_DNS_CACHE_TIMEOUT, 1800L );
        curl_easy_setopt( easy, CURLOPT_FOLLOWLOCATION, 1L );
        curl_easy_setopt( easy, CURLOPT_AUTOREFERER, 1L );
        curl_easy_setopt( easy, CURLOPT_FORBID_REUSE, 1L );
        curl_easy_setopt( easy, CURLOPT_MAXREDIRS, -1L );
        curl_easy_setopt( easy, CURLOPT_NOSIGNAL, 1L );
        curl_easy_setopt( easy, CURLOPT_PRIVATE, task );
        curl_easy_setopt( easy, CURLOPT_SSL_VERIFYHOST, 0L );
        curl_easy_setopt( easy, CURLOPT_SSL_VERIFYPEER, 0L );
        curl_easy_setopt( easy, CURLOPT_URL, task->url );
        curl_easy_setopt( easy, CURLOPT_USERAGENT,
                                           TR_NAME "/" LONG_VERSION_STRING );
        curl_easy_setopt( easy, CURLOPT_VERBOSE,
                                       getenv( "TR_CURL_VERBOSE" ) != NULL );
        if( web->haveAddr )
            curl_easy_setopt( easy, CURLOPT_INTERFACE, tr_ntop_non_ts( &web->addr ) );
        curl_easy_setopt( easy, CURLOPT_WRITEDATA, task );
        curl_easy_setopt( easy, CURLOPT_WRITEFUNCTION, writeFunc );
        if( task->range )
            curl_easy_setopt( easy, CURLOPT_RANGE, task->range );
        else /* don't set encoding on webseeds; it messes up binary data */
            curl_easy_setopt( easy, CURLOPT_ENCODING, "" );

        {
            const CURLMcode mcode = curl_multi_add_handle( web->multi, easy );
            tr_assert( mcode == CURLM_OK, "curl_multi_add_handle() failed: %d (%s)", mcode, curl_multi_strerror( mcode ) );
            if( mcode == CURLM_OK )
                ++web->still_running;
            else
                tr_err( "%s", curl_multi_strerror( mcode ) );

            tr_multi_perform( web, CURL_SOCKET_TIMEOUT );
        }
    }
}

/***
****
***/

static void
task_free( struct tr_web_task * task )
{
    evbuffer_free( task->response );
    tr_free( task->range );
    tr_free( task->url );
    tr_free( task );
}

static void
task_finish( struct tr_web_task * task, long response_code )
{
    dbgmsg( "finished a web task... response code is %ld", response_code );
    dbgmsg( "===================================================" );

    if( task->done_func != NULL )
        task->done_func( task->session,
                         response_code,
                         EVBUFFER_DATA( task->response ),
                         EVBUFFER_LENGTH( task->response ),
                         task->done_func_user_data );
    task_free( task );
}

static void
remove_finished_tasks( tr_web * g )
{
    CURL * easy;

    do
    {
        CURLMsg * msg;
        int msgs_left;

        easy = NULL;
        while(( msg = curl_multi_info_read( g->multi, &msgs_left ))) {
            if( msg->msg == CURLMSG_DONE ) {
                easy = msg->easy_handle;
                break;
            }
        }

        if( easy )
        {
            long code;
            struct tr_web_task * task;
            curl_easy_getinfo( easy, CURLINFO_PRIVATE, (void*)&task );
            curl_easy_getinfo( easy, CURLINFO_RESPONSE_CODE, &code );
            curl_multi_remove_handle( g->multi, easy );
            curl_easy_cleanup( easy );
            task_finish( task, code );
        }
    }
    while ( easy );

    g->prev_running = g->still_running;
}

static void
stop_timer( tr_web* g )
{
    dbgmsg( "deleting the pending global timer" );
    evtimer_del( &g->timer_event );
}

static void
restart_timer( tr_web * g )
{
    assert( tr_amInEventThread( g->session ) );
    assert( g->session != NULL );
    assert( g->session->events != NULL );

    stop_timer( g );
    dbgmsg( "adding a timeout for %.1f seconds from now", g->timer_msec/1000.0 );
    tr_timerAddMsec( &g->timer_event, g->timer_msec );
}

static void
web_close( tr_web * g )
{
    CURLMcode mcode;

    stop_timer( g );

    mcode = curl_multi_cleanup( g->multi );
    tr_assert( mcode == CURLM_OK, "curl_multi_cleanup() failed: %d (%s)", mcode, curl_multi_strerror( mcode ) );
    if( mcode != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( mcode ) );

    tr_free( g );
}

/* note: this function can free the tr_web if its 'closing' flag is set
   and no tasks remain.  callers must not reference their g pointer
   after calling this function */
static void
tr_multi_perform( tr_web * g, int fd )
{
    int closed = FALSE;
    CURLMcode mcode;

    dbgmsg( "check_run_count: prev_running %d, still_running %d",
            g->prev_running, g->still_running );

    /* invoke libcurl's processing */
    do {
        dbgmsg( "calling curl_multi_socket_action..." );
        mcode = curl_multi_socket_action( g->multi, fd, 0, &g->still_running );
        fd = CURL_SOCKET_TIMEOUT;
        dbgmsg( "done calling curl_multi_socket_action..." );
    } while( mcode == CURLM_CALL_MULTI_SOCKET );
    tr_assert( mcode == CURLM_OK, "curl_multi_perform() failed: %d (%s)", mcode, curl_multi_strerror( mcode ) );
    if( mcode != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( mcode ) );

    remove_finished_tasks( g );

    if( !g->still_running ) {
        stop_timer( g );
        if( g->closing ) {
            web_close( g );
            closed = TRUE;
        }
    }

    if( !closed )
        restart_timer( g );
}

/* libevent says that sock is ready to be processed, so wake up libcurl */
static void
event_cb( int fd, short kind UNUSED, void * g )
{
    tr_multi_perform( g, fd );
}

/* CURLMOPT_SOCKETFUNCTION */
static int
sock_cb( CURL            * easy UNUSED,
         curl_socket_t     fd,
         int               action,
         void            * vweb,
         void            * vevent )
{
    /*static int num_events = 0;*/
    struct tr_web * web = vweb;
    struct event * io_event = vevent;
    dbgmsg( "sock_cb: action is %d, fd is %d, io_event is %p", action, (int)fd, io_event );

    if( action == CURL_POLL_REMOVE )
    {
        if( io_event != NULL )
        {
            event_del( io_event );
            tr_free( io_event );
            curl_multi_assign( web->multi, fd, NULL ); /* does libcurl do this automatically? */
            /*fprintf( stderr, "-1 io_events to %d\n", --num_events );*/
        }
    }
    else
    {
        const short events = EV_PERSIST
                           | (( action & CURL_POLL_IN ) ? EV_READ : 0 )
                           | (( action & CURL_POLL_OUT ) ? EV_WRITE : 0 );

        if( io_event != NULL )
            event_del( io_event );
        else {
            io_event = tr_new0( struct event, 1 );
            curl_multi_assign( web->multi, fd, io_event );
            /*fprintf( stderr, "+1 io_events to %d\n", ++num_events );*/
        }

        dbgmsg( "enabling (libevent %hd, libcurl %d) polling on io_event %p, fd %d", events, action, io_event, fd );
        event_set( io_event, fd, events, event_cb, web );
        event_add( io_event, NULL );
    }

    return 0; /* libcurl doc sez: "The callback MUST return 0." */
}

/* libevent says that timer_msec have passed, so wake up libcurl */
static void
libevent_timer_cb( int fd UNUSED, short what UNUSED, void * g )
{
    dbgmsg( "libevent timer is done" );
    tr_multi_perform( g, CURL_SOCKET_TIMEOUT );
}

/* libcurl documentation: "If 0, it means you should proceed immediately
 * without waiting for anything. If it returns -1, there's no timeout at all
 * set ... (but) you must not wait too long (more than a few seconds perhaps)
 * before you call curl_multi_perform() again."  */
static void
multi_timer_cb( CURLM * multi UNUSED, long timer_msec, void * vg )
{
    tr_web * g = vg;

    if( timer_msec < 1 ) {
        if( timer_msec == 0 ) /* call it immediately */
            libevent_timer_cb( 0, 0, g );
        timer_msec = DEFAULT_TIMER_MSEC;
    }

    g->timer_msec = timer_msec;
    restart_timer( g );
}

/****
*****
****/

void
tr_webRun( tr_session         * session,
           const char         * url,
           const char         * range,
           tr_web_done_func     done_func,
           void               * done_func_user_data )
{
    if( session->web )
    {
        struct tr_web_task * task;
        static unsigned long tag = 0;

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

void
tr_webSetInterface( tr_web * web, const tr_address * addr )
{
    if(( web->haveAddr = ( addr != NULL )))
        web->addr = *addr;
}

tr_web*
tr_webInit( tr_session * session )
{
    tr_web * web;
    static int curlInited = FALSE;

    /* call curl_global_init if we haven't done it already.
     * try to enable ssl for https support; but if that fails,
     * try a plain vanilla init */
    if( curlInited == FALSE ) {
        curlInited = TRUE;
        if( curl_global_init( CURL_GLOBAL_SSL ) )
            curl_global_init( 0 );
    }

    web = tr_new0( struct tr_web, 1 );
    web->session = session;

    web->timer_msec = DEFAULT_TIMER_MSEC; /* overwritten by multi_timer_cb() */
    evtimer_set( &web->timer_event, libevent_timer_cb, web );

    web->multi = curl_multi_init( );
    curl_multi_setopt( web->multi, CURLMOPT_SOCKETDATA, web );
    curl_multi_setopt( web->multi, CURLMOPT_SOCKETFUNCTION, sock_cb );
    curl_multi_setopt( web->multi, CURLMOPT_TIMERDATA, web );
    curl_multi_setopt( web->multi, CURLMOPT_TIMERFUNCTION, multi_timer_cb );

    return web;
}

void
tr_webClose( tr_web ** web_in )
{
    tr_web * web = *web_in;
    *web_in = NULL;
    if( web->still_running < 1 )
        web_close( web );
    else
        web->closing = 1;
}

/*****
******
******
*****/

static struct http_msg {
    long code;
    const char * text;
} http_msg[] = {
    {   0, "No Response" },
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
    { 505, "HTTP Version Not Supported" }
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

/* escapes a string to be URI-legal as per RFC 2396.
   like curl_escape() but can optionally avoid escaping slashes. */
void
tr_http_escape( struct evbuffer  * out,
                const char       * str,
                int                len,
                tr_bool            escape_slashes )
{
    int i;

    if( ( len < 0 ) && ( str != NULL ) )
        len = strlen( str );

    for( i = 0; i < len; i++ ) {
        switch( str[i] ) {
        case ',': case '-': case '.':
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e':
        case 'f': case 'g': case 'h': case 'i': case 'j':
        case 'k': case 'l': case 'm': case 'n': case 'o':
        case 'p': case 'q': case 'r': case 's': case 't':
        case 'u': case 'v': case 'w': case 'x': case 'y': case 'z':
        case 'A': case 'B': case 'C': case 'D': case 'E':
        case 'F': case 'G': case 'H': case 'I': case 'J':
        case 'K': case 'L': case 'M': case 'N': case 'O':
        case 'P': case 'Q': case 'R': case 'S': case 'T':
        case 'U': case 'V': case 'W': case 'X': case 'Y': case 'Z':
            evbuffer_add( out, &str[i], 1 );
            break;
        case '/':
            if(!escape_slashes) {
                evbuffer_add( out, &str[i], 1 );
                break;
            }
            /* Fall through. */
        default:
            evbuffer_add_printf( out, "%%%02X", (unsigned)(str[i]&0xFF) );
            break;
        }
    }
}

char*
tr_http_unescape( const char * str, int len )
{
    char * tmp = curl_unescape( str, len );
    char * ret = tr_strdup( tmp );
    curl_free( tmp );
    return ret;
}
