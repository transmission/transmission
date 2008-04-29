/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include <stdio.h> /* snprintf */
#include <stdlib.h>
#include <string.h> /* strcmp, strchr */

#include <event.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "net.h"
#include "port-forwarding.h"
#include "publish.h"
#include "torrent.h"
#include "tracker.h"
#include "trcompat.h" /* strlcpy */
#include "trevent.h"
#include "utils.h"
#include "web.h"

enum
{
    HTTP_OK = 200,

    /* seconds between tracker pulses */
    PULSE_INTERVAL_MSEC = 1000,

    /* maximum number of concurrent tracker socket connections */
    MAX_TRACKER_SOCKETS = 16,

    /* maximum number of concurrent tracker socket connections during shutdown.
     * all the peer connections should be gone by now, so we can hog more 
     * connections to send `stop' messages to the trackers */
    MAX_TRACKER_SOCKETS_DURING_SHUTDOWN = 64,

    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = (60 * 15),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = (60 * 4),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = (60 * 2),

    /* this is how long we'll leave a request hanging before timeout */
    TIMEOUT_INTERVAL_SEC = 30,

    /* this is how long we'll leave a 'stop' request hanging before timeout.
       we wait less time for this so it doesn't slow down shutdowns */
    STOP_TIMEOUT_INTERVAL_SEC = 5,

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 80,

    /* the length of the 'key' argument passed in tracker requests */
    KEYLEN = 10
};

/**
***
**/

struct tr_tracker
{
    unsigned int isRunning     : 1;

    uint8_t randOffset;

    tr_session * session;

    /* these are set from the latest scrape or tracker response */
    int announceIntervalSec;
    int announceMinIntervalSec;
    int scrapeIntervalSec;
    int retryScrapeIntervalSec;

    /* index into the torrent's tr_info.trackers array */
    int trackerIndex;

    /* sent as the "key" argument in tracker requests
       to verify us if our IP address changes.
       This is immutable for the life of the tracker object. */
    char key_param[KEYLEN+1];

    tr_publisher_t * publisher;

    /* torrent hash string */
    uint8_t hash[SHA_DIGEST_LENGTH];
    char escaped[SHA_DIGEST_LENGTH*3 + 1];
    char * name;

    /* corresponds to the peer_id sent as a tracker request parameter.
       one tracker admin says: "When the same torrent is opened and
       closed and opened again without quitting Transmission ...
       change the peerid. It would help sometimes if a stopped event
       was missed to ensure that we didn't think someone was cheating. */
    uint8_t * peer_id;

    /* these are set from the latest tracker response... -1 is 'unknown' */
    int timesDownloaded;
    int seederCount;
    int leecherCount;
    char * trackerID;

    time_t manualAnnounceAllowedAt;
    time_t reannounceAt;
    time_t scrapeAt;

    time_t lastScrapeTime;
    long lastScrapeResponse;

    time_t lastAnnounceTime;
    long lastAnnounceResponse;
};

#define dbgmsg(name, fmt...) tr_deepLog(__FILE__, __LINE__, name, ##fmt )

/***
****
***/

static const tr_tracker_info *
getCurrentAddressFromTorrent( const tr_tracker * t, const tr_torrent * tor )
{
    assert( t->trackerIndex >= 0 );
    assert( t->trackerIndex < tor->info.trackerCount );
    return tor->info.trackers + t->trackerIndex;
}
    
static const tr_tracker_info *
getCurrentAddress( const tr_tracker * t )
{
    const tr_torrent * torrent;
    if(( torrent = tr_torrentFindFromHash( t->session, t->hash )))
        return getCurrentAddressFromTorrent( t, torrent );
    return NULL;
}

static int
trackerSupportsScrape( const tr_tracker * t, const tr_torrent * tor )
{
    const tr_tracker_info * info = getCurrentAddressFromTorrent( t, tor );
    return info && info->scrape;
}

/***
****
***/

tr_tracker *
findTracker( tr_session * session, const uint8_t * hash )
{
    tr_torrent * torrent = tr_torrentFindFromHash( session, hash );
    return torrent ? torrent->tracker : NULL;
}

/***
****  PUBLISH
***/

static const tr_tracker_event emptyEvent = { 0, NULL, NULL, NULL, 0, 0 };

static void
publishMessage( tr_tracker * t, const char * msg, int type )
{
    if( t )
    {
        tr_tracker_event event = emptyEvent;
        event.hash = t->hash;
        event.messageType = type;
        event.text = msg;
        tr_publisherPublish( t->publisher, t, &event );
    }
}

static void
publishErrorClear( tr_tracker * t )
{
    publishMessage( t, NULL, TR_TRACKER_ERROR_CLEAR );
}

static void
publishErrorMessageAndStop( tr_tracker * t, const char * msg )
{
    t->isRunning = 0;
    publishMessage( t, msg, TR_TRACKER_ERROR );
}

static void
publishWarning( tr_tracker * t, const char * msg )
{
    publishMessage( t, msg, TR_TRACKER_WARNING );
}

static void
publishNewPeers( tr_tracker * t, int allAreSeeds,
                 void * compact, int compactLen )
{
    tr_tracker_event event = emptyEvent;
    event.hash = t->hash;
    event.messageType = TR_TRACKER_PEERS;
    event.allAreSeeds = allAreSeeds;
    event.compact = compact;
    event.compactLen = compactLen;
    if( compactLen )
        tr_publisherPublish( t->publisher, t, &event );
}

/***
****
***/

static void onReqDone( tr_session * session );

static void
updateAddresses( tr_tracker  * t,
                 long          response_code,
                 int           moveToNextAddress,
                 int         * tryAgain )
{
    tr_torrent * torrent = tr_torrentFindFromHash( t->session, t->hash );


    if( !response_code ) /* tracker didn't respond */
    {
        tr_ninf( t->name, _( "Tracker hasn't responded yet.  Retrying..." ) );
        moveToNextAddress = TRUE;
    }
    else if( response_code == HTTP_OK )
    {
#if 0
/* FIXME */
        /* multitracker spec: "if a connection with a tracker is
           successful, it will be moved to the front of the tier." */
        const int i = t->addressIndex;
        const int j = t->tierFronts[i];
        const tr_tracker_info swap = t->addresses[i];
        t->addresses[i] = t->addresses[j];
        t->addresses[j] = swap;
#endif
    }
    else 
    {
        moveToNextAddress = TRUE;
    }

    *tryAgain = moveToNextAddress;
    if( moveToNextAddress )
    {
        if ( ++t->trackerIndex >= torrent->info.trackerCount ) /* we've tried them all */
        {
            *tryAgain = FALSE;
            t->trackerIndex = 0;
        }
        else
        {
            const tr_tracker_info * n = getCurrentAddressFromTorrent( t, torrent );
            tr_ninf( t->name, _( "Trying tracker \"%s\"" ), n->announce );
        }
    }
}

/* Convert to compact form */
static uint8_t *
parseOldPeers( tr_benc * bePeers, size_t * byteCount )
{
    int i;
    uint8_t *compact, *walk;
    const int peerCount = bePeers->val.l.count;

    assert( bePeers->type == TYPE_LIST );

    compact = tr_new( uint8_t, peerCount*6 );

    for( i=0, walk=compact; i<peerCount; ++i )
    {
        const char * s;
        int64_t itmp;
        struct in_addr addr;
        tr_port_t port;
        tr_benc * peer = &bePeers->val.l.vals[i];

        if( !tr_bencDictFindStr( peer, "ip", &s ) || tr_netResolve( s, &addr ) )
            continue;

        memcpy( walk, &addr, 4 );
        walk += 4;

        if( !tr_bencDictFindInt( peer, "port", &itmp ) || itmp<0 || itmp>0xffff )
            continue;

        port = htons( itmp );
        memcpy( walk, &port, 2 );
        walk += 2;
    }

    *byteCount = peerCount * 6;
    return compact;
}

static void
onStoppedResponse( tr_session    * session,
                   long            responseCode  UNUSED,
                   const void    * response      UNUSED,
                   size_t          responseLen   UNUSED,
                   void          * torrent_hash  UNUSED )
{
    dbgmsg( NULL, "got a response to some `stop' message" );
    onReqDone( session );
}

static void
onTrackerResponse( tr_session    * session,
                   long            responseCode,
                   const void    * response,
                   size_t          responseLen,
                   void          * torrent_hash )
{
    int moveToNextAddress = FALSE;
    int tryAgain;
    tr_tracker * t;

    onReqDone( session );
    t = findTracker( session, torrent_hash );
    tr_free( torrent_hash );
    if( !t ) /* tracker's been closed */
        return;

    dbgmsg( t->name, "tracker response: %d", responseCode );
    tr_ndbg( t->name, "tracker response: %d", responseCode );
    t->lastAnnounceResponse = responseCode;

    if( responseCode == HTTP_OK )
    {
        tr_benc benc;
        const int bencLoaded = !tr_bencLoad( response, responseLen, &benc, 0 );
        publishErrorClear( t );
        if( bencLoaded && tr_bencIsDict( &benc ) )
        {
            tr_benc * tmp;
            int64_t i;
            int incomplete = -1;
            const char * str;

            if(( tr_bencDictFindStr( &benc, "failure reason", &str ))) {
               // publishErrorMessageAndStop( t, str );
                moveToNextAddress = TRUE;
                publishMessage( t, str, TR_TRACKER_ERROR );
            }

            if(( tr_bencDictFindStr( &benc, "warning message", &str )))
                publishWarning( t, str );

            if(( tr_bencDictFindInt( &benc, "interval", &i ))) {
                dbgmsg( t->name, "setting interval to %d", (int)i );
                t->announceIntervalSec = i;
            }

            if(( tr_bencDictFindInt( &benc, "min interval", &i ))) {
                dbgmsg( t->name, "setting min interval to %d", (int)i );
                t->announceMinIntervalSec = i;
            }

            if(( tr_bencDictFindStr( &benc, "tracker id", &str )))
                t->trackerID = tr_strdup( str );

            if(( tr_bencDictFindInt( &benc, "complete", &i )))
                t->seederCount = i;

            if(( tr_bencDictFindInt( &benc, "incomplete", &i )))
                t->leecherCount = incomplete = i;

            if(( tmp = tr_bencDictFind( &benc, "peers" )))
            {
                const int allAreSeeds = incomplete == 0;

                if( tmp->type == TYPE_STR ) /* "compact" extension */
                {
                    publishNewPeers( t, allAreSeeds, tmp->val.s.s, tmp->val.s.i );
                }
                else if( tmp->type == TYPE_LIST ) /* original protocol */
                {
                    size_t byteCount = 0;
                    uint8_t * compact = parseOldPeers( tmp, &byteCount );
                    publishNewPeers( t, allAreSeeds, compact, byteCount );
                    tr_free( compact );
                }
            }
        }

        if( bencLoaded )
            tr_bencFree( &benc );
    }

    updateAddresses( t, responseCode, moveToNextAddress, &tryAgain );

    /**
    ***
    **/

    if( tryAgain )
        responseCode = 300;

    if( 200<=responseCode && responseCode<=299 )
    {
        const int interval = t->announceIntervalSec + t->randOffset;
        const time_t now = time ( NULL );
        dbgmsg( t->name, "request succeeded. reannouncing in %d seconds", interval );
        t->scrapeAt = now + t->scrapeIntervalSec + t->randOffset;
        t->reannounceAt = now + interval;
        t->manualAnnounceAllowedAt = now + t->announceMinIntervalSec;
    }
    else if( 300<=responseCode && responseCode<=399 )
    {
        /* it's a redirect... updateAddresses() has already
         * parsed the redirect, all that's left is to retry */
        const int interval = 5;
        dbgmsg( t->name, "got a redirect. retrying in %d seconds", interval );
        t->reannounceAt = time( NULL ) + interval;
        t->manualAnnounceAllowedAt = time( NULL ) + t->announceMinIntervalSec;
    }
    else if( 400<=responseCode && responseCode<=499 )
    {
        /* The request could not be understood by the server due to
         * malformed syntax. The client SHOULD NOT repeat the
         * request without modifications. */
        publishErrorMessageAndStop( t, _( "Tracker returned a 4xx message" ) );
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = 0;
    }
    else if( 500<=responseCode && responseCode<=599 )
    {
        /* Response status codes beginning with the digit "5" indicate
         * cases in which the server is aware that it has erred or is
         * incapable of performing the request.  So we pause a bit and
         * try again. */
        t->manualAnnounceAllowedAt = ~(time_t)0;
        t->reannounceAt = time( NULL ) + 60;
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
onScrapeResponse( tr_session   * session,
                  long           responseCode,
                  const void   * response,
                  size_t         responseLen,
                  void         * torrent_hash )
{
    int moveToNextAddress = FALSE;
    int tryAgain;
    tr_tracker * t;

    onReqDone( session );
    t = findTracker( session, torrent_hash );
    tr_free( torrent_hash );
    if( !t ) /* tracker's been closed... */
        return;

    dbgmsg( t->name, "scrape response: %ld\n", responseCode );
    tr_ndbg( t->name, "scrape response: %d", responseCode );
    t->lastScrapeResponse = responseCode;

    if( responseCode == HTTP_OK )
    {
        tr_benc benc, *files;
        const int bencLoaded = !tr_bencLoad( response, responseLen, &benc, 0 );
        if( bencLoaded && tr_bencDictFindDict( &benc, "files", &files ) )
        {
            int i;
            for( i=0; i<files->val.l.count; i+=2 )
            {
                int64_t itmp;
                const uint8_t* hash =
                    (const uint8_t*) files->val.l.vals[i].val.s.s;
                tr_benc * flags;
                tr_benc * tordict = &files->val.l.vals[i+1];
                if( memcmp( t->hash, hash, SHA_DIGEST_LENGTH ) )
                    continue;

                publishErrorClear( t );

                if(( tr_bencDictFindInt( tordict, "complete", &itmp )))
                    t->seederCount = itmp;

                if(( tr_bencDictFindInt( tordict, "incomplete", &itmp )))
                    t->leecherCount = itmp;

                if(( tr_bencDictFindInt( tordict, "downloaded", &itmp )))
                    t->timesDownloaded = itmp;

                if( tr_bencDictFindDict( tordict, "flags", &flags ))
                    if(( tr_bencDictFindInt( flags, "min_request_interval", &itmp )))
                        t->scrapeIntervalSec = i;

                tr_ndbg( t->name, "Scrape successful.  Rescraping in %d seconds.",
                         t->scrapeIntervalSec );

                t->retryScrapeIntervalSec = 30;
            }
        }
        else
            moveToNextAddress = TRUE;

        if( bencLoaded )
            tr_bencFree( &benc );
    }

    updateAddresses( t, responseCode, moveToNextAddress, &tryAgain );

    /**
    ***
    **/

    if( tryAgain )
        responseCode = 300;

    if( 200<=responseCode && responseCode<=299 )
    {
        const int interval = t->scrapeIntervalSec + t->randOffset;
        dbgmsg( t->name, "request succeeded. rescraping in %d seconds", interval );
        tr_ndbg( t->name, "request succeeded. rescraping in %d seconds", interval );
        t->scrapeAt = time( NULL ) + interval;
    }
    else if( 300<=responseCode && responseCode<=399 )
    {
        const int interval = 5;
        dbgmsg( t->name, "got a redirect. retrying in %d seconds", interval );
        t->scrapeAt = time( NULL ) + interval;
    }
    else
    {
        const int interval = t->retryScrapeIntervalSec + t->randOffset;
        dbgmsg( t->name, "Tracker responded to scrape with %d.  Retrying in %d seconds.",
                   responseCode,  interval );
        t->retryScrapeIntervalSec *= 2;
        t->scrapeAt = time( NULL ) + interval;
    }
}

/***
****
***/

enum
{
    TR_REQ_STARTED,
    TR_REQ_COMPLETED,
    TR_REQ_STOPPED,
    TR_REQ_REANNOUNCE,
    TR_REQ_SCRAPE,
    TR_REQ_COUNT
};

struct tr_tracker_request
{
    char * url;
    int reqtype; /* TR_REQ_* */
    uint8_t torrent_hash[SHA_DIGEST_LENGTH];
    tr_web_done_func * done_func;
    tr_session * session;
};

static void
freeRequest( struct tr_tracker_request * req )
{
    tr_free( req->url );
    tr_free( req );
}

static void
buildTrackerRequestURI( const tr_tracker  * t,
                        const tr_torrent  * torrent,
                        const char        * eventName,
                        struct evbuffer   * buf )
{
    const int isStopping = !strcmp( eventName, "stopped" );
    const int numwant = isStopping ? 0 : NUMWANT;
    const char * ann = getCurrentAddressFromTorrent(t,torrent)->announce;
    
    evbuffer_add_printf( buf, "%cinfo_hash=%s"
                              "&peer_id=%s"
                              "&port=%d"
                              "&uploaded=%"PRIu64
                              "&downloaded=%"PRIu64
                              "&corrupt=%"PRIu64
                              "&left=%"PRIu64
                              "&compact=1"
                              "&numwant=%d"
                              "&key=%s"
                              "%s%s"
                              "%s%s",
        strchr(ann, '?') ? '&' : '?',
        t->escaped,
        t->peer_id,
        tr_sharedGetPublicPort( t->session->shared ),
        torrent->uploadedCur,
        torrent->downloadedCur,
        torrent->corruptCur,
        tr_cpLeftUntilComplete( torrent->completion ),
        numwant,
        t->key_param,
        ( ( eventName && *eventName ) ? "&event=" : "" ),
        ( ( eventName && *eventName ) ? eventName : "" ),
        ( ( t->trackerID && *t->trackerID ) ? "&trackerid=" : "" ),
        ( ( t->trackerID && *t->trackerID ) ? t->trackerID : "" ) );
}

static struct tr_tracker_request*
createRequest( tr_session * session, const tr_tracker * tracker, int reqtype )
{
    static const char* strings[] = { "started", "completed", "stopped", "", "err" };
    const tr_torrent * torrent = tr_torrentFindFromHash( session, tracker->hash );
    const tr_tracker_info * address = getCurrentAddressFromTorrent( tracker, torrent );
    const int isStopping = reqtype == TR_REQ_STOPPED;
    struct tr_tracker_request * req;
    struct evbuffer * url;

    url = evbuffer_new( );
    evbuffer_add_printf( url, "%s", address->announce );
    buildTrackerRequestURI( tracker, torrent, strings[reqtype], url );

    req = tr_new0( struct tr_tracker_request, 1 );
    req->session = session;
    req->reqtype = reqtype;
    req->done_func =  isStopping ? onStoppedResponse : onTrackerResponse;
    req->url = tr_strdup( ( char * ) EVBUFFER_DATA( url ) );
    memcpy( req->torrent_hash, tracker->hash, SHA_DIGEST_LENGTH );

    evbuffer_free( url );
    return req;
}

static struct tr_tracker_request*
createScrape( tr_session * session, const tr_tracker * tracker )
{
    const tr_tracker_info * a = getCurrentAddress( tracker );
    struct tr_tracker_request * req;
    struct evbuffer * url = evbuffer_new( );

    evbuffer_add_printf( url, "%s%cinfo_hash=%s",
                         a->scrape, strchr(a->scrape,'?')?'&':'?',
                         tracker->escaped ); 

    req = tr_new0( struct tr_tracker_request, 1 );
    req->session = session;
    req->reqtype = TR_REQ_SCRAPE;
    req->url = tr_strdup( ( char * ) EVBUFFER_DATA( url ) );
    req->done_func = onScrapeResponse;
    memcpy( req->torrent_hash, tracker->hash, SHA_DIGEST_LENGTH );

    evbuffer_free( url );
    return req;
}

struct tr_tracker_handle
{
    unsigned int isShuttingDown : 1;
    int runningCount;
    tr_timer * pulseTimer;
};

static int pulse( void * vsession );

static void
ensureGlobalsExist( tr_session * session )
{
    if( session->tracker == NULL )
    {
        session->tracker = tr_new0( struct tr_tracker_handle, 1 );
        session->tracker->pulseTimer = tr_timerNew( session, pulse, session, PULSE_INTERVAL_MSEC );
        dbgmsg( NULL, "creating tracker timer" );
    }
}

void
tr_trackerShuttingDown( tr_session * session )
{
    if( session->tracker )
        session->tracker->isShuttingDown = 1;
}

static int
maybeFreeGlobals( tr_session * session )
{
    int globalsExist = session->tracker != NULL;

    if( globalsExist
        && ( session->tracker->runningCount < 1 )
        && ( session->torrentList== NULL ) )
    {
        dbgmsg( NULL, "freeing tracker timer" );
        tr_timerFree( &session->tracker->pulseTimer );
        tr_free( session->tracker );
        session->tracker = NULL;
        globalsExist = FALSE;
    }

    return globalsExist;
}

/***
****
***/

static void
invokeRequest( void * vreq )
{
    struct tr_tracker_request * req = vreq;
    uint8_t * hash;
    tr_tracker * t = findTracker( req->session, req->torrent_hash );

    if( t )
    {
        const time_t now = time( NULL );

        if( req->reqtype == TR_REQ_SCRAPE )
        {
            t->lastScrapeTime = now;
            t->scrapeAt = 0;
        }
        else
        {
            t->lastAnnounceTime = now;
            t->reannounceAt = 0;
            t->scrapeAt = req->reqtype == TR_REQ_STOPPED
                        ? now + t->scrapeIntervalSec + t->randOffset
                        : 0;
        }
    }

    ++req->session->tracker->runningCount;

    hash = tr_new0( uint8_t, SHA_DIGEST_LENGTH );
    memcpy( hash, req->torrent_hash, SHA_DIGEST_LENGTH );
    tr_webRun( req->session, req->url, req->done_func, hash );

    freeRequest( req );
}

static void ensureGlobalsExist( tr_session * );

static void
enqueueScrape( tr_session * session, const tr_tracker * tracker )
{
    struct tr_tracker_request * req;
    ensureGlobalsExist( session );
    req = createScrape( session, tracker );
    tr_runInEventThread( session, invokeRequest, req );
}

static void
enqueueRequest( tr_session * session, const tr_tracker * tracker, int reqtype )
{
    struct tr_tracker_request * req;
    ensureGlobalsExist( session );
    req = createRequest( session, tracker, reqtype );
    tr_runInEventThread( session, invokeRequest, req );
}

static int
pulse( void * vsession )
{
    tr_session * session = vsession;
    struct tr_tracker_handle * th = session->tracker;
    tr_torrent * tor;
    const time_t now = time( NULL );

    if( !session->tracker )
        return FALSE;

    if( th->runningCount )
        dbgmsg( NULL, "tracker pulse... %d running", th->runningCount );

    /* upkeep: queue periodic rescrape / reannounce */
    for( tor=session->torrentList; tor; tor=tor->next )
    {
        tr_tracker * t = tor->tracker;

        if( t->scrapeAt && trackerSupportsScrape( t, tor ) && ( now >= t->scrapeAt ) ) {
            t->scrapeAt = 0;
            enqueueScrape( session, t );
        }

        if( t->reannounceAt && t->isRunning && ( now >= t->reannounceAt ) ) {
            t->reannounceAt = 0;
            enqueueRequest( session, t, TR_REQ_REANNOUNCE );
        }
    }

    if( th->runningCount )
        dbgmsg( NULL, "tracker pulse after upkeep... %d running", th->runningCount );

    return maybeFreeGlobals( session );
}

static void
onReqDone( tr_session * session )
{
    if( session->tracker )
    {
        --session->tracker->runningCount;
        dbgmsg( NULL, "decrementing runningCount to %d", session->tracker->runningCount );
        pulse( session );
    }
}

/***
****  LIFE CYCLE
***/

static void
generateKeyParam( char * msg, int len )
{
    int i;
    const char * pool = "abcdefghijklmnopqrstuvwxyz0123456789";
    const int poolSize = strlen( pool );
    for( i=0; i<len; ++i )
        *msg++ = pool[tr_rand(poolSize)];
    *msg = '\0';
}

static int
is_rfc2396_alnum( char ch )
{
    return ( (ch >= 'a' && ch <= 'z' )
            || (ch >= 'A' && ch <= 'Z' )
            || (ch >= '0' && ch <= '9' ) );
}

static void
escape( char * out, const uint8_t * in, int in_len ) /* rfc2396 */
{
    const uint8_t *end = in + in_len;
    while( in != end )
        if( is_rfc2396_alnum(*in) )
            *out++ = (char) *in++;
        else 
            out += snprintf( out, 4, "%%%02X", (unsigned int)*in++ );
    *out = '\0';
}

tr_tracker *
tr_trackerNew( const tr_torrent * torrent )
{
    const tr_info * info = &torrent->info;
    tr_tracker * t;

    t = tr_new0( tr_tracker, 1 );
    t->publisher = tr_publisherNew( );
    t->session                  = torrent->handle;
    t->scrapeIntervalSec        = DEFAULT_SCRAPE_INTERVAL_SEC;
    t->retryScrapeIntervalSec   = 60;
    t->announceIntervalSec      = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    t->announceMinIntervalSec   = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    t->timesDownloaded          = -1;
    t->seederCount              = -1;
    t->leecherCount             = -1;
    t->lastAnnounceResponse     = -1;
    t->lastScrapeResponse       = -1;
    t->manualAnnounceAllowedAt  = ~(time_t)0;
    t->name = tr_strdup( info->name );
    t->randOffset = tr_rand( 120 );
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

    tr_publisherFree( &t->publisher );
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
tr_trackerSubscribe( tr_tracker          * t,
                     tr_delivery_func      func,
                     void                * user_data )
{
    return tr_publisherSubscribe( t->publisher, func, user_data );
}

void
tr_trackerUnsubscribe( tr_tracker        * t,
                       tr_publisher_tag    tag )
{
    if( t )
        tr_publisherUnsubscribe( t->publisher, tag );
}

const tr_tracker_info *
tr_trackerGetAddress( const tr_tracker   * t )
{
    return getCurrentAddress( t );
}

time_t
tr_trackerGetManualAnnounceTime( const struct tr_tracker * t )
{
    return t->isRunning ? t->manualAnnounceAllowedAt : 0;
}

int
tr_trackerCanManualAnnounce ( const tr_tracker * t)
{
    const time_t allow = tr_trackerGetManualAnnounceTime( t );
    return allow && ( allow <= time( NULL ) );
}

void
tr_trackerGetCounts( const tr_tracker  * t,
                     int               * setme_completedCount,
                     int               * setme_leecherCount,
                     int               * setme_seederCount )
{
    if( setme_completedCount )
       *setme_completedCount = t->timesDownloaded;

    if( setme_leecherCount )
       *setme_leecherCount = t->leecherCount;

    if( setme_seederCount )
       *setme_seederCount = t->seederCount;
}


void
tr_trackerStart( tr_tracker * t )
{
    if( t && !t->isRunning )
    {
        tr_free( t->peer_id );
        t->peer_id = tr_peerIdNew( );

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
    if( t && t->isRunning ) {
        t->isRunning = 0;
        t->reannounceAt = t->manualAnnounceAllowedAt = 0;
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
tr_trackerStat( const tr_tracker       * t,
                struct tr_tracker_stat * setme)
{
    assert( t );
    assert( setme );

    setme->lastScrapeTime = t->lastScrapeTime;
    setme->nextScrapeTime = t->scrapeAt;
    setme->lastAnnounceTime = t->lastAnnounceTime;
    setme->nextAnnounceTime = t->reannounceAt;
    setme->nextManualAnnounceTime = t->manualAnnounceAllowedAt;

    if( t->lastScrapeResponse == -1 ) /* never been scraped */
        *setme->scrapeResponse = '\0';
    else
        snprintf( setme->scrapeResponse,
                  sizeof( setme->scrapeResponse ),
                  "%s (%ld)",
                  tr_webGetResponseStr( t->lastScrapeResponse ),
                  t->lastScrapeResponse );

    if( t->lastAnnounceResponse == -1 ) /* never been announced */
        *setme->announceResponse = '\0';
    else
        snprintf( setme->announceResponse,
                  sizeof( setme->announceResponse ),
                  "%s (%ld)",
                  tr_webGetResponseStr( t->lastAnnounceResponse ),
                  t->lastAnnounceResponse );
}
