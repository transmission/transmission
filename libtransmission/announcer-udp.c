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

#define __LIBTRANSMISSION_ANNOUNCER_MODULE___

#include <string.h> /* memcpy(), memset() */

#include <event2/buffer.h>
#include <event2/dns.h>
#include <event2/util.h>

#include "transmission.h"
#include "announcer.h"
#include "announcer-common.h"
#include "crypto.h"
#include "peer-io.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "ptrarray.h"
#include "tr-udp.h"
#include "utils.h"

#define dbgmsg( name, ... ) \
if( tr_deepLoggingIsActive( ) ) do { \
  tr_deepLog( __FILE__, __LINE__, name, __VA_ARGS__ ); \
} while( 0 )

/****
*****
****/

static void
tau_sockaddr_setport( struct sockaddr * sa, tr_port port )
{
    if( sa->sa_family == AF_INET )
        ((struct sockaddr_in *)sa)->sin_port = htons(port);
    else if (sa->sa_family == AF_INET6)
        ((struct sockaddr_in6 *)sa)->sin6_port = htons(port);
}

static int
tau_sendto( tr_session * session,
            struct evutil_addrinfo * ai, tr_port port,
            const void * buf, size_t buflen )
{
    int sockfd;
    
    if( ai->ai_addr->sa_family == AF_INET )
        sockfd = session->udp_socket;
    else if( ai->ai_addr->sa_family == AF_INET6 )
        sockfd = session->udp6_socket;
    else
        sockfd = -1;

    if( sockfd < 0 ) {
        errno = EAFNOSUPPORT;
        return -1;
    }

    tau_sockaddr_setport( ai->ai_addr, port );
    return sendto( sockfd, buf, buflen, 0, ai->ai_addr, ai->ai_addrlen );
}

/****
*****
****/

static uint32_t
evbuffer_read_ntoh_32( struct evbuffer * buf )
{
    uint32_t val;
    evbuffer_remove( buf, &val, sizeof( uint32_t ) );
    return ntohl( val );
}

static uint64_t
evbuffer_read_ntoh_64( struct evbuffer * buf )
{
    uint64_t val;
    evbuffer_remove( buf, &val, sizeof( uint64_t ) );
    return tr_ntohll( val );
}

/****
*****
****/

typedef uint64_t tau_connection_t;

enum
{
    TAU_CONNECTION_TTL_SECS = 60
};

typedef uint32_t tau_transaction_t;

static tau_transaction_t
tau_transaction_new( void )
{
    tau_transaction_t tmp;
    tr_cryptoRandBuf( &tmp, sizeof( tau_transaction_t ) );
    return tmp;
}

/* used in the "action" field of a request */
typedef enum
{
    TAU_ACTION_CONNECT  = 0,
    TAU_ACTION_ANNOUNCE = 1,
    TAU_ACTION_SCRAPE   = 2,
    TAU_ACTION_ERROR    = 3
}
tau_action_t;

static tr_bool
is_tau_response_message( int action, int msglen )
{
    if( action == TAU_ACTION_CONNECT  ) return msglen == 16;
    if( action == TAU_ACTION_ANNOUNCE ) return msglen >= 20;
    if( action == TAU_ACTION_SCRAPE   ) return msglen >= 20;
    if( action == TAU_ACTION_ERROR    ) return msglen >= 8;
    return FALSE;
}

enum
{
    TAU_REQUEST_TTL = 60
};

/****
*****
*****  SCRAPE
*****
****/

struct tau_scrape_request
{
    void * payload;
    size_t payload_len;

    time_t sent_at;
    time_t created_at;
    tau_transaction_t transaction_id;

    tr_scrape_response response;
    tr_scrape_response_func * callback;
    void * user_data;
};

static struct tau_scrape_request *
tau_scrape_request_new( const tr_scrape_request  * in,
                        tr_scrape_response_func    callback,
                        void                     * user_data )
{
    int i;
    struct evbuffer * buf;
    struct tau_scrape_request * req;
    const tau_transaction_t transaction_id = tau_transaction_new( );

    /* build the payload */
    buf = evbuffer_new( );
    evbuffer_add_hton_32( buf, TAU_ACTION_SCRAPE );
    evbuffer_add_hton_32( buf, transaction_id );
    for( i=0; i<in->info_hash_count; ++i )
        evbuffer_add( buf, in->info_hash[i], SHA_DIGEST_LENGTH );

    /* build the tau_scrape_request */
    req = tr_new0( struct tau_scrape_request, 1 );
    req->created_at = tr_time( );
    req->transaction_id = transaction_id;
    req->callback = callback;
    req->user_data = user_data;
    req->response.url = tr_strdup( in->url );
    req->response.row_count = in->info_hash_count;
    req->payload_len = evbuffer_get_length( buf );
    req->payload = tr_memdup( evbuffer_pullup( buf, -1 ), req->payload_len );
    for( i=0; i<req->response.row_count; ++i )
        memcpy( req->response.rows[i].info_hash,
                in->info_hash[i], SHA_DIGEST_LENGTH );

    /* cleanup */
    evbuffer_free( buf );
    return req;
}

static void
tau_scrape_request_free( struct tau_scrape_request * req )
{
    tr_free( req->payload );
    tr_free( req->response.errmsg );
    tr_free( req->response.url );
    tr_free( req );
}

static void
tau_scrape_request_finished( tr_session                       * session,
                             const struct tau_scrape_request  * request )
{
    if( request->callback != NULL )
        request->callback( session, &request->response, request->user_data );
}

static void
tau_scrape_request_fail( tr_session                 * session,
                         struct tau_scrape_request  * request,
                         tr_bool                      did_connect,
                         tr_bool                      did_timeout,
                         const char                 * errmsg )
{
    request->response.did_connect = did_connect;
    request->response.did_timeout = did_timeout;
    request->response.errmsg = tr_strdup( errmsg );
    tau_scrape_request_finished( session, request );
}

static void
on_scrape_response( tr_session                  * session,
                     struct tau_scrape_request  * request,
                     tau_action_t                 action,
                     struct evbuffer            * buf )
{
    request->response.did_connect = TRUE;
    request->response.did_timeout = FALSE;

    if( action == TAU_ACTION_SCRAPE )
    {
        int i;
        for( i=0; i<request->response.row_count; ++i )
        {
            struct tr_scrape_response_row * row;

            if( evbuffer_get_length( buf ) < ( sizeof( uint32_t ) * 3 ) )
                break;

            row = &request->response.rows[i];
            row->seeders   = evbuffer_read_ntoh_32( buf );
            row->downloads = evbuffer_read_ntoh_32( buf );
            row->leechers  = evbuffer_read_ntoh_32( buf );
        }

        tau_scrape_request_finished( session, request );
    }
    else
    {
        char * errmsg;
        const size_t buflen = evbuffer_get_length( buf );

        if( ( action == TAU_ACTION_ERROR ) && ( buflen > 0 ) )
            errmsg = tr_strndup( evbuffer_pullup( buf, -1 ), buflen );
        else
            errmsg = tr_strdup( _( "Unknown error" ) );

        tau_scrape_request_fail( session, request, TRUE, FALSE, errmsg );
        tr_free( errmsg );
    }
}

/****
*****
*****  ANNOUNCE
*****
****/

struct tau_announce_request
{
    void * payload;
    size_t payload_len;

    time_t created_at;
    time_t sent_at;
    tau_transaction_t transaction_id;

    tr_announce_response response;
    tr_announce_response_func * callback;
    void * user_data;
};

typedef enum
{
    /* used in the "event" field of an announce request */
    TAU_ANNOUNCE_EVENT_NONE      = 0,
    TAU_ANNOUNCE_EVENT_COMPLETED = 1,
    TAU_ANNOUNCE_EVENT_STARTED   = 2,
    TAU_ANNOUNCE_EVENT_STOPPED   = 3
}
tau_announce_event;

static tau_announce_event
get_tau_announce_event( tr_announce_event e )
{
    switch( e )
    {
        case TR_ANNOUNCE_EVENT_COMPLETED: return TAU_ANNOUNCE_EVENT_COMPLETED;
        case TR_ANNOUNCE_EVENT_STARTED:   return TAU_ANNOUNCE_EVENT_STARTED;
        case TR_ANNOUNCE_EVENT_STOPPED:   return TAU_ANNOUNCE_EVENT_STOPPED;
        default:                          return TAU_ANNOUNCE_EVENT_NONE;
    }
}

static struct tau_announce_request *
tau_announce_request_new( const tr_announce_request  * in,
                          tr_announce_response_func    callback,
                          void                       * user_data )
{
    struct evbuffer * buf;
    struct tau_announce_request * req;
    const tau_transaction_t transaction_id = tau_transaction_new( );

    /* build the payload */
    buf = evbuffer_new( );
    evbuffer_add_hton_32( buf, TAU_ACTION_ANNOUNCE );
    evbuffer_add_hton_32( buf, transaction_id );
    evbuffer_add        ( buf, in->info_hash, SHA_DIGEST_LENGTH );
    evbuffer_add        ( buf, in->peer_id, PEER_ID_LEN );
    evbuffer_add_hton_64( buf, in->down );
    evbuffer_add_hton_64( buf, in->left );
    evbuffer_add_hton_64( buf, in->up );
    evbuffer_add_hton_32( buf, get_tau_announce_event( in->event ) );
    evbuffer_add_hton_32( buf, 0 );
    evbuffer_add_hton_32( buf, in->key );
    evbuffer_add_hton_32( buf, in->numwant );
    evbuffer_add_hton_16( buf, in->port );

    /* build the tau_announce_request */
    req = tr_new0( struct tau_announce_request, 1 );
    req->created_at = tr_time( );
    req->transaction_id = transaction_id;
    req->callback = callback;
    req->user_data = user_data;
    req->payload_len = evbuffer_get_length( buf );
    req->payload = tr_memdup( evbuffer_pullup( buf, -1 ), req->payload_len );
    memcpy( req->response.info_hash, in->info_hash, SHA_DIGEST_LENGTH );

    evbuffer_free( buf );
    return req;
}

static void
tau_announce_request_free( struct tau_announce_request * req )
{
    tr_free( req->response.tracker_id_str );
    tr_free( req->response.warning );
    tr_free( req->response.errmsg );
    tr_free( req->response.pex6 );
    tr_free( req->response.pex );
    tr_free( req->payload );
    tr_free( req );
}

static void
tau_announce_request_finished( tr_session                        * session,
                               const struct tau_announce_request * request )
{
    if( request->callback != NULL )
        request->callback( session, &request->response, request->user_data );
}

static void
tau_announce_request_fail( tr_session                   * session,
                           struct tau_announce_request  * request,
                           tr_bool                        did_connect,
                           tr_bool                        did_timeout,
                           const char                   * errmsg )
{
    request->response.did_connect = did_connect;
    request->response.did_timeout = did_timeout;
    request->response.errmsg = tr_strdup( errmsg );
    tau_announce_request_finished( session, request );
}

static void
on_announce_response( tr_session                  * session,
                     struct tau_announce_request  * request,
                     tau_action_t                   action,
                     struct evbuffer              * buf )
{
    request->response.did_connect = TRUE;
    request->response.did_timeout = FALSE;

    if( ( action == TAU_ACTION_ANNOUNCE )
        && ( evbuffer_get_length( buf ) >= 3*sizeof(uint32_t) ) )
    {
        tr_announce_response * resp = &request->response;
        resp->interval = evbuffer_read_ntoh_32( buf );
        resp->leechers = evbuffer_read_ntoh_32( buf );
        resp->seeders  = evbuffer_read_ntoh_32( buf );
        resp->pex = tr_peerMgrCompactToPex( evbuffer_pullup( buf, -1 ),
                                            evbuffer_get_length( buf ),
                                            NULL, 0,
                                            &request->response.pex_count );
        tau_announce_request_finished( session, request );
    }
    else
    {
        char * errmsg;
        const size_t buflen = evbuffer_get_length( buf );

        if( ( action == TAU_ACTION_ERROR ) && ( buflen > 0 ) )
            errmsg = tr_strndup( evbuffer_pullup( buf, -1 ), buflen );
        else
            errmsg = tr_strdup( _( "Unknown error" ) );

        tau_announce_request_fail( session, request, TRUE, FALSE, errmsg );
        tr_free( errmsg );
    }
}

/****
*****
*****  TRACKERS
*****
****/

struct tau_tracker
{
    tr_session * session;

    char * key;
    char * host;
    int port;

    tr_bool is_asking_dns;
    struct evutil_addrinfo * addr;
    time_t addr_expiration_time;

    tr_bool is_connecting;
    time_t connection_expiration_time;
    tau_connection_t connection_id;
    tau_transaction_t connection_transaction_id;

    time_t close_at;

    tr_ptrArray announces;
    tr_ptrArray scrapes;
};

static void tau_tracker_upkeep( struct tau_tracker * );

static void
tau_tracker_free( struct tau_tracker * t )
{
    if( t->addr )
        evutil_freeaddrinfo( t->addr );
    tr_ptrArrayDestruct( &t->announces, (PtrArrayForeachFunc)tau_announce_request_free );
    tr_ptrArrayDestruct( &t->scrapes, (PtrArrayForeachFunc)tau_scrape_request_free );
    tr_free( t->host );
    tr_free( t->key );
    tr_free( t );
}

static void
tau_tracker_fail_all( struct tau_tracker  * tracker,
                      tr_bool               did_connect,
                      tr_bool               did_timeout,
                      const char          * errmsg )
{
    int i;
    int n;
    tr_ptrArray * reqs;

    /* fail all the scrapes */
    reqs = &tracker->scrapes;
    for( i=0, n=tr_ptrArraySize(reqs); i<n; ++i )
        tau_scrape_request_fail( tracker->session, tr_ptrArrayNth( reqs, i ),
                                 did_connect, did_timeout, errmsg );
    tr_ptrArrayDestruct( reqs, (PtrArrayForeachFunc)tau_scrape_request_free );
    *reqs = TR_PTR_ARRAY_INIT;

    /* fail all the announces */
    reqs = &tracker->announces;
    for( i=0, n=tr_ptrArraySize(reqs); i<n; ++i )
        tau_announce_request_fail( tracker->session, tr_ptrArrayNth( reqs, i ),
                                   did_connect, did_timeout, errmsg );
    tr_ptrArrayDestruct( reqs, (PtrArrayForeachFunc)tau_announce_request_free );
    *reqs = TR_PTR_ARRAY_INIT;

}

static void
tau_tracker_on_dns( int errcode, struct evutil_addrinfo *addr, void * vtracker )
{
    struct tau_tracker * tracker = vtracker;

    tracker->is_asking_dns = FALSE;

    if ( errcode )
    {
        char * errmsg = tr_strdup_printf( _( "DNS Lookup failed: %s" ), 
                                          evdns_err_to_string( errcode ) );
        dbgmsg( tracker->key, "%s", errmsg );
        tau_tracker_fail_all( tracker, FALSE, FALSE, errmsg );
        tr_free( errmsg );
    }
    else
    {
        dbgmsg( tracker->key, "DNS lookup succeeded" );
        tracker->addr = addr;
        tracker->addr_expiration_time = tr_time() + (60*60); /* one hour */
        tau_tracker_upkeep( tracker );
    }
}

static void
tau_tracker_send_request( struct tau_tracker  * tracker,
                          const void          * payload,
                          size_t                payload_len )
{
    struct evbuffer * buf = evbuffer_new( );
    dbgmsg( tracker->key, "sending request w/connection id %"PRIu64"\n",
                          tracker->connection_id );
    evbuffer_add_hton_64( buf, tracker->connection_id );
    evbuffer_add_reference( buf, payload, payload_len, NULL, NULL );
    tau_sendto( tracker->session, tracker->addr, tracker->port,
                evbuffer_pullup( buf, -1 ),
                evbuffer_get_length( buf ) );
    evbuffer_free( buf );
}

static tr_bool
tau_tracker_is_empty( const struct tau_tracker * tracker )
{
    return tr_ptrArrayEmpty( &tracker->announces )
        && tr_ptrArrayEmpty( &tracker->scrapes );
}

static void
tau_tracker_upkeep( struct tau_tracker * tracker )
{
    int i;
    int n;
    tr_ptrArray * reqs;
    const time_t now = tr_time( );
    const tr_bool is_connected = tracker->connection_expiration_time > now;

    /* if the address info is too old, expire it */
    if( tracker->addr && ( tracker->addr_expiration_time <= now ) ) {
        dbgmsg( tracker->host, "Expiring old DNS result" );     
        evutil_freeaddrinfo( tracker->addr );
        tracker->addr = NULL;
    }

    /* are there any requests pending? */
    if( tau_tracker_is_empty( tracker ) )
        return;

    /* if we don't have an address yet, try & get one now. */
    if( !tracker->addr && !tracker->is_asking_dns )
    {
        struct evutil_addrinfo hints;
        memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = EVUTIL_AI_CANONNAME;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        tracker->is_asking_dns = TRUE;
        dbgmsg( tracker->host, "Trying a new DNS lookup" );     
        evdns_getaddrinfo( tracker->session->evdns_base,
                           tracker->host, NULL, &hints,
                           tau_tracker_on_dns, tracker );
        return;
    }

    /* also need a valid connection ID... */
    if( !is_connected && !tracker->is_connecting && tracker->addr )
    {
        struct evbuffer * buf = evbuffer_new( );
        tracker->is_connecting = TRUE;
        tracker->connection_transaction_id = tau_transaction_new( );
        dbgmsg( tracker->key, "Trying to connect. Transaction ID is %u",
                tracker->connection_transaction_id );
        evbuffer_add_hton_64( buf, 0x41727101980LL );
        evbuffer_add_hton_32( buf, TAU_ACTION_CONNECT );
        evbuffer_add_hton_32( buf, tracker->connection_transaction_id );
        tau_sendto( tracker->session, tracker->addr, tracker->port,
                    evbuffer_pullup( buf, -1 ),
                    evbuffer_get_length( buf ) );
        evbuffer_free( buf );
        return;
    }

    /* send the announce requests */
    reqs = &tracker->announces;
    for( i=0, n=tr_ptrArraySize(reqs); i<n; ++i )
    {
        tr_bool remove_request = FALSE;
        struct tau_announce_request * req = tr_ptrArrayNth( reqs, i );
        if( is_connected && !req->sent_at ) {
            dbgmsg( tracker->key, "Sending an announce request" );
            req->sent_at = now;
            tau_tracker_send_request( tracker, req->payload, req->payload_len );
            remove_request = req->callback == NULL;
        }
        else if( req->created_at + TAU_REQUEST_TTL < now ) {
            tau_announce_request_fail( tracker->session, req, FALSE, TRUE, NULL );
            remove_request = TRUE;
        }
        if( tracker->close_at && ( tracker->close_at <= time(NULL) ) )
            remove_request = TRUE;
        if( remove_request ) {
            tau_announce_request_free( req );
            tr_ptrArrayRemove( reqs, i );
            --i;
            --n;
        }
    }

    /* send the scrape requests */
    reqs = &tracker->scrapes;
    for( i=0, n=tr_ptrArraySize(reqs); i<n; ++i )
    {
        tr_bool remove_request = FALSE;
        struct tau_scrape_request * req = tr_ptrArrayNth( reqs, i );
        if( is_connected && !req->sent_at ) {
            dbgmsg( tracker->key, "Sending a scrape request" );
            req->sent_at = now;
            tau_tracker_send_request( tracker, req->payload, req->payload_len );
            remove_request = req->callback == NULL;
        }
        else if( req->created_at + TAU_REQUEST_TTL < now ) {
            tau_scrape_request_fail( tracker->session, req, FALSE, TRUE, NULL );
            remove_request = TRUE;
        }
        if( tracker->close_at && ( tracker->close_at <= time(NULL) ) )
            remove_request = TRUE;
        if( remove_request ) {
            tau_scrape_request_free( req );
            tr_ptrArrayRemove( reqs, i );
            --i;
            --n;
        }
    }
}

static void
on_tracker_connection_response( struct tau_tracker  * tracker,
                                tau_action_t          action,
                                struct evbuffer     * buf )
{
    const time_t now = tr_time( );

    tracker->is_connecting = FALSE;
    tracker->connection_transaction_id = 0;

    if( action == TAU_ACTION_CONNECT )
    {
        tracker->connection_id = evbuffer_read_ntoh_64( buf );
        tracker->connection_expiration_time = now + TAU_CONNECTION_TTL_SECS;
        dbgmsg( tracker->key, "Got a new connection ID from tracker: %"PRIu64,
                tracker->connection_id );
    }
    else
    {
        char * errmsg;
        const size_t buflen = evbuffer_get_length( buf );

        if( ( action == TAU_ACTION_ERROR ) && ( buflen > 0 ) )
            errmsg = tr_strndup( evbuffer_pullup( buf, -1 ), buflen );
        else
            errmsg = tr_strdup( _( "Connection refused" ) );

        dbgmsg( tracker->key, "%s", errmsg );
        tau_tracker_fail_all( tracker, TRUE, FALSE, errmsg );
        tr_free( errmsg );
    }

    tau_tracker_upkeep( tracker );
}

/****
*****
*****  SESSION
*****
****/

struct tr_announcer_udp
{
    /* tau_tracker */
    tr_ptrArray trackers;

    tr_session * session;
};

static struct tr_announcer_udp*
announcer_udp_get( tr_session * session )
{
    struct tr_announcer_udp * tau;

    if( session->announcer_udp != NULL )
        return session->announcer_udp;

    tau = tr_new0( struct tr_announcer_udp, 1 );
    tau->trackers = TR_PTR_ARRAY_INIT;
    tau->session = session;
    session->announcer_udp = tau;
    return tau;
}

/* Finds the tau_tracker struct that corresponds to this url.
   If it doesn't exist yet, create one. */
static struct tau_tracker *
tau_session_get_tracker( struct tr_announcer_udp * tau, const char * url )
{
    int i;
    int n;
    int port;
    char * host;
    char * key;
    struct tau_tracker * tracker = NULL;

    /* see if we've already got a tracker that matches this host + port */
    tr_urlParse( url, -1, NULL, &host, &port, NULL );
    key = tr_strdup_printf( "%s:%d", host, port );
    for( i=0, n=tr_ptrArraySize( &tau->trackers ); !tracker && i<n; ++i ) {
        struct tau_tracker * tmp = tr_ptrArrayNth( &tau->trackers, i );
        if( !tr_strcmp0( tmp->key, key ) )
            tracker = tmp;
    }

    /* if we don't have a match, build a new tracker */
    if( tracker == NULL )
    {
        tracker = tr_new0( struct tau_tracker, 1 );
        tracker->session = tau->session;
        tracker->key = key;
        tracker->host = host;
        tracker->port = port;
        tracker->scrapes = TR_PTR_ARRAY_INIT;
        tracker->announces = TR_PTR_ARRAY_INIT;
        tr_ptrArrayAppend( &tau->trackers, tracker );
        dbgmsg( tracker->key, "New tau_tracker created" );
    }
    else
    {
        tr_free( key );
        tr_free( host );
    }

    return tracker;
}

/****
*****
*****  PUBLIC API
*****
****/

void
tr_tracker_udp_upkeep( tr_session * session )
{
    struct tr_announcer_udp * tau = session->announcer_udp;

    if( tau != NULL )
        tr_ptrArrayForeach( &tau->trackers,
                            (PtrArrayForeachFunc)tau_tracker_upkeep );
}

tr_bool
tr_tracker_udp_is_empty( const tr_session * session )
{
    int i;
    int n;
    struct tr_announcer_udp * tau = session->announcer_udp;

    if( tau != NULL )
        for( i=0, n=tr_ptrArraySize(&tau->trackers); i<n; ++i )
            if( !tau_tracker_is_empty( tr_ptrArrayNth( &tau->trackers, i ) ) )
                return FALSE;

    return TRUE;
}

/* drop dead now. */
void
tr_tracker_udp_close( tr_session * session )
{
    struct tr_announcer_udp * tau = session->announcer_udp;

    if( tau != NULL )
    {
        session->announcer_udp = NULL;
        tr_ptrArrayDestruct( &tau->trackers, (PtrArrayForeachFunc)tau_tracker_free );
        tr_free( tau );
    }
        
}

/* start shutting down.
   This doesn't destroy everything if there are requests,
   but sets a deadline on how much longer to wait for the remaining ones */
void
tr_tracker_udp_start_shutdown( tr_session * session )
{
    const time_t now = time( NULL );
    struct tr_announcer_udp * tau = session->announcer_udp;

    if( tau != NULL )
    {
        int i, n;
        for( i=0, n=tr_ptrArraySize(&tau->trackers); i<n; ++i )
        {
            struct tau_tracker * tracker = tr_ptrArrayNth( &tau->trackers, i );
            tracker->close_at = now + 3;
            tau_tracker_upkeep( tracker );
        }
    }
}

/* @brief process an incoming udp message if it's a tracker response.
 * @return true if msg was a tracker response; false otherwise */
tr_bool
tau_handle_message( tr_session * session, const uint8_t * msg, size_t msglen )
{
    int i;
    int n;
    struct tr_announcer_udp * tau;
    tau_action_t action_id;
    tau_transaction_t transaction_id;
    struct evbuffer * buf;

    /*fprintf( stderr, "got an incoming udp message w/len %zu\n", msglen );*/

    if( !session || !session->announcer_udp )
        return FALSE;
    if( msglen < (sizeof(uint32_t)*2) )
        return FALSE;

    /* extract the action_id and see if it makes sense */
    buf = evbuffer_new( );
    evbuffer_add_reference( buf, msg, msglen, NULL, NULL );
    action_id = evbuffer_read_ntoh_32( buf );
    if( !is_tau_response_message( action_id, msglen ) ) {
        evbuffer_free( buf );
        return FALSE;
    }

    /* extract the transaction_id and look for a match */
    tau = session->announcer_udp;
    transaction_id = evbuffer_read_ntoh_32( buf );
    /*fprintf( stderr, "UDP got a transaction_id %u...\n", transaction_id );*/
    for( i=0, n=tr_ptrArraySize( &tau->trackers ); i<n; ++i )
    {
        int j, jn;
        tr_ptrArray * reqs;
        struct tau_tracker * tracker = tr_ptrArrayNth( &tau->trackers, i );

        /* is it a connection response? */
        if( tracker->is_connecting
            && ( transaction_id == tracker->connection_transaction_id ) )
        {
            dbgmsg( tracker->key, "%"PRIu32" is my connection request!", transaction_id );
            on_tracker_connection_response( tracker, action_id, buf );
            evbuffer_free( buf );
            return TRUE;
        }

        /* is it a response to one of this tracker's announces? */
        reqs = &tracker->announces;
        for( j=0, jn=tr_ptrArraySize(reqs); j<jn; ++j ) {
            struct tau_announce_request * req = tr_ptrArrayNth( reqs, j );
            if( req->sent_at && ( transaction_id == req->transaction_id ) ) {
                dbgmsg( tracker->key, "%"PRIu32" is an announce request!", transaction_id );
                tr_ptrArrayRemove( reqs, j );
                on_announce_response( session, req, action_id, buf );
                tau_announce_request_free( req );
                evbuffer_free( buf );
                return TRUE;
            }
        }

        /* is it a response to one of this tracker's scrapes? */
        reqs = &tracker->scrapes;
        for( j=0, jn=tr_ptrArraySize(reqs); j<jn; ++j ) {
            struct tau_scrape_request * req = tr_ptrArrayNth( reqs, j );
            if( req->sent_at && ( transaction_id == req->transaction_id ) ) {
                dbgmsg( tracker->key, "%"PRIu32" is a scrape request!", transaction_id );
                tr_ptrArrayRemove( reqs, j );
                on_scrape_response( session, req, action_id, buf );
                tau_scrape_request_free( req );
                evbuffer_free( buf );
                return TRUE;
            }
        }
    }

    /* no match... */
    evbuffer_free( buf );
    return FALSE;
}

void
tr_tracker_udp_announce( tr_session                 * session,
                         const tr_announce_request  * request,
                         tr_announce_response_func    response_func,
                         void                       * user_data )
{
    struct tr_announcer_udp * tau = announcer_udp_get( session );
    struct tau_tracker * tracker = tau_session_get_tracker( tau, request->url );
    struct tau_announce_request * r = tau_announce_request_new( request,
                                                                response_func,
                                                                user_data );
    tr_ptrArrayAppend( &tracker->announces, r );
    tau_tracker_upkeep( tracker );
}

void
tr_tracker_udp_scrape( tr_session               * session,
                       const tr_scrape_request  * request,
                       tr_scrape_response_func    response_func,
                       void                     * user_data )
{
    struct tr_announcer_udp * tau = announcer_udp_get( session );
    struct tau_tracker * tracker = tau_session_get_tracker( tau, request->url );
    struct tau_scrape_request * r = tau_scrape_request_new( request,
                                                            response_func,
                                                            user_data );
    tr_ptrArrayAppend( &tracker->scrapes, r );
    tau_tracker_upkeep( tracker );
}
