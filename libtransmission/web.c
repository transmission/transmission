/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
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

#include <curl/curl.h>
#include <event.h>
#include <evdns.h>

#include "transmission.h"
#include "list.h"
#include "net.h"
#include "session.h"
#include "trevent.h"
#include "utils.h"
#include "version.h"
#include "web.h"

enum
{
    TR_MEMORY_TRASH = 0xCC,

    DEFAULT_TIMER_MSEC = 1500 /* arbitrary */
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

/***
****
***/

struct tr_web
{
    tr_bool closing;
    tr_bool haveAddr;
    int taskCount;
    long timer_msec;
    CURLM * multi;
    tr_session * session;
    tr_address addr;
    tr_list * dns_cache;
    struct event timer_event;
};

struct dns_cache_item;
static void dns_cache_item_free( struct dns_cache_item * );

static void
web_free( tr_web * g )
{
    curl_multi_cleanup( g->multi );
    evtimer_del( &g->timer_event );
    tr_list_free( &g->dns_cache, (TrListForeachFunc)dns_cache_item_free );
    memset( g, TR_MEMORY_TRASH, sizeof( struct tr_web ) );
    tr_free( g );
}

/***
****
***/

struct tr_web_task
{
    unsigned long tag;
    struct evbuffer * response;
    char * url;
    char * host;
    const char * resolved_host;
    char * range;
    tr_session * session;
    tr_web_done_func * done_func;
    void * done_func_user_data;
    struct event timer_event;
    CURL * easy;
    CURLM * multi;
};

static void
task_free( struct tr_web_task * task )
{
    evtimer_del( &task->timer_event );
    evbuffer_free( task->response );
    tr_free( task->host );
    tr_free( task->range );
    tr_free( task->url );
    memset( task, TR_MEMORY_TRASH, sizeof( struct tr_web_task ) );
    tr_free( task );
}

/***
****
***/

struct dns_cache_item
{
    char * host;
    char * resolved_host;
    time_t expiration;
};

static void
dns_cache_item_free( struct dns_cache_item * item )
{
    tr_free( item->host );
    tr_free( item->resolved_host );
    tr_free( item );
}

static const char *
dns_get_cached_host( struct tr_web_task * task, const char * host )
{
    tr_list * l;
    tr_web * g = task->session->web;
    struct dns_cache_item * item = NULL;

    if( g != NULL )
    {
        /* do we have it cached? */
        for( l=g->dns_cache; l!=NULL; l=l->next ) {
            struct dns_cache_item * tmp = l->data;
            if( !strcmp( host, tmp->host ) ) {
                item = tmp;
                break;
            }
        }

        /* has the ttl expired? */
        if( ( item != NULL ) && ( item->expiration <= tr_time( ) ) ) {
            tr_list_remove_data( &g->dns_cache, item );
            dns_cache_item_free( item );
            item = NULL;
        }
    }

    if( item != NULL )
        dbgmsg( "found cached dns entry for \"%s\": %s", host, item->resolved_host );

    return item ? item->resolved_host : NULL;
}

static const char*
dns_set_cached_host( struct tr_web_task * task, const char * host, const char * resolved, int ttl )
{
    char * ret = NULL;
    tr_web * g;

    assert( task != NULL );
    assert( host != NULL );
    assert( resolved != NULL );
    assert( ttl >= 0 );

    g = task->session->web;
    if( g != NULL )
    {
        struct dns_cache_item * item = tr_new( struct dns_cache_item, 1 );
        item->host = tr_strdup( host );
        item->resolved_host = tr_strdup( resolved );
        item->expiration = tr_time( ) + ttl;
        tr_list_append( &g->dns_cache, item );
        ret = item->resolved_host;
        dbgmsg( "adding dns cache entry for \"%s\": %s", host, resolved );
    }
    return ret;
}

/***
****
***/

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
sockoptfunction( void * vtask, curl_socket_t fd, curlsocktype purpose UNUSED )
{
    struct tr_web_task * task = vtask;
    const tr_bool isScrape = strstr( task->url, "scrape" ) != NULL;
    const tr_bool isAnnounce = strstr( task->url, "announce" ) != NULL;

    /* announce and scrape requests have tiny payloads. */
    if( isScrape || isAnnounce )
    {
        const int sndbuf = 1024;
        const int rcvbuf = isScrape ? 2048 : 3072;
        setsockopt( fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf) );
        setsockopt( fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf) );
    }

    /* return nonzero if this function encountered an error */
    return 0;
}

static int
getCurlProxyType( tr_proxy_type t )
{
    if( t == TR_PROXY_SOCKS4 ) return CURLPROXY_SOCKS4;
    if( t == TR_PROXY_SOCKS5 ) return CURLPROXY_SOCKS5;
    return CURLPROXY_HTTP;
}

static int
getTimeoutFromURL( const char * url )
{
    if( strstr( url, "scrape" ) != NULL ) return 20;
    if( strstr( url, "announce" ) != NULL ) return 30;
    return 240;
}

static void task_timeout_cb( int fd UNUSED, short what UNUSED, void * task );

static void
addTask( void * vtask )
{
    struct tr_web_task * task = vtask;
    const tr_session * session = task->session;

    if( session && session->web )
    {
        CURL * e = curl_easy_init( );
        struct tr_web * web = session->web;
        const int timeout = getTimeoutFromURL( task->url );
        const long verbose = getenv( "TR_CURL_VERBOSE" ) != NULL;
        const char * user_agent = TR_NAME "/" LONG_VERSION_STRING;
        char * url = NULL;

        /* If we've got a resolved host, insert it into the URL: replace
         * "http://www.craptrackular.org/announce?key=val&key2=..." with
         * "http://127.0.0.1/announce?key=val&key2=..."
         * so that curl's DNS won't block */
        if( task->resolved_host != NULL )
        {
            struct evbuffer * buf = evbuffer_new( );
            char * pch = strstr( task->url, task->host );
            char * tail = pch + strlen( task->host );
            evbuffer_add( buf, task->url, pch - task->url );
            evbuffer_add_printf( buf, "%s", task->resolved_host );
            evbuffer_add_printf( buf, "%s", tail );
            url = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
            dbgmsg( "old url: \"%s\" -- new url: \"%s\"", task->url, url );
            evbuffer_free( buf );
        }

        dbgmsg( "adding task #%lu [%s]", task->tag, url ? url : task->url );

        if( !task->range && session->isProxyEnabled ) {
            curl_easy_setopt( e, CURLOPT_PROXY, session->proxy );
            curl_easy_setopt( e, CURLOPT_PROXYAUTH, CURLAUTH_ANY );
            curl_easy_setopt( e, CURLOPT_PROXYPORT, session->proxyPort );
            curl_easy_setopt( e, CURLOPT_PROXYTYPE,
                                      getCurlProxyType( session->proxyType ) );
        }
        if( !task->range && session->isProxyAuthEnabled ) {
            char * str = tr_strdup_printf( "%s:%s", session->proxyUsername,
                                                    session->proxyPassword );
            curl_easy_setopt( e, CURLOPT_PROXYUSERPWD, str );
            tr_free( str );
        }

        task->easy = e;
        task->multi = web->multi;

        /* use our own timeout instead of CURLOPT_TIMEOUT because the latter
         * doesn't play nicely with curl_multi.  See curl bug #2501457 */
        evtimer_set( &task->timer_event, task_timeout_cb, task );
        tr_timerAdd( &task->timer_event, timeout, 0 );

        curl_easy_setopt( e, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4 );
        curl_easy_setopt( e, CURLOPT_SOCKOPTFUNCTION, sockoptfunction );
        curl_easy_setopt( e, CURLOPT_SOCKOPTDATA, task );
        curl_easy_setopt( e, CURLOPT_WRITEDATA, task );
        curl_easy_setopt( e, CURLOPT_WRITEFUNCTION, writeFunc );
        curl_easy_setopt( e, CURLOPT_DNS_CACHE_TIMEOUT, 1800L );
        curl_easy_setopt( e, CURLOPT_FOLLOWLOCATION, 1L );
        curl_easy_setopt( e, CURLOPT_AUTOREFERER, 1L );
        curl_easy_setopt( e, CURLOPT_FORBID_REUSE, 1L );
        curl_easy_setopt( e, CURLOPT_MAXREDIRS, -1L );
        curl_easy_setopt( e, CURLOPT_NOSIGNAL, 1L );
        curl_easy_setopt( e, CURLOPT_PRIVATE, task );
        curl_easy_setopt( e, CURLOPT_SSL_VERIFYHOST, 0L );
        curl_easy_setopt( e, CURLOPT_SSL_VERIFYPEER, 0L );
        curl_easy_setopt( e, CURLOPT_URL, url ? url : task->url );
        curl_easy_setopt( e, CURLOPT_USERAGENT, user_agent );
        curl_easy_setopt( e, CURLOPT_VERBOSE, verbose );
        if( web->haveAddr )
            curl_easy_setopt( e, CURLOPT_INTERFACE, tr_ntop_non_ts( &web->addr ) );
        if( task->range )
            curl_easy_setopt( e, CURLOPT_RANGE, task->range );

        if( curl_multi_add_handle( web->multi, e ) == CURLM_OK )
            ++web->taskCount;

        tr_free( url );
    }
}

static void
dns_ipv6_done_cb( int err, char type, int count, int ttl, void * addresses, void * vtask )
{
    struct tr_web_task * task = vtask;

    if( !err && ( task->host != NULL ) && ( count > 0 ) && ( ttl >= 0 ) && ( type == DNS_IPv6_AAAA ) )
    {
        int i;
        char buf[INET6_ADDRSTRLEN+1];
        struct in6_addr *in6_addrs = addresses;

        for( i=0; i<count; ++i ) {
            const char * b = inet_ntop(AF_INET6, &in6_addrs[i], buf,sizeof(buf));
            if( b != NULL ) {
                /* FIXME: is there a better way to tell which one to use if count > 1? */
                task->resolved_host = dns_set_cached_host( task, task->host, b, ttl );
                break;
            }
        }
    }

    addTask( task );
}

static void
dns_ipv4_done_cb( int err, char type, int count, int ttl, void * addresses, void * vtask )
{
    struct tr_web_task * task = vtask;

    if( !err && ( task->host != NULL ) && ( count > 0 ) && ( ttl >= 0 ) && ( type == DNS_IPv4_A ) )
    {
        struct in_addr * in_addrs = addresses;
        const char * resolved = inet_ntoa( in_addrs[0] );
        task->resolved_host = dns_set_cached_host( task, task->host, resolved, ttl );
        /* FIXME: if count > 1, is there a way to decide which is best to use? */
    }

    if( task->resolved_host || evdns_resolve_ipv6( task->host, 0, dns_ipv6_done_cb, task ) )
        dns_ipv6_done_cb( DNS_ERR_UNKNOWN, DNS_IPv6_AAAA, 0, 0, NULL, task );
}

static void
doDNS( void * vtask )
{
    char * host = NULL;
    struct tr_web_task * task = vtask;

    assert( task->resolved_host == NULL );

    if( !tr_httpParseURL( task->url, -1, &host, NULL, NULL ) ) {
        task->host = host;
        task->resolved_host = dns_get_cached_host( task, host );
    }

    if( task->resolved_host || evdns_resolve_ipv4( host, 0, dns_ipv4_done_cb, task ) )
        dns_ipv4_done_cb( DNS_ERR_UNKNOWN, DNS_IPv4_A, 0, 0, NULL, task );
}

/***
****
***/

static void
task_finish( struct tr_web_task * task, long response_code )
{
    dbgmsg( "finished web task %lu; got %ld", task->tag, response_code );

    if( task->done_func != NULL )
        task->done_func( task->session,
                         response_code,
                         EVBUFFER_DATA( task->response ),
                         EVBUFFER_LENGTH( task->response ),
                         task->done_func_user_data );
    task_free( task );
}

static void
remove_task( struct tr_web_task * task )
{
    long code;

    curl_easy_getinfo( task->easy, CURLINFO_RESPONSE_CODE, &code );
    curl_multi_remove_handle( task->multi, task->easy );
    curl_easy_cleanup( task->easy );
    task_finish( task, code );
}

static void
task_timeout_cb( int fd UNUSED, short what UNUSED, void * task )
{
    remove_task( task );
}

static void
remove_finished_tasks( tr_web * g )
{
    CURLMsg * msg;
    int msgs_left;

    while(( msg = curl_multi_info_read( g->multi, &msgs_left ))) {
        if(( msg->msg == CURLMSG_DONE ) && ( msg->easy_handle != NULL )) {
            struct tr_web_task * task;
            CURL * e = msg->easy_handle;
            curl_easy_getinfo( e, CURLINFO_PRIVATE, (void*)&task );
            assert( e == task->easy );
            remove_task( task );
        }
    }
}

static void
restart_timer( tr_web * g )
{
    dbgmsg( "adding a timeout for %.1f seconds from now", g->timer_msec/1000.0 );
    evtimer_del( &g->timer_event );
    tr_timerAddMsec( &g->timer_event, g->timer_msec );
}

static void
tr_multi_perform( tr_web * g, int fd, int curl_what )
{
    CURLMcode m;

    dbgmsg( "check_run_count: %d taskCount", g->taskCount );

    /* invoke libcurl's processing */
    do
        m = curl_multi_socket_action( g->multi, fd, curl_what, &g->taskCount );
    while( m == CURLM_CALL_MULTI_SOCKET );

    remove_finished_tasks( g );

    if( g->closing && !g->taskCount )
        web_free( g );
    else
        restart_timer( g );
}

/* libevent says that sock is ready to be processed, so wake up libcurl */
static void
event_cb( int fd, short ev_what, void * g )
{
    int curl_what = 0;
    if( ev_what & EV_READ ) curl_what |= CURL_POLL_IN;
    if( ev_what & EV_WRITE ) curl_what |= CURL_POLL_OUT;
    tr_multi_perform( g, fd, curl_what );
}

/* CURLMOPT_SOCKETFUNCTION */
static int
sock_cb( CURL * e UNUSED, curl_socket_t fd, int curl_what,
         void * vweb, void * vevent )
{
    /*static int num_events = 0;*/
    struct tr_web * web = vweb;
    struct event * io_event = vevent;
    dbgmsg( "sock_cb: curl_what %d, fd %d, io_event %p",
            curl_what, (int)fd, io_event );

    if( io_event != NULL )
        event_del( io_event );

    if( curl_what & ( CURL_POLL_IN | CURL_POLL_OUT ) )
    {
        const short ev_what = EV_PERSIST
                           | (( curl_what & CURL_POLL_IN ) ? EV_READ : 0 )
                           | (( curl_what & CURL_POLL_OUT ) ? EV_WRITE : 0 );

        if( io_event == NULL ) {
            io_event = tr_new0( struct event, 1 );
            curl_multi_assign( web->multi, fd, io_event );
            /*fprintf( stderr, "+1 io_events to %d\n", ++num_events );*/
        }

        dbgmsg( "enabling (libevent %hd, libcurl %d) on io_event %p, fd %d",
                ev_what, curl_what, io_event, fd );
        event_set( io_event, fd, ev_what, event_cb, web );
        assert( io_event->ev_base != NULL );
        event_add( io_event, NULL );
    }

    if( ( io_event != NULL ) && ( curl_what & CURL_POLL_REMOVE ) )
    {
        memset( io_event, TR_MEMORY_TRASH, sizeof( struct event ) );
        tr_free( io_event );
        /*fprintf( stderr, "-1 io_events to %d\n", --num_events );*/
    }

    return 0; /* libcurl documentation: "The callback MUST return 0." */
}

/* libevent says that timer_msec have passed, so wake up libcurl */
static void
libevent_timer_cb( int fd UNUSED, short what UNUSED, void * g )
{
    dbgmsg( "libevent timer is done" );
    tr_multi_perform( g, CURL_SOCKET_TIMEOUT, 0 );
}

/* libcurl documentation: "If 0, it means you should proceed immediately
 * without waiting for anything. If it returns -1, there's no timeout at all
 * set ... (but) you must not wait too long (more than a few seconds perhaps)
 * before you call curl_multi_perform() again."  */
static void
multi_timer_cb( CURLM * multi UNUSED, long timer_msec, void * vg )
{
    tr_web * g = vg;

    g->timer_msec = timer_msec > 0 ? timer_msec : DEFAULT_TIMER_MSEC;

    if( timer_msec < 1 )
        tr_multi_perform( g, CURL_SOCKET_TIMEOUT, 0 );
    else
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
    if( session->web != NULL )
    {
        static unsigned long tag = 0;
        struct tr_web_task * task = tr_new0( struct tr_web_task, 1 );
        task->session = session;
        task->url = tr_strdup( url );
        task->range = tr_strdup( range );
        task->done_func = done_func;
        task->done_func_user_data = done_func_user_data;
        task->tag = ++tag;
        task->response = evbuffer_new( );
        tr_runInEventThread( session, doDNS, task );
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

    /* try to enable ssl for https support; but if that fails,
     * try a plain vanilla init */
    if( curl_global_init( CURL_GLOBAL_SSL ) )
        curl_global_init( 0 );

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
    if( web->taskCount < 1 )
        web_free( web );
    else
        web->closing = 1;
}

/*****
******
******
*****/

const char *
tr_webGetResponseStr( long code )
{
    switch( code )
    {
        case   0: return "No Response";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 306: return "(Unused)";
        case 307: return "Temporary Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Request Entity Too Large";
        case 414: return "Request-URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Requested Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        default:  return "Unknown Error";
    }
}

void
tr_http_escape( struct evbuffer  * out,
                const char * str, int len, tr_bool escape_slashes )
{
    int i;

    if( ( len < 0 ) && ( str != NULL ) )
        len = strlen( str );

    for( i = 0; i < len; i++ ) {
        if( str[i] == ',' || str[i] == '-' || str[i] == '.'
            || ( '0' <= str[i] && str[i] <= '9' )
            || ( 'A' <= str[i] && str[i] <= 'Z' )
            || ( 'a' <= str[i] && str[i] <= 'z' )
            || ( str[i] == '/' && !escape_slashes ) )
            evbuffer_add( out, &str[i], 1 );
        else
            evbuffer_add_printf( out, "%%%02X", (unsigned)(str[i]&0xFF) );
    }
}

char *
tr_http_unescape( const char * str, int len )
{
    char * tmp = curl_unescape( str, len );
    char * ret = tr_strdup( tmp );
    curl_free( tmp );
    return ret;
}
