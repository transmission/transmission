/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include <assert.h>
#include <ctype.h> /* isalnum */
#include <limits.h> /* INT_MAX */
#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h> /* strcmp, strchr */
#include <sys/queue.h> /* for evhttp */
#include <sys/types.h> /* for evhttp */

#include <event.h>
#include <evhttp.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "net.h"
#include "ptrarray.h"
#include "publish.h"
#include "timer.h"
#include "tracker.h"
#include "utils.h"

#undef tr_dbg
#define tr_dbg tr_inf

#define MINUTES_TO_MSEC(N) ((N) * 60 * 1000)

/* manual announces via "update tracker" are allowed this frequently */
#define MANUAL_ANNOUNCE_INTERVAL_MSEC (MINUTES_TO_MSEC(10))

/* unless the tracker tells us otherwise, rescrape this frequently */
#define DEFAULT_SCRAPE_INTERVAL_MSEC (MINUTES_TO_MSEC(15))

/* unless the tracker tells us otherwise, reannounce this frequently */
#define DEFAULT_ANNOUNCE_INTERVAL_MSEC (MINUTES_TO_MSEC(20))

/* this is how long we'll leave a scrape request hanging before timeout */
#define SCRAPE_TIMEOUT_INTERVAL_SEC 8

/* this is how long we'll leave a tracker request hanging before timeout */
#define REQ_TIMEOUT_INTERVAL_SEC 8

/* the number of peers that is our goal */
#define NUMWANT 150

/* the length of the 'key' argument passed in tracker requests */
#define TR_KEY_LEN 20


/**
***
**/

typedef struct
{
    tr_ptrArray_t * torrents;
    tr_ptrArray_t * scraping;
    tr_ptrArray_t * scrapeQueue;

    /* these are set from the latest scrape or tracker response */
    int announceIntervalMsec;
    int minAnnounceIntervalMsec;
    int scrapeIntervalMsec;

    /* calculated when we get fewer scrapes
       back than we asked for */
    int multiscrapeMax;

    tr_tracker_info_t * redirect;
    tr_tracker_info_t * addresses;
    int addressIndex;
    int addressCount;
    int * tierFronts;

    char * primaryAddress;

    /* sent as the "key" argument in tracker requests
       to verify us if our IP address changes.
       This is immutable for the life of the tracker object. */
    char key_param[TR_KEY_LEN+1];

    tr_timer_tag scrapeTag;
}
Tracker;

/* this is the Torrent struct, but since it's the pointer
   passed around in the public API of this tracker module,
   its *public* name is tr_tracker_s... wheee */
typedef struct tr_tracker_s
{
    tr_publisher_t * publisher;

    /* torrent hash string */
    uint8_t hash[SHA_DIGEST_LENGTH];
    char escaped[SHA_DIGEST_LENGTH * 3 + 1];

    /* corresponds to the peer_id sent as a tracker request parameter.
       OiNK's op TooMuchTime says: "When the same torrent is opened and
       closed and opened again without quitting Transmission ...
       change the peerid. It would help sometimes if a stopped event
       was missed to ensure that we didn't think someone was cheating. */
    char peer_id[TR_ID_LEN + 1];

    /* these are set from the latest scrape or tracker response */
    int timesDownloaded;
    int seeders;
    int leechers;
    char * trackerID;

    /* the last tracker request we sent. (started, stopped, etc.)
       automatic announces are an empty string;
       NULL means no message has ever been sent */
    char * lastRequest;

    uint64_t manualAnnounceAllowedAt;

    Tracker * tracker;

    tr_timer_tag scrapeTag;
    tr_timer_tag reannounceTag;

    struct evhttp_connection * httpConn;
    struct evhttp_request * httpReq;

    tr_torrent_t * torrent;
}
Torrent;

static int
trackerCompare( const void * va, const void * vb )
{
    const Tracker * a = ( const Tracker * ) va;
    const Tracker * b = ( const Tracker * ) vb;
    return strcmp( a->primaryAddress, b->primaryAddress );
}

static int
torrentCompare( const void * va, const void * vb )
{
    const Torrent * a = (const Torrent*) va;
    const Torrent * b = (const Torrent*) vb;
    return memcmp( a->hash, b->hash, SHA_DIGEST_LENGTH );
}

/***
****  PUBLISH
***/

static const tr_tracker_event_t emptyEvent = { 0, NULL, NULL, NULL, 0 };

static void
publishMessage( Torrent * tor, const char * msg, int type )
{
    tr_tracker_event_t event = emptyEvent;
    event.hash = tor->hash;
    event.messageType = type;
    event.text = msg;
    tr_publisherPublish( tor->publisher, tor, &event );
}
static void
publishErrorMessage( Torrent * tor, const char * msg )
{
    publishMessage( tor, msg, TR_TRACKER_ERROR );
}
static void
publishWarningMessage( Torrent * tor, const char * msg )
{
    publishMessage( tor, msg, TR_TRACKER_WARNING );
}

static void
publishNewPeers( Torrent * tor, int count, uint8_t * peers )
{
    tr_tracker_event_t event = emptyEvent;
    event.hash = tor->hash;
    event.messageType = TR_TRACKER_PEERS;
    event.peerCount = count;
    event.peerCompact = peers;
    tr_inf( "torrent %s got %d new peers", tor->torrent->info.name, count );
    tr_publisherPublish( tor->publisher, tor, &event );
}

static void
publishStopped( Torrent * tor )
{
    tr_tracker_event_t event = emptyEvent;
    event.hash = tor->hash;
    event.messageType = TR_TRACKER_STOPPED;
    tr_publisherPublish( tor->publisher, tor, &event );
}

/***
****  LIFE CYCLE
***/

static tr_ptrArray_t *
getTrackerLookupTable( void )
{
    static tr_ptrArray_t * myTrackers = NULL;
    if( !myTrackers )
         myTrackers = tr_ptrArrayNew( );
    return myTrackers;
}

static void
generateKeyParam( char * msg, int len )
{
    int i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    for( i=0; i<len; ++i )
        *msg++ = pool[tr_rand(36)];
    *msg = '\0';
}

static int onTrackerScrapeNow( void* );

static void
tr_trackerScrapeSoon( Tracker * t )
{
    if( !t->scrapeTag )
         t->scrapeTag = tr_timerNew( onTrackerScrapeNow, t, NULL, 1000 );
}

static Tracker*
tr_trackerGet( const tr_info_t * info )
{
    tr_ptrArray_t * trackers = getTrackerLookupTable( );
    Tracker *t, tmp;
    assert( info != NULL );
    assert( info->primaryAddress && *info->primaryAddress );
    tmp.primaryAddress = info->primaryAddress;
    t = tr_ptrArrayFindSorted( trackers, &tmp, trackerCompare );

    assert( t==NULL || !strcmp(t->primaryAddress,info->primaryAddress) );

    if( t == NULL ) /* no such tracker.... create one */
    {
        int i, j, sum, *iwalk;
        tr_tracker_info_t * nwalk;
        tr_dbg( "making a new tracker for \"%s\"", info->primaryAddress );

        t = tr_new0( Tracker, 1 );
        t->primaryAddress = tr_strdup( info->primaryAddress );
        t->scrapeIntervalMsec      = DEFAULT_SCRAPE_INTERVAL_MSEC;
        t->announceIntervalMsec    = DEFAULT_ANNOUNCE_INTERVAL_MSEC;
        t->minAnnounceIntervalMsec = DEFAULT_ANNOUNCE_INTERVAL_MSEC;
        t->multiscrapeMax = INT_MAX;
        t->torrents    = tr_ptrArrayNew( );
        t->scraping    = tr_ptrArrayNew( );
        t->scrapeQueue = tr_ptrArrayNew( );
        generateKeyParam( t->key_param, TR_KEY_LEN );

        for( sum=i=0; i<info->trackerTiers; ++i )
             sum += info->trackerList[i].count;
        t->addresses = nwalk = tr_new0( tr_tracker_info_t, sum );
        t->addressIndex = 0;
        t->addressCount = sum;
        t->tierFronts = iwalk = tr_new0( int, sum );

        for( i=0; i<info->trackerTiers; ++i )
        {
            const int tierFront = nwalk - t->addresses;

            for( j=0; j<info->trackerList[i].count; ++j )
            {
                const tr_tracker_info_t * src = &info->trackerList[i].list[j];
                nwalk->address = tr_strdup( src->address );
                nwalk->port = src->port;
                nwalk->announce = tr_strdup( src->announce );
                nwalk->scrape = tr_strdup( src->scrape );
                ++nwalk;

                *iwalk++ = tierFront;
            }
        }

        assert( nwalk - t->addresses == sum );
        assert( iwalk - t->tierFronts == sum );

        tr_ptrArrayInsertSorted( trackers, t, trackerCompare );
    }

    return t;
}

static Torrent *
getExistingTorrent( Tracker * t, const uint8_t hash[SHA_DIGEST_LENGTH] )
{
    Torrent tmp;
    memcpy( tmp.hash, hash, SHA_DIGEST_LENGTH );
    return tr_ptrArrayFindSorted( t->torrents, &tmp, torrentCompare );
}

static void
escape( char * out, const uint8_t * in, int in_len )
{
    const uint8_t *end = in + in_len;
    while( in != end )
        if( isalnum(*in) )
            *out++ = (char) *in++;
        else 
            out += snprintf( out, 4, "%%%02X", (unsigned int)*in++ );
    *out = '\0';
}

void
tr_trackerFree( Torrent * tor )
{
    Tracker * t = tor->tracker;

    tr_ptrArrayRemoveSorted( t->torrents, tor, torrentCompare );

    tr_publisherFree( tor->publisher );
    tr_free( tor->trackerID );
    tr_free( tor->lastRequest );
    tr_free( tor );

    if( tr_ptrArrayEmpty( t->torrents ) ) /* last one.. free the tracker too */
    {
        int i;
        tr_ptrArrayRemoveSorted( getTrackerLookupTable( ), t, trackerCompare );

        tr_ptrArrayFree( t->torrents );
        tr_ptrArrayFree( t->scrapeQueue );
        tr_ptrArrayFree( t->scraping );

        for( i=0; i<t->addressCount; ++i )
            tr_trackerInfoClear( &t->addresses[i] );

        if( t->redirect ) {
            tr_trackerInfoClear( t->redirect );
            tr_free( t->redirect );
        }

        tr_free( t->primaryAddress );
        tr_free( t->addresses );
        tr_free( t->tierFronts );
        tr_free( t );
    }
}

Torrent*
tr_trackerNew( tr_torrent_t * torrent )
{
    Torrent * tor;
    Tracker * t = tr_trackerGet( &torrent->info );
    assert( getExistingTorrent( t, torrent->info.hash ) == NULL );

    /* create a new Torrent and queue it for scraping */
    tor = tr_new0( Torrent, 1 );
    tor->publisher = tr_publisherNew( );
    tor->tracker = t;
    tor->torrent = torrent;
    tor->manualAnnounceAllowedAt = ~0;
    memcpy( tor->hash, torrent->info.hash, SHA_DIGEST_LENGTH );
    escape( tor->escaped, torrent->info.hash, SHA_DIGEST_LENGTH );
    tr_ptrArrayInsertSorted( t->torrents, tor, torrentCompare );
    tr_ptrArrayInsertSorted( t->scrapeQueue, tor, torrentCompare );
    tr_trackerScrapeSoon( t );
    return tor;
}

/***
****  UTIL
***/

static int
parseBencResponse( struct evhttp_request * req, benc_val_t * setme )
{
    const unsigned char * body = EVBUFFER_DATA( req->input_buffer );
    const int bodylen = EVBUFFER_LENGTH( req->input_buffer );
    int ret = 1;
    int i;

    for( i=0; ret && i<bodylen; ++i )
        if( !tr_bencLoad( body+i, bodylen-1, setme, NULL ) )
            ret = 0;
    return ret;
}

static int
updateAddresses( Tracker * t, const struct evhttp_request * req )
{
    int ret = TR_OK;
    int moveToNextAddress = FALSE;

    if( !req )
    {
        moveToNextAddress = TRUE;
    }
    else if( req->response_code == HTTP_OK )
    {
        if( t->redirect != NULL )
        {
            /* multitracker spec: "if a connection with a tracker is
               successful, it will be moved to the front of the tier." */
            const int i = t->addressIndex;
            const int j = t->tierFronts[i];
            if( i != j ) {
                tr_tracker_info_t swap = t->addresses[i];
                t->addresses[i] = t->addresses[j];
                t->addresses[j] = swap;
            }
        }
    }
    else if(    ( req->response_code == HTTP_MOVEPERM )
             || ( req->response_code == HTTP_MOVETEMP ) )
    {
        const char * loc = evhttp_find_header( req->input_headers, "Location" );
        tr_tracker_info_t tmp;
        if( tr_trackerInfoInit( &tmp, loc, -1 ) ) /* a bad redirect? */
        {
            moveToNextAddress = TRUE;
        }
        else if( req->response_code == HTTP_MOVEPERM )
        {
            tr_tracker_info_t * cur = &t->addresses[t->addressIndex];
            tr_trackerInfoClear( cur );
            *cur = tmp;
        }
        else if( req->response_code == HTTP_MOVETEMP )
        {
            if( t->redirect == NULL )
                t->redirect = tr_new0( tr_tracker_info_t, 1 );
            else
                tr_trackerInfoClear( t->redirect );
            *t->redirect = tmp;
        }
    }
    else 
    {
        tr_inf( "Connecting to %s gave error [%s]",
                t->addresses[t->addressIndex].announce, 
                req->response_code_line );
        moveToNextAddress = TRUE;
    }

    if( moveToNextAddress )
    {
        if ( ++t->addressIndex >= t->addressCount )
        {
            t->addressIndex = 0;
            ret = TR_ERROR;
        }
    }

    return ret;
}

static tr_tracker_info_t *
getCurrentAddress( const Tracker * t )
{
    assert( t->addresses != NULL );
    assert( t->addressIndex >= 0 );
    assert( t->addressIndex < t->addressCount );
    return &t->addresses[t->addressIndex];
}

static void
addCommonHeaders( const Tracker * t,
                  struct evhttp_request * req )
{
    char buf[1024];
    tr_tracker_info_t * address = getCurrentAddress( t );
    snprintf( buf, sizeof(buf), "%s:%d", address->address, address->port );
    evhttp_add_header( req->output_headers, "Host", buf );
    evhttp_add_header( req->output_headers, "Connection", "close" );
    evhttp_add_header( req->output_headers, "User-Agent",
                                         TR_NAME "/" LONG_VERSION_STRING );
}

/***
****
****  SCRAPE
****
***/

static int
onTorrentScrapeNow( void * vtor )
{
    Torrent * tor = (Torrent *) vtor;
    tr_timerFree( &tor->scrapeTag );
    tr_ptrArrayInsertSorted( tor->tracker->scrapeQueue, tor, torrentCompare );
    tr_trackerScrapeSoon( tor->tracker );
    return FALSE;
}

static void
onScrapeResponse( struct evhttp_request * req, void * vt )
{
    Tracker * t = (Tracker*) vt;

    tr_inf( "scrape response from  '%s': %s",
            t->primaryAddress,
            (req ? req->response_code_line : "(null)") );

    if( req && ( req->response_code == HTTP_OK ) )
    {
        int numResponses = 0;
        benc_val_t benc, *files;
        int n_scraping = tr_ptrArraySize( t->scraping );
        const int bencLoaded = !parseBencResponse( req, &benc );

        if( bencLoaded
            && (( files = tr_bencDictFind( &benc, "files" ) ))
            && ( files->type == TYPE_DICT ) )
        {
            int i;
            for( i=0; i<files->val.l.count; i+=2 )
            {
                const uint8_t* hash =
                    (const uint8_t*) files->val.l.vals[i].val.s.s;
                benc_val_t *tmp, *flags;
                benc_val_t *tordict = &files->val.l.vals[i+1];
                Torrent * tor = getExistingTorrent( t, hash );
                ++numResponses;
                   
                if( !tor ) {
                    tr_err( "Got an unrequested scrape response!" );
                    continue;
                }

                if(( tmp = tr_bencDictFind( tordict, "complete" )))
                    tor->seeders = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "incomplete" )))
                    tor->leechers = tmp->val.i;

                if(( tmp = tr_bencDictFind( tordict, "downloaded" )))
                    tor->timesDownloaded = tmp->val.i;

                if(( flags = tr_bencDictFind( tordict, "flags" )))
                    if(( tmp = tr_bencDictFind( flags, "min_request_interval")))
                        t->scrapeIntervalMsec = tmp->val.i * 1000;
                

                assert( tr_ptrArrayFindSorted(t->scraping,tor,torrentCompare) );
                tr_ptrArrayRemoveSorted( t->scraping, tor, torrentCompare );

                assert( !tor->scrapeTag );
                tor->scrapeTag = tr_timerNew( onTorrentScrapeNow,
                                              tor, NULL,
                                              t->scrapeIntervalMsec );
                tr_dbg( "torrent '%s' scraped.  re-scraping in %d seconds",
                        tor->torrent->info.name, t->scrapeIntervalMsec/1000 );
            }

            if( !files->val.l.count )
            {
                /* got an empty files dictionary!  This probably means the
                   torrents we're scraping have expired from the tracker,
                   so make sure they're stopped.  It also means any previous
                   changes to multiscrapeMax are suspect, so reset that. */

                int n;
                Torrent ** torrents = (Torrent**)
                    tr_ptrArrayPeek( t->scraping, &n );
                for( i=0; i<n; ++i )
                    tr_trackerStop( torrents[i] );
                tr_ptrArrayClear( t->scraping );
                n_scraping = 0;

                t->multiscrapeMax = INT_MAX;
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );

        /* if the tracker gave us back fewer torrents than we
           thought we should get, maybe our multiscrape string
           is too big... limit it based on how many we got back */
        if( ( 0 < numResponses ) && ( numResponses < n_scraping ) )
            t->multiscrapeMax = numResponses;
    }

    updateAddresses( t, req );

    if( !tr_ptrArrayEmpty( t->scraping ) )
    {
        int i, n;
        Torrent ** torrents =
            (Torrent**) tr_ptrArrayPeek( t->scraping, &n );
        for( i=0; i<n; ++i )
            onTorrentScrapeNow( torrents[i] );
        tr_ptrArrayClear( t->scraping );
    }

    if( !tr_ptrArrayEmpty( t->scrapeQueue ) )
        tr_trackerScrapeSoon( t );
}

static int
onTrackerScrapeNow( void * vt )
{
    Tracker * t = (Tracker*) vt;

    assert( tr_ptrArrayEmpty( t->scraping ) );

    if( !tr_ptrArrayEmpty( t->scrapeQueue ) )
    {
        int i, n, len, addr_len, ask_n;
        char *march, *uri;
        Torrent ** torrents =
            (Torrent**) tr_ptrArrayPeek( t->scrapeQueue, &n );
        struct evhttp_connection *evcon = NULL;
        struct evhttp_request *req = NULL;
        const tr_tracker_info_t * address = getCurrentAddress( t );

        ask_n = n;
        if( ask_n > t->multiscrapeMax )
            ask_n = t->multiscrapeMax;

        /**
        ***  Build the scrape request
        **/

        len = addr_len = strlen( address->scrape );
        for( i=0; i<ask_n; ++i )
            len += strlen("&info_hash=") + strlen(torrents[i]->escaped);
        ++len; /* for nul */
        uri = march = tr_new( char, len );
        memcpy( march, address->scrape, addr_len ); march += addr_len;
        for( i=0; i<ask_n; ++i ) {
            const int elen = strlen( torrents[i]->escaped );
            *march++ = i?'&':'?';
            memcpy( march, "info_hash=", 10); march += 10;
            memcpy( march, torrents[i]->escaped, elen ); march += elen;
        }
        *march++ = '\0';
        assert( march - uri == len );

        /* move the first n_ask torrents from scrapeQueue to scraping */
        for( i=0; i<ask_n; ++i )
            tr_ptrArrayInsertSorted( t->scraping, torrents[i], torrentCompare );
        tr_ptrArrayErase( t->scrapeQueue, 0, ask_n );

        /* don't scrape again until we have some response from the tracker */
        tr_timerFree( &t->scrapeTag );

        /* ping the tracker */
        tr_inf( "scrape to %s:%d: %s", address->address, address->port, uri );
        evcon = evhttp_connection_new( address->address, address->port );
        assert( evcon != NULL );
        evhttp_connection_set_timeout( evcon, SCRAPE_TIMEOUT_INTERVAL_SEC );
        req = evhttp_request_new( onScrapeResponse, t );
        assert( req );
        addCommonHeaders( t, req );
        evhttp_make_request( evcon, req, EVHTTP_REQ_GET, uri );

        tr_free( uri );
    }

    return FALSE;
}

/***
****
****  TRACKER REQUESTS
****
***/

static int
torrentIsRunning( Torrent * tor )
{
    return tor->lastRequest && strcmp( tor->lastRequest, "stopped" );
}

static char*
buildTrackerRequestURI( const Torrent * tor, const char * eventName )
{
    const tr_torrent_t * torrent = tor->torrent;
    const int stopping = !strcmp( eventName, "stopped" );
    const int numwant = stopping ? 0 : NUMWANT;
    char buf[4096];

    snprintf( buf, sizeof(buf), "%s"
                                "?info_hash=%s"
                                "&peer_id=%s"
                                "&port=%d"
                                "&uploaded=%"PRIu64
                                "&downloaded=%"PRIu64
                                "&left=%"PRIu64
                                "&compact=1"
                                "&numwant=%d"
                                "&key=%s"
                                "%s%s"
                                "%s%s",
        getCurrentAddress(tor->tracker)->announce,
        tor->escaped,
        tor->peer_id,
        torrent->publicPort,
        torrent->uploadedCur,
        torrent->downloadedCur,
        tr_cpLeftUntilComplete( torrent->completion ),
        numwant,
        tor->tracker->key_param,
        ( ( eventName && *eventName ) ? "&event=" : "" ),
        ( ( eventName && *eventName ) ? eventName : "" ),
        ( ( tor->trackerID && *tor->trackerID ) ? "&trackerid=" : "" ),
        ( ( tor->trackerID && *tor->trackerID ) ? tor->trackerID : "" ) );

    return tr_strdup( buf );
}

/* Convert to compact form */
static uint8_t *
parseOldPeers( benc_val_t * bePeers, int * peerCount )
{
    int i, count;
    uint8_t * compact;

    assert( bePeers->type == TYPE_LIST );

    compact = tr_new( uint8_t, 6 * bePeers->val.l.count );

    for( i=count=0; i<bePeers->val.l.count; ++i )
    {
        struct in_addr addr;
        tr_port_t port;
        benc_val_t * val;
        benc_val_t * peer = &bePeers->val.l.vals[i];

        val = tr_bencDictFind( peer, "ip" );
        if( !val || val->type!=TYPE_STR || tr_netResolve(val->val.s.s, &addr) )
            continue;

        memcpy( &compact[6 * count], &addr, 4 );

        val = tr_bencDictFind( peer, "port" );
        if( !val || val->type!=TYPE_INT || val->val.i<0 || val->val.i>0xffff )
            continue;

        port = htons( val->val.i );
        memcpy( &compact[6 * count + 4], &port, 2 );
        ++count;
    }

    *peerCount = count;
    return compact;
}

static int
onReannounceNow( void * vtor );

/* handle braindead trackers whose minimums is higher
   than the interval. */
static void
setAnnounceInterval( Tracker  * t,
                     int        minimum,
                     int        interval )
{
    assert( t != NULL );

    if( minimum > 0 )
        t->minAnnounceIntervalMsec = minimum;

    if( interval > 0 )
       t->announceIntervalMsec = interval;

    if( t->announceIntervalMsec < t->minAnnounceIntervalMsec )
        t->announceIntervalMsec = t->minAnnounceIntervalMsec;
}

static void
onTrackerResponse( struct evhttp_request * req, void * vtor )
{
    Torrent * tor = (Torrent *) vtor;
    const int isStopped = !torrentIsRunning( tor );
    int reannounceInterval;

    tr_inf( "torrent \"%s\" tracker response: %s",
            tor->torrent->info.name,
            ( req ? req->response_code_line : "(null)") );

    if( req && ( req->response_code == HTTP_OK ) )
    {
        benc_val_t benc;
        const int bencLoaded = !parseBencResponse( req, &benc );

        if( bencLoaded && benc.type==TYPE_DICT )
        {
            benc_val_t * tmp;

            if(( tmp = tr_bencDictFind( &benc, "failure reason" )))
                publishErrorMessage( tor, tmp->val.s.s );

            if(( tmp = tr_bencDictFind( &benc, "warning message" )))
                publishWarningMessage( tor, tmp->val.s.s );

            if(( tmp = tr_bencDictFind( &benc, "interval" )))
                setAnnounceInterval( tor->tracker, -1, tmp->val.i * 1000 );

            if(( tmp = tr_bencDictFind( &benc, "min interval" )))
                setAnnounceInterval( tor->tracker, tmp->val.i * 1000, -1 );

            if(( tmp = tr_bencDictFind( &benc, "tracker id" )))
                tor->trackerID = tr_strndup( tmp->val.s.s, tmp->val.s.i );

            if(( tmp = tr_bencDictFind( &benc, "complete" )))
                tor->seeders = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "incomplete" )))
                tor->leechers = tmp->val.i;

            if(( tmp = tr_bencDictFind( &benc, "peers" )))
            {
                int peerCount = 0;
                uint8_t * peerCompact = NULL;

                if( tmp->type == TYPE_LIST ) /* original protocol */
                {
                    if( tmp->val.l.count > 0 )
                        peerCompact = parseOldPeers( tmp, &peerCount );
                }
                else if( tmp->type == TYPE_STR ) /* "compact" extension */
                {
                    if( tmp->val.s.i >= 6 )
                    {
                        peerCount = tmp->val.s.i / 6;
                        peerCompact = tr_new( uint8_t, tmp->val.s.i );
                        memcpy( peerCompact, tmp->val.s.s, tmp->val.s.i );
                    }
                }

                publishNewPeers( tor, peerCount, peerCompact );
                tr_free( peerCompact );
            }
        }

        reannounceInterval = isStopped
            ? -1
            : tor->tracker->announceIntervalMsec;
    }
    else
    {
        tr_inf( "Bad response from tracker '%s' on request '%s'"
                "for torrent '%s'... trying again in 30 seconds",
                tor->tracker->primaryAddress,
                tor->lastRequest,
                tor->torrent->info.name );

        reannounceInterval = 30 * 1000;
    }

    if( updateAddresses( tor->tracker, req ) )
    {
        char buf[1024];
        snprintf( buf, sizeof(buf), "Unable to connect to \"%s\"",
                  tor->tracker->primaryAddress );
        publishErrorMessage( tor, buf );
    }

    assert( tor->httpConn != NULL );
    tor->httpConn = NULL;

    if( isStopped )
        publishStopped( tor );
    else if( reannounceInterval > 0 ) {
        tr_inf( "torrent '%s' reannouncing in %d seconds",
                tor->torrent->info.name, (reannounceInterval/1000) );
        tor->reannounceTag = tr_timerNew( onReannounceNow, tor, NULL,
                                          reannounceInterval );
        tor->manualAnnounceAllowedAt
                           = tr_date() + MANUAL_ANNOUNCE_INTERVAL_MSEC;
    }
}

static int
sendTrackerRequest( void * vtor, const char * eventName )
{
    Torrent * tor = (Torrent *) vtor;
    const tr_tracker_info_t * address = getCurrentAddress( tor->tracker );
    char * uri = buildTrackerRequestURI( tor, eventName );

    tr_inf( "tracker request to %s:%d: %s", address->address,
                                            address->port, uri );

    /* kill any pending requests */
    tr_timerFree( &tor->reannounceTag );

    /* make a connection if we don't have one */
    if( tor->httpConn == NULL )
        tor->httpConn = evhttp_connection_new( address->address,
                                                address->port );

    tr_free( tor->lastRequest );
    tor->lastRequest = tr_strdup( eventName );
    evhttp_connection_set_timeout( tor->httpConn, REQ_TIMEOUT_INTERVAL_SEC );
    tor->httpReq = evhttp_request_new( onTrackerResponse, tor );
    addCommonHeaders( tor->tracker, tor->httpReq );

    evhttp_make_request( tor->httpConn, tor->httpReq, EVHTTP_REQ_GET, uri );
    tr_free( uri );

    return FALSE;
}

static int
onReannounceNow( void * vtor )
{
    sendTrackerRequest( (Torrent*)vtor, "" );
    return FALSE;
}

/***
****  PUBLIC
***/

tr_publisher_tag
tr_trackerSubscribe( Torrent             * tor,
                     tr_delivery_func      func,
                     void                * user_data )
{
    return tr_publisherSubscribe( tor->publisher, func, user_data );
}

void
tr_trackerUnsubscribe( Torrent           * tor,
                       tr_publisher_tag    tag )
{
    tr_publisherUnsubscribe( tor->publisher, tag );
}

const tr_tracker_info_t *
tr_trackerGetAddress( const Torrent * tor )
{
    return getCurrentAddress( tor->tracker );
}

int
tr_trackerCanManualAnnounce ( const Torrent * tor )
{
    /* return true if this torrent's currently running
       and it's been long enough since the last announce */
    return ( tor != NULL )
        && ( tor->reannounceTag != NULL )
        && ( tr_date() >= tor->manualAnnounceAllowedAt );
}

void
tr_trackerGetCounts( const Torrent       * tor,
                     int                 * setme_completedCount,
                     int                 * setme_leecherCount,
                     int                 * setme_seederCount )
{
    if( setme_completedCount)
       *setme_completedCount = tor->timesDownloaded;

    if( setme_leecherCount)
       *setme_leecherCount = tor->leechers;

    if( setme_seederCount)
       *setme_seederCount = tor->seeders;
}

void
tr_trackerStart( Torrent * tor )
{
    tr_peerIdNew( tor->peer_id, sizeof(tor->peer_id) );
    assert( !tor->reannounceTag && "torrent's already started!" );
    sendTrackerRequest( tor, "started" );
}

void
tr_trackerReannounce( Torrent * tor )
{
    sendTrackerRequest( tor, "started" );
}

void
tr_trackerCompleted( Torrent * tor )
{
    sendTrackerRequest( tor, "completed" );
}

void
tr_trackerStop( Torrent * tor )
{
    sendTrackerRequest( tor, "stopped" );
}

void
tr_trackerChangeMyPort( Torrent * tor )
{
    if( torrentIsRunning( tor ) )
        tr_trackerReannounce( tor );
}
