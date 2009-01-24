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
#include <stdlib.h> /* bsearch */

#include <event.h>

#define CURL_DISABLE_TYPECHECK /* otherwise -Wunreachable-code goes insane */
#include <curl/curl.h>

#include "transmission.h"
#include "session.h"
#include "list.h"
#include "net.h" /* socklen_t */
#include "trevent.h"
#include "utils.h"
#include "web.h"

enum
{
    /* arbitrary number */
    MAX_CONCURRENT_TASKS = 100,

    /* arbitrary number */
    DEFAULT_TIMER_MSEC = 2500
};

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
    long timer_ms;
    CURLM * multi;
    tr_session * session;
#if 0
    tr_list * easy_queue;
#endif
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
writeFunc( void * ptr, size_t size, size_t nmemb, void * task )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( ((struct tr_web_task*)task)->response, ptr, byteCount );
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
addTask( void * vtask )
{
    struct tr_web_task * task = vtask;
    const tr_session * session = task->session;

    if( session && session->web )
    {
        struct tr_web * web = session->web;
        CURL * easy;

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

        curl_easy_setopt( easy, CURLOPT_DNS_CACHE_TIMEOUT, 360L );
        curl_easy_setopt( easy, CURLOPT_CONNECTTIMEOUT, 60L );
        curl_easy_setopt( easy, CURLOPT_FOLLOWLOCATION, 1L );
        curl_easy_setopt( easy, CURLOPT_FORBID_REUSE, 1L );
        curl_easy_setopt( easy, CURLOPT_MAXREDIRS, 16L );
        curl_easy_setopt( easy, CURLOPT_NOSIGNAL, 1L );
        curl_easy_setopt( easy, CURLOPT_PRIVATE, task );
        curl_easy_setopt( easy, CURLOPT_SSL_VERIFYHOST, 0L );
        curl_easy_setopt( easy, CURLOPT_SSL_VERIFYPEER, 0L );
        curl_easy_setopt( easy, CURLOPT_URL, task->url );
        curl_easy_setopt( easy, CURLOPT_USERAGENT,
                                           TR_NAME "/" LONG_VERSION_STRING );
        curl_easy_setopt( easy, CURLOPT_VERBOSE,
                                       getenv( "TR_CURL_VERBOSE" ) != NULL );
        curl_easy_setopt( easy, CURLOPT_WRITEDATA, task );
        curl_easy_setopt( easy, CURLOPT_WRITEFUNCTION, writeFunc );
        if( task->range )
            curl_easy_setopt( easy, CURLOPT_RANGE, task->range );
        else /* don't set encoding on webseeds; it messes up binary data */
            curl_easy_setopt( easy, CURLOPT_ENCODING, "" );

#if 0
        if( web->still_running >= MAX_CONCURRENT_TASKS )
        {
            tr_list_append( &web->easy_queue, easy );
            dbgmsg( " >> enqueueing a task ... size is now %d",
                                           tr_list_size( web->easy_queue ) );
        }
        else
#endif
        {
            const CURLMcode rc = curl_multi_add_handle( web->multi, easy );
            if( rc == CURLM_OK )
                ++web->still_running;
            else
                tr_err( "%s", curl_multi_strerror( rc ) );
        }
    }
}

/***
****
***/

struct tr_web_sockinfo
{
    struct event ev;
    int evset;
};

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

        if( easy ) {
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
    if( evtimer_pending( &g->timer_event, NULL ) )
    {
        dbgmsg( "deleting the pending global timer" );
        evtimer_del( &g->timer_event );
    }
}

static void
restart_timer( tr_web * g )
{
    struct timeval interval;
    stop_timer( g );
    dbgmsg( "adding a timeout for %ld seconds from now", g->timer_ms/1000L );
    tr_timevalMsec( g->timer_ms, &interval );
    evtimer_add( &g->timer_event, &interval );
}

#if 0
static void
add_tasks_from_queue( tr_web * g )
{
    while( ( g->still_running < MAX_CONCURRENT_TASKS ) 
        && ( tr_list_size( g->easy_queue ) > 0 ) )
    {
        CURL * easy = tr_list_pop_front( &g->easy_queue );
        if( easy )
        {
            const CURLMcode rc = curl_multi_add_handle( g->multi, easy );
            if( rc != CURLM_OK )
                tr_err( "%s", curl_multi_strerror( rc ) );
            else {
                dbgmsg( "pumped the task queue, %d remain",
                        tr_list_size( g->easy_queue ) );
                ++g->still_running;
            }
        }
    }
}
#endif

static void
web_close( tr_web * g )
{
    stop_timer( g );
    curl_multi_cleanup( g->multi );
    tr_free( g );
}

/* note: this function can free the tr_web if its 'closing' flag is set
   and no tasks remain.  callers must not reference their g pointer
   after calling this function */
static void
tr_multi_socket_action( tr_web * g, int fd )
{
    int closed = FALSE;
    CURLMcode rc;

    dbgmsg( "check_run_count: prev_running %d, still_running %d",
            g->prev_running, g->still_running );

    /* invoke libcurl's processing */
    do {
        rc = curl_multi_socket_action( g->multi, fd, 0, &g->still_running );
        dbgmsg( "event_cb(): fd %d, still_running is %d",
                fd, g->still_running );
    } while( rc == CURLM_CALL_MULTI_PERFORM );
    if( rc != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( rc ) );

    remove_finished_tasks( g );

#if 0
    add_tasks_from_queue( g );
#endif

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
    tr_multi_socket_action( g, fd );
}

/* libevent says that timer_ms have passed, so wake up libcurl */
static void
timer_cb( int socket UNUSED, short action UNUSED, void * g )
{
    dbgmsg( "libevent timer is done" );
    tr_multi_socket_action( g, CURL_SOCKET_TIMEOUT );
}

static void
remsock( struct tr_web_sockinfo * f )
{
    if( f ) {
        dbgmsg( "deleting sockinfo %p", f );
        if( f->evset )
            event_del( &f->ev );
        tr_free( f );
    }
}

static void
setsock( curl_socket_t            sockfd,
         int                      action,
         struct tr_web          * g,
         struct tr_web_sockinfo * f )
{
    const int kind = EV_PERSIST
                   | (( action & CURL_POLL_IN ) ? EV_READ : 0 )
                   | (( action & CURL_POLL_OUT ) ? EV_WRITE : 0 );
    dbgmsg( "setsock: fd is %d, curl action is %d, libevent action is %d",
            sockfd, action, kind );
    if( f->evset )
        event_del( &f->ev );
    event_set( &f->ev, sockfd, kind, event_cb, g );
    f->evset = 1;
    event_add( &f->ev, NULL );
}

static void
addsock( curl_socket_t    sockfd,
         int              action,
         struct tr_web  * g )
{
    struct tr_web_sockinfo * f = tr_new0( struct tr_web_sockinfo, 1 );
    dbgmsg( "creating a sockinfo %p for fd %d", f, sockfd );
    setsock( sockfd, action, g, f );
    curl_multi_assign( g->multi, sockfd, f );
}

/* CURLMOPT_SOCKETFUNCTION */
static int
sock_cb( CURL            * e UNUSED,
         curl_socket_t     s,
         int               what,
         void            * vg,
         void            * vf)
{
    struct tr_web * g = vg;
    struct tr_web_sockinfo * f = vf;
    dbgmsg( "sock_cb: what is %d, sockinfo is %p", what, f );

    if( what == CURL_POLL_REMOVE )
        remsock( f );
    else if( !f )
        addsock( s, what, g );
    else
        setsock( s, what, g, f );

    return 0;
}


/* libcurl documentation: "If 0, it means you should proceed immediately
 * without waiting for anything. If it returns -1, there's no timeout at all
 * set ... (but) you must not wait too long (more than a few seconds perhaps)
 * before you call curl_multi_perform() again."  */
static void
multi_timer_cb( CURLM *multi UNUSED, long timer_ms, void * vg )
{
    tr_web * g = vg;

    if( timer_ms < 1 ) {
        if( timer_ms == 0 ) /* call it immediately */
            timer_cb( 0, 0, g );
        timer_ms = DEFAULT_TIMER_MSEC;
    }

    g->timer_ms = timer_ms;
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
    web->multi = curl_multi_init( );
    web->session = session;
    web->timer_ms = DEFAULT_TIMER_MSEC; /* overwritten by multi_timer_cb() */

    evtimer_set( &web->timer_event, timer_cb, web );
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
