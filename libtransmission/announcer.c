/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
#include <limits.h> /* INT_MAX */
#include <stdio.h>
#include <stdlib.h> /* qsort () */
#include <string.h> /* strcmp (), memcpy () */

#include <event2/buffer.h>
#include <event2/event.h> /* evtimer */

#define __LIBTRANSMISSION_ANNOUNCER_MODULE__

#include "transmission.h"
#include "announcer.h"
#include "announcer-common.h"
#include "crypto-utils.h" /* tr_rand_int (), tr_rand_int_weak () */
#include "log.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex () */
#include "ptrarray.h"
#include "session.h"
#include "torrent.h"
#include "utils.h"

struct tr_tier;

static void tier_build_log_name (const struct tr_tier * tier,
                                 char * buf, size_t buflen);

#define dbgmsg(tier, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        { \
          char name[128]; \
          tier_build_log_name (tier, name, sizeof (name)); \
          tr_logAddDeep (__FILE__, __LINE__, name, __VA_ARGS__); \
        } \
    } \
  while (0)

enum
{
    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = (60 * 30),

    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = (60 * 10),

    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = (60 * 2),

    /* how many web tasks we allow at one time */
    MAX_CONCURRENT_TASKS = 48,

    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 80,

    UPKEEP_INTERVAL_SECS = 1,

    /* this is how often to call the UDP tracker upkeep */
    TAU_UPKEEP_INTERVAL_SECS = 5
};

/***
****
***/

const char*
tr_announce_event_get_string (tr_announce_event e)
{
    switch (e)
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
compareTransfer (uint64_t a_uploaded, uint64_t a_downloaded,
                 uint64_t b_uploaded, uint64_t b_downloaded)
{
    /* higher upload count goes first */
    if (a_uploaded != b_uploaded)
        return a_uploaded > b_uploaded ? -1 : 1;

    /* then higher download count goes first */
    if (a_downloaded != b_downloaded)
        return a_downloaded > b_downloaded ? -1 : 1;

    return 0;
}

/**
 * Comparison function for tr_announce_requests.
 *
 * The primary key (amount of data transferred) is used to prioritize
 * tracker announcements of active torrents. The remaining keys are
 * used to satisfy the uniqueness requirement of a sorted tr_ptrArray.
 */
static int
compareStops (const void * va, const void * vb)
{
    int i;
    const tr_announce_request * a = va;
    const tr_announce_request * b = vb;

    /* primary key: volume of data transferred. */
    if ((i = compareTransfer (a->up, a->down, b->up, b->down)))
        return i;

    /* secondary key: the torrent's info_hash */
    if ((i = memcmp (a->info_hash, b->info_hash, SHA_DIGEST_LENGTH)))
        return i;

    /* tertiary key: the tracker's announec url */
    return tr_strcmp0 (a->url, b->url);
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
    time_t tauUpkeepAt;
}
tr_announcer;

bool
tr_announcerHasBacklog (const struct tr_announcer * announcer)
{
    return announcer->slotsAvailable < 1;
}

static void
onUpkeepTimer (evutil_socket_t foo UNUSED, short bar UNUSED, void * vannouncer);

void
tr_announcerInit (tr_session * session)
{
    tr_announcer * a;

    assert (tr_isSession (session));

    a = tr_new0 (tr_announcer, 1);
    a->stops = TR_PTR_ARRAY_INIT;
    a->key = tr_rand_int (INT_MAX);
    a->session = session;
    a->slotsAvailable = MAX_CONCURRENT_TASKS;
    a->upkeepTimer = evtimer_new (session->event_base, onUpkeepTimer, a);
    tr_timerAdd (a->upkeepTimer, UPKEEP_INTERVAL_SECS, 0);

    session->announcer = a;
}

static void flushCloseMessages (tr_announcer * announcer);

void
tr_announcerClose (tr_session * session)
{
    tr_announcer * announcer = session->announcer;

    flushCloseMessages (announcer);

    tr_tracker_udp_start_shutdown (session);

    event_free (announcer->upkeepTimer);
    announcer->upkeepTimer = NULL;

    tr_ptrArrayDestruct (&announcer->stops, NULL);

    session->announcer = NULL;
    tr_free (announcer);
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
getKey (const char * url)
{
    char * ret;
    char * scheme = NULL;
    char * host = NULL;
    int port = 0;

    tr_urlParse (url, TR_BAD_SIZE, &scheme, &host, &port, NULL);
    ret = tr_strdup_printf ("%s://%s:%d", (scheme?scheme:"invalid"), (host?host:"invalid"), port);

    tr_free (host);
    tr_free (scheme);
    return ret;
}

static void
trackerConstruct (tr_tracker * tracker, const tr_tracker_info * inf)
{
    memset (tracker, 0, sizeof (tr_tracker));
    tracker->key = getKey (inf->announce);
    tracker->announce = tr_strdup (inf->announce);
    tracker->scrape = tr_strdup (inf->scrape);
    tracker->id = inf->id;
    tracker->seederCount = -1;
    tracker->leecherCount = -1;
    tracker->downloadCount = -1;
}

static void
trackerDestruct (tr_tracker * tracker)
{
    tr_free (tracker->tracker_id_str);
    tr_free (tracker->scrape);
    tr_free (tracker->announce);
    tr_free (tracker->key);
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

    tr_tracker * trackers;
    int tracker_count;
    tr_tracker * currentTracker;
    int currentTrackerIndex;

    tr_torrent * tor;

    time_t scrapeAt;
    time_t lastScrapeStartTime;
    time_t lastScrapeTime;
    bool lastScrapeSucceeded;
    bool lastScrapeTimedOut;

    time_t announceAt;
    time_t manualAnnounceAllowedAt;
    time_t lastAnnounceStartTime;
    time_t lastAnnounceTime;
    bool lastAnnounceSucceeded;
    bool lastAnnounceTimedOut;

    tr_announce_event * announce_events;
    int announce_event_count;
    int announce_event_alloc;

    /* unique lookup key */
    int key;

    int scrapeIntervalSec;
    int announceIntervalSec;
    int announceMinIntervalSec;

    int lastAnnouncePeerCount;

    bool isRunning;
    bool isAnnouncing;
    bool isScraping;
    bool wasCopied;

    char lastAnnounceStr[128];
    char lastScrapeStr[128];
}
tr_tier;

static time_t
get_next_scrape_time (const tr_session * session, const tr_tier * tier, int interval)
{
    time_t ret;
    const time_t now = tr_time ();

    /* Maybe don't scrape paused torrents */
    if (!tier->isRunning && !session->scrapePausedTorrents)
        ret = 0;

    /* Add the interval, and then increment to the nearest 10th second.
     * The latter step is to increase the odds of several torrents coming
     * due at the same time to improve multiscrape. */
    else {
        ret = now + interval;
        while (ret % 10)
            ++ret;
    }

    return ret;
}

static void
tierConstruct (tr_tier * tier, tr_torrent * tor)
{
    static int nextKey = 1;

    memset (tier, 0, sizeof (tr_tier));

    tier->key = nextKey++;
    tier->currentTrackerIndex = -1;
    tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    tier->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    tier->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    tier->scrapeAt = get_next_scrape_time (tor->session, tier, tr_rand_int_weak (180));
    tier->tor = tor;
}

static void
tierDestruct (tr_tier * tier)
{
    tr_free (tier->announce_events);
}

static void
tier_build_log_name (const tr_tier * tier, char * buf, size_t buflen)
{
    tr_snprintf (buf, buflen, "[%s---%s]",
     (tier && tier->tor) ? tr_torrentName (tier->tor) : "?",
     (tier && tier->currentTracker) ? tier->currentTracker->key : "?");
}

static void
tierIncrementTracker (tr_tier * tier)
{
    /* move our index to the next tracker in the tier */
    const int i = (tier->currentTracker == NULL)
                ? 0
                : (tier->currentTrackerIndex + 1) % tier->tracker_count;
    tier->currentTrackerIndex = i;
    tier->currentTracker = &tier->trackers[i];

    /* reset some of the tier's fields */
    tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    tier->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    tier->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    tier->isAnnouncing = false;
    tier->isScraping = false;
    tier->lastAnnounceStartTime = 0;
    tier->lastScrapeStartTime = 0;
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
    tr_tier * tiers;
    int tier_count;

    tr_tracker * trackers;
    int tracker_count;

    tr_tracker_callback callback;
    void * callbackData;
}
tr_torrent_tiers;

static tr_torrent_tiers*
tiersNew (void)
{
    return tr_new0 (tr_torrent_tiers, 1);
}

static void
tiersDestruct (tr_torrent_tiers * tt)
{
    int i;

    for (i=0; i<tt->tracker_count; ++i)
        trackerDestruct (&tt->trackers[i]);
    tr_free (tt->trackers);

    for (i=0; i<tt->tier_count; ++i)
        tierDestruct (&tt->tiers[i]);
    tr_free (tt->tiers);
}

static void
tiersFree (tr_torrent_tiers * tt)
{
    tiersDestruct (tt);
    tr_free (tt);
}

static tr_tier*
getTier (tr_announcer * announcer, const uint8_t * info_hash, int tierId)
{
    tr_tier * tier = NULL;

    if (announcer != NULL)
    {
        tr_session * session = announcer->session;
        tr_torrent * tor = tr_torrentFindFromHash (session, info_hash);

        if (tor && tor->tiers)
        {
            int i;
            tr_torrent_tiers * tt = tor->tiers;

            for (i=0; !tier && i<tt->tier_count; ++i)
                if (tt->tiers[i].key == tierId)
                    tier = &tt->tiers[i];
        }
    }

    return tier;
}

/***
****  PUBLISH
***/

static const tr_tracker_event TRACKER_EVENT_INIT = { 0, 0, 0, 0, 0, 0 };

static void
publishMessage (tr_tier * tier, const char * msg, int type)
{
    if (tier && tier->tor && tier->tor->tiers && tier->tor->tiers->callback)
    {
        tr_torrent_tiers * tiers = tier->tor->tiers;
        tr_tracker_event event = TRACKER_EVENT_INIT;
        event.messageType = type;
        event.text = msg;
        if (tier->currentTracker)
            event.tracker = tier->currentTracker->announce;

        (*tiers->callback) (tier->tor, &event, tiers->callbackData);
    }
}

static void
publishErrorClear (tr_tier * tier)
{
    publishMessage (tier, NULL, TR_TRACKER_ERROR_CLEAR);
}

static void
publishWarning (tr_tier * tier, const char * msg)
{
    publishMessage (tier, msg, TR_TRACKER_WARNING);
}

static void
publishError (tr_tier * tier, const char * msg)
{
    publishMessage (tier, msg, TR_TRACKER_ERROR);
}

static int8_t
getSeedProbability (tr_tier * tier, int seeds, int leechers, int pex_count)
{
    /* special case optimization:
       ocelot omits seeds from peer lists sent to seeds on private trackers.
       so check for that case... */
    if ((leechers == pex_count) && tr_torrentIsPrivate (tier->tor)
                                  && tr_torrentIsSeed (tier->tor)
                                  && (seeds + leechers < NUMWANT))
        return 0;

    if (seeds>=0 && leechers>=0 && (seeds+leechers>0))
        return (int8_t)((100.0*seeds)/ (seeds+leechers));

    return -1; /* unknown */
}

static void
publishPeersPex (tr_tier * tier, int seeds, int leechers,
                 const tr_pex * pex, int n)
{
    if (tier->tor->tiers->callback)
    {
        tr_tracker_event e = TRACKER_EVENT_INIT;
        e.messageType = TR_TRACKER_PEERS;
        e.seedProbability = getSeedProbability (tier, seeds, leechers, n);
        e.pex = pex;
        e.pexCount = n;
        dbgmsg (tier, "got %d peers; seed prob %d", n, (int)e.seedProbability);

        (*tier->tor->tiers->callback) (tier->tor, &e, NULL);
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
filter_trackers_compare_func (const void * va, const void * vb)
{
    const struct ann_tracker_info * a = va;
    const struct ann_tracker_info * b = vb;
    if (a->info.tier != b->info.tier)
        return a->info.tier - b->info.tier;
    return -strcmp (a->scheme, b->scheme);
}

/**
 * Massages the incoming list of trackers into something we can use.
 */
static tr_tracker_info *
filter_trackers (tr_tracker_info * input, int input_count, int * setme_count)
{
    int i, in;
    int j, jn;
    int n = 0;
    struct tr_tracker_info * ret;
    struct ann_tracker_info * tmp = tr_new0 (struct ann_tracker_info, input_count);

    /*for (i=0, in=input_count; i<in; ++i) fprintf (stderr, "IN: [%d][%s]\n", input[i].tier, input[i].announce);*/

    /* build a list of valid trackers */
    for (i=0, in=input_count; i<in; ++i) {
        if (tr_urlIsValidTracker (input[i].announce)) {
            int port;
            char * scheme;
            char * host;
            char * path;
            bool is_duplicate = false;
            tr_urlParse (input[i].announce, TR_BAD_SIZE, &scheme, &host, &port, &path);

            /* weed out one common source of duplicates:
             * "http://tracker/announce" +
             * "http://tracker:80/announce"
             */
            for (j=0, jn=n; !is_duplicate && j<jn; ++j)
                is_duplicate = (tmp[j].port==port)
                            && !strcmp (tmp[j].scheme,scheme)
                            && !strcmp (tmp[j].host,host)
                            && !strcmp (tmp[j].path,path);

            if (is_duplicate) {
                tr_free (path);
                tr_free (host);
                tr_free (scheme);
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
    for (i=0, in=n; i<in; ++i)
        for (j=i+1, jn=n; j<jn; ++j)
            if ((tmp[i].info.tier!=tmp[j].info.tier)
                       && (tmp[i].port==tmp[j].port)
                       && !tr_strcmp0 (tmp[i].host,tmp[j].host)
                       && !tr_strcmp0 (tmp[i].path,tmp[j].path))
                tmp[j].info.tier = tmp[i].info.tier;

    /* sort them, for two reasons:
     * (1) unjumble the tiers from the previous step
     * (2) move the UDP trackers to the front of each tier */
    qsort (tmp, n, sizeof (struct ann_tracker_info), filter_trackers_compare_func);

    /* build the output */
    *setme_count = n;
    ret = tr_new0 (tr_tracker_info, n);
    for (i=0, in=n; i<in; ++i)
        ret[i] = tmp[i].info;

    /* cleanup */
    for (i=0, in=n; i<in; ++i) {
        tr_free (tmp[i].path);
        tr_free (tmp[i].host);
        tr_free (tmp[i].scheme);
    }
    tr_free (tmp);

    /*for (i=0, in=n; i<in; ++i) fprintf (stderr, "OUT: [%d][%s]\n", ret[i].tier, ret[i].announce);*/
    return ret;
}


static void
addTorrentToTier (tr_torrent_tiers * tt, tr_torrent * tor)
{
    int i, n;
    int tier_count;
    tr_tier * tier;
    tr_tracker_info * infos = filter_trackers (tor->info.trackers,
                                               tor->info.trackerCount, &n);

    /* build the array of trackers */
    tt->trackers = tr_new0 (tr_tracker, n);
    tt->tracker_count = n;
    for (i=0; i<n; ++i)
        trackerConstruct (&tt->trackers[i], &infos[i]);

    /* count how many tiers there are */
    tier_count = 0;
    for (i=0; i<n; ++i)
        if (!i || (infos[i].tier != infos[i-1].tier))
            ++tier_count;

    /* build the array of tiers */
    tier = NULL;
    tt->tiers = tr_new0 (tr_tier, tier_count);
    tt->tier_count = 0;
    for (i=0; i<n; ++i) {
        if (i && (infos[i].tier == infos[i-1].tier))
            ++tier->tracker_count;
        else {
            tier = &tt->tiers[tt->tier_count++];
            tierConstruct (tier, tor);
            tier->trackers = &tt->trackers[i];
            tier->tracker_count = 1;
            tierIncrementTracker (tier);
        }
    }

    /* cleanup */
    tr_free (infos);
}

tr_torrent_tiers *
tr_announcerAddTorrent (tr_torrent           * tor,
                        tr_tracker_callback    callback,
                        void                 * callbackData)
{
    tr_torrent_tiers * tiers;

    assert (tr_isTorrent (tor));

    tiers = tiersNew ();
    tiers->callback = callback;
    tiers->callbackData = callbackData;

    addTorrentToTier (tiers, tor);

    return tiers;
}

/***
****
***/

static bool
tierCanManualAnnounce (const tr_tier * tier)
{
    return tier->manualAnnounceAllowedAt <= tr_time ();
}

bool
tr_announcerCanManualAnnounce (const tr_torrent * tor)
{
    int i;
    struct tr_torrent_tiers * tt = NULL;

    assert (tr_isTorrent (tor));
    assert (tor->tiers != NULL);

    if (tor->isRunning)
        tt = tor->tiers;

    /* return true if any tier can manual announce */
    for (i=0; tt && i<tt->tier_count; ++i)
        if (tierCanManualAnnounce (&tt->tiers[i]))
            return true;

    return false;
}

time_t
tr_announcerNextManualAnnounce (const tr_torrent * tor)
{
    int i;
    time_t ret = ~ (time_t)0;
    struct tr_torrent_tiers * tt = tor->tiers;

    /* find the earliest manual announce time from all peers */
    for (i=0; tt && i<tt->tier_count; ++i)
        if (tt->tiers[i].isRunning)
            ret = MIN (ret, tt->tiers[i].manualAnnounceAllowedAt);

    return ret;
}

static void
dbgmsg_tier_announce_queue (const tr_tier * tier)
{
    if (tr_logGetDeepEnabled ())
    {
        int i;
        char name[128];
        char * message;
        struct evbuffer * buf = evbuffer_new ();

        tier_build_log_name (tier, name, sizeof (name));
        for (i=0; i<tier->announce_event_count; ++i)
        {
            const tr_announce_event e = tier->announce_events[i];
            const char * str = tr_announce_event_get_string (e);
            evbuffer_add_printf (buf, "[%d:%s]", i, str);
        }

        message = evbuffer_free_to_str (buf, NULL);
        tr_logAddDeep (__FILE__, __LINE__, name, "announce queue is %s", message);
        tr_free (message);
    }
}

static void
tier_announce_remove_trailing (tr_tier * tier, tr_announce_event e)
{
    while ((tier->announce_event_count > 0)
        && (tier->announce_events[tier->announce_event_count-1] == e))
        --tier->announce_event_count;
}

static void
tier_announce_event_push (tr_tier            * tier,
                          tr_announce_event    e,
                          time_t               announceAt)
{
    int i;

    assert (tier != NULL);

    dbgmsg_tier_announce_queue (tier);
    dbgmsg (tier, "queued \"%s\"", tr_announce_event_get_string (e));

    if (tier->announce_event_count > 0)
    {
        /* special case #1: if we're adding a "stopped" event,
         * dump everything leading up to it except "completed" */
        if (e == TR_ANNOUNCE_EVENT_STOPPED) {
            bool has_completed = false;
            const tr_announce_event c = TR_ANNOUNCE_EVENT_COMPLETED;
            for (i=0; !has_completed && i<tier->announce_event_count; ++i)
                has_completed = c == tier->announce_events[i];
            tier->announce_event_count = 0;
            if (has_completed)
                tier->announce_events[tier->announce_event_count++] = c;
        }

        /* special case #2: dump all empty strings leading up to this event */
        tier_announce_remove_trailing (tier, TR_ANNOUNCE_EVENT_NONE);

        /* special case #3: no consecutive duplicates */
        tier_announce_remove_trailing (tier, e);
    }

    /* make room in the array for another event */
    if (tier->announce_event_alloc <= tier->announce_event_count) {
        tier->announce_event_alloc += 4;
        tier->announce_events = tr_renew (tr_announce_event,
                                          tier->announce_events,
                                          tier->announce_event_alloc);
    }

    /* add it */
    tier->announce_events[tier->announce_event_count++] = e;
    tier->announceAt = announceAt;

    dbgmsg_tier_announce_queue (tier);
    dbgmsg (tier, "announcing in %d seconds", (int)difftime (announceAt,tr_time ()));
}

static tr_announce_event
tier_announce_event_pull (tr_tier * tier)
{
    const tr_announce_event e = tier->announce_events[0];

    tr_removeElementFromArray (tier->announce_events,
                               0, sizeof (tr_announce_event),
                               tier->announce_event_count--);

    return e;
}

static void
torrentAddAnnounce (tr_torrent * tor, tr_announce_event e, time_t announceAt)
{
    int i;
    struct tr_torrent_tiers * tt = tor->tiers;

    /* walk through each tier and tell them to announce */
    for (i=0; i<tt->tier_count; ++i)
        tier_announce_event_push (&tt->tiers[i], e, announceAt);
}

void
tr_announcerTorrentStarted (tr_torrent * tor)
{
    torrentAddAnnounce (tor, TR_ANNOUNCE_EVENT_STARTED, tr_time ());
}
void
tr_announcerManualAnnounce (tr_torrent * tor)
{
    torrentAddAnnounce (tor, TR_ANNOUNCE_EVENT_NONE, tr_time ());
}
void
tr_announcerTorrentStopped (tr_torrent * tor)
{
    torrentAddAnnounce (tor, TR_ANNOUNCE_EVENT_STOPPED, tr_time ());
}
void
tr_announcerTorrentCompleted (tr_torrent * tor)
{
    torrentAddAnnounce (tor, TR_ANNOUNCE_EVENT_COMPLETED, tr_time ());
}
void
tr_announcerChangeMyPort (tr_torrent * tor)
{
    tr_announcerTorrentStarted (tor);
}

/***
****
***/

void
tr_announcerAddBytes (tr_torrent * tor, int type, uint32_t byteCount)
{
    int i;
    struct tr_torrent_tiers * tt = tor->tiers;

    assert (tr_isTorrent (tor));
    assert (type==TR_ANN_UP || type==TR_ANN_DOWN || type==TR_ANN_CORRUPT);

    for (i=0; i<tt->tier_count; ++i)
        tt->tiers[i].byteCounts[ type ] += byteCount;
}

/***
****
***/

static tr_announce_request *
announce_request_new (const tr_announcer  * announcer,
                      tr_torrent          * tor,
                      const tr_tier       * tier,
                      tr_announce_event     event)
{
    tr_announce_request * req = tr_new0 (tr_announce_request, 1);
    req->port = tr_sessionGetPublicPeerPort (announcer->session);
    req->url = tr_strdup (tier->currentTracker->announce);
    req->tracker_id_str = tr_strdup (tier->currentTracker->tracker_id_str);
    memcpy (req->info_hash, tor->info.hash, SHA_DIGEST_LENGTH);
    memcpy (req->peer_id, tr_torrentGetPeerId(tor), PEER_ID_LEN);
    req->up = tier->byteCounts[TR_ANN_UP];
    req->down = tier->byteCounts[TR_ANN_DOWN];
    req->corrupt = tier->byteCounts[TR_ANN_CORRUPT];
    req->leftUntilComplete = tr_torrentHasMetadata (tor)
            ? tor->info.totalSize - tr_torrentHaveTotal (tor)
            : ~ (uint64_t)0;
    req->event = event;
    req->numwant = event == TR_ANNOUNCE_EVENT_STOPPED ? 0 : NUMWANT;
    req->key = announcer->key;
    req->partial_seed = tr_torrentGetCompleteness (tor) == TR_PARTIAL_SEED;
    tier_build_log_name (tier, req->log_name, sizeof (req->log_name));
    return req;
}

static void announce_request_free (tr_announce_request * req);

void
tr_announcerRemoveTorrent (tr_announcer * announcer, tr_torrent * tor)
{
    struct tr_torrent_tiers * tt = tor->tiers;

    if (tt != NULL)
    {
        int i;
        for (i=0; i<tt->tier_count; ++i)
        {
            tr_tier * tier = &tt->tiers[i];
            if (tier->isRunning)
            {
                const tr_announce_event e = TR_ANNOUNCE_EVENT_STOPPED;
                tr_announce_request * req = announce_request_new (announcer, tor, tier, e);

                if (tr_ptrArrayFindSorted (&announcer->stops, req, compareStops) != NULL)
                    announce_request_free (req);
                else
                    tr_ptrArrayInsertSorted (&announcer->stops, req, compareStops);
            }
        }

        tiersFree (tor->tiers);
        tor->tiers = NULL;
    }
}

static int
getRetryInterval (const tr_tracker * t)
{
  switch (t->consecutiveFailures)
    {
      case 0:  return 0;
      case 1:  return 20;
      case 2:  return tr_rand_int_weak (60) + (60 * 5);
      case 3:  return tr_rand_int_weak (60) + (60 * 15);
      case 4:  return tr_rand_int_weak (60) + (60 * 30);
      case 5:  return tr_rand_int_weak (60) + (60 * 60);
      default: return tr_rand_int_weak (60) + (60 * 120);
    }
}

struct announce_data
{
    int tierId;
    time_t timeSent;
    tr_announce_event event;
    tr_session * session;

    /** If the request succeeds, the value for tier's "isRunning" flag */
    bool isRunningOnSuccess;
};

static void
on_announce_error (tr_tier * tier, const char * err, tr_announce_event e)
{
    int interval;

    /* increment the error count */
    if (tier->currentTracker != NULL)
        ++tier->currentTracker->consecutiveFailures;

    /* set the error message */
    dbgmsg (tier, "%s", err);
    tr_logAddTorInfo (tier->tor, "%s", err);
    tr_strlcpy (tier->lastAnnounceStr, err, sizeof (tier->lastAnnounceStr));

    /* switch to the next tracker */
    tierIncrementTracker (tier);

    /* schedule a reannounce */
    interval = getRetryInterval (tier->currentTracker);
    dbgmsg (tier, "Retrying announce in %d seconds.", interval);
    tr_logAddTorInfo (tier->tor, "Retrying announce in %d seconds.", interval);
    tier_announce_event_push (tier, e, tr_time () + interval);
}

static void
on_announce_done (const tr_announce_response  * response,
                  void                        * vdata)
{
    struct announce_data * data = vdata;
    tr_announcer * announcer = data->session->announcer;
    tr_tier * tier = getTier (announcer, response->info_hash, data->tierId);
    const time_t now = tr_time ();
    const tr_announce_event event = data->event;

    if (announcer)
        ++announcer->slotsAvailable;

    if (tier != NULL)
    {
        tr_tracker * tracker;

        dbgmsg (tier, "Got announce response: "
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
                      response->warning ? response->warning : "none");

        tier->lastAnnounceTime = now;
        tier->lastAnnounceTimedOut = response->did_timeout;
        tier->lastAnnounceSucceeded = false;
        tier->isAnnouncing = false;
        tier->manualAnnounceAllowedAt = now + tier->announceMinIntervalSec;

        if (!response->did_connect)
        {
            on_announce_error (tier, _("Could not connect to tracker"), event);
        }
        else if (response->did_timeout)
        {
            on_announce_error (tier, _("Tracker did not respond"), event);
        }
        else if (response->errmsg)
        {
            /* If the torrent's only tracker returned an error, publish it.
               Don't bother publishing if there are other trackers -- it's
               all too common for people to load up dozens of dead trackers
               in a torrent's metainfo... */
            if (tier->tor->info.trackerCount < 2)
                publishError (tier, response->errmsg);

            on_announce_error (tier, response->errmsg, event);
        }
        else
        {
            int i;
            const char * str;
            int scrape_fields = 0;
            int seeders = 0;
            int leechers = 0;
            int downloads = 0;
            const bool isStopped = event == TR_ANNOUNCE_EVENT_STOPPED;

            publishErrorClear (tier);

            if ((tracker = tier->currentTracker))
            {
                tracker->consecutiveFailures = 0;

                if (response->seeders >= 0)
                {
                    tracker->seederCount = seeders = response->seeders;
                    ++scrape_fields;
                }

                if (response->leechers >= 0)
                {
                    tracker->leecherCount = leechers = response->leechers;
                    ++scrape_fields;
                }
                if (response->downloads >= 0)
                {
                    tracker->downloadCount = downloads = response->downloads;
                    ++scrape_fields;
                }

                if ((str = response->tracker_id_str))
                {
                    tr_free (tracker->tracker_id_str);
                    tracker->tracker_id_str = tr_strdup (str);
                }
            }

            if ((str = response->warning))
            {
                tr_strlcpy (tier->lastAnnounceStr, str,
                            sizeof (tier->lastAnnounceStr));
                dbgmsg (tier, "tracker gave \"%s\"", str);
                publishWarning (tier, str);
            }
            else
            {
                tr_strlcpy (tier->lastAnnounceStr, _("Success"),
                            sizeof (tier->lastAnnounceStr));
            }

            if ((i = response->min_interval))
                tier->announceMinIntervalSec = i;

            if ((i = response->interval))
                tier->announceIntervalSec = i;

            if (response->pex_count > 0)
                publishPeersPex (tier, seeders, leechers,
                                 response->pex, response->pex_count);

            if (response->pex6_count > 0)
                publishPeersPex (tier, seeders, leechers,
                                 response->pex6, response->pex6_count);

            tier->isRunning = data->isRunningOnSuccess;

            /* if the tracker included scrape fields in its announce response,
               then a separate scrape isn't needed */
            if (scrape_fields >= 3 || (scrape_fields >= 1 && tracker->scrape != NULL))
            {
                tr_logAddTorDbg (tier->tor, "Announce response contained scrape info; "
                                      "rescheduling next scrape to %d seconds from now.",
                                      tier->scrapeIntervalSec);
                tier->scrapeAt = get_next_scrape_time (announcer->session, tier, tier->scrapeIntervalSec);
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = true;
            }
            else if (tier->lastScrapeTime + tier->scrapeIntervalSec <= now)
            {
                tier->scrapeAt = get_next_scrape_time (announcer->session, tier, 0);
            }

            tier->lastAnnounceSucceeded = true;
            tier->lastAnnouncePeerCount = response->pex_count
                                        + response->pex6_count;

            if (isStopped)
            {
                /* now that we've successfully stopped the torrent,
                 * we can reset the up/down/corrupt count we've kept
                 * for this tracker */
                tier->byteCounts[ TR_ANN_UP ] = 0;
                tier->byteCounts[ TR_ANN_DOWN ] = 0;
                tier->byteCounts[ TR_ANN_CORRUPT ] = 0;
            }

            if (!isStopped && !tier->announce_event_count)
            {
                /* the queue is empty, so enqueue a perodic update */
                i = tier->announceIntervalSec;
                dbgmsg (tier, "Sending periodic reannounce in %d seconds", i);
                tier_announce_event_push (tier, TR_ANNOUNCE_EVENT_NONE, now + i);
            }
        }
    }

    tr_free (data);
}

static void
announce_request_free (tr_announce_request * req)
{
    tr_free (req->tracker_id_str);
    tr_free (req->url);
    tr_free (req);
}

static void
announce_request_delegate (tr_announcer               * announcer,
                           tr_announce_request        * request,
                           tr_announce_response_func    callback,
                           void                       * callback_data)
{
    tr_session * session = announcer->session;

#if 0
    fprintf (stderr, "ANNOUNCE: event %s isPartialSeed %d port %d key %d numwant %d"
                     " up %"PRIu64" down %"PRIu64" corrupt %"PRIu64" left %"PRIu64
                     " url [%s] tracker_id_str [%s] peer_id [%20.20s]\n",
             tr_announce_event_get_string (request->event),
             (int)request->partial_seed,
             (int)request->port,
             request->key,
             request->numwant,
             request->up,
             request->down,
             request->corrupt,
             request->leftUntilComplete,
             request->url,
             request->tracker_id_str,
             request->peer_id);
#endif

    if (!memcmp (request->url, "http", 4))
        tr_tracker_http_announce (session, request, callback, callback_data);
    else if (!memcmp (request->url, "udp://", 6))
        tr_tracker_udp_announce (session, request, callback, callback_data);
    else
        tr_logAddError ("Unsupported url: %s", request->url);

    announce_request_free (request);
}

static void
tierAnnounce (tr_announcer * announcer, tr_tier * tier)
{
    tr_announce_event announce_event;
    tr_announce_request * req;
    struct announce_data * data;
    tr_torrent * tor = tier->tor;
    const time_t now = tr_time ();

    assert (!tier->isAnnouncing);
    assert (tier->announce_event_count > 0);

    announce_event = tier_announce_event_pull (tier);
    req = announce_request_new (announcer, tor, tier, announce_event);

    data = tr_new0 (struct announce_data, 1);
    data->session = announcer->session;
    data->tierId = tier->key;
    data->isRunningOnSuccess = tor->isRunning;
    data->timeSent = now;
    data->event = announce_event;

    tier->isAnnouncing = true;
    tier->lastAnnounceStartTime = now;
    --announcer->slotsAvailable;

    announce_request_delegate (announcer, req, on_announce_done, data);
}

/***
****
****  SCRAPE
****
***/

static void
on_scrape_error (tr_session * session, tr_tier * tier, const char * errmsg)
{
    int interval;

    /* increment the error count */
    if (tier->currentTracker != NULL)
        ++tier->currentTracker->consecutiveFailures;

    /* set the error message */
    dbgmsg (tier, "Scrape error: %s", errmsg);
    tr_logAddTorInfo (tier->tor, "Scrape error: %s", errmsg);
    tr_strlcpy (tier->lastScrapeStr, errmsg, sizeof (tier->lastScrapeStr));

    /* switch to the next tracker */
    tierIncrementTracker (tier);

    /* schedule a rescrape */
    interval = getRetryInterval (tier->currentTracker);
    dbgmsg (tier, "Retrying scrape in %zu seconds.", (size_t)interval);
    tr_logAddTorInfo (tier->tor, "Retrying scrape in %zu seconds.", (size_t)interval);
    tier->lastScrapeSucceeded = false;
    tier->scrapeAt = get_next_scrape_time (session, tier, interval);
}

static tr_tier *
find_tier (tr_torrent * tor, const char * scrape)
{
    int i;
    struct tr_torrent_tiers * tt = tor->tiers;

    for (i=0; tt && i<tt->tier_count; ++i) {
        const tr_tracker * const tracker = tt->tiers[i].currentTracker;
        if (tracker && !tr_strcmp0 (scrape, tracker->scrape))
            return &tt->tiers[i];
    }

    return NULL;
}

static void
on_scrape_done (const tr_scrape_response * response, void * vsession)
{
    int i;
    const time_t now = tr_time ();
    tr_session * session = vsession;
    tr_announcer * announcer = session->announcer;

    for (i=0; i<response->row_count; ++i)
    {
        const struct tr_scrape_response_row * row = &response->rows[i];
        tr_torrent * tor = tr_torrentFindFromHash (session, row->info_hash);

        if (tor != NULL)
        {
            tr_tier * tier = find_tier (tor, response->url);

            if (tier != NULL)
            {
                dbgmsg (tier, "scraped url:%s -- "
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
                              response->errmsg ? response->errmsg : "none");

                tier->isScraping = false;
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = false;
                tier->lastScrapeTimedOut = response->did_timeout;

                if (!response->did_connect)
                {
                    on_scrape_error (session, tier, _("Could not connect to tracker"));
                }
                else if (response->did_timeout)
                {
                    on_scrape_error (session, tier, _("Tracker did not respond"));
                }
                else if (response->errmsg)
                {
                    on_scrape_error (session, tier, response->errmsg);
                }
                else
                {
                    tr_tracker * tracker;

                    tier->lastScrapeSucceeded = true;
                    tier->scrapeIntervalSec = MAX (DEFAULT_SCRAPE_INTERVAL_SEC,
                                                   response->min_request_interval);
                    tier->scrapeAt = get_next_scrape_time (session, tier, tier->scrapeIntervalSec);
                    tr_logAddTorDbg (tier->tor, "Scrape successful. Rescraping in %d seconds.",
                               tier->scrapeIntervalSec);

                    if ((tracker = tier->currentTracker))
                    {
                        if (row->seeders >= 0)
                            tracker->seederCount = row->seeders;
                        if (row->leechers >= 0)
                            tracker->leecherCount = row->leechers;
                        if (row->downloads >= 0)
                            tracker->downloadCount = row->downloads;
                        tracker->downloaderCount = row->downloaders;
                        tracker->consecutiveFailures = 0;
                    }
                }
            }
        }
    }

    if (announcer)
        ++announcer->slotsAvailable;
}

static void
scrape_request_delegate (tr_announcer             * announcer,
                         const tr_scrape_request  * request,
                         tr_scrape_response_func    callback,
                         void                     * callback_data)
{
    tr_session * session = announcer->session;

    if (!memcmp (request->url, "http", 4))
        tr_tracker_http_scrape (session, request, callback, callback_data);
    else if (!memcmp (request->url, "udp://", 6))
        tr_tracker_udp_scrape (session, request, callback, callback_data);
    else
        tr_logAddError ("Unsupported url: %s", request->url);
}

static void
multiscrape (tr_announcer * announcer, tr_ptrArray * tiers)
{
    int i;
    int request_count = 0;
    const time_t now = tr_time ();
    const int tier_count = tr_ptrArraySize (tiers);
    const int max_request_count = MIN (announcer->slotsAvailable, tier_count);
    tr_scrape_request * requests = tr_new0 (tr_scrape_request, max_request_count);

    /* batch as many info_hashes into a request as we can */
    for (i=0; i<tier_count; ++i)
    {
        int j;
        tr_tier * tier = tr_ptrArrayNth (tiers, i);
        char * url = tier->currentTracker->scrape;
        const uint8_t * hash = tier->tor->info.hash;

        assert (url != NULL);

        /* if there's a request with this scrape URL and a free slot, use it */
        for (j=0; j<request_count; ++j)
        {
            tr_scrape_request * req = &requests[j];

            if (req->info_hash_count >= TR_MULTISCRAPE_MAX)
                continue;
            if (tr_strcmp0 (req->url, url))
                continue;

            memcpy (req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH);
            tier->isScraping = true;
            tier->lastScrapeStartTime = now;
            break;
        }

        /* otherwise, if there's room for another request, build a new one */
        if ((j==request_count) && (request_count < max_request_count))
        {
            tr_scrape_request * req = &requests[request_count++];
            req->url = url;
            tier_build_log_name (tier, req->log_name, sizeof (req->log_name));

            memcpy (req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH);
            tier->isScraping = true;
            tier->lastScrapeStartTime = now;
        }
    }

    /* send the requests we just built */
    for (i=0; i<request_count; ++i)
        scrape_request_delegate (announcer, &requests[i], on_scrape_done, announcer->session);

    /* cleanup */
    tr_free (requests);
}

static void
flushCloseMessages (tr_announcer * announcer)
{
    int i;
    const int n = tr_ptrArraySize (&announcer->stops);

    for (i=0; i<n; ++i)
        announce_request_delegate (announcer, tr_ptrArrayNth (&announcer->stops, i), NULL, NULL);

    tr_ptrArrayClear (&announcer->stops);
}

static bool
tierNeedsToAnnounce (const tr_tier * tier, const time_t now)
{
    return !tier->isAnnouncing
        && !tier->isScraping
        && (tier->announceAt != 0)
        && (tier->announceAt <= now)
        && (tier->announce_event_count > 0);
}

static bool
tierNeedsToScrape (const tr_tier * tier, const time_t now)
{
    return (!tier->isScraping)
        && (tier->scrapeAt != 0)
        && (tier->scrapeAt <= now)
        && (tier->currentTracker != NULL)
        && (tier->currentTracker->scrape != NULL);
}

static int
compareTiers (const void * va, const void * vb)
{
    int ret;
    const tr_tier * a = * (const tr_tier**)va;
    const tr_tier * b = * (const tr_tier**)vb;

    /* primary key: larger stats come before smaller */
    ret = compareTransfer (a->byteCounts[TR_ANN_UP], a->byteCounts[TR_ANN_DOWN],
                           b->byteCounts[TR_ANN_UP], b->byteCounts[TR_ANN_DOWN]);

    /* secondary key: announcements that have been waiting longer go first */
    if (!ret && (a->announceAt != b->announceAt))
        ret = a->announceAt < b->announceAt ? -1 : 1;

    return ret;
}

static void
announceMore (tr_announcer * announcer)
{
    int i;
    int n;
    tr_torrent * tor;
    tr_ptrArray announceMe = TR_PTR_ARRAY_INIT;
    tr_ptrArray scrapeMe = TR_PTR_ARRAY_INIT;
    const time_t now = tr_time ();

    dbgmsg (NULL, "announceMore: slotsAvailable is %d", announcer->slotsAvailable);

    if (announcer->slotsAvailable < 1)
        return;

    /* build a list of tiers that need to be announced */
    tor = NULL;
    while ((tor = tr_torrentNext (announcer->session, tor))) {
        struct tr_torrent_tiers * tt = tor->tiers;
        for (i=0; tt && i<tt->tier_count; ++i) {
            tr_tier * tier = &tt->tiers[i];
            if (tierNeedsToAnnounce (tier, now))
                tr_ptrArrayAppend (&announceMe, tier);
            else if (tierNeedsToScrape (tier, now))
                tr_ptrArrayAppend (&scrapeMe, tier);
        }
    }

    /* if there are more tiers than slots available, prioritize */
    n = tr_ptrArraySize (&announceMe);
    if (n > announcer->slotsAvailable)
        qsort (tr_ptrArrayBase (&announceMe), n, sizeof (tr_tier*), compareTiers);

    /* announce some */
    n = MIN (tr_ptrArraySize (&announceMe), announcer->slotsAvailable);
    for (i=0; i<n; ++i) {
        tr_tier * tier = tr_ptrArrayNth (&announceMe, i);
        tr_logAddTorDbg (tier->tor, "%s", "Announcing to tracker");
        dbgmsg (tier, "announcing tier %d of %d", i, n);
        tierAnnounce (announcer, tier);
    }

    /* scrape some */
    multiscrape (announcer, &scrapeMe);

    /* cleanup */
    tr_ptrArrayDestruct (&scrapeMe, NULL);
    tr_ptrArrayDestruct (&announceMe, NULL);
}

static void
onUpkeepTimer (evutil_socket_t foo UNUSED, short bar UNUSED, void * vannouncer)
{
    tr_announcer * announcer = vannouncer;
    tr_session * session = announcer->session;
    const bool is_closing = session->isClosed;
    const time_t now = tr_time ();

    tr_sessionLock (session);

    /* maybe send out some "stopped" messages for closed torrents */
    flushCloseMessages (announcer);

    /* maybe send out some announcements to trackers */
    if (!is_closing)
        announceMore (announcer);

    /* TAU upkeep */
    if (announcer->tauUpkeepAt <= now) {
        announcer->tauUpkeepAt = now + TAU_UPKEEP_INTERVAL_SECS;
        tr_tracker_udp_upkeep (session);
    }

    /* set up the next timer */
    tr_timerAdd (announcer->upkeepTimer, UPKEEP_INTERVAL_SECS, 0);

    tr_sessionUnlock (session);
}

/***
****
***/

tr_tracker_stat *
tr_announcerStats (const tr_torrent * torrent, int * setmeTrackerCount)
{
    int i;
    int out = 0;
    tr_tracker_stat * ret;
    struct tr_torrent_tiers * tt;
    const time_t now = tr_time ();

    assert (tr_isTorrent (torrent));

    tt = torrent->tiers;

    /* alloc the stats */
    *setmeTrackerCount = tt->tracker_count;
    ret = tr_new0 (tr_tracker_stat, tt->tracker_count);

    /* populate the stats */
    for (i=0; i<tt->tier_count; ++i)
    {
        int j;
        const tr_tier * const tier = &tt->tiers[i];
        for (j=0; j<tier->tracker_count; ++j)
        {
            const tr_tracker * const tracker = &tier->trackers[j];
            tr_tracker_stat * st = &ret[out++];

            st->id = tracker->id;
            tr_strlcpy (st->host, tracker->key, sizeof (st->host));
            tr_strlcpy (st->announce, tracker->announce, sizeof (st->announce));
            st->tier = i;
            st->isBackup = tracker != tier->currentTracker;
            st->lastScrapeStartTime = tier->lastScrapeStartTime;
            if (tracker->scrape)
                tr_strlcpy (st->scrape, tracker->scrape, sizeof (st->scrape));
            else
                st->scrape[0] = '\0';

            st->seederCount = tracker->seederCount;
            st->leecherCount = tracker->leecherCount;
            st->downloadCount = tracker->downloadCount;

            if (st->isBackup)
            {
                st->scrapeState = TR_TRACKER_INACTIVE;
                st->announceState = TR_TRACKER_INACTIVE;
                st->nextScrapeTime = 0;
                st->nextAnnounceTime = 0;
            }
            else
            {
                if ((st->hasScraped = tier->lastScrapeTime != 0)) {
                    st->lastScrapeTime = tier->lastScrapeTime;
                    st->lastScrapeSucceeded = tier->lastScrapeSucceeded;
                    st->lastScrapeTimedOut = tier->lastScrapeTimedOut;
                    tr_strlcpy (st->lastScrapeResult, tier->lastScrapeStr,
                                sizeof (st->lastScrapeResult));
                }

                if (tier->isScraping)
                    st->scrapeState = TR_TRACKER_ACTIVE;
                else if (!tier->scrapeAt)
                    st->scrapeState = TR_TRACKER_INACTIVE;
                else if (tier->scrapeAt > now)
                {
                    st->scrapeState = TR_TRACKER_WAITING;
                    st->nextScrapeTime = tier->scrapeAt;
                }
                else
                    st->scrapeState = TR_TRACKER_QUEUED;

                st->lastAnnounceStartTime = tier->lastAnnounceStartTime;

                if ((st->hasAnnounced = tier->lastAnnounceTime != 0)) {
                    st->lastAnnounceTime = tier->lastAnnounceTime;
                    tr_strlcpy (st->lastAnnounceResult, tier->lastAnnounceStr,
                                sizeof (st->lastAnnounceResult));
                    st->lastAnnounceSucceeded = tier->lastAnnounceSucceeded;
                    st->lastAnnounceTimedOut = tier->lastAnnounceTimedOut;
                    st->lastAnnouncePeerCount = tier->lastAnnouncePeerCount;
                }

                if (tier->isAnnouncing)
                    st->announceState = TR_TRACKER_ACTIVE;
                else if (!torrent->isRunning || !tier->announceAt)
                    st->announceState = TR_TRACKER_INACTIVE;
                else if (tier->announceAt > now)
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
tr_announcerStatsFree (tr_tracker_stat * trackers,
                       int trackerCount UNUSED)
{
    tr_free (trackers);
}

/***
****
***/

static void
copy_tier_attributes_impl (struct tr_tier * tgt, int trackerIndex, const tr_tier * src)
{
    const tr_tier keep = *tgt;

    /* sanity clause */
    assert (trackerIndex < tgt->tracker_count);
    assert (!tr_strcmp0 (tgt->trackers[trackerIndex].announce, src->currentTracker->announce));

    /* bitwise copy will handle most of tr_tier's fields... */
    *tgt = *src;

    /* ...fix the fields that can't be cleanly bitwise-copied */
    tgt->wasCopied = true;
    tgt->trackers = keep.trackers;
    tgt->tracker_count = keep.tracker_count;
    tgt->announce_events = tr_memdup (src->announce_events, sizeof (tr_announce_event) * src->announce_event_count);
    tgt->announce_event_count = src->announce_event_count;
    tgt->announce_event_alloc = src->announce_event_count;
    tgt->currentTrackerIndex = trackerIndex;
    tgt->currentTracker = &tgt->trackers[trackerIndex];
    tgt->currentTracker->seederCount = src->currentTracker->seederCount;
    tgt->currentTracker->leecherCount = src->currentTracker->leecherCount;
    tgt->currentTracker->downloadCount = src->currentTracker->downloadCount;
    tgt->currentTracker->downloaderCount = src->currentTracker->downloaderCount;
}

static void
copy_tier_attributes (struct tr_torrent_tiers * tt, const tr_tier * src)
{
    int i, j;
    bool found = false;

    /* find a tier (if any) which has a match for src->currentTracker */
    for (i=0; !found && i<tt->tier_count; ++i)
        for (j=0; !found && j<tt->tiers[i].tracker_count; ++j)
            if ((found = !tr_strcmp0 (src->currentTracker->announce, tt->tiers[i].trackers[j].announce)))
                copy_tier_attributes_impl (&tt->tiers[i], j, src);
}

void
tr_announcerResetTorrent (tr_announcer * announcer UNUSED, tr_torrent * tor)
{
    int i;
    const time_t now = tr_time ();
    struct tr_torrent_tiers * tt = tor->tiers;
    tr_torrent_tiers old = *tt;

    assert (tt != NULL);

    /* remove the old tiers / trackers */
    tt->tiers = NULL;
    tt->trackers = NULL;
    tt->tier_count = 0;
    tt->tracker_count = 0;

    /* create the new tiers / trackers */
    addTorrentToTier (tt, tor);

    /* copy the old tiers' states into their replacements */
    for (i=0; i<old.tier_count; ++i)
        if (old.tiers[i].currentTracker != NULL)
            copy_tier_attributes (tt, &old.tiers[i]);

    /* kickstart any tiers that didn't get started */
    if (tor->isRunning)
        for (i=0; i<tt->tier_count; ++i)
            if (!tt->tiers[i].wasCopied)
                tier_announce_event_push (&tt->tiers[i], TR_ANNOUNCE_EVENT_STARTED, now);

    /* cleanup */
    tiersDestruct (&old);
}
