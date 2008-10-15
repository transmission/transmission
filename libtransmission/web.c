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

#if LIBCURL_VERSION_NUM < 0x071003
#define curl_multi_socket_action(m,fd,mask,i) curl_multi_socket((m),(fd),(i))
#endif

#define dbgmsg( ... )  tr_deepLog( __FILE__, __LINE__, "web", __VA_ARGS__ )

struct tr_web
{
    unsigned int dying : 1;
    int prev_running;
    int still_running;
    CURLM * multi;
    tr_session * session;
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
        CURLMcode rc;

        dbgmsg( "adding task #%lu [%s]", task->tag, task->url );

        ch = curl_easy_init( );

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
        curl_easy_setopt( ch, CURLOPT_VERBOSE, getenv( "TR_CURL_VERBOSE" ) != NULL );
        if( task->range )
            curl_easy_setopt( ch, CURLOPT_RANGE, task->range );
        else /* don't set encoding if range is sent; it messes up binary data */
            curl_easy_setopt( ch, CURLOPT_ENCODING, "" );

        rc = curl_multi_add_handle( web->multi, ch );
        if( rc != CURLM_OK )
            tr_err( "%s", curl_multi_strerror( rc ) );
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
finish_task( struct tr_web_task * task, long response_code )
{
    dbgmsg( "finished a web task... response code is %ld", response_code );
    dbgmsg( "===================================================" );
    task->done_func( task->session,
                     response_code,
                     EVBUFFER_DATA( task->response ),
                     EVBUFFER_LENGTH( task->response ),
                     task->done_func_user_data );
    evbuffer_free( task->response );
    tr_free( task->range );
    tr_free( task->url );
    tr_free( task );
}

static void
webDestroy( tr_web * web )
{
    timeout_del( &web->timer_event );
    curl_multi_cleanup( web->multi );
    tr_free( web );
}

/* note: this function can free the tr_web if it's been flagged for deletion
   and there are no more tasks remaining.  so, callers need to make sure to
   not reference their g pointer after calling this function */
static void
check_run_count( tr_web * g )
{
    dbgmsg( "check_run_count: prev_running %d, still_running %d",
            g->prev_running, g->still_running );

    if( g->prev_running > g->still_running )
    {
        CURLMsg * msg;
        int msgs_left;
        CURL * easy;
        CURLcode res;

        do{
            easy = NULL;
            while(( msg = curl_multi_info_read( g->multi, &msgs_left ))) {
                if( msg->msg == CURLMSG_DONE ) {
                    easy = msg->easy_handle;
                    res = msg->data.result;
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
                finish_task( task, code );
            }
        } while ( easy );
    }

    g->prev_running = g->still_running;

    if( g->still_running <= 0 ) {
        if( evtimer_pending( &g->timer_event, NULL ) ) {
            dbgmsg( "deleting the pending global timer" );
            evtimer_del( &g->timer_event );
        }
    }

    if( g->dying && ( g->still_running < 1 ) ) {
        dbgmsg( "destroying the web global now that all the tasks are done" );
        webDestroy( g );
    }
}

/* libevent says that sock is ready to be processed, so wake up libcurl */
static void
event_cb( int fd, short kind, void * vg )
{
    tr_web * g = vg;
    CURLMcode rc;
    int error = 0;
    int mask;
    socklen_t errsz = sizeof( error );

    getsockopt( fd, SOL_SOCKET, SO_ERROR, &error, &errsz );
    if( error )
        mask = CURL_CSELECT_ERR;
    else {
        mask = 0;
        if( kind & EV_READ  ) mask |= CURL_CSELECT_IN;
        if( kind & EV_WRITE ) mask |= CURL_CSELECT_OUT;
    }

    do {
        dbgmsg( stderr, "event_cb calling socket_action fd %d, mask %d", fd, mask );
        rc = curl_multi_socket_action( g->multi, fd, mask, &g->still_running );
    } while( rc == CURLM_CALL_MULTI_PERFORM );
    if( rc != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( rc ) );

    check_run_count( g );
}

/* libevent says that timeout_ms have passed, so wake up libcurl */
static void
timer_cb( int socket UNUSED, short action UNUSED, void * vg )
{
    tr_web * g = vg;
    CURLMcode rc;
    dbgmsg( "libevent timer is done" );

    do {
        dbgmsg( "timer_cb calling CURL_SOCKET_TIMEOUT" );
        rc = curl_multi_socket_action( g->multi, 0, CURL_SOCKET_TIMEOUT,
                                       &g->still_running );
    } while( rc == CURLM_CALL_MULTI_PERFORM );

    if( rc != CURLM_OK )
        tr_err( "%s", curl_multi_strerror( rc ) );

    check_run_count( g );
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
    const int kind = (action & CURL_POLL_IN ? EV_READ : 0)
                   | (action & CURL_POLL_OUT ? EV_WRITE : 0);
    dbgmsg( "setsock: fd is %d, curl action is %d, libevent action is %d", sockfd, action, kind );
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


/* libcurl wants us to tell it when timeout_ms have passed */
static void
multi_timer_cb( CURLM *multi UNUSED, long timeout_ms, void * vweb )
{
    tr_web * web = vweb;
    struct timeval timeout;
    dbgmsg( "adding a timeout for %ld seconds from now", timeout_ms/1000l );
    tr_timevalMsec( timeout_ms, &timeout );
    timeout_add( &web->timer_event, &timeout );
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

    timeout_set( &web->timer_event, timer_cb, web );
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
        webDestroy( web );
    else
        web->dying = 1;
}

/*****
******
******
*****/

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
