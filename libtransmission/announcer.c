/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
#include <limits.h>

#include <event.h>
#include <evhttp.h> /* for HTTP_OK */

#include "transmission.h"
#include "announcer.h"
#include "crypto.h"
#include "net.h"
#include "ptrarray.h"
#include "publish.h"
#include "session.h"
#include "tr-dht.h"
#include "torrent.h"
#include "utils.h"
#include "web.h"

#define dbgmsg( tier, ... ) \
if( tr_deepLoggingIsActive( ) ) do { \
  char name[128]; \
  tr_snprintf( name, sizeof( name ), "[%s--%s]", tr_torrentName( tier->tor ), \
      ( tier->currentTracker ? tier->currentTracker->host->name : "" ) ); \
  tr_deepLog( __FILE__, __LINE__, name, __VA_ARGS__ ); \
} while( 0 )

enum
{
    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = ( 60 * 15 ),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = ( 60 * 10 ),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = ( 60 * 2 ),

    /* how long to wait before a reannounce the first time we get an error */
    FIRST_ANNOUNCE_RETRY_INTERVAL_SEC = 30,

    /* how long to wait before a rescrape the first time we get an error */
    FIRST_SCRAPE_RETRY_INTERVAL_SEC = 120,

    /* the length of the 'key' argument passed in tracker requests */
    KEYLEN = 8,

    /* how many scrapes we allow at one time */
    MAX_CONCURRENT_SCRAPES = 96,

    /* how many announces we allow at one time */
    MAX_CONCURRENT_ANNOUNCES = 96,

    /* if a tracker takes more than this long to respond,
     * we treat it as nonresponsive */
    MAX_TRACKER_RESPONSE_TIME_SECS = ( 60 * 2 ),

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 200,

    /* how long to put slow (nonresponsive) trackers in the penalty box */
    SLOW_HOST_PENALTY_SECS = ( 60 * 10 ),

    UPKEEP_INTERVAL_SECS = 1

};

/***
****
***/

static int
compareTransfer( int a_uploaded, int a_downloaded,
                 int b_uploaded, int b_downloaded )
{
    /* higher upload count goes first */
    if( a_uploaded != b_uploaded )
        return a_uploaded > b_uploaded ? -1 : 1;

    /* then higher download count goes first */
    if( a_downloaded != b_downloaded )
        return a_downloaded > b_downloaded ? -1 : 1;

    return 0;
}

/***
****
***/

/**
 * used by tr_announcer to recognize nonresponsive
 * trackers and de-prioritize them
 */
typedef struct
{
    char * name;

    /* how many seconds it took to get the last tracker response */
    int lastResponseInterval;

    /* the last time we sent an announce or scrape message */
    time_t lastRequestTime;
}
tr_host;

static int
compareHosts( const void * va, const void * vb )
{
    const tr_host * a = va;
    const tr_host * b = vb;
    return strcmp( a->name, b->name );
}

static int
compareHostToName( const void * va, const void * vb )
{
    const tr_host * a = va;
    return strcmp( a->name, vb );
}

/* format: hostname + ':' + port */
static char *
getHostName( const char * url )
{
    int port = 0;
    char * host = NULL;
    char * ret;
    tr_httpParseURL( url, strlen( url ), &host, &port, NULL );
    ret = tr_strdup_printf( "%s:%d", ( host ? host : "invalid" ), port );
    tr_free( host );
    return ret;
}

static tr_host*
hostNew( const char * name )
{
    tr_host * host = tr_new0( tr_host, 1 );
    host->name = tr_strdup( name );
    return host;
}

static void
hostFree( void * vhost )
{
    tr_host * host = vhost;

    tr_free( host->name );
    tr_free( host );
}

/***
****
***/

/**
 * Since we can't poll a tr_torrent's fields after it's destroyed,
 * we pre-build the "stop" announcement message when a torrent
 * is removed from Transmission
 */
struct stop_message
{
    tr_host * host;
    char * url;
    int up;
    int down;
};

static void
stopFree( struct stop_message * stop )
{
    tr_free( stop->url );
    tr_free( stop );
}

static int
compareStops( const void * va, const void * vb )
{
    const struct stop_message * a = va;
    const struct stop_message * b = vb;
    return compareTransfer( a->up, a->down, b->up, b->down);
}

/***
****
***/

/**
 * "global" (per-tr_session) fields
 */
typedef struct tr_announcer
{
    tr_ptrArray hosts; /* tr_host */
    tr_ptrArray stops; /* struct stop_message */
    tr_session * session;
    struct event * upkeepTimer;
    int announceSlotsAvailable;
    int scrapeSlotsAvailable;
}
tr_announcer;

tr_bool
tr_announcerHasBacklog( const struct tr_announcer * announcer )
{
    return ( announcer->scrapeSlotsAvailable < 1 )
        || ( announcer->announceSlotsAvailable < 1 );
}

static tr_host *
getHost( tr_announcer * announcer, const char * url )
{
    char * name = getHostName( url );
    tr_host * host;

    host = tr_ptrArrayFindSorted( &announcer->hosts, name, compareHostToName );
    if( host == NULL )
    {
        host = hostNew( name );
        tr_ptrArrayInsertSorted( &announcer->hosts, host, compareHosts );
    }

    tr_free( name );
    return host;
}

static void
onUpkeepTimer( int foo UNUSED, short bar UNUSED, void * vannouncer );

void
tr_announcerInit( tr_session * session )
{
    tr_announcer * a;

    assert( tr_isSession( session ) );

    a = tr_new0( tr_announcer, 1 );
    a->hosts = TR_PTR_ARRAY_INIT;
    a->stops = TR_PTR_ARRAY_INIT;
    a->session = session;
    a->announceSlotsAvailable = MAX_CONCURRENT_ANNOUNCES;
    a->scrapeSlotsAvailable = MAX_CONCURRENT_SCRAPES;
    a->upkeepTimer = tr_new0( struct event, 1 );
    evtimer_set( a->upkeepTimer, onUpkeepTimer, a );
    tr_timerAdd( a->upkeepTimer, UPKEEP_INTERVAL_SECS, 0 );

    session->announcer = a;
}

static void flushCloseMessages( tr_announcer * announcer );

void
tr_announcerClose( tr_session * session )
{
    tr_announcer * announcer = session->announcer;

    flushCloseMessages( announcer );

    evtimer_del( announcer->upkeepTimer );
    tr_free( announcer->upkeepTimer );
    announcer->upkeepTimer = NULL;

    tr_ptrArrayDestruct( &announcer->stops, NULL );
    tr_ptrArrayDestruct( &announcer->hosts, hostFree );

    session->announcer = NULL;
    tr_free( announcer );
}

/***
****
***/

/* a row in tr_tier's list of trackers */
typedef struct
{
    tr_host * host;

    char * announce;
    char * scrape;

    char * tracker_id;

    int seederCount;
    int leecherCount;
    int downloadCount;
    int downloaderCount;
    
    uint32_t id;

    /* sent as the "key" argument in tracker requests
     * to verify us if our IP address changes.
     * This is immutable for the life of the tracker object.
     * The +1 is for '\0' */
    char key_param[KEYLEN + 1];
}
tr_tracker_item;

static void
generateKeyParam( char * msg, size_t msglen )
{
    size_t i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int poolSize = strlen( pool );

    for( i=0; i<msglen; ++i )
        *msg++ = pool[tr_cryptoRandInt( poolSize )];
    *msg = '\0';
}

static tr_tracker_item*
trackerNew( tr_announcer  * announcer,
            const char    * announce,
            const char    * scrape,
            uint32_t        id )
{
    tr_tracker_item * tracker = tr_new0( tr_tracker_item, 1  );
    tracker->host = getHost( announcer, announce );
    tracker->announce = tr_strdup( announce );
    tracker->scrape = tr_strdup( scrape );
    tracker->id = id;
    generateKeyParam( tracker->key_param, KEYLEN );
    tracker->seederCount = -1;
    tracker->leecherCount = -1;
    tracker->downloadCount = -1;
    return tracker;
}

static void
trackerFree( void * vtracker )
{
    tr_tracker_item * tracker = vtracker;

    tr_free( tracker->tracker_id );
    tr_free( tracker->announce );
    tr_free( tracker->scrape );
    tr_free( tracker );
}

/***
****
***/

struct tr_torrent_tiers;

/** @brief A group of trackers in a single tier, as per the multitracker spec */
typedef struct
{
    tr_ptrArray trackers; /* tr_tracker_item */
    tr_tracker_item * currentTracker;

    tr_torrent * tor;

    time_t lastScrapeTime;
    time_t lastAnnounceTime;
    time_t lastScrapeStartTime;
    time_t lastAnnounceStartTime;
    tr_bool lastScrapeSucceeded;
    tr_bool lastAnnounceSucceeded;

    time_t scrapeAt;
    time_t manualAnnounceAllowedAt;

    time_t announceAt;
    const char * announceEvent;

    /* unique lookup key */
    int key;

    int currentTrackerIndex;
    int scrapeIntervalSec;
    int announceIntervalSec;
    int announceMinIntervalSec;
    int retryAnnounceIntervalSec;
    int retryScrapeIntervalSec;

    int lastAnnouncePeerCount;

    tr_bool isRunning;
    tr_bool isAnnouncing;
    tr_bool isScraping;

    char lastAnnounceStr[128];
    char lastScrapeStr[128];
}
tr_tier;

static tr_tier *
tierNew( tr_torrent * tor )
{
    tr_tier * t;
    static int nextKey = 1;
    const time_t now = tr_time( );

    t = tr_new0( tr_tier, 1 );
    t->key = nextKey++;
    t->trackers = TR_PTR_ARRAY_INIT;
    t->currentTracker = NULL;
    t->currentTrackerIndex = -1;
    t->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    t->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    t->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    t->scrapeAt = now + tr_cryptoWeakRandInt( 60*10 );
    t->tor = tor;

    return t;
}

static void
tierFree( void * vtier )
{
    tr_tier * tier = vtier;
    tr_ptrArrayDestruct( &tier->trackers, trackerFree );
    tr_free( tier );
}

static void
tierIncrementTracker( tr_tier * tier )
{
    /* move our index to the next tracker in the tier */
    const int i = ( tier->currentTrackerIndex + 1 )
                        % tr_ptrArraySize( &tier->trackers );
    tier->currentTracker = tr_ptrArrayNth( &tier->trackers, i );
    tier->currentTrackerIndex = i;

    /* reset some of the tier's fields */
    tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    tier->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    tier->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    tier->retryAnnounceIntervalSec = FIRST_ANNOUNCE_RETRY_INTERVAL_SEC;
    tier->retryScrapeIntervalSec = FIRST_SCRAPE_RETRY_INTERVAL_SEC;
    tier->isAnnouncing = FALSE;
    tier->isScraping = FALSE;
    tier->lastAnnounceStartTime = 0;
    tier->lastScrapeStartTime = 0;
}

static void
tierAddTracker( tr_announcer * announcer,
                tr_tier      * tier,
                const char   * announce,
                const char   * scrape,
                uint32_t       id )
{
    tr_tracker_item * tracker = trackerNew( announcer, announce, scrape, id );

    tr_ptrArrayAppend( &tier->trackers, tracker );
    dbgmsg( tier, "adding tracker %s", announce );

    if( !tier->currentTracker )
        tierIncrementTracker( tier );
}


/***
****
***/

/**
 * @brief Opaque, per-torrent data structure for tracker announce information
 *
 * this opaque data structure can be found in tr_torrent.tiers
 */
typedef struct tr_torrent_tiers
{
    tr_ptrArray tiers;
    tr_publisher publisher;
}
tr_torrent_tiers;

static tr_torrent_tiers*
tiersNew( void )
{
    tr_torrent_tiers * tiers = tr_new0( tr_torrent_tiers, 1 );
    tiers->tiers = TR_PTR_ARRAY_INIT;
    tiers->publisher = TR_PUBLISHER_INIT;
    return tiers;
}

static void
tiersClear( tr_torrent_tiers * tiers )
{
    tr_ptrArrayDestruct( &tiers->tiers, tierFree );
    tiers->tiers = TR_PTR_ARRAY_INIT;
}

static void
tiersFree( tr_torrent_tiers * tiers )
{
    tr_publisherDestruct( &tiers->publisher );
    tr_ptrArrayDestruct( &tiers->tiers, tierFree );
    tr_free( tiers );
}

static tr_tier*
getTier( tr_announcer * announcer, int torrentId, int tierId )
{
    tr_tier * tier = NULL;

    if( announcer )
    {
        tr_torrent * tor = tr_torrentFindFromId( announcer->session, torrentId );

        if( tor && tor->tiers )
        {
            int i;
            tr_ptrArray * tiers = &tor->tiers->tiers;
            const int n = tr_ptrArraySize( tiers );
            for( i=0; !tier && i<n; ++i )
            {
                tr_tier * tmp = tr_ptrArrayNth( tiers, i );
                if( tmp->key == tierId )
                    tier = tmp;
            }
        }
    }

    return tier;
}

/***
****  PUBLISH
***/

static const tr_tracker_event emptyEvent = { 0, NULL, NULL, 0, 0 };

static void
publishMessage( tr_tier * tier, const char * msg, int type )
{
    if( tier && tier->tor && tier->tor->tiers )
    {
        tr_torrent_tiers * tiers = tier->tor->tiers;
        tr_tracker_event event = emptyEvent;
        event.messageType = type;
        event.text = msg;
        tr_publisherPublish( &tiers->publisher, tier, &event );
    }
}

static void
publishErrorClear( tr_tier * tier )
{
    publishMessage( tier, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishErrorMessageAndStop( tr_tier * tier, const char * msg )
{
    tier->isRunning = FALSE;

    publishMessage( tier, msg, TR_TRACKER_ERROR );
}

static void
publishWarning( tr_tier * tier, const char * msg )
{
    publishMessage( tier, msg, TR_TRACKER_WARNING );
}

static int
publishNewPeers( tr_tier * tier, tr_bool allAreSeeds,
                 const void * compact, int compactLen )
{
    tr_tracker_event e = emptyEvent;

    e.messageType = TR_TRACKER_PEERS;
    e.allAreSeeds = allAreSeeds;
    e.compact = compact;
    e.compactLen = compactLen;

    if( compactLen )
        tr_publisherPublish( &tier->tor->tiers->publisher, tier, &e );

    return compactLen / 6;
}

static int
publishNewPeersCompact( tr_tier * tier, tr_bool allAreSeeds,
                        const void * compact, int compactLen )
{
    int i;
    const uint8_t *compactWalk;
    uint8_t *array, *walk;
    const int peerCount = compactLen / 6;
    const int arrayLen = peerCount * ( sizeof( tr_address ) + 2 );
    tr_address addr;
    tr_port port;

    addr.type = TR_AF_INET;
    memset( &addr.addr, 0, sizeof( addr.addr ) );
    array = tr_new( uint8_t, arrayLen );
    for ( i=0, walk=array, compactWalk=compact ; i<peerCount ; ++i )
    {
        memcpy( &addr.addr.addr4, compactWalk, 4 );
        memcpy( &port, compactWalk + 4, 2 );

        memcpy( walk, &addr, sizeof( addr ) );
        memcpy( walk + sizeof( addr ), &port, 2 );

        walk += sizeof( tr_address ) + 2;
        compactWalk += 6;
    }

    publishNewPeers( tier, allAreSeeds, array, arrayLen );

    tr_free( array );

    return peerCount;
}

static int
publishNewPeersCompact6( tr_tier * tier, tr_bool allAreSeeds,
                         const void * compact, int compactLen )
{
    int i;
    const uint8_t *compactWalk;
    uint8_t *array, *walk;
    const int peerCount = compactLen / 18;
    const int arrayLen = peerCount * ( sizeof( tr_address ) + 2 );
    tr_address addr;
    tr_port port;

    addr.type = TR_AF_INET6;
    memset( &addr.addr, 0, sizeof( addr.addr ) );
    array = tr_new( uint8_t, arrayLen );
    for ( i = 0, walk = array, compactWalk = compact ; i < peerCount ; ++i )
    {
        memcpy( &addr.addr.addr6, compactWalk, 16 );
        memcpy( &port, compactWalk + 16, 2 );
        compactWalk += 18;

        memcpy( walk, &addr, sizeof( addr ) );
        memcpy( walk + sizeof( addr ), &port, 2 );
        walk += sizeof( tr_address ) + 2;
    }
    publishNewPeers( tier, allAreSeeds, array, arrayLen );
    tr_free( array );

    return peerCount;
}

static char*
createAnnounceURL( const tr_announcer     * announcer,
                   const tr_torrent       * torrent,
                   const tr_tier          * tier,
                   const char             * eventName )
{
    const int isStopping = !strcmp( eventName, "stopped" );
    const int numwant = isStopping ? 0 : NUMWANT;
    const tr_tracker_item  * tracker = tier->currentTracker;
    const char * ann = tracker->announce;
    struct evbuffer * buf = evbuffer_new( );
    char * ret;
    const unsigned char * ipv6;

    evbuffer_add_printf( buf, "%s"
                              "%c"
                              "info_hash=%s"
                              "&peer_id=%s"
                              "&port=%d"
                              "&uploaded=%" PRIu64
                              "&downloaded=%" PRIu64
                              "&left=%" PRIu64
                              "&numwant=%d"
                              "&key=%s"
                              "&compact=1"
                              "&supportcrypto=1",
                              ann,
                              strchr( ann, '?' ) ? '&' : '?',
                              torrent->info.hashEscaped,
                              torrent->peer_id,
                              (int)tr_sessionGetPeerPort( announcer->session ),
                              torrent->uploadedCur,
                              torrent->downloadedCur,
                              tr_cpLeftUntilComplete( &torrent->completion ),
                              numwant,
                              tracker->key_param );

    if( torrent->corruptCur )
        evbuffer_add_printf( buf, "&corrupt=%" PRIu64, torrent->corruptCur );

    if( !isStopping )
        evbuffer_add_printf( buf, "&upload_only=%d", tr_torrentIsSeed( torrent ) ? 1 : 0 );

    if( announcer->session->encryptionMode == TR_ENCRYPTION_REQUIRED )
        evbuffer_add_printf( buf, "&requirecrypto=1" );

    if( eventName && *eventName )
        evbuffer_add_printf( buf, "&event=%s", eventName );

    if( tracker->tracker_id && *tracker->tracker_id )
        evbuffer_add_printf( buf, "&trackerid=%s", tracker->tracker_id );

    /* There are two incompatible techniques for announcing an IPv6 address.
       BEP-7 suggests adding an "ipv6=" parameter to the announce URL,
       while OpenTracker requires that peers announce twice, once over IPv4
       and once over IPv6.

       To be safe, we should do both: add the "ipv6=" parameter and
       announce twice.  At any rate, we're already computing our IPv6
       address (for the LTEP handshake), so this comes for free. */

    ipv6 = tr_globalIPv6( );
    if( ipv6 ) {
        char ipv6_readable[INET6_ADDRSTRLEN];
        inet_ntop( AF_INET6, ipv6, ipv6_readable, INET6_ADDRSTRLEN );
        evbuffer_add_printf( buf, "&ipv6=");
        tr_http_escape( buf, ipv6_readable, strlen(ipv6_readable), TRUE );
    }

    ret = tr_strndup( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );
    evbuffer_free( buf );

    dbgmsg( tier, "announce URL is \"%s\"", ret );
    return ret;
}


/***
****
***/

static tr_bool
announceURLIsSupported( const char * announce )
{
    return ( announce != NULL )
        && ( strstr( announce, "tracker.thepiratebay.org" ) == NULL ) /* dead */
        && ( ( strstr( announce, "http://" ) == announce ) ||
             ( strstr( announce, "https://" ) == announce ) );
}

static void
addTorrentToTier( tr_announcer * announcer, tr_torrent_tiers * tiers, tr_torrent * tor )
{
    int i, n;
    const tr_tracker_info ** infos;
    const int trackerCount = tor->info.trackerCount;
    const tr_tracker_info  * trackers = tor->info.trackers;

    /* get the trackers that we support... */
    infos = tr_new0( const tr_tracker_info*, trackerCount );
    for( i=n=0; i<trackerCount; ++i )
        if( announceURLIsSupported( trackers[i].announce ) )
            infos[n++] = &trackers[i];

    /* build our private table of tiers... */
    if( n > 0 )
    {
        int tierIndex = -1;
        tr_tier * tier = NULL;

        for( i=0; i<n; ++i )
        {
            const tr_tracker_info * info = infos[i];

            if( info->tier != tierIndex )
                tier = NULL;

            tierIndex = info->tier;

            if( tier == NULL ) {
                tier = tierNew( tor );
                dbgmsg( tier, "adding tier" );
                tr_ptrArrayAppend( &tiers->tiers, tier );
            }

            tierAddTracker( announcer, tier, info->announce, info->scrape, info->id );
        }
    }

    tr_free( infos );
}

tr_torrent_tiers *
tr_announcerAddTorrent( tr_announcer * announcer, tr_torrent * tor )
{
    tr_torrent_tiers * tiers;

    assert( announcer != NULL );
    assert( tr_isTorrent( tor ) );

    tiers = tiersNew( );

    addTorrentToTier( announcer, tiers, tor );

    return tiers;
}

void
tr_announcerResetTorrent( tr_announcer * announcer, tr_torrent * tor )
{
    if( tor->tiers != NULL )
    {
        tiersClear( tor->tiers );

        addTorrentToTier( announcer, tor->tiers, tor );

        if( tor->isRunning )
            tr_announcerTorrentStarted( tor );
    }
}

tr_publisher_tag
tr_announcerSubscribe( struct tr_torrent_tiers   * tiers,
                       tr_delivery_func            func,
                       void                      * userData )
{
    return tr_publisherSubscribe( &tiers->publisher, func, userData );
}

void
tr_announcerUnsubscribe( struct tr_torrent_tiers  * tiers,
                         tr_publisher_tag           tag )
{
    if( tiers )
        tr_publisherUnsubscribe( &tiers->publisher, tag );
}

static tr_bool
tierCanManualAnnounce( const tr_tier * tier )
{
    return tier->isRunning
        && tier->manualAnnounceAllowedAt <= tr_time( );
}

tr_bool
tr_announcerCanManualAnnounce( const tr_torrent * tor )
{
    int i;
    int n;
    const tr_tier ** tiers;

    assert( tr_isTorrent( tor ) );
    assert( tor->tiers != NULL );

    n = tr_ptrArraySize( &tor->tiers->tiers );
    tiers = (const tr_tier**) tr_ptrArrayBase( &tor->tiers->tiers );
    for( i=0; i<n; ++i )
        if( tierCanManualAnnounce( tiers[i] ) )
            return TRUE;

    return FALSE;
}

time_t
tr_announcerNextManualAnnounce( const tr_torrent * tor )
{
    int i;
    int n;
    const tr_torrent_tiers * tiers;
    time_t ret = ~(time_t)0;

    assert( tr_isTorrent( tor  ) );

    tiers = tor->tiers;
    n = tr_ptrArraySize( &tiers->tiers );
    for( i=0; i<n; ++i ) {
        tr_tier * tier = tr_ptrArrayNth( (tr_ptrArray*)&tiers->tiers, i );
        if( tier->isRunning )
            ret = MIN( ret, tier->manualAnnounceAllowedAt );
    }

    return ret;
}

static void
tierClearNextAnnounce( tr_tier * tier )
{
    tier->announceAt = 0;
    tier->announceEvent = NULL;
    dbgmsg( tier, "cleared out announceEvent -- no announces pending." );
}

static void
tierSetNextAnnounce( tr_tier * tier, const char * announceEvent, time_t announceAt )
{
    assert( tier != NULL );
    assert( announceEvent != NULL );

    if( !strcmp( announceEvent, "manual" ) && !tierCanManualAnnounce( tier ) )
        return;

    tier->announceAt = announceAt;
    tier->announceEvent = announceEvent;
    dbgmsg( tier, "next announce event is \"%s\" in %d seconds\n", announceEvent, (int)difftime(announceAt,time(NULL)) );
}

static void
torrentSetNextAnnounce( tr_torrent * tor, const char * announceEvent, time_t announceAt )
{
    int i;
    int n;
    tr_torrent_tiers * tiers;

    assert( tr_isTorrent( tor ) );

    tiers = tor->tiers;
    n = tr_ptrArraySize( &tiers->tiers );
    for( i=0; i<n; ++i )
        tierSetNextAnnounce( tr_ptrArrayNth( &tiers->tiers, i ), announceEvent, announceAt );
}

void
tr_announcerManualAnnounce( tr_torrent * tor )
{
    torrentSetNextAnnounce( tor, "manual", tr_time( ) );
}
void
tr_announcerTorrentStarted( tr_torrent * tor )
{
    torrentSetNextAnnounce( tor, "started", tr_time( ) );
}
void
tr_announcerTorrentStopped( tr_torrent * tor )
{
    torrentSetNextAnnounce( tor, "stopped", tr_time( ) );
}
void
tr_announcerTorrentCompleted( tr_torrent * tor )
{
    torrentSetNextAnnounce( tor, "completed", tr_time( ) );
}
void
tr_announcerChangeMyPort( tr_torrent * tor )
{
    tr_announcerTorrentStarted( tor );
}


void
tr_announcerRemoveTorrent( tr_announcer * announcer, tr_torrent * tor )
{
    assert( tr_isTorrent( tor ) );

    if( tor->tiers )
    {
        int i;
        const int n = tr_ptrArraySize( &tor->tiers->tiers );
        for( i=0; i<n; ++i )
        {
            tr_tier * tier = tr_ptrArrayNth( &tor->tiers->tiers, i );

            if( tier->isRunning )
            {
                struct stop_message * s = tr_new0( struct stop_message, 1 );
                s->up = tor->uploadedCur;
                s->down = tor->downloadedCur;
                s->url = createAnnounceURL( announcer, tor, tier, "stopped" );
                s->host = tier->currentTracker->host;
                tr_ptrArrayInsertSorted( &announcer->stops, s, compareStops );
            }
        }

        tiersFree( tor->tiers );
        tor->tiers = NULL;
    }
}

/* return true if (1) we've tried it recently AND (2) it didn't respond... */
static tr_bool
hostIsNotResponding( const tr_host * host, const time_t now )
{
    tr_bool b = ( host->lastRequestTime )
             && ( host->lastRequestTime >= ( now - SLOW_HOST_PENALTY_SECS ) )
             && ( host->lastResponseInterval > MAX_TRACKER_RESPONSE_TIME_SECS );
    return b;
}

static tr_bool
tierIsNotResponding( const tr_tier * tier, const time_t now )
{
    return !tier->currentTracker
        || hostIsNotResponding( tier->currentTracker->host, now );
}

static int
compareTiers( const void * va, const void * vb )
{
    int ret = 0;
    tr_bool af, bf;
    const tr_tier * a = *(const tr_tier**)va;
    const tr_tier * b = *(const tr_tier**)vb;

    /* working domains come before non-working */
    if( !ret ) {
        const time_t now = tr_time( );
        af = tierIsNotResponding( a, now );
        bf = tierIsNotResponding( b, now );
        if( af != bf )
            ret = !af ? -1 : 1;
    }

    /* stops come before starts */
    if( !ret ) {
        af = a->tor->isRunning;
        bf = b->tor->isRunning;
        if( af != bf )
            ret = af ? 1 : -1;
    }

    /* upload comes before download */
    if( !ret )
        ret = compareTransfer( a->tor->uploadedCur, a->tor->downloadedCur,
                               b->tor->uploadedCur, b->tor->downloadedCur );

    /* incomplete comes before complete */
    if( !ret ) {
        af = a->tor->completeness == TR_LEECH;
        bf = b->tor->completeness == TR_LEECH;
        if( af != bf )
            return af ? -1 : 1;
    }

    /* private before public */
    if( !ret ) {
        af = tr_torrentIsPrivate( a->tor );
        bf = tr_torrentIsPrivate( b->tor );
        if( af != bf )
            ret = af ? -1 : 1;
    }

    return ret;
}

static uint8_t *
parseOldPeers( tr_benc * bePeers, size_t *  byteCount )
{
    int       i;
    uint8_t * array, *walk;
    const int peerCount = bePeers->val.l.count;

    assert( tr_bencIsList( bePeers ) );

    array = tr_new( uint8_t, peerCount * ( sizeof( tr_address ) + 2 ) );

    for( i = 0, walk = array; i < peerCount; ++i )
    {
        const char * s;
        int64_t      itmp;
        tr_address   addr;
        tr_port      port;
        tr_benc    * peer = &bePeers->val.l.vals[i];

        if( tr_bencDictFindStr( peer, "ip", &s ) )
            if( tr_pton( s, &addr ) == NULL )
                continue;

        if( !tr_bencDictFindInt( peer, "port", &itmp )
                || itmp < 0
                || itmp > USHRT_MAX )
            continue;

        memcpy( walk, &addr, sizeof( tr_address ) );
        port = htons( itmp );
        memcpy( walk + sizeof( tr_address ), &port, 2 );
        walk += sizeof( tr_address ) + 2;
    }

    *byteCount = peerCount * sizeof( tr_address ) + 2;
    return array;
}

static tr_bool
parseAnnounceResponse( tr_tier     * tier,
                       const char  * response,
                       size_t        responseLen,
                       tr_bool     * gotScrape )
{
    tr_benc benc;
    tr_bool success = FALSE;
    int scrapeFields = 0;
    const int bencLoaded = !tr_bencLoad( response, responseLen, &benc, NULL );

    dbgmsg( tier, "response len: %d, isBenc: %d", (int)responseLen, (int)bencLoaded );
    publishErrorClear( tier );
    if( bencLoaded && tr_bencIsDict( &benc ) )
    {
        int incomplete = -1;
        size_t rawlen;
        int64_t i;
        tr_benc * tmp;
        const char * str;
        const uint8_t * raw;

        success = TRUE;

        tier->retryAnnounceIntervalSec = FIRST_ANNOUNCE_RETRY_INTERVAL_SEC;

        if( tr_bencDictFindStr( &benc, "failure reason", &str ) )
        {
            tr_strlcpy( tier->lastAnnounceStr, str,
                        sizeof( tier->lastAnnounceStr ) );
            dbgmsg( tier, "tracker gave \"%s\"", str );
            publishMessage( tier, str, TR_TRACKER_ERROR );
            success = FALSE;
        }

        if( tr_bencDictFindStr( &benc, "warning message", &str ) )
        {
            tr_strlcpy( tier->lastAnnounceStr, str,
                        sizeof( tier->lastAnnounceStr ) );
            dbgmsg( tier, "tracker gave \"%s\"", str );
            publishWarning( tier, str );
        }

        if( tr_bencDictFindInt( &benc, "interval", &i ) )
        {
            dbgmsg( tier, "setting interval to %d", (int)i );
            tier->announceIntervalSec = i;
        }

        if( tr_bencDictFindInt( &benc, "min interval", &i ) )
        {
            dbgmsg( tier, "setting min interval to %d", (int)i );
            tier->announceMinIntervalSec = i;
        }

        if( tr_bencDictFindStr( &benc, "tracker id", &str ) )
        {
            tier->currentTracker->tracker_id = tr_strdup( str );
        }

        if( tr_bencDictFindInt( &benc, "complete", &i ) )
        {
            ++scrapeFields;
            tier->currentTracker->seederCount = i;
        }

        if( tr_bencDictFindInt( &benc, "incomplete", &i ) )
        {
            ++scrapeFields;
            tier->currentTracker->leecherCount = incomplete = i;
        }

        if( tr_bencDictFindInt( &benc, "downloaded", &i ) )
        {
            ++scrapeFields;
            tier->currentTracker->downloadCount = i;
        }

        if( tr_bencDictFindRaw( &benc, "peers", &raw, &rawlen ) )
        {
            /* "compact" extension */
            const int allAreSeeds = incomplete == 0;
            tier->lastAnnouncePeerCount = publishNewPeersCompact( tier, allAreSeeds, raw, rawlen );
        }
        else if( tr_bencDictFindList( &benc, "peers", &tmp ) )
        {
            /* original version of peers */
            const tr_bool allAreSeeds = incomplete == 0;
            size_t byteCount = 0;
            uint8_t * array = parseOldPeers( tmp, &byteCount );
            tier->lastAnnouncePeerCount = publishNewPeers( tier, allAreSeeds, array, byteCount );
            tr_free( array );
        }

        if( tr_bencDictFindRaw( &benc, "peers6", &raw, &rawlen ) )
        {
            /* "compact" extension */
            const tr_bool allAreSeeds = incomplete == 0;
            tier->lastAnnouncePeerCount += publishNewPeersCompact6( tier, allAreSeeds, raw, rawlen );
        }

        if( tier->lastAnnounceStr[0] == '\0' )
            tr_strlcpy( tier->lastAnnounceStr, _( "Success" ),
                        sizeof( tier->lastAnnounceStr ) );
    }

    if( bencLoaded )
        tr_bencFree( &benc );

    *gotScrape = scrapeFields >= 2;

    return success;
}

struct announce_data
{
    int torrentId;
    int tierId;
    time_t timeSent;

    /** If the request succeeds, the value for tier's "isRunning" flag */
    tr_bool isRunningOnSuccess;
};

static void
onAnnounceDone( tr_session   * session,
                long           responseCode,
                const void   * response,
                size_t         responseLen,
                void         * vdata )
{
    tr_announcer * announcer = session->announcer;
    struct announce_data * data = vdata;
    tr_tier * tier = getTier( announcer, data->torrentId, data->tierId );
    tr_bool gotScrape = FALSE;
    tr_bool success = FALSE;
    const time_t now = time ( NULL );

    if( announcer && tier )
    {
        tier->lastAnnouncePeerCount = 0;

        if( tier->currentTracker->host )
        {
            tr_host * host = tier->currentTracker->host;
            host->lastRequestTime = data->timeSent;
            host->lastResponseInterval = now - data->timeSent;
        }

        if( responseCode == HTTP_OK )
        {
            tier->lastAnnounceTime = now;
            success = parseAnnounceResponse( tier, response, responseLen, &gotScrape );
            dbgmsg( tier, "success is %d", success );
        }
        else if( responseCode )
        {
            /* %1$ld - http status code, such as 404
             * %2$s - human-readable explanation of the http status code */
            char * buf = tr_strdup_printf(
                _( "Announce failed: tracker gave HTTP Response Code %1$ld (%2$s)" ),
                responseCode,
                tr_webGetResponseStr( responseCode ) );

            tr_strlcpy( tier->lastAnnounceStr, buf,
                        sizeof( tier->lastAnnounceStr ) );

            tier->lastAnnounceTime = now;

            /* if the response is serious, *and* if the response may require
             * human intervention, then notify the user... otherwise just log it */
            if( responseCode >= 400 )
                if( tr_torrentIsPrivate( tier->tor ) || ( tier->tor->info.trackerCount < 2 ) )
                    publishWarning( tier, buf );
            tr_torinf( tier->tor, "%s", buf );
            dbgmsg( tier, "%s", buf );

            tr_free( buf );
        }
        else
        {
            tr_strlcpy( tier->lastAnnounceStr,
                        _( "tracker did not respond" ),
                        sizeof( tier->lastAnnounceStr ) );
            dbgmsg( tier, "%s", tier->lastAnnounceStr );
        }
    }

    if( tier )
    {
        tier->isAnnouncing = FALSE;

        if( responseCode == 0 )
        {
            const int interval = 120 + tr_cryptoWeakRandInt( 120 );
            dbgmsg( tier, "No response from tracker... retrying in %d seconds.", interval );
            tier->manualAnnounceAllowedAt = ~(time_t)0;
            tierSetNextAnnounce( tier, tier->announceEvent, now + interval );
        }
        else if( 200 <= responseCode && responseCode <= 299 )
        {
            const int interval = tier->announceIntervalSec;
            dbgmsg( tier, "request succeeded. reannouncing in %d seconds", interval );

            if( gotScrape )
            {
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = 1;
                tier->scrapeAt = now + tier->scrapeIntervalSec;
            }

            tier->manualAnnounceAllowedAt = now + tier->announceMinIntervalSec;

            if( strcmp( tier->announceEvent, "stopped" ) == 0 )
                tierClearNextAnnounce( tier );
            else
                tierSetNextAnnounce( tier, "", now + interval );
        }
        else if( 300 <= responseCode && responseCode <= 399 )
        {
            /* how did this get here?  libcurl handles this */
            const int interval = 5;
            dbgmsg( tier, "got a redirect. retrying in %d seconds", interval );
            tierSetNextAnnounce( tier, tier->announceEvent, now + interval );
            tier->manualAnnounceAllowedAt = now + tier->announceMinIntervalSec;
        }
        else if( 400 <= responseCode && responseCode <= 499 )
        {
            /* The request could not be understood by the server due to
             * malformed syntax. The client SHOULD NOT repeat the
             * request without modifications. */
            if( tr_torrentIsPrivate( tier->tor ) || ( tier->tor->info.trackerCount < 2 ) )
                publishErrorMessageAndStop( tier, _( "Tracker returned a 4xx message" ) );
            tier->manualAnnounceAllowedAt = ~(time_t)0;
            tierClearNextAnnounce( tier );
        }
        else if( 500 <= responseCode && responseCode <= 599 )
        {
            /* Response status codes beginning with the digit "5" indicate
             * cases in which the server is aware that it has erred or is
             * incapable of performing the request.  So we pause a bit and
             * try again. */
            tier->manualAnnounceAllowedAt = ~(time_t)0;
            tierSetNextAnnounce( tier, tier->announceEvent, now + tier->retryAnnounceIntervalSec );
            tier->retryAnnounceIntervalSec *= 2;
        }
        else
        {
            /* WTF did we get?? */
            const int interval = 120;
            dbgmsg( tier, "Invalid response from tracker... retrying in two minutes." );
            tier->manualAnnounceAllowedAt = ~(time_t)0;
            tierSetNextAnnounce( tier, tier->announceEvent, now + interval );
        }

        tier->lastAnnounceSucceeded = success;

        if( success )
            tier->isRunning = data->isRunningOnSuccess;

        if( !success )
            tierIncrementTracker( tier );
    }

    if( announcer != NULL )
    {
        ++announcer->announceSlotsAvailable;
    }

    tr_free( data );
}

static const char *
getAnnounceEvent( const tr_tier * tier )
{
    const char * event;

    assert( tier != NULL );
    assert( tier->announceEvent != NULL );
    assert( tr_isTorrent( tier->tor ) );

    /* BEP 21: "In order to tell the tracker that a peer is a partial seed,
     * it MUST send an event=paused parameter in every announce while
     * it is a partial seed." */
    if( tr_cpGetStatus( &tier->tor->completion ) == TR_PARTIAL_SEED )
        return "paused";

    event = tier->announceEvent;

    if( !strcmp( event, "manual" ) )
        return "started";

    return event;
}

static void
tierAnnounce( tr_announcer * announcer, tr_tier * tier )
{
    char * url;
    struct announce_data * data;
    const tr_torrent * tor = tier->tor;
    const time_t now = tr_time( );

    assert( !tier->isAnnouncing );

    data = tr_new0( struct announce_data, 1 );
    data->torrentId = tr_torrentId( tor );
    data->tierId = tier->key;
    data->isRunningOnSuccess = tor->isRunning;
    data->timeSent = now;

    url = createAnnounceURL( announcer, tor, tier, getAnnounceEvent( tier ) );

    tier->isAnnouncing = TRUE;
    tier->lastAnnounceStartTime = now;
    --announcer->announceSlotsAvailable;
    tr_webRun( announcer->session, url, NULL, onAnnounceDone, data );

    tr_free( url );
}

static tr_bool
parseScrapeResponse( tr_tier     * tier,
                     const char  * response,
                     size_t        responseLen,
                     char        * result,
                     size_t        resultlen )
{
    tr_bool success = FALSE;
    tr_benc benc, *files;
    const int bencLoaded = !tr_bencLoad( response, responseLen, &benc, NULL );
    if( bencLoaded && tr_bencDictFindDict( &benc, "files", &files ) )
    {
        const char * key;
        tr_benc * val;
        int i = 0;
        while( tr_bencDictChild( files, i++, &key, &val ))
        {
            int64_t intVal;
            tr_benc * flags;

            if( memcmp( tier->tor->info.hash, key, SHA_DIGEST_LENGTH ) )
                continue;

            success = TRUE;
            publishErrorClear( tier );

            if( ( tr_bencDictFindInt( val, "complete", &intVal ) ) )
                tier->currentTracker->seederCount = intVal;

            if( ( tr_bencDictFindInt( val, "incomplete", &intVal ) ) )
                tier->currentTracker->leecherCount = intVal;

            if( ( tr_bencDictFindInt( val, "downloaded", &intVal ) ) )
                tier->currentTracker->downloadCount = intVal;

            if( ( tr_bencDictFindInt( val, "downloaders", &intVal ) ) )
                tier->currentTracker->downloaderCount = intVal;

            if( tr_bencDictFindDict( val, "flags", &flags ) )
                if( ( tr_bencDictFindInt( flags, "min_request_interval", &intVal ) ) )
                    tier->scrapeIntervalSec = intVal;

            /* as per ticket #1045, safeguard against trackers returning
             * a very low min_request_interval... */
            if( tier->scrapeIntervalSec < DEFAULT_SCRAPE_INTERVAL_SEC )
                tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;

            tr_tordbg( tier->tor,
                       "Scrape successful. Rescraping in %d seconds.",
                       tier->scrapeIntervalSec );

            tier->retryScrapeIntervalSec = FIRST_SCRAPE_RETRY_INTERVAL_SEC;
        }
    }

    if( bencLoaded )
        tr_bencFree( &benc );

    if( success )
        tr_strlcpy( result, _( "Success" ), resultlen );
    else
        tr_strlcpy( result, _( "Error parsing response" ), resultlen );

    return success;
}

static void
onScrapeDone( tr_session   * session,
              long           responseCode,
              const void   * response,
              size_t         responseLen,
              void         * vdata )
{
    tr_bool success = FALSE;
    tr_announcer * announcer = session->announcer;
    struct announce_data * data = vdata;
    tr_tier * tier = getTier( announcer, data->torrentId, data->tierId );
    const time_t now = tr_time( );

    if( announcer )
        ++announcer->scrapeSlotsAvailable;

    if( announcer && tier )
    {
        tier->isScraping = FALSE;
        tier->lastScrapeTime = now;

        if( tier->currentTracker->host )
        {
            tr_host * host = tier->currentTracker->host;
            host->lastRequestTime = data->timeSent;
            host->lastResponseInterval = now - data->timeSent;
        }

        if( 200 <= responseCode && responseCode <= 299 )
        {
            const int interval = tier->scrapeIntervalSec;
            tier->scrapeAt = now + interval;

            if( responseCode == HTTP_OK )
                success = parseScrapeResponse( tier, response, responseLen,
                                               tier->lastScrapeStr, sizeof( tier->lastScrapeStr ) );
            else
                tr_snprintf( tier->lastScrapeStr, sizeof( tier->lastScrapeStr ),
                             _( "tracker gave HTTP Response Code %1$ld (%2$s)" ),
                             responseCode, tr_webGetResponseStr( responseCode ) );
            tr_tordbg( tier->tor, "%s", tier->lastScrapeStr );
        }
        else if( 300 <= responseCode && responseCode <= 399 )
        {
            /* this shouldn't happen; libcurl should handle this */
            const int interval = 5;
            tier->scrapeAt = now + interval;
            tr_snprintf( tier->lastScrapeStr, sizeof( tier->lastScrapeStr ),
                         "Got a redirect. Retrying in %d seconds", interval );
            tr_tordbg( tier->tor, "%s", tier->lastScrapeStr );
        }
        else
        {
            const int interval = tier->retryScrapeIntervalSec;
            tier->retryScrapeIntervalSec *= 2;

            /* Don't retry on a 4xx.
             * Retry at growing intervals on a 5xx */
            if( 400 <= responseCode && responseCode <= 499 )
                tier->scrapeAt = 0;
            else
                tier->scrapeAt = now + interval;

            /* %1$ld - http status code, such as 404
             * %2$s - human-readable explanation of the http status code */
            if( !responseCode )
                tr_strlcpy( tier->lastScrapeStr, _( "tracker did not respond" ),
                            sizeof( tier->lastScrapeStr ) );
            else
                tr_snprintf( tier->lastScrapeStr, sizeof( tier->lastScrapeStr ),
                             _( "tracker gave HTTP Response Code %1$ld (%2$s)" ),
                             responseCode, tr_webGetResponseStr( responseCode ) );
        }

        tier->lastScrapeSucceeded = success;
    }

    tr_free( data );
}

static void
tierScrape( tr_announcer * announcer, tr_tier * tier )
{
    char * url;
    const char * scrape;
    struct announce_data * data;
    const time_t now = tr_time( );

    assert( tier );
    assert( !tier->isScraping );
    assert( tier->currentTracker != NULL );
    assert( tr_isTorrent( tier->tor ) );

    data = tr_new0( struct announce_data, 1 );
    data->torrentId = tr_torrentId( tier->tor );
    data->tierId = tier->key;

    scrape = tier->currentTracker->scrape;

    url = tr_strdup_printf( "%s%cinfo_hash=%s",
                            scrape,
                            strchr( scrape, '?' ) ? '&' : '?',
                            tier->tor->info.hashEscaped );

    tier->isScraping = TRUE;
    tier->lastScrapeStartTime = now;
    --announcer->scrapeSlotsAvailable;
    dbgmsg( tier, "scraping \"%s\"", url );
    tr_webRun( announcer->session, url, NULL, onScrapeDone, data );

    tr_free( url );
}

static void
flushCloseMessages( tr_announcer * announcer )
{
    int i;
    const int n = tr_ptrArraySize( &announcer->stops );

    for( i=0; i<n; ++i )
    {
        struct stop_message * stop = tr_ptrArrayNth( &announcer->stops, i );
        tr_webRun( announcer->session, stop->url, NULL, NULL, NULL );
        stopFree( stop );
    }

    tr_ptrArrayClear( &announcer->stops );
}

static tr_bool
tierNeedsToAnnounce( const tr_tier * tier, const time_t now )
{
    return !tier->isAnnouncing
        && !tier->isScraping
        && ( tier->announceEvent != NULL )
        && ( tier->announceAt <= now );
}

static tr_bool
tierNeedsToScrape( const tr_tier * tier, const time_t now )
{
    return !tier->isScraping
        && ( tier->scrapeAt != 0 )
        && ( tier->scrapeAt <= now )
        && ( tier->currentTracker != NULL )
        && ( tier->currentTracker->scrape != NULL );
}

static void
announceMore( tr_announcer * announcer )
{
    const tr_bool canAnnounce = announcer->announceSlotsAvailable > 0;
    const tr_bool canScrape = announcer->scrapeSlotsAvailable > 0;
    tr_torrent * tor = NULL;
    const time_t now = tr_time( );

    if( announcer->announceSlotsAvailable > 0 )
    {
        int i;
        int n;
        tr_ptrArray announceMe = TR_PTR_ARRAY_INIT;
        tr_ptrArray scrapeMe = TR_PTR_ARRAY_INIT;

        /* build a list of tiers that need to be announced */
        while(( tor = tr_torrentNext( announcer->session, tor ))) {
            if( tor->tiers ) {
                n = tr_ptrArraySize( &tor->tiers->tiers );
                for( i=0; i<n; ++i ) {
                    tr_tier * tier = tr_ptrArrayNth( &tor->tiers->tiers, i );
                    if( canAnnounce && tierNeedsToAnnounce( tier, now ) )
                        tr_ptrArrayAppend( &announceMe, tier );
                    else if( canScrape && tierNeedsToScrape( tier, now ) )
                        tr_ptrArrayAppend( &scrapeMe, tier );
                }
            }
        }

        /* if there are more tiers than slots available, prioritize */
        n = tr_ptrArraySize( &announceMe );
        if( n > announcer->announceSlotsAvailable )
            qsort( tr_ptrArrayBase( &announceMe ), n, sizeof( tr_tier * ), compareTiers );

        /* announce some */
        n = MIN( tr_ptrArraySize( &announceMe ), announcer->announceSlotsAvailable );
        for( i=0; i<n; ++i ) {
            tr_tier * tier = tr_ptrArrayNth( &announceMe, i );
            dbgmsg( tier, "announcing tier %d of %d", i, n );
            tierAnnounce( announcer, tier );
        }


        /* scrape some */
        n = MIN( tr_ptrArraySize( &scrapeMe ), announcer->scrapeSlotsAvailable );
        for( i=0; i<n; ++i ) {
            tr_tier * tier = tr_ptrArrayNth( &scrapeMe, i );
            dbgmsg( tier, "scraping tier %d of %d", (i+1), n );
            tierScrape( announcer, tier );
        }

#if 0
char timebuf[64];
tr_getLogTimeStr( timebuf, 64 );
fprintf( stderr, "[%s] announce.c has %d requests ready to send (announce: %d, scrape: %d)\n", timebuf, (int)(tr_ptrArraySize(&announceMe)+tr_ptrArraySize(&scrapeMe)), (int)tr_ptrArraySize(&announceMe), (int)tr_ptrArraySize(&scrapeMe) );
#endif

        /* cleanup */
        tr_ptrArrayDestruct( &scrapeMe, NULL );
        tr_ptrArrayDestruct( &announceMe, NULL );
    }

    tor = NULL;
    while(( tor = tr_torrentNext( announcer->session, tor ))) {
        if( tor->dhtAnnounceAt <= now ) {
            if( tor->isRunning && tr_torrentAllowsDHT(tor) ) {
                int rc;
                rc = tr_dhtAnnounce(tor, AF_INET, 1);
                if(rc == 0)
                    /* The DHT is not ready yet.  Try again soon. */
                    tor->dhtAnnounceAt = now + 5 + tr_cryptoWeakRandInt( 5 );
                else
                    /* We should announce at least once every 30 minutes. */
                    tor->dhtAnnounceAt =
                        now + 25 * 60 + tr_cryptoWeakRandInt( 3 * 60 );
            }
        }

        if( tor->dhtAnnounce6At <= now ) {
            if( tor->isRunning && tr_torrentAllowsDHT(tor) ) {
                int rc;
                rc = tr_dhtAnnounce(tor, AF_INET6, 1);
                if(rc == 0)
                    tor->dhtAnnounce6At = now + 5 + tr_cryptoWeakRandInt( 5 );
                else
                    tor->dhtAnnounce6At =
                        now + 25 * 60 + tr_cryptoWeakRandInt( 3 * 60 );
            }
        }
    }
}

static void
onUpkeepTimer( int foo UNUSED, short bar UNUSED, void * vannouncer )
{
    tr_announcer * announcer = vannouncer;
    tr_sessionLock( announcer->session );

    /* maybe send out some "stopped" messages for closed torrents */
    flushCloseMessages( announcer );

    /* maybe send out some announcements to trackers */
    announceMore( announcer );

    /* set up the next timer */
    tr_timerAdd( announcer->upkeepTimer, UPKEEP_INTERVAL_SECS, 0 );

    tr_sessionUnlock( announcer->session );
}

/***
****
***/

tr_tracker_stat *
tr_announcerStats( const tr_torrent * torrent,
                   int              * setmeTrackerCount )
{
    int i;
    int n;
    int out = 0;
    int tierCount;
    tr_tracker_stat * ret;
    const time_t now = tr_time( );

    assert( tr_isTorrent( torrent ) );

    /* count the trackers... */
    for( i=n=0, tierCount=tr_ptrArraySize( &torrent->tiers->tiers ); i<tierCount; ++i ) {
        const tr_tier * tier = tr_ptrArrayNth( &torrent->tiers->tiers, i );
        n += tr_ptrArraySize( &tier->trackers );
    }

    /* alloc the stats */
    *setmeTrackerCount = n;
    ret = tr_new0( tr_tracker_stat, n );

    /* populate the stats */
    for( i=0, tierCount=tr_ptrArraySize( &torrent->tiers->tiers ); i<tierCount; ++i )
    {
        int j;
        const tr_tier * tier = tr_ptrArrayNth( &torrent->tiers->tiers, i );
        n = tr_ptrArraySize( &tier->trackers );
        for( j=0; j<n; ++j )
        {
            const tr_tracker_item * tracker = tr_ptrArrayNth( (tr_ptrArray*)&tier->trackers, j );
            tr_tracker_stat * st = ret + out++;

            st->id = tracker->id;
            tr_strlcpy( st->host, tracker->host->name, sizeof( st->host ) );
            tr_strlcpy( st->announce, tracker->announce, sizeof( st->announce ) );
            st->tier = i + 1;
            st->isBackup = tracker != tier->currentTracker;
            st->lastScrapeStartTime = tier->lastScrapeStartTime;

            st->seederCount = tracker->seederCount;
            st->leecherCount = tracker->leecherCount;
            st->downloadCount = tracker->downloadCount;

            if( st->isBackup )
            {
                st->scrapeState = TR_TRACKER_INACTIVE;
                st->announceState = TR_TRACKER_INACTIVE;
                st->nextScrapeTime = 0;
                st->nextAnnounceTime = 0;
            }
            else
            {
                if(( st->hasScraped = tier->lastScrapeTime != 0 )) {
                    st->lastScrapeTime = tier->lastScrapeTime;
                    st->lastScrapeSucceeded = tier->lastScrapeSucceeded;
                    tr_strlcpy( st->lastScrapeResult, tier->lastScrapeStr, sizeof( st->lastScrapeResult ) );
                }

                if( tier->isScraping )
                    st->scrapeState = TR_TRACKER_ACTIVE;
                else if( !tier->scrapeAt )
                    st->scrapeState = TR_TRACKER_INACTIVE;
                else if( tier->scrapeAt > now )
                {
                    st->scrapeState = TR_TRACKER_WAITING;
                    st->nextScrapeTime = tier->scrapeAt;
                }
                else
                    st->scrapeState = TR_TRACKER_QUEUED;

                st->lastAnnounceStartTime = tier->lastAnnounceStartTime;

                if(( st->hasAnnounced = tier->lastAnnounceTime != 0 )) {
                    st->lastAnnounceTime = tier->lastAnnounceTime;
                    tr_strlcpy( st->lastAnnounceResult, tier->lastAnnounceStr, sizeof( st->lastAnnounceResult ) );
                    if(( st->lastAnnounceSucceeded = tier->lastAnnounceSucceeded )) {
                        st->lastAnnouncePeerCount = tier->lastAnnouncePeerCount;
                    }
                }

                if( tier->isAnnouncing )
                    st->announceState = TR_TRACKER_ACTIVE;
                else if( !torrent->isRunning || !tier->announceAt )
                    st->announceState = TR_TRACKER_INACTIVE;
                else if( tier->announceAt > now )
                {
                    st->announceState = TR_TRACKER_WAITING;
                    st->nextAnnounceTime = tier->announceAt;
                }
                else
                    st->announceState = TR_TRACKER_QUEUED;
            }
        }
    }

    return ret;
}

void
tr_announcerStatsFree( tr_tracker_stat * trackers,
                       int trackerCount UNUSED )
{
    tr_free( trackers );
}
