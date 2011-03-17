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

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h> /* qsort() */
#include <string.h> /* strcmp(), memcpy() */

#include <event2/buffer.h>
#include <event2/event.h> /* evtimer */

#define __LIBTRANSMISSION_ANNOUNCER_MODULE___

#include "transmission.h"
#include "announcer.h"
#include "announcer-common.h"
#include "crypto.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "ptrarray.h"
#include "session.h"
#include "tr-lpd.h"
#include "torrent.h"
#include "utils.h"

struct tr_tier;

static void tier_build_log_name( const struct tr_tier * tier,
                                 char * buf, size_t buflen );

#define dbgmsg( tier, ... ) \
if( tr_deepLoggingIsActive( ) ) do { \
  char name[128]; \
  tier_build_log_name( tier, name, sizeof( name ) ); \
  tr_deepLog( __FILE__, __LINE__, name, __VA_ARGS__ ); \
} while( 0 )

enum
{
    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = ( 60 * 30 ),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = ( 60 * 10 ),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = ( 60 * 2 ),

    /* how many web tasks we allow at one time */
    MAX_CONCURRENT_TASKS = 48,

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 80,

    UPKEEP_INTERVAL_SECS = 1,

    /* this is an upper limit for the frequency of LDS announces */
    LPD_HOUSEKEEPING_INTERVAL_SECS = 5,

    /* this is how often to call the UDP tracker upkeep */
    TAU_UPKEEP_INTERVAL_SECS = 5
};

/***
****
***/

const char*
tr_announce_event_get_string( tr_announce_event e )
{
    switch( e )
    {
        case TR_ANNOUNCE_EVENT_COMPLETED:  return "completed";
        case TR_ANNOUNCE_EVENT_STARTED:    return "started";
        case TR_ANNOUNCE_EVENT_STOPPED:    return "stopped";
        default:                           return "";
    }
}

/***
****
***/

static int
compareTransfer( uint64_t a_uploaded, uint64_t a_downloaded,
                 uint64_t b_uploaded, uint64_t b_downloaded )
{
    /* higher upload count goes first */
    if( a_uploaded != b_uploaded )
        return a_uploaded > b_uploaded ? -1 : 1;

    /* then higher download count goes first */
    if( a_downloaded != b_downloaded )
        return a_downloaded > b_downloaded ? -1 : 1;

    return 0;
}

static int
compareStops( const void * va, const void * vb )
{
    const tr_announce_request * a = va;
    const tr_announce_request * b = vb;
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
    tr_ptrArray stops; /* tr_announce_request */

    tr_session * session;
    struct event * upkeepTimer;
    int slotsAvailable;
    int key;
    time_t lpdUpkeepAt;
    time_t tauUpkeepAt;
}
tr_announcer;

tr_bool
tr_announcerHasBacklog( const struct tr_announcer * announcer )
{
    return announcer->slotsAvailable < 1;
}

static inline time_t
jitterize( const int val )
{
    const double jitter = 0.1;
    assert( val > 0 );
    return val + tr_cryptoWeakRandInt((int)(1 + val * jitter));
}

static void
onUpkeepTimer( int foo UNUSED, short bar UNUSED, void * vannouncer );

void
tr_announcerInit( tr_session * session )
{
    tr_announcer * a;

    assert( tr_isSession( session ) );

    a = tr_new0( tr_announcer, 1 );
    a->stops = TR_PTR_ARRAY_INIT;
    a->key = tr_cryptoRandInt( INT_MAX );
    a->session = session;
    a->slotsAvailable = MAX_CONCURRENT_TASKS;
    a->lpdUpkeepAt = tr_time() + jitterize(5);
    a->upkeepTimer = evtimer_new( session->event_base, onUpkeepTimer, a );
    tr_timerAdd( a->upkeepTimer, UPKEEP_INTERVAL_SECS, 0 );

    session->announcer = a;
}

static void flushCloseMessages( tr_announcer * announcer );

void
tr_announcerClose( tr_session * session )
{
    tr_announcer * announcer = session->announcer;

    flushCloseMessages( announcer );

    event_free( announcer->upkeepTimer );
    announcer->upkeepTimer = NULL;

    tr_ptrArrayDestruct( &announcer->stops, NULL );

    session->announcer = NULL;
    tr_free( announcer );
}

/***
****
***/

/* a row in tr_tier's list of trackers */
typedef struct
{
    char * key;
    char * announce;
    char * scrape;

    char * tracker_id_str;

    int seederCount;
    int leecherCount;
    int downloadCount;
    int downloaderCount;

    int consecutiveFailures;

    uint32_t id;
}
tr_tracker;

/* format: host+':'+ port */
static char *
getKey( const char * url )
{
    char * ret;
    char * scheme = NULL;
    char * host = NULL;
    int port = 0;

    tr_urlParse( url, -1, &scheme, &host, &port, NULL );
    ret = tr_strdup_printf( "%s://%s:%d", (scheme?scheme:"invalid"), (host?host:"invalid"), port );

    tr_free( host );
    tr_free( scheme );
    return ret;
}

static tr_tracker*
trackerNew( const char * announce, const char * scrape, uint32_t id )
{
    tr_tracker * tracker = tr_new0( tr_tracker, 1 );
    tracker->key = getKey( announce );
    tracker->announce = tr_strdup( announce );
    tracker->scrape = tr_strdup( scrape );
    tracker->id = id;
    tracker->seederCount = -1;
    tracker->leecherCount = -1;
    tracker->downloadCount = -1;
    return tracker;
}

static void
trackerFree( void * vtracker )
{
    tr_tracker * tracker = vtracker;

    tr_free( tracker->tracker_id_str );
    tr_free( tracker->scrape );
    tr_free( tracker->announce );
    tr_free( tracker->key );
    tr_free( tracker );
}

/***
****
***/

struct tr_torrent_tiers;

/** @brief A group of trackers in a single tier, as per the multitracker spec */
typedef struct tr_tier
{
    /* number of up/down/corrupt bytes since the last time we sent an
     * "event=stopped" message that was acknowledged by the tracker */
    uint64_t byteCounts[3];

    tr_ptrArray trackers; /* tr_tracker */
    tr_tracker * currentTracker;
    int currentTrackerIndex;

    tr_torrent * tor;

    time_t scrapeAt;
    time_t lastScrapeStartTime;
    time_t lastScrapeTime;
    tr_bool lastScrapeSucceeded;
    tr_bool lastScrapeTimedOut;

    time_t announceAt;
    time_t manualAnnounceAllowedAt;
    time_t lastAnnounceStartTime;
    time_t lastAnnounceTime;
    tr_bool lastAnnounceSucceeded;
    tr_bool lastAnnounceTimedOut;

    tr_announce_event * announce_events;
    int announce_event_count;
    int announce_event_alloc;

    /* unique lookup key */
    int key;

    int scrapeIntervalSec;
    int announceIntervalSec;
    int announceMinIntervalSec;

    int lastAnnouncePeerCount;

    tr_bool isRunning;
    tr_bool isAnnouncing;
    tr_bool isScraping;
    tr_bool wasCopied;

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
    t->announce_events = NULL;
    t->announce_event_count = 0;
    t->announce_event_alloc = 0;
    t->trackers = TR_PTR_ARRAY_INIT;
    t->currentTracker = NULL;
    t->currentTrackerIndex = -1;
    t->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    t->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    t->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    t->scrapeAt = now + tr_cryptoWeakRandInt( 60*3 );
    t->tor = tor;

    return t;
}

static void
tierFree( void * vtier )
{
    tr_tier * tier = vtier;
    tr_ptrArrayDestruct( &tier->trackers, trackerFree );
    tr_free( tier->announce_events );
    tr_free( tier );
}

static void
tier_build_log_name( const tr_tier * tier, char * buf, size_t buflen )
{
    tr_snprintf( buf, buflen, "[%s---%s]",
       ( tier && tier->tor ) ? tr_torrentName( tier->tor ) : "?",
       ( tier && tier->currentTracker ) ? tier->currentTracker->key : "?" );
}

static void
tierIncrementTracker( tr_tier * tier )
{
    /* move our index to the next tracker in the tier */
    const int i = ( tier->currentTracker == NULL )
                ? 0
                : ( tier->currentTrackerIndex + 1 ) % tr_ptrArraySize( &tier->trackers );
    tier->currentTracker = tr_ptrArrayNth( &tier->trackers, i );
    tier->currentTrackerIndex = i;

    /* reset some of the tier's fields */
    tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    tier->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    tier->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    tier->isAnnouncing = FALSE;
    tier->isScraping = FALSE;
    tier->lastAnnounceStartTime = 0;
    tier->lastScrapeStartTime = 0;
}

static void
tierAddTracker( tr_tier    * tier,
                const char * announce,
                const char * scrape,
                uint32_t     id )
{
    tr_tracker * tracker = trackerNew( announce, scrape, id );

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
    tr_ptrArray tiers; /* tr_tier */
    tr_tracker_callback * callback;
    void * callbackData;
}
tr_torrent_tiers;

static tr_torrent_tiers*
tiersNew( void )
{
    tr_torrent_tiers * tiers = tr_new0( tr_torrent_tiers, 1 );
    tiers->tiers = TR_PTR_ARRAY_INIT;
    return tiers;
}

static void
tiersFree( tr_torrent_tiers * tiers )
{
    tr_ptrArrayDestruct( &tiers->tiers, tierFree );
    tr_free( tiers );
}

static tr_tier*
getTier( tr_announcer * announcer, const uint8_t * info_hash, int tierId )
{
    tr_tier * tier = NULL;

    if( announcer != NULL )
    {
        tr_session * session = announcer->session;
        tr_torrent * tor = tr_torrentFindFromHash( session, info_hash );

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

static const tr_tracker_event TRACKER_EVENT_INIT = { 0, 0, 0, 0, 0, 0 };

static void
publishMessage( tr_tier * tier, const char * msg, int type )
{
    if( tier && tier->tor && tier->tor->tiers && tier->tor->tiers->callback )
    {
        tr_torrent_tiers * tiers = tier->tor->tiers;
        tr_tracker_event event = TRACKER_EVENT_INIT;
        event.messageType = type;
        event.text = msg;
        if( tier->currentTracker )
            event.tracker = tier->currentTracker->announce;

        tiers->callback( tier->tor, &event, tiers->callbackData );
    }
}

static void
publishErrorClear( tr_tier * tier )
{
    publishMessage( tier, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishWarning( tr_tier * tier, const char * msg )
{
    publishMessage( tier, msg, TR_TRACKER_WARNING );
}

static void
publishError( tr_tier * tier, const char * msg )
{
    publishMessage( tier, msg, TR_TRACKER_ERROR );
}

static int8_t
getSeedProbability( tr_tier * tier, int seeds, int leechers, int pex_count )
{
    /* special case optimization:
       ocelot omits seeds from peer lists sent to seeds on private trackers.
       so check for that case... */
    if( ( leechers == pex_count ) && tr_torrentIsPrivate( tier->tor )
                                  && tr_torrentIsSeed( tier->tor )
                                  && ( seeds + leechers < NUMWANT ) )
        return 0;

    if( !seeds )
        return 0;

    if( seeds>=0 && leechers>=0 )
        return (int8_t)((100.0*seeds)/(seeds+leechers));

    return -1; /* unknown */
}

static void
publishPeersPex( tr_tier * tier, int seeds, int leechers,
                 const tr_pex * pex, int n )
{
    if( tier->tor->tiers->callback )
    {
        tr_tracker_event e = TRACKER_EVENT_INIT;
        e.messageType = TR_TRACKER_PEERS;
        e.seedProbability = getSeedProbability( tier, seeds, leechers, n );
        e.pex = pex;
        e.pexCount = n;
        dbgmsg( tier, "got %d peers; seed prob %d", n, (int)e.seedProbability );

        tier->tor->tiers->callback( tier->tor, &e, NULL );
    }
}

/***
****
***/

struct ann_tracker_info
{
    tr_tracker_info info;

    char * scheme;
    char * host;
    char * path;
    int port;
};


/* primary key: tier
 * secondary key: udp comes before http */
static int
filter_trackers_compare_func( const void * va, const void * vb )
{
    const struct ann_tracker_info * a = va;
    const struct ann_tracker_info * b = vb;
    if( a->info.tier != b->info.tier )
        return a->info.tier - b->info.tier;
    return -strcmp( a->scheme, b->scheme );
}

/**
 * Massages the incoming list of trackers into something we can use.
 */
static tr_tracker_info *
filter_trackers( tr_tracker_info * input, int input_count, int * setme_count )
{
    int i, in;
    int j, jn;
    int n = 0;
    struct tr_tracker_info * ret;
    struct ann_tracker_info * tmp = tr_new0( struct ann_tracker_info, input_count );

    /*for( i=0, in=input_count; i<in; ++i ) fprintf( stderr, "IN: [%d][%s]\n", input[i].tier, input[i].announce );*/

    /* build a list of valid trackers */
    for( i=0, in=input_count; i<in; ++i ) {
        if( tr_urlIsValidTracker( input[i].announce ) ) {
            int port;
            char * scheme;
            char * host;
            char * path;
            tr_bool is_duplicate = FALSE;
            tr_urlParse( input[i].announce, -1, &scheme, &host, &port, &path );

            /* weed out one common source of duplicates:
             * "http://tracker/announce" +
             * "http://tracker:80/announce"
             */
            for( j=0, jn=n; !is_duplicate && j<jn; ++j )
                is_duplicate = (tmp[j].port==port)
                            && !strcmp(tmp[j].scheme,scheme)
                            && !strcmp(tmp[j].host,host)
                            && !strcmp(tmp[j].path,path);

            if( is_duplicate ) {
                tr_free( path );
                tr_free( host );
                tr_free( scheme );
                continue;
            }
            tmp[n].info = input[i];
            tmp[n].scheme = scheme;
            tmp[n].host = host;
            tmp[n].port = port;
            tmp[n].path = path;
            n++;
        }
    }

    /* if two announce URLs differ only by scheme, put them in the same tier.
     * (note: this can leave gaps in the `tier' values, but since the calling
     * function doesn't care, there's no point in removing the gaps...) */
    for( i=0, in=n; i<n; ++i )
        for( j=0, jn=n; j<n; ++j )
            if( (i!=j) && (tmp[i].port==tmp[j].port)
                       && !tr_strcmp0(tmp[i].host,tmp[j].host)
                       && !tr_strcmp0(tmp[i].path,tmp[j].path) )
                tmp[j].info.tier = tmp[i].info.tier;

    /* sort them, for two reasons:
     * (1) unjumble the tiers from the previous step
     * (2) move the UDP trackers to the front of each tier */
    qsort( tmp, n, sizeof(struct ann_tracker_info), filter_trackers_compare_func );

    /* build the output */
    *setme_count = n;
    ret = tr_new0( tr_tracker_info, n );
    for( i=0, in=n; i<in; ++i )
        ret[i] = tmp[i].info;

    /* cleanup */
    for( i=0, in=n; i<n; ++i ) {
        tr_free( tmp[i].path );
        tr_free( tmp[i].host );
        tr_free( tmp[i].scheme );
    }
    tr_free( tmp );

    /*for( i=0, in=n; i<in; ++i ) fprintf( stderr, "OUT: [%d][%s]\n", ret[i].tier, ret[i].announce );*/
    return ret;
}


static void
addTorrentToTier( tr_torrent_tiers * tiers, tr_torrent * tor )
{
    int i, n;
    tr_tracker_info * infos = filter_trackers( tor->info.trackers,
                                               tor->info.trackerCount, &n );

    /* build our internal table of tiers... */
    if( n > 0 )
    {
        int tierIndex = -1;
        tr_tier * tier = NULL;

        for( i=0; i<n; ++i )
        {
            const tr_tracker_info * info = &infos[i];

            if( info->tier != tierIndex )
                tier = NULL;

            tierIndex = info->tier;

            if( tier == NULL ) {
                tier = tierNew( tor );
                dbgmsg( tier, "adding tier" );
                tr_ptrArrayAppend( &tiers->tiers, tier );
            }

            tierAddTracker( tier, info->announce, info->scrape, info->id );
        }
    }

    tr_free( infos );
}

tr_torrent_tiers *
tr_announcerAddTorrent( tr_torrent           * tor,
                        tr_tracker_callback  * callback,
                        void                 * callbackData )
{
    tr_torrent_tiers * tiers;

    assert( tr_isTorrent( tor ) );

    tiers = tiersNew( );
    tiers->callback = callback;
    tiers->callbackData = callbackData;

    addTorrentToTier( tiers, tor );

    return tiers;
}

/***
****
***/

static tr_bool
tierCanManualAnnounce( const tr_tier * tier )
{
    return tier->manualAnnounceAllowedAt <= tr_time( );
}

tr_bool
tr_announcerCanManualAnnounce( const tr_torrent * tor )
{
    int i;
    int n;
    const tr_tier ** tiers;

    assert( tr_isTorrent( tor ) );
    assert( tor->tiers != NULL );

    if( !tor->isRunning )
        return FALSE;

    /* return true if any tier can manual announce */
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

    assert( tr_isTorrent( tor ) );

    /* find the earliest manual announce time from all peers */
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
dbgmsg_tier_announce_queue( const tr_tier * tier )
{
    if( tr_deepLoggingIsActive( ) )
    {
        int i;
        char * str;
        char name[128];
        struct evbuffer * buf = evbuffer_new( );

        tier_build_log_name( tier, name, sizeof( name ) );
        for( i=0; i<tier->announce_event_count; ++i )
        {
            const tr_announce_event e = tier->announce_events[i];
            const char * str = tr_announce_event_get_string( e );
            evbuffer_add_printf( buf, "[%d:%s]", i, str );
        }
        str = evbuffer_free_to_str( buf );
        tr_deepLog( __FILE__, __LINE__, name, "announce queue is %s", str );
        tr_free( str );
    }
}

static void
tier_announce_remove_trailing( tr_tier * tier, tr_announce_event e )
{
    while( ( tier->announce_event_count > 0 )
        && ( tier->announce_events[tier->announce_event_count-1] == e ) )
        --tier->announce_event_count;
}

static void
tier_announce_event_push( tr_tier            * tier,
                          tr_announce_event    e,
                          time_t               announceAt )
{
    int i;

    assert( tier != NULL );

    dbgmsg_tier_announce_queue( tier );
    dbgmsg( tier, "queued \"%s\"", tr_announce_event_get_string( e ) );

    if( tier->announce_event_count > 0 )
    {
        /* special case #1: if we're adding a "stopped" event,
         * dump everything leading up to it except "completed" */
        if( e == TR_ANNOUNCE_EVENT_STOPPED ) {
            tr_bool has_completed = FALSE;
            const tr_announce_event c = TR_ANNOUNCE_EVENT_COMPLETED;
            for( i=0; !has_completed && i<tier->announce_event_count; ++i )
                has_completed = c == tier->announce_events[i];
            tier->announce_event_count = 0;
            if( has_completed )
                tier->announce_events[tier->announce_event_count++] = c;
        }

        /* special case #2: dump all empty strings leading up to this event */
        tier_announce_remove_trailing( tier, TR_ANNOUNCE_EVENT_NONE );

        /* special case #3: no consecutive duplicates */
        tier_announce_remove_trailing( tier, e );
    }

    /* make room in the array for another event */
    if( tier->announce_event_alloc <= tier->announce_event_count ) {
        tier->announce_event_alloc += 4;
        tier->announce_events = tr_renew( tr_announce_event,
                                          tier->announce_events,
                                          tier->announce_event_alloc );
    }

    /* add it */
    tier->announce_events[tier->announce_event_count++] = e;
    tier->announceAt = announceAt;

    dbgmsg_tier_announce_queue( tier );
    dbgmsg( tier, "announcing in %d seconds", (int)difftime(announceAt,tr_time()) );
}

static tr_announce_event
tier_announce_event_pull( tr_tier * tier )
{
    const tr_announce_event e = tier->announce_events[0];

    tr_removeElementFromArray( tier->announce_events,
                               0, sizeof( tr_announce_event ),
                               tier->announce_event_count-- );

    return e;
}

static void
torrentAddAnnounce( tr_torrent * tor, tr_announce_event e, time_t announceAt )
{
    int i;
    int n;
    tr_torrent_tiers * tiers;

    assert( tr_isTorrent( tor ) );

    /* walk through each tier and tell them to announce */
    tiers = tor->tiers;
    n = tr_ptrArraySize( &tiers->tiers );
    for( i=0; i<n; ++i )
    {
        tr_tier * tier = tr_ptrArrayNth( &tiers->tiers, i );
        tier_announce_event_push( tier, e, announceAt );
    }
}

void
tr_announcerTorrentStarted( tr_torrent * tor )
{
    torrentAddAnnounce( tor, TR_ANNOUNCE_EVENT_STARTED, tr_time( ) );
}
void
tr_announcerManualAnnounce( tr_torrent * tor )
{
    torrentAddAnnounce( tor, TR_ANNOUNCE_EVENT_NONE, tr_time( ) );
}
void
tr_announcerTorrentStopped( tr_torrent * tor )
{
    torrentAddAnnounce( tor, TR_ANNOUNCE_EVENT_STOPPED, tr_time( ) );
}
void
tr_announcerTorrentCompleted( tr_torrent * tor )
{
    torrentAddAnnounce( tor, TR_ANNOUNCE_EVENT_COMPLETED, tr_time( ) );
}
void
tr_announcerChangeMyPort( tr_torrent * tor )
{
    tr_announcerTorrentStarted( tor );
}

/***
****
***/

void
tr_announcerAddBytes( tr_torrent * tor, int type, uint32_t byteCount )
{
    int i, n;
    tr_torrent_tiers * tiers;

    assert( tr_isTorrent( tor ) );
    assert( type==TR_ANN_UP || type==TR_ANN_DOWN || type==TR_ANN_CORRUPT );

    tiers = tor->tiers;
    n = tr_ptrArraySize( &tiers->tiers );
    for( i=0; i<n; ++i )
    {
        tr_tier * tier = tr_ptrArrayNth( &tiers->tiers, i );
        tier->byteCounts[ type ] += byteCount;
    }
}

/***
****
***/

static tr_announce_request *
announce_request_new( const tr_announcer  * announcer,
                      const tr_torrent    * tor,
                      const tr_tier       * tier,
                      tr_announce_event     event )
{
    tr_announce_request * req = tr_new0( tr_announce_request, 1 );
    req->port = tr_sessionGetPublicPeerPort( announcer->session );
    req->url = tr_strdup( tier->currentTracker->announce );
    req->tracker_id_str = tr_strdup( tier->currentTracker->tracker_id_str );
    memcpy( req->info_hash, tor->info.hash, SHA_DIGEST_LENGTH );
    memcpy( req->peer_id, tor->peer_id, PEER_ID_LEN );
    req->up = tier->byteCounts[TR_ANN_UP];
    req->down = tier->byteCounts[TR_ANN_DOWN];
    req->corrupt = tier->byteCounts[TR_ANN_CORRUPT];
    req->left = tr_cpLeftUntilComplete( &tor->completion ),
    req->event = event;
    req->numwant = event == TR_ANNOUNCE_EVENT_STOPPED ? 0 : NUMWANT;
    req->key = announcer->key;
    req->partial_seed = tr_cpGetStatus( &tor->completion ) == TR_PARTIAL_SEED;
    tier_build_log_name( tier, req->log_name, sizeof( req->log_name ) );
    return req;
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
                const tr_announce_event e = TR_ANNOUNCE_EVENT_STOPPED;
                tr_announce_request * req = announce_request_new( announcer, tor, tier, e );
                tr_ptrArrayInsertSorted( &announcer->stops, req, compareStops );
            }
        }

        tiersFree( tor->tiers );
        tor->tiers = NULL;
    }
}

static int
getRetryInterval( const tr_tracker * t )
{
    int minutes;
    const unsigned int jitter_seconds = tr_cryptoWeakRandInt( 60 );
    switch( t->consecutiveFailures ) {
        case 0:  minutes =   1; break;
        case 1:  minutes =   5; break;
        case 2:  minutes =  15; break;
        case 3:  minutes =  30; break;
        case 4:  minutes =  60; break;
        default: minutes = 120; break;
    }
    return ( minutes * 60 ) + jitter_seconds;
}

struct announce_data
{
    int tierId;
    time_t timeSent;
    tr_announce_event event;

    /** If the request succeeds, the value for tier's "isRunning" flag */
    tr_bool isRunningOnSuccess;
};

static void
on_announce_error( tr_tier * tier, const char * err, tr_announce_event e )
{
    int interval;

    /* increment the error count */
    if( tier->currentTracker != NULL )
        ++tier->currentTracker->consecutiveFailures;

    /* set the error message */
    dbgmsg( tier, "%s", err );
    tr_torinf( tier->tor, "%s", err );
    tr_strlcpy( tier->lastAnnounceStr, err, sizeof( tier->lastAnnounceStr ) );

    /* switch to the next tracker */
    tierIncrementTracker( tier );

    /* schedule a reannounce */
    interval = getRetryInterval( tier->currentTracker );
    dbgmsg( tier, "Retrying announce in %d seconds.", interval );
    tier_announce_event_push( tier, e, tr_time( ) + interval );
}

static void
on_announce_done( tr_session                  * session,
                  const tr_announce_response  * response,
                  void                        * vdata )
{
    tr_announcer * announcer = session->announcer;
    struct announce_data * data = vdata;
    tr_tier * tier = getTier( announcer, response->info_hash, data->tierId );
    const time_t now = tr_time( );
    const tr_announce_event event = data->event;

    if( announcer )
        ++announcer->slotsAvailable;

    if( tier != NULL )
    {
        tr_tracker * tracker;

        dbgmsg( tier, "Got announce response: "
                      "connected:%d "
                      "timeout:%d "
                      "seeders:%d "
                      "leechers:%d "
                      "downloads:%d "
                      "interval:%d "
                      "min_interval:%d "
                      "tracker_id_str:%s "
                      "pex:%zu "
                      "pex6:%zu "
                      "err:%s "
                      "warn:%s",
                      (int)response->did_connect,
                      (int)response->did_timeout,
                      response->seeders,
                      response->leechers,
                      response->downloads,
                      response->interval,
                      response->min_interval,
                      response->tracker_id_str ? response->tracker_id_str : "none",
                      response->pex_count,
                      response->pex6_count,
                      response->errmsg ? response->errmsg : "none",
                      response->warning ? response->warning : "none" );

        tier->lastAnnounceTime = now;
        tier->lastAnnounceTimedOut = response->did_timeout;
        tier->lastAnnounceSucceeded = FALSE;
        tier->isAnnouncing = FALSE;
        tier->manualAnnounceAllowedAt = now + tier->announceMinIntervalSec;

        if( !response->did_connect )
        {
            on_announce_error( tier, _( "Could not connect to tracker" ), event );
        }
        else if( response->did_timeout )
        {
            on_announce_error( tier, _( "Tracker did not respond" ), event );
        }
        else if( response->errmsg )
        {
            publishError( tier, response->errmsg );
            on_announce_error( tier, response->errmsg, event );
        }
        else
        {
            int i;
            const char * str;
            const tr_bool isStopped = event == TR_ANNOUNCE_EVENT_STOPPED;

            publishErrorClear( tier );

            if(( tracker = tier->currentTracker ))
                tracker->consecutiveFailures = 0;

            if(( str = response->warning ))
            {
                tr_strlcpy( tier->lastAnnounceStr, str,
                            sizeof( tier->lastAnnounceStr ) );
                dbgmsg( tier, "tracker gave \"%s\"", str );
                publishWarning( tier, str );
            }

            if(( i = response->min_interval ))
                tier->announceMinIntervalSec = i;

            if(( i = response->interval ))
                tier->announceIntervalSec = i;

            if(( str = response->tracker_id_str ))
            {
                tr_free( tier->currentTracker->tracker_id_str );
                tier->currentTracker->tracker_id_str = tr_strdup( str );
            }

            tier->currentTracker->seederCount = response->seeders;
            tier->currentTracker->leecherCount = response->leechers;
            tier->currentTracker->downloadCount = response->downloads;

            if( response->pex_count > 0 )
                publishPeersPex( tier, response->seeders, response->leechers,
                                 response->pex, response->pex_count );

            if( response->pex6_count > 0 )
                publishPeersPex( tier, response->seeders, response->leechers,
                                 response->pex6, response->pex6_count );

            if( !*tier->lastAnnounceStr )
                tr_strlcpy( tier->lastAnnounceStr, _( "Success" ),
                            sizeof( tier->lastAnnounceStr ) );

            tier->isRunning = data->isRunningOnSuccess;
            tier->scrapeAt = now + tier->scrapeIntervalSec;
            tier->lastScrapeTime = now;
            tier->lastScrapeSucceeded = TRUE;
            tier->lastAnnounceSucceeded = TRUE;
            tier->lastAnnouncePeerCount = response->pex_count
                                        + response->pex6_count;

            if( isStopped )
            {
                /* now that we've successfully stopped the torrent,
                 * we can reset the up/down/corrupt count we've kept
                 * for this tracker */
                tier->byteCounts[ TR_ANN_UP ] = 0;
                tier->byteCounts[ TR_ANN_DOWN ] = 0;
                tier->byteCounts[ TR_ANN_CORRUPT ] = 0;
            }

            if( !isStopped && !tier->announce_event_count )
            {
                /* the queue is empty, so enqueue a perodic update */
                i = tier->announceIntervalSec;
                dbgmsg( tier, "Sending periodic reannounce in %d seconds", i );
                tier_announce_event_push( tier, TR_ANNOUNCE_EVENT_NONE, now + i );
            }
        }
    }

    tr_free( data );
}

static void
announce_request_delegate( tr_announcer               * announcer,
                           tr_announce_request        * request,
                           tr_announce_response_func  * callback,
                           void                       * callback_data )
{
    tr_session * session = announcer->session;

    if( !memcmp( request->url, "http", 4 ) )
        tr_tracker_http_announce( session, request, callback, callback_data );
    else if( !memcmp( request->url, "udp://", 6 ) )
        tr_tracker_udp_announce( session, request, callback, callback_data );
    else
        tr_err( "Unsupported ur: %s", request->url );

    tr_free( request->tracker_id_str );
    tr_free( request->url );
    tr_free( request );
}

static void
tierAnnounce( tr_announcer * announcer, tr_tier * tier )
{
    tr_announce_event announce_event;
    tr_announce_request * req;
    struct announce_data * data;
    const tr_torrent * tor = tier->tor;
    const time_t now = tr_time( );

    assert( !tier->isAnnouncing );
    assert( tier->announce_event_count > 0 );

    announce_event = tier_announce_event_pull( tier );
    req = announce_request_new( announcer, tor, tier, announce_event );

    data = tr_new0( struct announce_data, 1 );
    data->tierId = tier->key;
    data->isRunningOnSuccess = tor->isRunning;
    data->timeSent = now;
    data->event = announce_event;

    tier->isAnnouncing = TRUE;
    tier->lastAnnounceStartTime = now;
    --announcer->slotsAvailable;

    announce_request_delegate( announcer, req, on_announce_done, data );
}

/***
****
****  SCRAPE
****
***/

static void
on_scrape_error( tr_tier * tier, const char * errmsg )
{
    int interval;

    /* increment the error count */
    if( tier->currentTracker != NULL )
        ++tier->currentTracker->consecutiveFailures;

    /* set the error message */
    dbgmsg( tier, "Scrape error: %s", errmsg );
    tr_torinf( tier->tor, "Scrape error: %s", errmsg );
    tr_strlcpy( tier->lastScrapeStr, errmsg, sizeof( tier->lastScrapeStr ) );

    /* switch to the next tracker */
    tierIncrementTracker( tier );

    /* schedule a rescrape */
    interval = getRetryInterval( tier->currentTracker );
    dbgmsg( tier, "Retrying scrape in %d seconds.", interval );
    tier->lastScrapeSucceeded = FALSE;
    tier->scrapeAt = tr_time() + interval;
}

static tr_tier *
find_tier( tr_torrent * tor, const char * url )
{
    int i;
    const int n = tr_ptrArraySize( &tor->tiers->tiers );
    tr_tier ** tiers = (tr_tier**) tr_ptrArrayBase( &tor->tiers->tiers );

    for( i=0; i<n; ++i ) {
        tr_tracker * tracker = tiers[i]->currentTracker;
        if( tracker && !tr_strcmp0( tracker->scrape, url ) )
            return tiers[i];
    }

    return NULL;
}

static void
on_scrape_done( tr_session                * session,
                const tr_scrape_response  * response,
                void                      * user_data UNUSED )
{
    int i;
    const time_t now = tr_time( );
    tr_announcer * announcer = session->announcer;

    for( i=0; i<response->row_count; ++i )
    {
        const struct tr_scrape_response_row * row = &response->rows[i];
        tr_torrent * tor = tr_torrentFindFromHash( session, row->info_hash );

        if( tor != NULL )
        {
            tr_tier * tier = find_tier( tor, response->url );

            if( tier != NULL )
            {
                dbgmsg( tier, "scraped url:%s -- "
                              "did_connect:%d "
                              "did_timeout:%d "
                              "seeders:%d "
                              "leechers:%d "
                              "downloads:%d "
                              "downloaders:%d "
                              "min_request_interval:%d "
                              "err:%s ",
                              response->url,
                              (int)response->did_connect,
                              (int)response->did_timeout,
                              row->seeders,
                              row->leechers,
                              row->downloads,
                              row->downloaders,
                              response->min_request_interval,
                              response->errmsg ? response->errmsg : "none" );

                tier->isScraping = FALSE;
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = FALSE;
                tier->lastScrapeTimedOut = response->did_timeout;

                if( !response->did_connect )
                {
                    on_scrape_error( tier, _( "Could not connect to tracker" ) );
		}
                else if( response->did_timeout )
                {
                    on_scrape_error( tier, _( "Tracker did not respond" ) );
                }
                else if( response->errmsg )
                {
                    on_scrape_error( tier, response->errmsg );
                }
                else
                {
                    tr_tracker * tracker;

                    tier->lastScrapeSucceeded = TRUE;
                    tier->scrapeIntervalSec = MAX( DEFAULT_SCRAPE_INTERVAL_SEC,
                                                   response->min_request_interval );
                    tier->scrapeAt = now + tier->scrapeIntervalSec;
                    tr_tordbg( tier->tor, "Scrape successful. Rescraping in %d seconds.",
                               tier->scrapeIntervalSec );

                    if(( tracker = tier->currentTracker ))
                    {
                        tracker->seederCount = row->seeders;
                        tracker->leecherCount = row->leechers;
                        tracker->downloadCount = row->downloads;
                        tracker->downloaderCount = row->downloaders;
                        tracker->consecutiveFailures = 0;
                    }
                }
            }
        }
    }

    if( announcer )
        ++announcer->slotsAvailable;
}

static void
scrape_request_delegate( tr_announcer             * announcer,
                         tr_scrape_request        * request,
                         tr_scrape_response_func  * callback,
                         void                     * callback_data )
{
    tr_session * session = announcer->session;

    if( !memcmp( request->url, "http", 4 ) )
        tr_tracker_http_scrape( session, request, callback, callback_data );
    else if( !memcmp( request->url, "udp://", 6 ) )
        tr_tracker_udp_scrape( session, request, callback, callback_data );
    else
        tr_err( "Unsupported ur: %s", request->url );
}

static void
multiscrape( tr_announcer * announcer, tr_ptrArray * tiers )
{
    int i;
    int request_count = 0;
    const time_t now = tr_time( );
    const int tier_count = tr_ptrArraySize( tiers );
    const int max_request_count = MIN( announcer->slotsAvailable, tier_count );
    tr_scrape_request * requests = tr_new0( tr_scrape_request, max_request_count );

    /* batch as many info_hashes into a request as we can */
    for( i=0; i<tier_count; ++i )
    {
        int j;
        tr_tier * tier = tr_ptrArrayNth( tiers, i );
        char * url = tier->currentTracker->scrape;
        const uint8_t * hash = tier->tor->info.hash;

        /* if there's a request with this scrape URL and a free slot, use it */
        for( j=0; j<request_count; ++j )
        {
            tr_scrape_request * req = &requests[j];

            if( req->info_hash_count >= TR_MULTISCRAPE_MAX )
                continue;
            if( tr_strcmp0( req->url, url ) )
                continue;

            memcpy( req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH );
            tier->isScraping = TRUE;
            tier->lastScrapeStartTime = now;
            break;
        }

        /* otherwise, if there's room for another request, build a new one */
        if( ( j==request_count ) && ( request_count < max_request_count ) )
        {
            tr_scrape_request * req = &requests[request_count++];
            req->url = url;
            tier_build_log_name( tier, req->log_name, sizeof( req->log_name ) );

            memcpy( req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH );
            tier->isScraping = TRUE;
            tier->lastScrapeStartTime = now;
        }
    }

    /* send the requests we just built */
    for( i=0; i<request_count; ++i )
        scrape_request_delegate( announcer, &requests[i], on_scrape_done, NULL );

    /* cleanup */
    tr_free( requests );
}

static void
flushCloseMessages( tr_announcer * announcer )
{
    int i;
    const int n = tr_ptrArraySize( &announcer->stops );

    for( i=0; i<n; ++i )
        announce_request_delegate( announcer, tr_ptrArrayNth( &announcer->stops, i ), NULL, NULL );

    tr_ptrArrayClear( &announcer->stops );
}

static tr_bool
tierNeedsToAnnounce( const tr_tier * tier, const time_t now )
{
    return !tier->isAnnouncing
        && !tier->isScraping
        && ( tier->announceAt != 0 )
        && ( tier->announceAt <= now )
        && ( tier->announce_event_count > 0 );
}

static tr_bool
tierNeedsToScrape( const tr_tier * tier, const time_t now )
{
    return ( !tier->isScraping )
        && ( tier->scrapeAt != 0 )
        && ( tier->scrapeAt <= now )
        && ( tier->currentTracker != NULL )
        && ( tier->currentTracker->scrape != NULL );
}

static int
compareTiers( const void * va, const void * vb )
{
    int ret;
    const tr_tier * a = *(const tr_tier**)va;
    const tr_tier * b = *(const tr_tier**)vb;

    /* primary key: larger stats come before smaller */
    ret = compareTransfer( a->byteCounts[TR_ANN_UP], a->byteCounts[TR_ANN_DOWN],
                           b->byteCounts[TR_ANN_UP], b->byteCounts[TR_ANN_DOWN] );

    /* secondary key: announcements that have been waiting longer go first */
    if( !ret && ( a->announceAt != b->announceAt ) )
        ret = a->announceAt < b->announceAt ? -1 : 1;

    return ret;
}

static void
announceMore( tr_announcer * announcer )
{
    int i;
    int n;
    tr_torrent * tor;
    tr_ptrArray announceMe = TR_PTR_ARRAY_INIT;
    tr_ptrArray scrapeMe = TR_PTR_ARRAY_INIT;
    const time_t now = tr_time( );

    dbgmsg( NULL, "announceMore: slotsAvailable is %d", announcer->slotsAvailable );

    if( announcer->slotsAvailable < 1 )
        return;

    /* build a list of tiers that need to be announced */
    tor = NULL;
    while(( tor = tr_torrentNext( announcer->session, tor ))) {
        if( tor->tiers ) {
            n = tr_ptrArraySize( &tor->tiers->tiers );
            for( i=0; i<n; ++i ) {
                tr_tier * tier = tr_ptrArrayNth( &tor->tiers->tiers, i );
                if( tierNeedsToAnnounce( tier, now ) )
                    tr_ptrArrayAppend( &announceMe, tier );
                else if( tierNeedsToScrape( tier, now ) )
                    tr_ptrArrayAppend( &scrapeMe, tier );
            }
        }
    }

    /* if there are more tiers than slots available, prioritize */
    n = tr_ptrArraySize( &announceMe );
    if( n > announcer->slotsAvailable )
        qsort( tr_ptrArrayBase(&announceMe), n, sizeof(tr_tier*), compareTiers );

    /* announce some */
    n = MIN( tr_ptrArraySize( &announceMe ), announcer->slotsAvailable );
    for( i=0; i<n; ++i ) {
        tr_tier * tier = tr_ptrArrayNth( &announceMe, i );
        dbgmsg( tier, "announcing tier %d of %d", i, n );
        tierAnnounce( announcer, tier );
    }

    /* scrape some */
    multiscrape( announcer, &scrapeMe );

    /* cleanup */
    tr_ptrArrayDestruct( &scrapeMe, NULL );
    tr_ptrArrayDestruct( &announceMe, NULL );
}

static void
onUpkeepTimer( int foo UNUSED, short bar UNUSED, void * vannouncer )
{
    const time_t now = tr_time( );
    tr_announcer * announcer = vannouncer;
    tr_sessionLock( announcer->session );

    /* maybe send out some "stopped" messages for closed torrents */
    flushCloseMessages( announcer );

    /* maybe send out some announcements to trackers */
    announceMore( announcer );

    /* LPD upkeep */
    if( announcer->lpdUpkeepAt <= now ) {
        const int seconds = LPD_HOUSEKEEPING_INTERVAL_SECS;
        announcer->lpdUpkeepAt = now + jitterize( seconds );
        tr_lpdAnnounceMore( now, seconds );
    }

    /* TAU upkeep */
    if( announcer->tauUpkeepAt <= now ) {
        announcer->tauUpkeepAt = now + TAU_UPKEEP_INTERVAL_SECS;
        tr_tracker_udp_upkeep( announcer->session );
    }

    /* set up the next timer */
    tr_timerAdd( announcer->upkeepTimer, UPKEEP_INTERVAL_SECS, 0 );

    tr_sessionUnlock( announcer->session );
}

/***
****
***/

tr_tracker_stat *
tr_announcerStats( const tr_torrent * torrent, int * setmeTrackerCount )
{
    int i;
    int n;
    int out = 0;
    int tierCount;
    tr_tracker_stat * ret;
    const time_t now = tr_time( );

    assert( tr_isTorrent( torrent ) );
    assert( tr_torrentIsLocked( torrent ) );

    /* count the trackers... */
    tierCount = tr_ptrArraySize( &torrent->tiers->tiers );
    for( i=n=0; i<tierCount; ++i ) {
        const tr_tier * tier = tr_ptrArrayNth( &torrent->tiers->tiers, i );
        n += tr_ptrArraySize( &tier->trackers );
    }

    /* alloc the stats */
    *setmeTrackerCount = n;
    ret = tr_new0( tr_tracker_stat, n );

    /* populate the stats */
    tierCount = tr_ptrArraySize( &torrent->tiers->tiers );
    for( i=0; i<tierCount; ++i )
    {
        int j;
        const tr_tier * tier = tr_ptrArrayNth( &torrent->tiers->tiers, i );
        n = tr_ptrArraySize( &tier->trackers );
        for( j=0; j<n; ++j )
        {
            const tr_tracker * tracker = tr_ptrArrayNth( (tr_ptrArray*)&tier->trackers, j );
            tr_tracker_stat * st = ret + out++;

            st->id = tracker->id;
            tr_strlcpy( st->host, tracker->key, sizeof( st->host ) );
            tr_strlcpy( st->announce, tracker->announce, sizeof( st->announce ) );
            st->tier = i;
            st->isBackup = tracker != tier->currentTracker;
            st->lastScrapeStartTime = tier->lastScrapeStartTime;
            if( tracker->scrape )
                tr_strlcpy( st->scrape, tracker->scrape, sizeof( st->scrape ) );
            else
                st->scrape[0] = '\0';

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
                    st->lastScrapeTimedOut = tier->lastScrapeTimedOut;
                    tr_strlcpy( st->lastScrapeResult, tier->lastScrapeStr,
                                sizeof( st->lastScrapeResult ) );
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
                    tr_strlcpy( st->lastAnnounceResult, tier->lastAnnounceStr,
                                sizeof( st->lastAnnounceResult ) );
                    st->lastAnnounceSucceeded = tier->lastAnnounceSucceeded;
                    st->lastAnnounceTimedOut = tier->lastAnnounceTimedOut;
                    st->lastAnnouncePeerCount = tier->lastAnnouncePeerCount;
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

/***
****
***/

static void
trackerItemCopyAttributes( tr_tracker * t, const tr_tracker * o )
{
    assert( t != o );
    assert( t != NULL );
    assert( o != NULL );

    t->seederCount = o->seederCount;
    t->leecherCount = o->leecherCount;
    t->downloadCount = o->downloadCount;
    t->downloaderCount = o->downloaderCount;
}

static void
tierCopyAttributes( tr_tier * t, const tr_tier * o )
{
    tr_tier bak;

    assert( t != NULL );
    assert( o != NULL );
    assert( t != o );

    bak = *t;
    *t = *o;
    t->tor = bak.tor;
    t->trackers = bak.trackers;
    t->announce_events = tr_memdup( o->announce_events, sizeof( tr_announce_event ) * o->announce_event_count );
    t->announce_event_count = o->announce_event_count;
    t->announce_event_alloc = o->announce_event_count;
    t->currentTracker = bak.currentTracker;
    t->currentTrackerIndex = bak.currentTrackerIndex;
}

void
tr_announcerResetTorrent( tr_announcer * announcer UNUSED, tr_torrent * tor )
{
    tr_ptrArray oldTiers = TR_PTR_ARRAY_INIT;

    /* if we had tiers already, make a backup of them */
    if( tor->tiers != NULL )
    {
        oldTiers = tor->tiers->tiers;
        tor->tiers->tiers = TR_PTR_ARRAY_INIT;
    }

    /* create the new tier/tracker structs */
    addTorrentToTier( tor->tiers, tor );

    /* if we had tiers already, merge their state into the new structs */
    if( !tr_ptrArrayEmpty( &oldTiers ) )
    {
        int i, in;
        for( i=0, in=tr_ptrArraySize(&oldTiers); i<in; ++i )
        {
            int j, jn;
            const tr_tier * o = tr_ptrArrayNth( &oldTiers, i );

            if( o->currentTracker == NULL )
                continue;

            for( j=0, jn=tr_ptrArraySize(&tor->tiers->tiers); j<jn; ++j )
            {
                int k, kn;
                tr_tier * t = tr_ptrArrayNth(&tor->tiers->tiers,j);

                for( k=0, kn=tr_ptrArraySize(&t->trackers); k<kn; ++k )
                {
                    tr_tracker * item = tr_ptrArrayNth(&t->trackers,k);
                    if( strcmp( o->currentTracker->announce, item->announce ) )
                        continue;
                    tierCopyAttributes( t, o );
                    t->currentTracker = item;
                    t->currentTrackerIndex = k;
                    t->wasCopied = TRUE;
                    trackerItemCopyAttributes( item, o->currentTracker );
                    dbgmsg( t, "attributes copied to tier %d, tracker %d"
                                               "from tier %d, tracker %d",
                            i, o->currentTrackerIndex, j, k );

                }
            }
        }
    }

    /* kickstart any tiers that didn't get started */
    if( tor->isRunning )
    {
        int i, n;
        const time_t now = tr_time( );
        tr_tier ** tiers = (tr_tier**) tr_ptrArrayPeek( &tor->tiers->tiers, &n );
        for( i=0; i<n; ++i ) {
            tr_tier * tier = tiers[i];
            if( !tier->wasCopied )
                tier_announce_event_push( tier, TR_ANNOUNCE_EVENT_STARTED, now );
        }
    }

    /* cleanup */
    tr_ptrArrayDestruct( &oldTiers, tierFree );
}
