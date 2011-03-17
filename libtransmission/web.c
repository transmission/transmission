/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <string.h> /* strlen(), strstr() */
#include <stdlib.h> /* getenv() */

#ifdef WIN32
  #include <ws2tcpip.h>
#else
  #include <sys/select.h>
#endif

#include <curl/curl.h>

#include <event2/buffer.h>

#include "transmission.h"
#include "list.h"
#include "net.h" /* tr_address */
#include "platform.h" /* mutex */
#include "session.h"
#include "trevent.h" /* tr_runInEventThread() */
#include "utils.h"
#include "version.h" /* User-Agent */
#include "web.h"

#if LIBCURL_VERSION_NUM >= 0x070F06 /* CURLOPT_SOCKOPT* was added in 7.15.6 */
 #define USE_LIBCURL_SOCKOPT
#endif

enum
{
    THREADFUNC_MAX_SLEEP_MSEC = 1000,
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
    int close_mode;
    tr_list * tasks;
    tr_lock * taskLock;
};


/***
****
***/

struct tr_web_task
{
    long code;
    long timeout_secs;
    tr_bool did_connect;
    tr_bool did_timeout;
    struct evbuffer * response;
    struct evbuffer * freebuf;
    char * url;
    char * range;
    char * cookies;
    tr_session * session;
    tr_web_done_func * done_func;
    void * done_func_user_data;
};

static void
task_free( struct tr_web_task * task )
{
    if( task->freebuf )
        evbuffer_free( task->freebuf );
    tr_free( task->cookies );
    tr_free( task->range );
    tr_free( task->url );
    tr_free( task );
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

#ifdef USE_LIBCURL_SOCKOPT
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
#endif

static long
getTimeoutFromURL( const struct tr_web_task * task )
{
    long timeout;
    const tr_session * session = task->session;

    if( !session || session->isClosed ) timeout = 20L;
    else if( strstr( task->url, "scrape" ) != NULL ) timeout = 30L;
    else if( strstr( task->url, "announce" ) != NULL ) timeout = 90L;
    else timeout = 240L;

    return timeout;
}

static CURL *
createEasy( tr_session * s, struct tr_web_task * task )
{
    const tr_address * addr;
    tr_bool is_default_value;
    CURL * e = curl_easy_init( );
    const long verbose = getenv( "TR_CURL_VERBOSE" ) != NULL;
    char * cookie_filename = tr_buildPath( s->configDir, "cookies.txt", NULL );

    task->timeout_secs = getTimeoutFromURL( task );

    curl_easy_setopt( e, CURLOPT_AUTOREFERER, 1L );
    curl_easy_setopt( e, CURLOPT_COOKIEFILE, cookie_filename );
    curl_easy_setopt( e, CURLOPT_ENCODING, "gzip;q=1.0, deflate, identity" );
    curl_easy_setopt( e, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( e, CURLOPT_MAXREDIRS, -1L );
    curl_easy_setopt( e, CURLOPT_NOSIGNAL, 1L );
    curl_easy_setopt( e, CURLOPT_PRIVATE, task );
#ifdef USE_LIBCURL_SOCKOPT
    curl_easy_setopt( e, CURLOPT_SOCKOPTFUNCTION, sockoptfunction );
    curl_easy_setopt( e, CURLOPT_SOCKOPTDATA, task );
#endif
    curl_easy_setopt( e, CURLOPT_SSL_VERIFYHOST, 0L );
    curl_easy_setopt( e, CURLOPT_SSL_VERIFYPEER, 0L );
    curl_easy_setopt( e, CURLOPT_TIMEOUT, task->timeout_secs );
    curl_easy_setopt( e, CURLOPT_URL, task->url );
    curl_easy_setopt( e, CURLOPT_USERAGENT, TR_NAME "/" SHORT_VERSION_STRING );
    curl_easy_setopt( e, CURLOPT_VERBOSE, verbose );
    curl_easy_setopt( e, CURLOPT_WRITEDATA, task );
    curl_easy_setopt( e, CURLOPT_WRITEFUNCTION, writeFunc );

    if((( addr = tr_sessionGetPublicAddress( s, TR_AF_INET, &is_default_value ))) && !is_default_value )
        curl_easy_setopt( e, CURLOPT_INTERFACE, tr_ntop_non_ts( addr ) );
    else if ((( addr = tr_sessionGetPublicAddress( s, TR_AF_INET6, &is_default_value ))) && !is_default_value )
        curl_easy_setopt( e, CURLOPT_INTERFACE, tr_ntop_non_ts( addr ) );

    if( task->cookies != NULL )
        curl_easy_setopt( e, CURLOPT_COOKIE, task->cookies );

    if( task->range )
        curl_easy_setopt( e, CURLOPT_RANGE, task->range );

    if( s->curl_easy_config_func != NULL )
        s->curl_easy_config_func( s, e, task->url );

    tr_free( cookie_filename );
    return e;
}

/***
****
***/

static void
task_finish_func( void * vtask )
{
    struct tr_web_task * task = vtask;
    dbgmsg( "finished web task %p; got %ld", task, task->code );

    if( task->done_func != NULL )
        task->done_func( task->session,
                         task->did_connect,
                         task->did_timeout,
                         task->code,
                         evbuffer_pullup( task->response, -1 ),
                         evbuffer_get_length( task->response ),
                         task->done_func_user_data );

    task_free( task );
}

/****
*****
****/

void
tr_webRun( tr_session         * session,
           const char         * url,
           const char         * range,
           const char         * cookies,
           tr_web_done_func     done_func,
           void               * done_func_user_data )
{
    tr_webRunWithBuffer( session, url, range, cookies,
                         done_func, done_func_user_data,
                         NULL );
}

void
tr_webRunWithBuffer( tr_session         * session,
                     const char         * url,
                     const char         * range,
                     const char         * cookies,
                     tr_web_done_func     done_func,
                     void               * done_func_user_data,
                     struct evbuffer    * buffer )
{
    struct tr_web * web = session->web;

    if( web != NULL )
    {
        struct tr_web_task * task = tr_new0( struct tr_web_task, 1 );

        task->session = session;
        task->url = tr_strdup( url );
        task->range = tr_strdup( range );
        task->cookies = tr_strdup( cookies);
        task->done_func = done_func;
        task->done_func_user_data = done_func_user_data;
        task->response = buffer ? buffer : evbuffer_new( );
        task->freebuf = buffer ? NULL : task->response;

        tr_lockLock( web->taskLock );
        tr_list_append( &web->tasks, task );
        tr_lockUnlock( web->taskLock );
    }
}

/**
 * Portability wrapper for select().
 *
 * http://msdn.microsoft.com/en-us/library/ms740141%28VS.85%29.aspx
 * On win32, any two of the parameters, readfds, writefds, or exceptfds,
 * can be given as null. At least one must be non-null, and any non-null
 * descriptor set must contain at least one handle to a socket.
 */
static void
tr_select( int nfds,
           fd_set * r_fd_set, fd_set * w_fd_set, fd_set * c_fd_set,
           struct timeval  * t )
{
#ifdef WIN32
    if( !r_fd_set->fd_count && !w_fd_set->fd_count && !c_fd_set->fd_count )
    {
        const long int msec = t->tv_sec*1000 + t->tv_usec/1000;
        tr_wait_msec( msec );
    }
    else if( select( 0, r_fd_set->fd_count ? r_fd_set : NULL,
                        w_fd_set->fd_count ? w_fd_set : NULL,
                        c_fd_set->fd_count ? c_fd_set : NULL, t ) < 0 )
    {
        char errstr[512];
        const int e = EVUTIL_SOCKET_ERROR( );
        tr_net_strerror( errstr, sizeof( errstr ), e );
        dbgmsg( "Error: select (%d) %s", e, errstr );
    }
#else
    select( nfds, r_fd_set, w_fd_set, c_fd_set, t );
#endif
}

static void
tr_webThreadFunc( void * vsession )
{
    CURLM * multi;
    struct tr_web * web;
    int taskCount = 0;
    struct tr_web_task * task;
    tr_session * session = vsession;

    /* try to enable ssl for https support; but if that fails,
     * try a plain vanilla init */
    if( curl_global_init( CURL_GLOBAL_SSL ) )
        curl_global_init( 0 );

    web = tr_new0( struct tr_web, 1 );
    web->close_mode = ~0;
    web->taskLock = tr_lockNew( );
    web->tasks = NULL;
    multi = curl_multi_init( );
    session->web = web;

    for( ;; )
    {
        long msec;
        int unused;
        CURLMsg * msg;
        CURLMcode mcode;

        if( web->close_mode == TR_WEB_CLOSE_NOW )
            break;
        if( ( web->close_mode == TR_WEB_CLOSE_WHEN_IDLE ) && ( web->tasks == NULL ) )
            break;

        /* add tasks from the queue */
        tr_lockLock( web->taskLock );
        while(( task = tr_list_pop_front( &web->tasks )))
        {
            dbgmsg( "adding task to curl: [%s]", task->url );
            curl_multi_add_handle( multi, createEasy( session, task ));
            /*fprintf( stderr, "adding a task.. taskCount is now %d\n", taskCount );*/
            ++taskCount;
        }
        tr_lockUnlock( web->taskLock );

        /* maybe wait a little while before calling curl_multi_perform() */
        msec = 0;
        curl_multi_timeout( multi, &msec );
        if( msec < 0 )
            msec = THREADFUNC_MAX_SLEEP_MSEC;
        if( session->isClosed )
            msec = 100; /* on shutdown, call perform() more frequently */
        if( msec > 0 )
        {
            int usec;
            int max_fd;
            struct timeval t;
            fd_set r_fd_set, w_fd_set, c_fd_set;

            max_fd = 0;
            FD_ZERO( &r_fd_set );
            FD_ZERO( &w_fd_set );
            FD_ZERO( &c_fd_set );
            curl_multi_fdset( multi, &r_fd_set, &w_fd_set, &c_fd_set, &max_fd );

            if( msec > THREADFUNC_MAX_SLEEP_MSEC )
                msec = THREADFUNC_MAX_SLEEP_MSEC;

            usec = msec * 1000;
            t.tv_sec =  usec / 1000000;
            t.tv_usec = usec % 1000000;
            tr_select( max_fd+1, &r_fd_set, &w_fd_set, &c_fd_set, &t );
        }

        /* call curl_multi_perform() */
        do {
            mcode = curl_multi_perform( multi, &unused );
        } while( mcode == CURLM_CALL_MULTI_PERFORM );

        /* pump completed tasks from the multi */
        while(( msg = curl_multi_info_read( multi, &unused )))
        {
            if(( msg->msg == CURLMSG_DONE ) && ( msg->easy_handle != NULL ))
            {
                double total_time;
                struct tr_web_task * task;
                long req_bytes_sent;
                CURL * e = msg->easy_handle;
                curl_easy_getinfo( e, CURLINFO_PRIVATE, (void*)&task );
                curl_easy_getinfo( e, CURLINFO_RESPONSE_CODE, &task->code );
                curl_easy_getinfo( e, CURLINFO_REQUEST_SIZE, &req_bytes_sent );
                curl_easy_getinfo( e, CURLINFO_TOTAL_TIME, &total_time );
                task->did_connect = task->code>0 || req_bytes_sent>0;
                task->did_timeout = !task->code && ( total_time >= task->timeout_secs );
                curl_multi_remove_handle( multi, e );
                curl_easy_cleanup( e );
/*fprintf( stderr, "removing a completed task.. taskCount is now %d (response code: %d, response len: %d)\n", taskCount, (int)task->code, (int)evbuffer_get_length(task->response) );*/
                tr_runInEventThread( task->session, task_finish_func, task );
                --taskCount;
            }
        }

#if 0
{
tr_list * l;
for( l=web->tasks; l!=NULL; l=l->next )
    fprintf( stderr, "still pending: %s\n", ((struct tr_web_task*)l->data)->url );
}
fprintf( stderr, "loop is ending... web is closing\n" );
#endif
    }

    /* Discard any remaining tasks.
     * This is rare, but can happen on shutdown with unresponsive trackers. */
    while(( task = tr_list_pop_front( &web->tasks ))) {
        dbgmsg( "Discarding task \"%s\"", task->url );
        task_free( task );
    }

    /* cleanup */
    curl_multi_cleanup( multi );
    tr_lockFree( web->taskLock );
    tr_free( web );
    session->web = NULL;
}

void
tr_webInit( tr_session * session )
{
    tr_threadNew( tr_webThreadFunc, session );
}

void
tr_webClose( tr_session * session, tr_web_close_mode close_mode )
{
    if( session->web != NULL )
    {
        session->web->close_mode = close_mode;

        if( close_mode == TR_WEB_CLOSE_NOW )
            while( session->web != NULL )
                tr_wait_msec( 100 );
    }
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
    const char * end;

    if( ( len < 0 ) && ( str != NULL ) )
        len = strlen( str );

    for( end=str+len; str && str!=end; ++str ) {
        if(    ( *str == ',' )
            || ( *str == '-' )
            || ( *str == '.' )
            || ( ( '0' <= *str ) && ( *str <= '9' ) )
            || ( ( 'A' <= *str ) && ( *str <= 'Z' ) )
            || ( ( 'a' <= *str ) && ( *str <= 'z' ) )
            || ( ( *str == '/' ) && ( !escape_slashes ) ) )
            evbuffer_add( out, str, 1 );
        else
            evbuffer_add_printf( out, "%%%02X", (unsigned)(*str&0xFF) );
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

static int
is_rfc2396_alnum( uint8_t ch )
{
    return ( '0' <= ch && ch <= '9' )
        || ( 'A' <= ch && ch <= 'Z' )
        || ( 'a' <= ch && ch <= 'z' )
        || ch == '.'
        || ch == '-'
        || ch == '_'
        || ch == '~';
}

void
tr_http_escape_sha1( char * out, const uint8_t * sha1_digest )
{
    const uint8_t * in = sha1_digest;
    const uint8_t * end = in + SHA_DIGEST_LENGTH;

    while( in != end )
        if( is_rfc2396_alnum( *in ) )
            *out++ = (char) *in++;
        else
            out += tr_snprintf( out, 4, "%%%02x", (unsigned int)*in++ );

    *out = '\0';
}
