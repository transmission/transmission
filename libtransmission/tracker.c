/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
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
#include <stdlib.h>
#include <string.h> /* strcmp, strchr */

#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h"
#include "net.h"
#include "publish.h"
#include "resume.h"
#include "session.h"
#include "torrent.h"
#include "tracker.h"
#include "tr-dht.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"

enum
{
    /* the announceAt fields are set to this when the action is disabled */
    TR_TRACKER_STOPPED = 0,

    /* the announceAt fields are set to this when the action is in progress */
    TR_TRACKER_BUSY = 1,

    HTTP_OK = 200,

    /* seconds between tracker pulses */
    PULSE_INTERVAL_MSEC = 1500,

    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = ( 60 * 15 ),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = ( 60 * 10 ),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = ( 60 * 2 ),

    /* how long to wait before a rescrape the first time we get an error */
    FIRST_SCRAPE_RETRY_INTERVAL_SEC = 30,

    /* how long to wait before a reannounce the first time we get an error */
    FIRST_ANNOUNCE_RETRY_INTERVAL_SEC = 30,

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 200,

    /* the length of the 'key' argument passed in tracker requests */
    KEYLEN = 10
};

/**
***
**/

struct tr_tracker
{
    tr_bool         isRunning;

    uint8_t         randOffset;

    /* sent as the "key" argument in tracker requests
       to verify us if our IP address changes.
       This is immutable for the life of the tracker object. */
    char    key_param[KEYLEN + 1];

    /* these are set from the latest scrape or tracker response */
    int    announceIntervalSec;
    int    announceMinIntervalSec;
    int    scrapeIntervalSec;
    int    retryScrapeIntervalSec;
    int    retryAnnounceIntervalSec;

    /* index into the torrent's tr_info.trackers array */
    int               trackerIndex;

    tr_session *      session;

    tr_publisher      publisher;

    /* torrent hash string */
    uint8_t    hash[SHA_DIGEST_LENGTH];
    char       escaped[SHA_DIGEST_LENGTH * 3 + 1];
    char *     name;
    int        torrentId;

    /* corresponds to the peer_id sent as a tracker request parameter.
       one tracker admin says: "When the same torrent is opened and
       closed and opened again without quitting Transmission ...
       change the peerid. It would help sometimes if a stopped event
       was missed to ensure that we didn't think someone was cheating. */
    uint8_t *  peer_id;

    /* these are set from the latest tracker response... -1 is 'unknown' */
    int       timesDownloaded;
    int       seederCount;
    int       downloaderCount;
    int       leecherCount;
    char *    trackerID;

    time_t    manualAnnounceAllowedAt;
    time_t    reannounceAt;

    /* 0==never, 1==in progress, other values==when to scrape */
    time_t    scrapeAt;

    time_t    lastScrapeTime;
    char      lastScrapeStr[128];

    time_t    lastAnnounceTime;
    char      lastAnnounceStr[128];
};

#define dbgmsg( name, ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, name, __VA_ARGS__ ); \
    } while( 0 )

/***
****
***/

static const tr_tracker_info *
getCurrentAddressFromTorrent( tr_tracker *       t,
                              const tr_torrent * tor )
{
    /* user might have removed trackers,
     * so check to make sure our current index is in-bounds */
    if( t->trackerIndex >= tor->info.trackerCount )
        t->trackerIndex = 0;

    assert( t->trackerIndex >= 0 );
    assert( t->trackerIndex < tor->info.trackerCount );
    return tor->info.trackers + t->trackerIndex;
}

static const tr_tracker_info *
getCurrentAddress( tr_tracker * t )
{
    const tr_torrent * torrent;

    if( ( torrent = tr_torrentFindFromId( t->session, t->torrentId ) ) )
        return getCurrentAddressFromTorrent( t, torrent );
    return NULL;
}

static int
trackerSupportsScrape( tr_tracker *       t,
                       const tr_torrent * tor )
{
    const tr_tracker_info * info = getCurrentAddressFromTorrent( t, tor );

    return info && info->scrape;
}

/***
****
***/

static tr_tracker *
findTracker( tr_session * session, int torrentId )
{
    tr_torrent * torrent = tr_torrentFindFromId( session, torrentId );

    return torrent ? torrent->tracker : NULL;
}

/***
****  PUBLISH
***/

static const tr_tracker_event emptyEvent = { 0, NULL, NULL, 0, 0 };

static void
publishMessage( tr_tracker * t,
                const char * msg,
                int          type )
{
    if( t )
    {
        tr_tracker_event event = emptyEvent;
        event.messageType = type;
        event.text = msg;
        tr_publisherPublish( &t->publisher, t, &event );
    }
}

static void
publishErrorClear( tr_tracker * t )
{
    publishMessage( t, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishErrorMessageAndStop( tr_tracker * t,
                            const char * msg )
{
    t->isRunning = 0;
    publishMessage( t, msg, TR_TRACKER_ERROR );
}

static void
publishWarning( tr_tracker * t,
                const char * msg )
{
    publishMessage( t, msg, TR_TRACKER_WARNING );
}

static void
publishNewPeers( tr_tracker * t,
                 int          allAreSeeds,
                 const void * compact,
                 int          compactLen )
{
    tr_tracker_event event = emptyEvent;

    event.messageType = TR_TRACKER_PEERS;
    event.allAreSeeds = allAreSeeds;
    event.compact = compact;
    event.compactLen = compactLen;
    if( compactLen )
        tr_publisherPublish( &t->publisher, t, &event );
}

static void
publishNewPeersCompact( tr_tracker * t,
                        int          allAreSeeds,
                        const void * compact,
                        int          compactLen )
{
    int i;
    const uint8_t *compactWalk;
    uint8_t *array, *walk;
    const int peerCount = compactLen / 6;
    const int arrayLen = peerCount * ( sizeof( tr_address ) + 2 );
    tr_address addr;
    tr_port port;

    addr.type = TR_AF_INET;
    memset( &addr.addr, 0x00, sizeof( addr.addr ) );
    array = tr_new( uint8_t, arrayLen );
    for ( i = 0, walk = array, compactWalk = compact ; i < peerCount ; i++ )
    {
        memcpy( &addr.addr.addr4, compactWalk, 4 );
        memcpy( &port, compactWalk + 4, 2 );

        memcpy( walk, &addr, sizeof( addr ) );
        memcpy( walk + sizeof( addr ), &port, 2 );

        walk += sizeof( tr_address ) + 2;
        compactWalk += 6;
    }
    publishNewPeers( t, allAreSeeds, array, arrayLen );
    tr_free( array );
}

static void
publishNewPeersCompact6( tr_tracker * t,
                         int          allAreSeeds,
                         const void * compact,
                         int          compactLen )
{
    int i;
    const uint8_t *compactWalk;
    uint8_t *array, *walk;
    const int peerCount = compactLen / 18;
    const int arrayLen = peerCount * ( sizeof( tr_address ) + 2 );
    tr_address addr;
    tr_port port;

    addr.type = TR_AF_INET6;
    memset( &addr.addr, 0x00, sizeof( addr.addr ) );
    array = tr_new( uint8_t, arrayLen );
    for ( i = 0, walk = array, compactWalk = compact ; i < peerCount ; i++ )
    {
        memcpy( &addr.addr.addr6, compactWalk, 16 );
        memcpy( &port, compactWalk + 16, 2 );
        compactWalk += 18;

        memcpy( walk, &addr, sizeof( addr ) );
        memcpy( walk + sizeof( addr ), &port, 2 );
        walk += sizeof( tr_address ) + 2;
    }
    publishNewPeers( t, allAreSeeds, array, arrayLen );
    tr_free( array );
}

/***
****
***/

static void onReqDone( tr_session * session );

static int
updateAddresses( tr_tracker * t,
                 int          success )
{
    int          retry;

    tr_torrent * torrent = tr_torrentFindFromHash( t->session, t->hash );

    if( success )
    {
        /* multitracker spec: "if a connection with a tracker is
           successful, it will be moved to the front of the tier." */
        t->trackerIndex = tr_torrentPromoteTracker( torrent, t->trackerIndex );
        retry = FALSE; /* we succeeded; no need to retry */
    }
    else if( ++t->trackerIndex >= torrent->info.trackerCount )
    {
        t->trackerIndex = 0;
        retry = FALSE; /* we've tried them all */
    }
    else
    {
        const tr_tracker_info * n = getCurrentAddressFromTorrent( t, torrent );
        tr_ninf( t->name, _( "Trying tracker \"%s\"" ), n->announce );
        retry = TRUE;
    }

    return retry;
}

static uint8_t *
parseOldPeers( tr_benc * bePeers,
               size_t *  byteCount )
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
        {
            if( tr_pton( s, &addr ) == NULL )
                continue;
        }
        if( !tr_bencDictFindInt( peer, "port",
                                 &itmp ) || itmp < 0 || itmp > 0xffff )
            continue;

        memcpy( walk, &addr, sizeof( tr_address ) );
        port = htons( itmp );
        memcpy( walk + sizeof( tr_address ), &port, 2 );
        walk += sizeof( tr_address ) + 2;
    }

    *byteCount = peerCount * sizeof( tr_address ) + 2;
    return array;
}

static void
onStoppedResponse( tr_session    * session,
                   long            responseCode UNUSED,
                   const void    * response     UNUSED,
                   size_t          responseLen  UNUSED,
                   void          * torrentId )
{
    tr_tracker * t = findTracker( session, tr_ptr2int( torrentId ) );
    if( t )
    {
        const time_t now = time( NULL );

        t->reannounceAt = TR_TRACKER_STOPPED;
        t->manualAnnounceAllowedAt = TR_TRACKER_STOPPED;

        if( t->scrapeAt <= now )
            t->scrapeAt = now + t->scrapeIntervalSec + t->randOffset;
    }

    dbgmsg( NULL, "got a response to some `stop' message" );
    onReqDone( session );
}

static void
onTrackerResponse( tr_session * session,
                   long         responseCode,
                   const void * response,
                   size_t       responseLen,
                   void       * torrentId )
{
    int retry;
    int success = FALSE;
    int scrapeFields = 0;
    tr_tracker * t;

    onReqDone( session );
    t = findTracker( session, tr_ptr2int( torrentId ) );
    if( !t ) /* tracker's been closed */
        return;

    dbgmsg( t->name, "tracker response: %ld", responseCode );
    tr_ndbg( t->name, "tracker response: %ld", responseCode );
    t->lastAnnounceStr[0] = '\0';

    if( responseCode == HTTP_OK )
    {
        tr_benc   benc;
        const int bencLoaded = !tr_bencLoad( response, responseLen,
                                             &benc, NULL );
        publishErrorClear( t );
        if( bencLoaded && tr_bencIsDict( &benc ) )
        {
            tr_benc *    tmp;
            int64_t      i;
            int          incomplete = -1;
            const char * str;
            const uint8_t * raw;
            size_t rawlen;

            success = TRUE;
            t->retryAnnounceIntervalSec = FIRST_SCRAPE_RETRY_INTERVAL_SEC;

            if( tr_bencDictFindStr( &benc, "failure reason", &str ) )
            {
                tr_strlcpy( t->lastAnnounceStr, str, sizeof( t->lastAnnounceStr ) );
                publishMessage( t, str, TR_TRACKER_ERROR );
                success = FALSE;
            }

            if( tr_bencDictFindStr( &benc, "warning message", &str ) )
            {
                tr_strlcpy( t->lastAnnounceStr, str, sizeof( t->lastAnnounceStr ) );
                publishWarning( t, str );
            }

            if( tr_bencDictFindInt( &benc, "interval", &i ) )
            {
                dbgmsg( t->name, "setting interval to %d", (int)i );
                t->announceIntervalSec = i;
            }

            if( tr_bencDictFindInt( &benc, "min interval", &i ) )
            {
                dbgmsg( t->name, "setting min interval to %d", (int)i );
                t->announceMinIntervalSec = i;
            }

            if( tr_bencDictFindStr( &benc, "tracker id", &str ) )
                t->trackerID = tr_strdup( str );

            if( tr_bencDictFindInt( &benc, "complete", &i ) )
            {
                ++scrapeFields;
                t->seederCount = i;
            }

            if( tr_bencDictFindInt( &benc, "incomplete", &i ) )
            {
                ++scrapeFields;
                t->leecherCount = incomplete = i;
            }

            if( tr_bencDictFindInt( &benc, "downloaded", &i ) )
            {
                ++scrapeFields;
                t->timesDownloaded = i;
            }

            if( tr_bencDictFindRaw( &benc, "peers", &raw, &rawlen ) )
            {
                /* "compact" extension */
                const int allAreSeeds = incomplete == 0;
                publishNewPeersCompact( t, allAreSeeds, raw, rawlen );
            }
            else if( tr_bencDictFindList( &benc, "peers", &tmp ) )
            {
                /* original version of peers */
                const int allAreSeeds = incomplete == 0;
                size_t byteCount = 0;
                uint8_t * array = parseOldPeers( tmp, &byteCount );
                publishNewPeers( t, allAreSeeds, array, byteCount );
                tr_free( array );
            }

            if( tr_bencDictFindRaw( &benc, "peers6", &raw, &rawlen ) )
            {
                /* "compact" extension */
                const int allAreSeeds = incomplete == 0;
                publishNewPeersCompact6( t, allAreSeeds, raw, rawlen );
            }

            if( !*t->lastAnnounceStr )
                tr_strlcpy( t->lastAnnounceStr, _( "Success" ), sizeof( t->lastAnnounceStr ) );
        }

        if( bencLoaded )
            tr_bencFree( &benc );
    }
    else if( responseCode )
    {
        /* %1$ld - http status code, such as 404
         * %2$s - human-readable explanation of the http status code */
        char * buf = tr_strdup_printf( _( "Announce failed: tracker gave HTTP Response Code %1$ld (%2$s)" ),
                                      responseCode,
                                      tr_webGetResponseStr( responseCode ) );
        tr_strlcpy( t->lastAnnounceStr, buf, sizeof( t->lastAnnounceStr ) );
        publishWarning( t, buf );
        tr_free( buf );
    }
    else
    {
        tr_strlcpy( t->lastAnnounceStr, _( "Announce failed: tracker did not respond." ), sizeof( t->lastAnnounceStr ) );
    }

    retry = updateAddresses( t, success );

    if( responseCode && retry )
        responseCode = 300;

    if( responseCode == 0 )
    {
        dbgmsg( t->name, "No response from tracker... retrying in two minutes." );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + t->randOffset + 120;
    }
    else if( 200 <= responseCode && responseCode <= 299 )
    {
        const int    interval = t->announceIntervalSec + t->randOffset;
        const time_t now = time ( NULL );
        dbgmsg( t->name, "request succeeded. reannouncing in %d seconds", interval );

        /* if the announce response was a superset of the scrape response,
           treat this as both a successful announce AND scrape. */
        if( scrapeFields >= 3 ) {
            t->lastScrapeTime = now;
            t->scrapeAt = now + t->scrapeIntervalSec + t->randOffset;
        }

        /* most trackers don't provide all the scrape responses, but do
           provide most of them, so don't scrape too soon anyway */
        if( ( scrapeFields == 2 ) && ( t->scrapeAt <= ( now + 120 ) ) ) {
            t->scrapeAt = now + t->scrapeIntervalSec + t->randOffset;
        }

        t->reannounceAt = now + interval;
        t->manualAnnounceAllowedAt = now + t->announceMinIntervalSec;
    }
    else if( 300 <= responseCode && responseCode <= 399 )
    {
        /* it's a redirect... updateAddresses() has already
         * parsed the redirect, all that's left is to retry */
        const int interval = 5;
        dbgmsg( t->name, "got a redirect. retrying in %d seconds", interval );
        t->reannounceAt = time( NULL ) + interval;
        t->manualAnnounceAllowedAt = time( NULL ) + t->announceMinIntervalSec;
    }
    else if( 400 <= responseCode && responseCode <= 499 )
    {
        /* The request could not be understood by the server due to
         * malformed syntax. The client SHOULD NOT repeat the
         * request without modifications. */
        publishErrorMessageAndStop( t, _( "Tracker returned a 4xx message" ) );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = TR_TRACKER_STOPPED;
    }
    else if( 500 <= responseCode && responseCode <= 599 )
    {
        /* Response status codes beginning with the digit "5" indicate
         * cases in which the server is aware that it has erred or is
         * incapable of performing the request.  So we pause a bit and
         * try again. */
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + t->retryAnnounceIntervalSec;
        t->retryAnnounceIntervalSec *= 2;
    }
    else
    {
        /* WTF did we get?? */
        dbgmsg( t->name, "Invalid response from tracker... retrying in two minutes." );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + t->randOffset + 120;
    }
}

static void
onScrapeResponse( tr_session * session,
                  long         responseCode,
                  const void * response,
                  size_t       responseLen,
                  void       * torrentId )
{
    int          success = FALSE;
    int          retry;
    tr_tracker * t;

    onReqDone( session );
    t = findTracker( session, tr_ptr2int( torrentId ) );
    if( !t ) /* tracker's been closed... */
        return;

    dbgmsg( t->name, "scrape response: %ld\n", responseCode );
    tr_ndbg( t->name, "scrape response: %ld", responseCode );
    t->lastScrapeStr[0] = '\0';

    if( responseCode == HTTP_OK )
    {
        tr_benc   benc, *files;
        const int bencLoaded = !tr_bencLoad( response, responseLen,
                                             &benc, NULL );
        if( bencLoaded && tr_bencDictFindDict( &benc, "files", &files ) )
        {
            const char * key;
            tr_benc * val;
            int i = 0;
            while( tr_bencDictChild( files, i++, &key, &val ))
            {
                int64_t intVal;
                tr_benc * flags;

                if( memcmp( t->hash, key, SHA_DIGEST_LENGTH ) )
                    continue;

                publishErrorClear( t );

                if( ( tr_bencDictFindInt( val, "complete", &intVal ) ) )
                    t->seederCount = intVal;

                if( ( tr_bencDictFindInt( val, "incomplete", &intVal ) ) )
                    t->leecherCount = intVal;

                if( ( tr_bencDictFindInt( val, "downloaded", &intVal ) ) )
                    t->timesDownloaded = intVal;

                if( ( tr_bencDictFindInt( val, "downloaders", &intVal ) ) )
                    t->downloaderCount = intVal;

                if( tr_bencDictFindDict( val, "flags", &flags ) )
                    if( ( tr_bencDictFindInt( flags, "min_request_interval", &intVal ) ) )
                        t->scrapeIntervalSec = intVal;

                /* as per ticket #1045, safeguard against trackers returning
                 * a very low min_request_interval... */
                if( t->scrapeIntervalSec < DEFAULT_SCRAPE_INTERVAL_SEC )
                    t->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;

                tr_ndbg( t->name,
                         "Scrape successful. Rescraping in %d seconds.",
                         t->scrapeIntervalSec );

                success = TRUE;
                t->retryScrapeIntervalSec = FIRST_SCRAPE_RETRY_INTERVAL_SEC;
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );
    }

    retry = updateAddresses( t, success );

    /**
    ***
    **/

    if( retry )
        responseCode = 300;

    if( 200 <= responseCode && responseCode <= 299 )
    {
        const int interval = t->scrapeIntervalSec + t->randOffset;
        t->scrapeAt = time( NULL ) + interval;

        tr_strlcpy( t->lastScrapeStr, _( "Success" ), sizeof( t->lastScrapeStr ) );
        tr_ndbg( t->name, "Request succeeded. Rescraping in %d seconds", interval );
    }
    else if( 300 <= responseCode && responseCode <= 399 )
    {
        const int interval = 5;
        t->scrapeAt = time( NULL ) + interval;

        tr_snprintf( t->lastScrapeStr, sizeof( t->lastScrapeStr ), "Got a redirect. Retrying in %d seconds", interval );
        tr_ndbg( t->name, "%s", t->lastScrapeStr );
    }
    else
    {
        const int interval = t->retryScrapeIntervalSec + t->randOffset;
        t->retryScrapeIntervalSec *= 2;
        t->scrapeAt = time( NULL ) + interval;

        /* %1$ld - http status code, such as 404
         * %2$s - human-readable explanation of the http status code */
        if( !responseCode )
            tr_strlcpy( t->lastScrapeStr, _( "Scrape failed: tracker did not respond." ), sizeof( t->lastScrapeStr ) );
        else
            tr_snprintf( t->lastScrapeStr, sizeof( t->lastScrapeStr ),
                         _( "Scrape failed: tracker gave HTTP Response Code %1$ld (%2$s)" ),
                         responseCode, tr_webGetResponseStr( responseCode ) );
    }

    dbgmsg( t->name, "%s", t->lastScrapeStr );
}

/***
****
***/

enum
{
    TR_REQ_STARTED,
    TR_REQ_COMPLETED,
    TR_REQ_STOPPED,
    TR_REQ_PAUSED,     /* BEP 21 */
    TR_REQ_REANNOUNCE,
    TR_REQ_SCRAPE,
    TR_NUM_REQ_TYPES
};

struct tr_tracker_request
{
    int                 reqtype; /* TR_REQ_* */
    int                 torrentId;
    struct evbuffer   * url;
    tr_web_done_func  * done_func;
    tr_session *        session;
};

static void
freeRequest( struct tr_tracker_request * req )
{
    evbuffer_free( req->url );
    tr_free( req );
}

static void
buildTrackerRequestURI( tr_tracker *       t,
                        const tr_torrent * torrent,
                        const char *       eventName,
                        struct evbuffer *  buf )
{
    const int    isStopping = !strcmp( eventName, "stopped" );
    const int    numwant = isStopping ? 0 : NUMWANT;
    const char * ann = getCurrentAddressFromTorrent( t, torrent )->announce;

    evbuffer_add_printf( buf, "%cinfo_hash=%s"
                              "&peer_id=%s"
                              "&port=%d"
                              "&uploaded=%" PRIu64
                              "&downloaded=%" PRIu64
                              "&corrupt=%" PRIu64
                              "&left=%" PRIu64
                              "&compact=1"
                              "&supportcrypto=1"
                              "&numwant=%d"
                              "&key=%s",
                              strchr( ann, '?' ) ? '&' : '?',
                              t->escaped,
                              t->peer_id,
                              tr_sessionGetPeerPort( t->session ),
                              torrent->uploadedCur,
                              torrent->downloadedCur,
                              torrent->corruptCur,
                              tr_cpLeftUntilComplete( &torrent->completion ),
                              numwant,
                              t->key_param );

    if( t->session->encryptionMode == TR_ENCRYPTION_REQUIRED )
        evbuffer_add_printf( buf, "&requirecrypto=1" );

    if( eventName && *eventName )
        evbuffer_add_printf( buf, "&event=%s", eventName );

    if( t->trackerID && *t->trackerID )
        evbuffer_add_printf( buf, "&trackerid=%s", t->trackerID );

}

static struct tr_tracker_request*
createRequest( tr_session * session,
               tr_tracker * tracker,
               int          reqtype )
{
    static const char* strings[] = { "started", "completed", "stopped", "paused", "", "err" };
    const tr_torrent * torrent = tr_torrentFindFromHash( session, tracker->hash );
    const tr_tracker_info * address = getCurrentAddressFromTorrent( tracker, torrent );
    int isStopping;
    struct tr_tracker_request * req;
    struct evbuffer * url;

    /* BEP 21: In order to tell the tracker that a peer is a partial seed, it MUST send
     * an event=paused parameter in every announce while it is a partial seed. */
    if( tr_cpGetStatus( &torrent->completion ) == TR_PARTIAL_SEED )
        reqtype = TR_REQ_PAUSED;

    isStopping = reqtype == TR_REQ_STOPPED;

    url = evbuffer_new( );
    evbuffer_add_printf( url, "%s", address->announce );
    buildTrackerRequestURI( tracker, torrent, strings[reqtype], url );

    req = tr_new0( struct tr_tracker_request, 1 );
    req->session = session;
    req->reqtype = reqtype;
    req->done_func =  isStopping ? onStoppedResponse : onTrackerResponse;
    req->url = url;
    req->torrentId = tracker->torrentId;

    return req;
}

static struct tr_tracker_request*
createScrape( tr_session * session,
              tr_tracker * tracker )
{
    const tr_tracker_info *     a = getCurrentAddress( tracker );
    struct tr_tracker_request * req;
    struct evbuffer *           url = evbuffer_new( );

    evbuffer_add_printf( url, "%s%cinfo_hash=%s",
                         a->scrape, strchr( a->scrape, '?' ) ? '&' : '?',
                         tracker->escaped );

    req = tr_new0( struct tr_tracker_request, 1 );
    req->session = session;
    req->reqtype = TR_REQ_SCRAPE;
    req->url = url;
    req->done_func = onScrapeResponse;
    req->torrentId = tracker->torrentId;

    return req;
}

struct tr_tracker_handle
{
    tr_bool     shutdownHint;
    int         runningCount;
    tr_timer *  pulseTimer;
};

static int trackerPulse( void * vsession );

void
tr_trackerSessionInit( tr_session * session )
{
    assert( tr_isSession( session ) );

    session->tracker = tr_new0( struct tr_tracker_handle, 1 );
    session->tracker->pulseTimer = tr_timerNew( session, trackerPulse, session, PULSE_INTERVAL_MSEC );
    dbgmsg( NULL, "creating tracker timer" );
}

void
tr_trackerSessionClose( tr_session * session )
{
    assert( tr_isSession( session ) );

    session->tracker->shutdownHint = TRUE;
}

static void
tr_trackerSessionDestroy( tr_session * session )
{
    if( session && session->tracker )
    {
        dbgmsg( NULL, "freeing tracker timer" );
        tr_timerFree( &session->tracker->pulseTimer );
        tr_free( session->tracker );
        session->tracker = NULL;
    }
}

/***
****
***/

static void
invokeRequest( void * vreq )
{
    struct tr_tracker_request * req = vreq;
    tr_tracker * t;

    assert( req != NULL );
    assert( tr_isSession( req->session ) );
    assert( req->torrentId >= 0 );
    assert( req->reqtype >= 0 );
    assert( req->reqtype < TR_NUM_REQ_TYPES );

    dbgmsg( NULL, "invokeRequest got session %p, tracker %p", req->session, req->session->tracker );

    t = findTracker( req->session, req->torrentId );

    if( t != NULL )
    {
        const time_t now = time( NULL );

        if( req->reqtype == TR_REQ_SCRAPE )
        {
            t->lastScrapeTime = now;
            t->scrapeAt = TR_TRACKER_BUSY;
        }
        else
        {
            t->lastAnnounceTime = now;
            t->reannounceAt = TR_TRACKER_BUSY;
            t->manualAnnounceAllowedAt = TR_TRACKER_BUSY;
        }
    }

    assert( req->session->tracker != NULL );
    ++req->session->tracker->runningCount;

    tr_webRun( req->session,
               (char*)EVBUFFER_DATA(req->url),
               NULL,
               req->done_func, tr_int2ptr( req->torrentId ) );

    freeRequest( req );
}

static void
enqueueScrape( tr_session * session,
               tr_tracker * tracker )
{
    struct tr_tracker_request * req;
    assert( tr_isSession( session ) );

    req = createScrape( session, tracker );
    tr_runInEventThread( session, invokeRequest, req );
}

static void
enqueueRequest( tr_session * session,
                tr_tracker * tracker,
                int          reqtype )
{
    struct tr_tracker_request * req;
    assert( tr_isSession( session ) );

    req = createRequest( session, tracker, reqtype );
    tr_runInEventThread( session, invokeRequest, req );
}

static int
trackerPulse( void * vsession )
{
    tr_session *               session = vsession;
    struct tr_tracker_handle * th = session->tracker;
    tr_torrent *               tor;
    const time_t               now = time( NULL );

    if( !th )
        return FALSE;

    if( th->runningCount )
        dbgmsg( NULL, "tracker pulse... %d running", th->runningCount );

    /* upkeep: queue periodic rescrape / reannounce */
    tor = NULL;
    while(( tor = tr_torrentNext( session, tor )))
    {
        tr_tracker * t = tor->tracker;

        if( ( t->scrapeAt > 1 )
          && ( t->scrapeAt <= now )
          && ( trackerSupportsScrape( t, tor ) ) )
        {
            t->scrapeAt = TR_TRACKER_BUSY;
            enqueueScrape( session, t );
        }

        if( ( t->reannounceAt > 1 )
          && ( t->reannounceAt <= now )
          && ( t->isRunning ) )
        {
            t->reannounceAt = TR_TRACKER_BUSY;
            t->manualAnnounceAllowedAt = TR_TRACKER_BUSY;
            enqueueRequest( session, t, TR_REQ_REANNOUNCE );
        }

        if( tor->dhtAnnounceAt <= now ) {
            int rc = 1;
            if( tor->isRunning && tr_torrentAllowsDHT(tor) )
                rc = tr_dhtAnnounce(tor, 1);
            if(rc == 0)
                /* The DHT is not ready yet.  Try again soon. */
                tor->dhtAnnounceAt = now + 5 + tr_cryptoWeakRandInt( 5 );
            else
                /* We should announce at least once every 30 minutes. */
                tor->dhtAnnounceAt = now + 25 * 60 + tr_cryptoWeakRandInt( 3 * 60 );
        }
    }

    if( th->runningCount )
        dbgmsg( NULL, "tracker pulse after upkeep... %d running",
                th->runningCount );

    /* free the tracker manager if no torrents are left */
    if(    ( th != NULL )
        && ( th->shutdownHint != FALSE )
        && ( th->runningCount < 1 )
        && ( tr_sessionCountTorrents( session ) == 0 ) )
    {
        tr_trackerSessionDestroy( session );
        return FALSE;
    }

    return TRUE;
}

static void
onReqDone( tr_session * session )
{
    if( session->tracker )
    {
        --session->tracker->runningCount;
        dbgmsg( NULL, "decrementing runningCount to %d",
                session->tracker->runningCount );
        trackerPulse( session );
    }
}

/***
****  LIFE CYCLE
***/

static void
generateKeyParam( char * msg,
                  int    len )
{
    int          i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int    poolSize = strlen( pool );

    for( i = 0; i < len; ++i )
        *msg++ = pool[tr_cryptoRandInt( poolSize )];
    *msg = '\0';
}

static int
is_rfc2396_alnum( char ch )
{
    return ( '0' <= ch && ch <= '9' )
           || ( 'A' <= ch && ch <= 'Z' )
           || ( 'a' <= ch && ch <= 'z' );
}

static void
escape( char *          out,
        const uint8_t * in,
        int             in_len )                     /* rfc2396 */
{
    const uint8_t *end = in + in_len;

    while( in != end )
        if( is_rfc2396_alnum( *in ) )
            *out++ = (char) *in++;
        else
            out += tr_snprintf( out, 4, "%%%02X", (unsigned int)*in++ );

    *out = '\0';
}

tr_tracker *
tr_trackerNew( const tr_torrent * torrent )
{
    const tr_info * info = &torrent->info;
    tr_tracker *    t;

    t = tr_new0( tr_tracker, 1 );
    t->publisher                = TR_PUBLISHER_INIT;
    t->session                  = torrent->session;
    t->scrapeIntervalSec        = DEFAULT_SCRAPE_INTERVAL_SEC;
    t->retryScrapeIntervalSec   = FIRST_SCRAPE_RETRY_INTERVAL_SEC;
    t->retryAnnounceIntervalSec = FIRST_ANNOUNCE_RETRY_INTERVAL_SEC;
    t->announceIntervalSec      = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    t->announceMinIntervalSec   = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    t->timesDownloaded          = -1;
    t->seederCount              = -1;
    t->downloaderCount          = -1;
    t->leecherCount             = -1;
    t->manualAnnounceAllowedAt  = ~(time_t)0;
    t->name                     = tr_strdup( info->name );
    t->torrentId                = torrent->uniqueId;
    t->randOffset               = tr_cryptoRandInt( 30 );
    memcpy( t->hash, info->hash, SHA_DIGEST_LENGTH );
    escape( t->escaped, info->hash, SHA_DIGEST_LENGTH );
    generateKeyParam( t->key_param, KEYLEN );

    t->trackerIndex = 0;

    if( trackerSupportsScrape( t, torrent ) )
        t->scrapeAt = time( NULL ) + t->randOffset;

    return t;
}

static void
onTrackerFreeNow( void * vt )
{
    tr_tracker * t = vt;

    tr_publisherDestruct( &t->publisher );
    tr_free( t->name );
    tr_free( t->trackerID );
    tr_free( t->peer_id );

    tr_free( t );
}

/***
****  PUBLIC
***/

void
tr_trackerFree( tr_tracker * t )
{
    if( t )
        tr_runInEventThread( t->session, onTrackerFreeNow, t );
}

tr_publisher_tag
tr_trackerSubscribe( tr_tracker *     t,
                     tr_delivery_func func,
                     void *           user_data )
{
    return tr_publisherSubscribe( &t->publisher, func, user_data );
}

void
tr_trackerUnsubscribe( tr_tracker *     t,
                       tr_publisher_tag tag )
{
    if( t )
        tr_publisherUnsubscribe( &t->publisher, tag );
}

const tr_tracker_info *
tr_trackerGetAddress( tr_tracker * t, const tr_torrent * torrent )
{
    return getCurrentAddressFromTorrent( t, torrent );
}

time_t
tr_trackerGetManualAnnounceTime( const struct tr_tracker * t )
{
    return t->isRunning ? t->manualAnnounceAllowedAt : 0;
}

int
tr_trackerCanManualAnnounce( const tr_tracker * t )
{
    const time_t allow = tr_trackerGetManualAnnounceTime( t );

    return allow && ( allow <= time( NULL ) );
}

void
tr_trackerGetCounts( const tr_tracker * t,
                     int              * setme_completedCount,
                     int              * setme_leecherCount,
                     int              * setme_seederCount,
                     int              * setme_downloaderCount )
{
    if( setme_completedCount )
        *setme_completedCount = t->timesDownloaded;

    if( setme_leecherCount )
        *setme_leecherCount = t->leecherCount;

    if( setme_seederCount )
        *setme_seederCount = t->seederCount;

    if( setme_downloaderCount )
        *setme_downloaderCount = t->downloaderCount;
}

void
tr_trackerStart( tr_tracker * t )
{
    if( t && !t->isRunning )
    {
        tr_torrent * tor;

        /* change the peer-id */
        tr_free( t->peer_id );
        t->peer_id = tr_peerIdNew( );
        if(( tor = tr_torrentFindFromHash( t->session, t->hash ))) {
            tr_free( tor->peer_id );
            tor->peer_id = (uint8_t*) tr_strdup( t->peer_id );
        }

        t->isRunning = 1;
        enqueueRequest( t->session, t, TR_REQ_STARTED );
    }
}

void
tr_trackerReannounce( tr_tracker * t )
{
    enqueueRequest( t->session, t, TR_REQ_REANNOUNCE );
}

void
tr_trackerCompleted( tr_tracker * t )
{
    enqueueRequest( t->session, t, TR_REQ_COMPLETED );
}

void
tr_trackerStop( tr_tracker * t )
{
    if( t && t->isRunning )
    {
        t->isRunning = 0;
        t->reannounceAt = TR_TRACKER_STOPPED;
        t->manualAnnounceAllowedAt = TR_TRACKER_STOPPED;
        enqueueRequest( t->session, t, TR_REQ_STOPPED );
    }
}

void
tr_trackerChangeMyPort( tr_tracker * t )
{
    if( t->isRunning )
        tr_trackerReannounce( t );
}

void
tr_trackerStat( const tr_tracker * t,
                struct tr_stat *   setme )
{
    assert( t );
    assert( setme );

    setme->lastScrapeTime = t->lastScrapeTime;
    setme->nextScrapeTime = t->scrapeAt;
    setme->lastAnnounceTime = t->lastAnnounceTime;
    setme->nextAnnounceTime = t->reannounceAt;
    setme->manualAnnounceTime = t->manualAnnounceAllowedAt;
    tr_strlcpy( setme->scrapeResponse, t->lastScrapeStr, sizeof( setme->scrapeResponse ) );
    tr_strlcpy( setme->announceResponse, t->lastAnnounceStr, sizeof( setme->announceResponse ) );
}
