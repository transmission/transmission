/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <array>
#include <climits> /* INT_MAX */
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <event2/buffer.h>
#include <event2/event.h> /* evtimer */

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"

#include "announce-list.h"
#include "announcer-common.h"
#include "announcer.h"
#include "crypto-utils.h" /* tr_rand_int(), tr_rand_int_weak() */
#include "log.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"
#include "web-utils.h"

using namespace std::literals;

struct tr_tier;

static void tier_build_log_name(struct tr_tier const* tier, char* buf, size_t buflen);

#define dbgmsg(tier, ...) \
    do \
    { \
        if (tr_logGetDeepEnabled()) \
        { \
            char name[128]; \
            tier_build_log_name(tier, name, TR_N_ELEMENTS(name)); \
            tr_logAddDeep(__FILE__, __LINE__, name, __VA_ARGS__); \
        } \
    } while (0)

/* unless the tracker says otherwise, rescrape this frequently */
static auto constexpr DefaultScrapeIntervalSec = int{ 60 * 30 };
/* unless the tracker says otherwise, this is the announce interval */
static auto constexpr DefaultAnnounceIntervalSec = int{ 60 * 10 };
/* unless the tracker says otherwise, this is the announce min_interval */
static auto constexpr DefaultAnnounceMinIntervalSec = int{ 60 * 2 };
/* the value of the 'numwant' argument passed in tracker requests. */
static auto constexpr Numwant = int{ 80 };

/* how often to announce & scrape */
static auto constexpr UpkeepIntervalMsec = int{ 500 };
static auto constexpr MaxAnnouncesPerUpkeep = int{ 20 };
static auto constexpr MaxScrapesPerUpkeep = int{ 20 };

/* this is how often to call the UDP tracker upkeep */
static auto constexpr TauUpkeepIntervalSecs = int{ 5 };

/* how many infohashes to remove when we get a scrape-too-long error */
static auto constexpr TrMultiscrapeStep = int{ 5 };

/***
****
***/

char const* tr_announce_event_get_string(tr_announce_event e)
{
    switch (e)
    {
    case TR_ANNOUNCE_EVENT_COMPLETED:
        return "completed";

    case TR_ANNOUNCE_EVENT_STARTED:
        return "started";

    case TR_ANNOUNCE_EVENT_STOPPED:
        return "stopped";

    default:
        return "";
    }
}

namespace
{

struct StopsCompare
{
    int compare(tr_announce_request const* a, tr_announce_request const* b) const // <=>
    {
        // primary key: volume of data transferred
        auto const ax = a->up + a->down;
        auto const bx = b->up + b->down;
        if (ax < bx)
        {
            return -1;
        }
        if (ax > bx)
        {
            return 1;
        }

        // secondary key: the torrent's info_hash
        if (a->info_hash < b->info_hash)
        {
            return -1;
        }
        if (a->info_hash > b->info_hash)
        {
            return 1;
        }

        // tertiary key: the tracker's announce url
        if (a->announce_url < b->announce_url)
        {
            return -1;
        }
        if (a->announce_url > b->announce_url)
        {
            return 1;
        }

        return 0;
    }

    bool operator()(tr_announce_request const* a, tr_announce_request const* b) const // less than
    {
        return compare(a, b) < 0;
    }
};

} // namespace

/***
****
***/

struct tr_scrape_info
{
    tr_interned_string scrape_url;

    int multiscrape_max;

    tr_scrape_info(tr_interned_string scrape_url_in, int const multiscrape_max_in)
        : scrape_url{ scrape_url_in }
        , multiscrape_max{ multiscrape_max_in }
    {
    }
};

/**
 * "global" (per-tr_session) fields
 */
struct tr_announcer
{
    std::set<tr_announce_request*, StopsCompare> stops;
    std::map<tr_interned_string, tr_scrape_info> scrape_info;

    tr_session* session;
    struct event* upkeepTimer;
    int key;
    time_t tauUpkeepAt;
};

static tr_scrape_info* tr_announcerGetScrapeInfo(tr_announcer* announcer, tr_interned_string url)
{
    if (std::empty(url))
    {
        return nullptr;
    }

    auto& scrapes = announcer->scrape_info;
    auto const it = scrapes.try_emplace(url, url, TR_MULTISCRAPE_MAX);
    return &it.first->second;
}

static void onUpkeepTimer(evutil_socket_t fd, short what, void* vannouncer);

void tr_announcerInit(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    auto* a = new tr_announcer{};
    a->key = tr_rand_int(INT_MAX);
    a->session = session;
    a->upkeepTimer = evtimer_new(session->event_base, onUpkeepTimer, a);
    tr_timerAddMsec(a->upkeepTimer, UpkeepIntervalMsec);

    session->announcer = a;
}

static void flushCloseMessages(tr_announcer* announcer);

void tr_announcerClose(tr_session* session)
{
    tr_announcer* announcer = session->announcer;

    flushCloseMessages(announcer);

    tr_tracker_udp_start_shutdown(session);

    event_free(announcer->upkeepTimer);
    announcer->upkeepTimer = nullptr;

    session->announcer = nullptr;
    delete announcer;
}

/***
****
***/

/* a row in tr_tier's list of trackers */
struct tr_tracker
{
    tr_interned_string key;
    tr_interned_string announce_url;
    struct tr_scrape_info* scrape_info;

    char* tracker_id_str;

    int seederCount;
    int leecherCount;
    int downloadCount;
    int downloaderCount;

    int consecutiveFailures;

    uint32_t id;
};

// format: `${host}:${port}`
tr_interned_string tr_announcerGetKey(tr_url_parsed_t const& parsed)
{
    std::string buf;
    tr_buildBuf(buf, parsed.host, ":"sv, parsed.portstr);
    return tr_interned_string{ buf };
}

static void trackerConstruct(tr_announcer* announcer, tr_tracker* tracker, tr_announce_list::tracker_info const& info)
{
    memset(tracker, 0, sizeof(tr_tracker));
    tracker->key = info.host;
    tracker->announce_url = info.announce_str;
    tracker->scrape_info = std::empty(info.scrape_str) ? nullptr : tr_announcerGetScrapeInfo(announcer, info.scrape_str);
    tracker->id = info.id;
    tracker->seederCount = -1;
    tracker->leecherCount = -1;
    tracker->downloadCount = -1;
}

static void trackerDestruct(tr_tracker* tracker)
{
    tr_free(tracker->tracker_id_str);
}

/***
****
***/

struct tr_announcer_tiers;

/** @brief A group of trackers in a single tier, as per the multitracker spec */
struct tr_tier
{
    /* number of up/down/corrupt bytes since the last time we sent an
     * "event=stopped" message that was acknowledged by the tracker */
    uint64_t byteCounts[3];

    tr_tracker* trackers;
    int tracker_count;
    tr_tracker* currentTracker;
    int currentTrackerIndex;

    tr_torrent* tor;

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

    tr_announce_event* announce_events;
    int announce_event_priority;
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
};

static time_t get_next_scrape_time(tr_session const* session, tr_tier const* tier, int interval)
{
    /* Maybe don't scrape paused torrents */
    if (!tier->isRunning && !session->scrapePausedTorrents)
    {
        return 0;
    }

    /* Add the interval, and then increment to the nearest 10th second.
     * The latter step is to increase the odds of several torrents coming
     * due at the same time to improve multiscrape. */
    time_t const now = tr_time();
    time_t ret = now + interval;
    while (ret % 10 != 0)
    {
        ++ret;
    }

    return ret;
}

static void tierConstruct(tr_tier* tier, tr_torrent* tor)
{
    static int nextKey = 1;

    memset(tier, 0, sizeof(tr_tier));

    tier->key = nextKey++;
    tier->currentTrackerIndex = -1;
    tier->scrapeIntervalSec = DefaultScrapeIntervalSec;
    tier->announceIntervalSec = DefaultAnnounceIntervalSec;
    tier->announceMinIntervalSec = DefaultAnnounceMinIntervalSec;
    tier->scrapeAt = get_next_scrape_time(tor->session, tier, 0);
    tier->tor = tor;
}

static void tierDestruct(tr_tier* tier)
{
    tr_free(tier->announce_events);
}

static void tier_build_log_name(tr_tier const* tier, char* buf, size_t buflen)
{
    auto const* const name = tier != nullptr && tier->tor != nullptr ? tr_torrentName(tier->tor) : "?";
    auto const key_sv = tier != nullptr && tier->currentTracker != nullptr ? tier->currentTracker->key.sv() : "?"sv;
    tr_snprintf(buf, buflen, "[%s---%" TR_PRIsv "]", name, TR_PRIsv_ARG(key_sv));
}

static void tierIncrementTracker(tr_tier* tier)
{
    /* move our index to the next tracker in the tier */
    int const i = tier->currentTracker == nullptr ? 0 : (tier->currentTrackerIndex + 1) % tier->tracker_count;
    tier->currentTrackerIndex = i;
    tier->currentTracker = &tier->trackers[i];

    /* reset some of the tier's fields */
    tier->scrapeIntervalSec = DefaultScrapeIntervalSec;
    tier->announceIntervalSec = DefaultAnnounceIntervalSec;
    tier->announceMinIntervalSec = DefaultAnnounceMinIntervalSec;
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
struct tr_announcer_tiers
{
    tr_tier* tiers;
    int tier_count;

    tr_tracker* trackers;
    int tracker_count;

    tr_tracker_callback callback;
    void* callbackData;
};

static tr_announcer_tiers* tiersNew(void)
{
    return tr_new0(tr_announcer_tiers, 1);
}

static void tiersDestruct(tr_announcer_tiers* tt)
{
    for (int i = 0; i < tt->tracker_count; ++i)
    {
        trackerDestruct(&tt->trackers[i]);
    }

    tr_free(tt->trackers);

    for (int i = 0; i < tt->tier_count; ++i)
    {
        tierDestruct(&tt->tiers[i]);
    }

    tr_free(tt->tiers);
}

static void tiersFree(tr_announcer_tiers* tt)
{
    tiersDestruct(tt);
    tr_free(tt);
}

static tr_tier* getTier(tr_announcer* announcer, tr_sha1_digest_t const& info_hash, int tierId)
{
    tr_tier* tier = nullptr;

    if (announcer != nullptr)
    {
        tr_torrent* const tor = announcer->session->getTorrent(info_hash);

        if (tor != nullptr && tor->announcer_tiers != nullptr)
        {
            tr_announcer_tiers* tt = tor->announcer_tiers;

            for (int i = 0; tier == nullptr && i < tt->tier_count; ++i)
            {
                if (tt->tiers[i].key == tierId)
                {
                    tier = &tt->tiers[i];
                }
            }
        }
    }

    return tier;
}

/***
****  PUBLISH
***/

static void publishMessage(tr_tier* tier, std::string_view msg, TrackerEventType type)
{
    if (tier != nullptr && tier->tor != nullptr && tier->tor->announcer_tiers != nullptr &&
        tier->tor->announcer_tiers->callback != nullptr)
    {
        tr_announcer_tiers* tiers = tier->tor->announcer_tiers;
        auto event = tr_tracker_event{};
        event.messageType = type;
        event.text = msg;

        if (tier->currentTracker != nullptr)
        {
            event.announce_url = tier->currentTracker->announce_url;
        }

        (*tiers->callback)(tier->tor, &event, tiers->callbackData);
    }
}

static void publishErrorClear(tr_tier* tier)
{
    publishMessage(tier, ""sv, TR_TRACKER_ERROR_CLEAR);
}

static void publishWarning(tr_tier* tier, std::string_view msg)
{
    publishMessage(tier, msg, TR_TRACKER_WARNING);
}

static void publishError(tr_tier* tier, std::string_view msg)
{
    publishMessage(tier, msg, TR_TRACKER_ERROR);
}

static void publishPeerCounts(tr_tier* tier, int seeders, int leechers)
{
    if (tier->tor->announcer_tiers->callback != nullptr)
    {
        auto e = tr_tracker_event{};
        e.messageType = TR_TRACKER_COUNTS;
        e.seeders = seeders;
        e.leechers = leechers;
        dbgmsg(tier, "peer counts: %d seeders, %d leechers.", seeders, leechers);

        (*tier->tor->announcer_tiers->callback)(tier->tor, &e, nullptr);
    }
}

static void publishPeersPex(tr_tier* tier, int seeders, int leechers, tr_pex const* pex, int n)
{
    if (tier->tor->announcer_tiers->callback != nullptr)
    {
        auto e = tr_tracker_event{};
        e.messageType = TR_TRACKER_PEERS;
        e.seeders = seeders;
        e.leechers = leechers;
        e.pex = pex;
        e.pexCount = n;
        dbgmsg(tier, "tracker knows of %d seeders and %d leechers and gave a list of %d peers.", seeders, leechers, n);

        (*tier->tor->announcer_tiers->callback)(tier->tor, &e, nullptr);
    }
}

/***
****
***/

static void addTorrentToTier(tr_announcer_tiers* tt, tr_torrent* tor)
{
    auto const n = tor->trackerCount();
    auto const tiers = tor->tiers();

    // build the tracker and tier arrays
    tt->trackers = tr_new0(tr_tracker, n);
    tt->tracker_count = n;
    tt->tiers = tr_new0(tr_tier, std::size(tiers));

    auto prev_tier = std::optional<tr_tracker_tier_t>{};
    tr_tier* tier = nullptr;
    for (size_t i = 0; i < n; ++i)
    {
        auto const info = tor->tracker(i);

        trackerConstruct(tor->session->announcer, &tt->trackers[i], info);

        if (!prev_tier || *prev_tier != info.tier)
        {
            tier = &tt->tiers[tt->tier_count++];
            tierConstruct(tier, tor);
            tier->trackers = &tt->trackers[i];
            tier->tracker_count = 1;
            tierIncrementTracker(tier);

            prev_tier = info.tier;
        }
        else
        {
            ++tier->tracker_count;
        }
    }
}

tr_announcer_tiers* tr_announcerAddTorrent(tr_torrent* tor, tr_tracker_callback callback, void* callbackData)
{
    TR_ASSERT(tr_isTorrent(tor));

    tr_announcer_tiers* tiers = tiersNew();
    tiers->callback = callback;
    tiers->callbackData = callbackData;

    addTorrentToTier(tiers, tor);

    return tiers;
}

/***
****
***/

static bool tierCanManualAnnounce(tr_tier const* tier)
{
    return tier->manualAnnounceAllowedAt <= tr_time();
}

bool tr_announcerCanManualAnnounce(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->announcer_tiers != nullptr);

    struct tr_announcer_tiers const* tt = nullptr;

    if (tor->isRunning)
    {
        tt = tor->announcer_tiers;
    }

    /* return true if any tier can manual announce */
    for (int i = 0; tt != nullptr && i < tt->tier_count; ++i)
    {
        if (tierCanManualAnnounce(&tt->tiers[i]))
        {
            return true;
        }
    }

    return false;
}

time_t tr_announcerNextManualAnnounce(tr_torrent const* tor)
{
    time_t ret = ~(time_t)0;
    struct tr_announcer_tiers const* tt = tor->announcer_tiers;

    /* find the earliest manual announce time from all peers */
    for (int i = 0; tt != nullptr && i < tt->tier_count; ++i)
    {
        if (tt->tiers[i].isRunning)
        {
            ret = std::min(ret, tt->tiers[i].manualAnnounceAllowedAt);
        }
    }

    return ret;
}

static void dbgmsg_tier_announce_queue(tr_tier const* tier)
{
    if (tr_logGetDeepEnabled())
    {
        char name[128];
        struct evbuffer* buf = evbuffer_new();

        tier_build_log_name(tier, name, sizeof(name));

        for (int i = 0; i < tier->announce_event_count; ++i)
        {
            tr_announce_event const e = tier->announce_events[i];
            char const* str = tr_announce_event_get_string(e);
            evbuffer_add_printf(buf, "[%d:%s]", i, str);
        }

        auto const message = evbuffer_free_to_str(buf);
        tr_logAddDeep(__FILE__, __LINE__, name, "announce queue is %" TR_PRIsv, TR_PRIsv_ARG(message));
    }
}

// higher priorities go to the front of the announce queue
static void tier_update_announce_priority(tr_tier* tier)
{
    int priority = -1;

    for (int i = 0; i < tier->announce_event_count; ++i)
    {
        priority = std::max(priority, int{ tier->announce_events[i] });
    }

    tier->announce_event_priority = priority;
}

static void tier_announce_remove_trailing(tr_tier* tier, tr_announce_event e)
{
    while (tier->announce_event_count > 0 && tier->announce_events[tier->announce_event_count - 1] == e)
    {
        --tier->announce_event_count;
    }

    tier_update_announce_priority(tier);
}

static void tier_announce_event_push(tr_tier* tier, tr_announce_event e, time_t announceAt)
{
    TR_ASSERT(tier != nullptr);

    dbgmsg_tier_announce_queue(tier);
    dbgmsg(tier, "queued \"%s\"", tr_announce_event_get_string(e));

    if (tier->announce_event_count > 0)
    {
        /* special case #1: if we're adding a "stopped" event,
         * dump everything leading up to it except "completed" */
        if (e == TR_ANNOUNCE_EVENT_STOPPED)
        {
            bool has_completed = false;
            tr_announce_event const c = TR_ANNOUNCE_EVENT_COMPLETED;

            for (int i = 0; !has_completed && i < tier->announce_event_count; ++i)
            {
                has_completed = c == tier->announce_events[i];
            }

            tier->announce_event_count = 0;

            if (has_completed)
            {
                tier->announce_events[tier->announce_event_count++] = c;
                tier_update_announce_priority(tier);
            }
        }

        /* special case #2: dump all empty strings leading up to this event */
        tier_announce_remove_trailing(tier, TR_ANNOUNCE_EVENT_NONE);

        /* special case #3: no consecutive duplicates */
        tier_announce_remove_trailing(tier, e);
    }

    /* make room in the array for another event */
    if (tier->announce_event_alloc <= tier->announce_event_count)
    {
        tier->announce_event_alloc += 4;
        tier->announce_events = tr_renew(tr_announce_event, tier->announce_events, tier->announce_event_alloc);
    }

    /* add it */
    tier->announceAt = announceAt;
    tier->announce_events[tier->announce_event_count++] = e;
    tier_update_announce_priority(tier);

    dbgmsg_tier_announce_queue(tier);
    dbgmsg(tier, "announcing in %d seconds", (int)difftime(announceAt, tr_time()));
}

static tr_announce_event tier_announce_event_pull(tr_tier* tier)
{
    tr_announce_event const e = tier->announce_events[0];

    tr_removeElementFromArray(tier->announce_events, 0, sizeof(tr_announce_event), tier->announce_event_count);
    --tier->announce_event_count;
    tier_update_announce_priority(tier);

    return e;
}

static void torrentAddAnnounce(tr_torrent* tor, tr_announce_event e, time_t announceAt)
{
    struct tr_announcer_tiers* tt = tor->announcer_tiers;

    /* walk through each tier and tell them to announce */
    for (int i = 0; i < tt->tier_count; ++i)
    {
        tier_announce_event_push(&tt->tiers[i], e, announceAt);
    }
}

void tr_announcerTorrentStarted(tr_torrent* tor)
{
    torrentAddAnnounce(tor, TR_ANNOUNCE_EVENT_STARTED, tr_time());
}

void tr_announcerManualAnnounce(tr_torrent* tor)
{
    torrentAddAnnounce(tor, TR_ANNOUNCE_EVENT_NONE, tr_time());
}

void tr_announcerTorrentStopped(tr_torrent* tor)
{
    torrentAddAnnounce(tor, TR_ANNOUNCE_EVENT_STOPPED, tr_time());
}

void tr_announcerTorrentCompleted(tr_torrent* tor)
{
    torrentAddAnnounce(tor, TR_ANNOUNCE_EVENT_COMPLETED, tr_time());
}

void tr_announcerChangeMyPort(tr_torrent* tor)
{
    tr_announcerTorrentStarted(tor);
}

/***
****
***/

void tr_announcerAddBytes(tr_torrent* tor, int type, uint32_t byteCount)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(type == TR_ANN_UP || type == TR_ANN_DOWN || type == TR_ANN_CORRUPT);

    struct tr_announcer_tiers* tt = tor->announcer_tiers;

    for (int i = 0; i < tt->tier_count; ++i)
    {
        tt->tiers[i].byteCounts[type] += byteCount;
    }
}

/***
****
***/

static tr_announce_request* announce_request_new(
    tr_announcer const* announcer,
    tr_torrent* tor,
    tr_tier const* tier,
    tr_announce_event event)
{
    tr_announce_request* req = tr_new0(tr_announce_request, 1);
    req->port = tr_sessionGetPublicPeerPort(announcer->session);
    req->announce_url = tier->currentTracker->announce_url;
    req->tracker_id_str = tr_strdup(tier->currentTracker->tracker_id_str);
    req->info_hash = tor->infoHash();
    req->peer_id = tr_torrentGetPeerId(tor);
    req->up = tier->byteCounts[TR_ANN_UP];
    req->down = tier->byteCounts[TR_ANN_DOWN];
    req->corrupt = tier->byteCounts[TR_ANN_CORRUPT];
    req->leftUntilComplete = tor->hasMetadata() ? tor->totalSize() - tor->hasTotal() : INT64_MAX;
    req->event = event;
    req->numwant = event == TR_ANNOUNCE_EVENT_STOPPED ? 0 : Numwant;
    req->key = announcer->key;
    req->partial_seed = tor->isPartialSeed();
    tier_build_log_name(tier, req->log_name, sizeof(req->log_name));
    return req;
}

static void announce_request_free(tr_announce_request* req);

void tr_announcerRemoveTorrent(tr_announcer* announcer, tr_torrent* tor)
{
    struct tr_announcer_tiers const* tt = tor->announcer_tiers;

    if (tt != nullptr)
    {
        for (int i = 0; i < tt->tier_count; ++i)
        {
            tr_tier const* tier = &tt->tiers[i];

            if (tier->isRunning)
            {
                tr_announce_event const e = TR_ANNOUNCE_EVENT_STOPPED;
                tr_announce_request* req = announce_request_new(announcer, tor, tier, e);

                if (announcer->stops.count(req))
                {
                    announce_request_free(req);
                }
                else
                {
                    announcer->stops.insert(req);
                }
            }
        }

        tiersFree(tor->announcer_tiers);
        tor->announcer_tiers = nullptr;
    }
}

static int getRetryInterval(tr_tracker const* t)
{
    switch (t->consecutiveFailures)
    {
    case 0:
        return 0;

    case 1:
        return 20;

    case 2:
        return tr_rand_int_weak(60) + 60 * 5;

    case 3:
        return tr_rand_int_weak(60) + 60 * 15;

    case 4:
        return tr_rand_int_weak(60) + 60 * 30;

    case 5:
        return tr_rand_int_weak(60) + 60 * 60;

    default:
        return tr_rand_int_weak(60) + 60 * 120;
    }
}

struct announce_data
{
    int tierId;
    time_t timeSent;
    tr_announce_event event;
    tr_session* session;

    /** If the request succeeds, the value for tier's "isRunning" flag */
    bool isRunningOnSuccess;
};

static void on_announce_error(tr_tier* tier, char const* err, tr_announce_event e)
{
    /* increment the error count */
    if (tier->currentTracker != nullptr)
    {
        ++tier->currentTracker->consecutiveFailures;
    }

    /* set the error message */
    tr_strlcpy(tier->lastAnnounceStr, err, sizeof(tier->lastAnnounceStr));

    /* switch to the next tracker */
    tierIncrementTracker(tier);

    /* schedule a reannounce */
    int const interval = getRetryInterval(tier->currentTracker);
    auto const* const key_cstr = tier->currentTracker->key.c_str();
    dbgmsg(tier, "Tracker '%s' announce error: %s (Retrying in %d seconds)", key_cstr, err, interval);
    tr_logAddTorInfo(tier->tor, "Tracker '%s' announce error: %s (Retrying in %d seconds)", key_cstr, err, interval);
    tier_announce_event_push(tier, e, tr_time() + interval);
}

static void on_announce_done(tr_announce_response const* response, void* vdata)
{
    auto* data = static_cast<struct announce_data*>(vdata);
    tr_announcer* announcer = data->session->announcer;
    tr_tier* tier = getTier(announcer, response->info_hash, data->tierId);
    time_t const now = tr_time();
    tr_announce_event const event = data->event;

    if (tier != nullptr)
    {
        dbgmsg(
            tier,
            "Got announce response: "
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
            response->tracker_id_str != nullptr ? response->tracker_id_str : "none",
            response->pex_count,
            response->pex6_count,
            (!std::empty(response->errmsg) ? response->errmsg.c_str() : "none"),
            (!std::empty(response->warning) ? response->warning.c_str() : "none"));

        tier->lastAnnounceTime = now;
        tier->lastAnnounceTimedOut = response->did_timeout;
        tier->lastAnnounceSucceeded = false;
        tier->isAnnouncing = false;
        tier->manualAnnounceAllowedAt = now + tier->announceMinIntervalSec;

        if (!response->did_connect)
        {
            on_announce_error(tier, _("Could not connect to tracker"), event);
        }
        else if (response->did_timeout)
        {
            on_announce_error(tier, _("Tracker did not respond"), event);
        }
        else if (!std::empty(response->errmsg))
        {
            /* If the torrent's only tracker returned an error, publish it.
               Don't bother publishing if there are other trackers -- it's
               all too common for people to load up dozens of dead trackers
               in a torrent's metainfo... */
            if (tier->tor->trackerCount() < 2)
            {
                publishError(tier, response->errmsg);
            }

            on_announce_error(tier, response->errmsg.c_str(), event);
        }
        else
        {
            auto const isStopped = event == TR_ANNOUNCE_EVENT_STOPPED;
            auto leechers = int{};
            auto scrape_fields = int{};
            auto seeders = int{};

            publishErrorClear(tier);

            tr_tracker* const tracker = tier->currentTracker;
            if (tracker != nullptr)
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
                    tracker->downloadCount = response->downloads;
                    ++scrape_fields;
                }

                if (response->tracker_id_str != nullptr)
                {
                    tr_free(tracker->tracker_id_str);
                    tracker->tracker_id_str = tr_strdup(response->tracker_id_str);
                }
            }

            if (auto const& warning = response->warning; !std::empty(warning))
            {
                tr_strlcpy(tier->lastAnnounceStr, warning.c_str(), sizeof(tier->lastAnnounceStr));
                dbgmsg(tier, "tracker gave \"%s\"", warning.c_str());
                publishWarning(tier, warning);
            }
            else
            {
                tr_strlcpy(tier->lastAnnounceStr, _("Success"), sizeof(tier->lastAnnounceStr));
            }

            if (response->min_interval != 0)
            {
                tier->announceMinIntervalSec = response->min_interval;
            }

            if (response->interval != 0)
            {
                tier->announceIntervalSec = response->interval;
            }

            if (response->pex_count > 0)
            {
                publishPeersPex(tier, seeders, leechers, response->pex, response->pex_count);
            }

            if (response->pex6_count > 0)
            {
                publishPeersPex(tier, seeders, leechers, response->pex6, response->pex6_count);
            }

            publishPeerCounts(tier, seeders, leechers);

            tier->isRunning = data->isRunningOnSuccess;

            /* if the tracker included scrape fields in its announce response,
               then a separate scrape isn't needed */
            if (scrape_fields >= 3 || (scrape_fields >= 1 && tracker->scrape_info == nullptr))
            {
                tr_logAddTorDbg(
                    tier->tor,
                    "Announce response contained scrape info; "
                    "rescheduling next scrape to %d seconds from now.",
                    tier->scrapeIntervalSec);
                tier->scrapeAt = get_next_scrape_time(announcer->session, tier, tier->scrapeIntervalSec);
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = true;
            }
            else if (tier->lastScrapeTime + tier->scrapeIntervalSec <= now)
            {
                tier->scrapeAt = get_next_scrape_time(announcer->session, tier, 0);
            }

            tier->lastAnnounceSucceeded = true;
            tier->lastAnnouncePeerCount = response->pex_count + response->pex6_count;

            if (isStopped)
            {
                /* now that we've successfully stopped the torrent,
                 * we can reset the up/down/corrupt count we've kept
                 * for this tracker */
                tier->byteCounts[TR_ANN_UP] = 0;
                tier->byteCounts[TR_ANN_DOWN] = 0;
                tier->byteCounts[TR_ANN_CORRUPT] = 0;
            }

            if (!isStopped && tier->announce_event_count == 0)
            {
                /* the queue is empty, so enqueue a perodic update */
                int const i = tier->announceIntervalSec;
                dbgmsg(tier, "Sending periodic reannounce in %d seconds", i);
                tier_announce_event_push(tier, TR_ANNOUNCE_EVENT_NONE, now + i);
            }
        }
    }

    tr_free(data);
}

static void announce_request_free(tr_announce_request* req)
{
    tr_free(req->tracker_id_str);
    tr_free(req);
}

static void announce_request_delegate(
    tr_announcer* announcer,
    tr_announce_request* request,
    tr_announce_response_func callback,
    void* callback_data)
{
    tr_session* session = announcer->session;

#if 0

    fprintf(stderr, "ANNOUNCE: event %s isPartialSeed %d port %d key %d numwant %d up %" PRIu64 " down %" PRIu64
        " corrupt %" PRIu64 " left %" PRIu64 " url [%s] tracker_id_str [%s] peer_id [%20.20s]\n",
        tr_announce_event_get_string(request->event), (int)request->partial_seed, (int)request->port, request->key,
        request->numwant, request->up, request->down, request->corrupt, request->leftUntilComplete, request->url,
        request->tracker_id_str, request->peer_id);

#endif

    if (auto const announce_sv = request->announce_url.sv();
        tr_strvStartsWith(announce_sv, "http://"sv) || tr_strvStartsWith(announce_sv, "https://"sv))
    {
        tr_tracker_http_announce(session, request, callback, callback_data);
    }
    else if (tr_strvStartsWith(announce_sv, "udp://"sv))
    {
        tr_tracker_udp_announce(session, request, callback, callback_data);
    }
    else
    {
        tr_logAddError("Unsupported url: %" TR_PRIsv, TR_PRIsv_ARG(announce_sv));
    }

    announce_request_free(request);
}

static void tierAnnounce(tr_announcer* announcer, tr_tier* tier)
{
    TR_ASSERT(!tier->isAnnouncing);
    TR_ASSERT(tier->announce_event_count > 0);

    time_t const now = tr_time();

    tr_torrent* tor = tier->tor;
    tr_announce_event announce_event = tier_announce_event_pull(tier);
    tr_announce_request* req = announce_request_new(announcer, tor, tier, announce_event);

    struct announce_data* data = tr_new0(struct announce_data, 1);
    data->session = announcer->session;
    data->tierId = tier->key;
    data->isRunningOnSuccess = tor->isRunning;
    data->timeSent = now;
    data->event = announce_event;

    tier->isAnnouncing = true;
    tier->lastAnnounceStartTime = now;

    announce_request_delegate(announcer, req, on_announce_done, data);
}

/***
****
****  SCRAPE
****
***/

static constexpr bool multiscrape_too_big(std::string_view errmsg)
{
    /* Found a tracker that returns some bespoke string for this case?
       Add your patch here and open a PR */
    auto constexpr TooLongErrors = std::array<std::string_view, 3>{
        "Bad Request",
        "GET string too long",
        "Request-URI Too Long",
    };

    for (auto const& tle : TooLongErrors)
    {
        if (tr_strvContains(errmsg, tle))
        {
            return true;
        }
    }

    return false;
}

static void on_scrape_error(tr_session const* session, tr_tier* tier, char const* errmsg)
{
    /* increment the error count */
    if (tier->currentTracker != nullptr)
    {
        ++tier->currentTracker->consecutiveFailures;
    }

    /* set the error message */
    tr_strlcpy(tier->lastScrapeStr, errmsg, sizeof(tier->lastScrapeStr));

    /* switch to the next tracker */
    tierIncrementTracker(tier);

    /* schedule a rescrape */
    int const interval = getRetryInterval(tier->currentTracker);
    auto const* const key_cstr = tier->currentTracker->key.c_str();
    dbgmsg(tier, "Tracker '%s' scrape error: %s (Retrying in %zu seconds)", key_cstr, errmsg, (size_t)interval);
    tr_logAddTorInfo(tier->tor, "Tracker '%s' error: %s (Retrying in %zu seconds)", key_cstr, errmsg, (size_t)interval);
    tier->lastScrapeSucceeded = false;
    tier->scrapeAt = get_next_scrape_time(session, tier, interval);
}

static tr_tier* find_tier(tr_torrent* tor, tr_interned_string scrape_url)
{
    struct tr_announcer_tiers* tt = tor->announcer_tiers;

    for (int i = 0; tt != nullptr && i < tt->tier_count; ++i)
    {
        tr_tracker const* const tracker = tt->tiers[i].currentTracker;

        if (tracker != nullptr && tracker->scrape_info != nullptr && tracker->scrape_info->scrape_url == scrape_url)
        {
            return &tt->tiers[i];
        }
    }

    return nullptr;
}

static void checkMultiscrapeMax(tr_announcer* announcer, tr_scrape_response const* response)
{
    if (!multiscrape_too_big(response->errmsg))
    {
        return;
    }

    auto const& url = response->scrape_url;
    struct tr_scrape_info* const scrape_info = tr_announcerGetScrapeInfo(announcer, url);
    if (scrape_info == nullptr)
    {
        return;
    }

    // Lower the max only if it hasn't already lowered for a similar
    // error. So if N parallel multiscrapes all have the same `max`
    // and error out, lower the value once for that batch, not N times.
    int& multiscrape_max = scrape_info->multiscrape_max;
    if (multiscrape_max < response->row_count)
    {
        return;
    }

    int const n = std::max(1, int{ multiscrape_max - TrMultiscrapeStep });
    if (multiscrape_max != n)
    {
        // don't log the full URL, since that might have a personal announce id
        // (note: we know 'parsed' will be successful since this url has a scrape_info)
        auto const parsed = *tr_urlParse(url.sv());
        auto clean_url = std::string{};
        tr_buildBuf(clean_url, parsed.scheme, "://"sv, parsed.host, ":"sv, parsed.portstr);
        tr_logAddNamedInfo(clean_url.c_str(), "Reducing multiscrape max to %d", n);
        multiscrape_max = n;
    }
}

static void on_scrape_done(tr_scrape_response const* response, void* vsession)
{
    time_t const now = tr_time();
    auto* session = static_cast<tr_session*>(vsession);
    tr_announcer* announcer = session->announcer;

    for (int i = 0; i < response->row_count; ++i)
    {
        struct tr_scrape_response_row const* row = &response->rows[i];
        tr_torrent* tor = session->getTorrent(row->info_hash);

        if (tor != nullptr)
        {
            tr_tier* tier = find_tier(tor, response->scrape_url);

            if (tier != nullptr)
            {
                auto const scrape_url_sv = response->scrape_url.sv();

                dbgmsg(
                    tier,
                    "scraped url:%" TR_PRIsv
                    " -- "
                    "did_connect:%d "
                    "did_timeout:%d "
                    "seeders:%d "
                    "leechers:%d "
                    "downloads:%d "
                    "downloaders:%d "
                    "min_request_interval:%d "
                    "err:%s ",
                    TR_PRIsv_ARG(scrape_url_sv),
                    (int)response->did_connect,
                    (int)response->did_timeout,
                    row->seeders,
                    row->leechers,
                    row->downloads,
                    row->downloaders,
                    response->min_request_interval,
                    std::empty(response->errmsg) ? "none" : response->errmsg.c_str());

                tier->isScraping = false;
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = false;
                tier->lastScrapeTimedOut = response->did_timeout;

                if (!response->did_connect)
                {
                    on_scrape_error(session, tier, _("Could not connect to tracker"));
                }
                else if (response->did_timeout)
                {
                    on_scrape_error(session, tier, _("Tracker did not respond"));
                }
                else if (!std::empty(response->errmsg))
                {
                    on_scrape_error(session, tier, response->errmsg.c_str());
                }
                else
                {
                    tier->lastScrapeSucceeded = true;
                    tier->scrapeIntervalSec = std::max(int{ DefaultScrapeIntervalSec }, response->min_request_interval);
                    tier->scrapeAt = get_next_scrape_time(session, tier, tier->scrapeIntervalSec);
                    tr_logAddTorDbg(tier->tor, "Scrape successful. Rescraping in %d seconds.", tier->scrapeIntervalSec);

                    tr_tracker* const tracker = tier->currentTracker;
                    if (tracker != nullptr)
                    {
                        if (row->seeders >= 0)
                        {
                            tracker->seederCount = row->seeders;
                        }

                        if (row->leechers >= 0)
                        {
                            tracker->leecherCount = row->leechers;
                        }

                        if (row->downloads >= 0)
                        {
                            tracker->downloadCount = row->downloads;
                        }

                        tracker->downloaderCount = row->downloaders;
                        tracker->consecutiveFailures = 0;
                    }

                    if (row->seeders >= 0 && row->leechers >= 0 && row->downloads >= 0)
                    {
                        publishPeerCounts(tier, row->seeders, row->leechers);
                    }
                }
            }
        }
    }

    checkMultiscrapeMax(announcer, response);
}

static void scrape_request_delegate(
    tr_announcer* announcer,
    tr_scrape_request const* request,
    tr_scrape_response_func callback,
    void* callback_data)
{
    tr_session* session = announcer->session;

    auto const scrape_sv = request->scrape_url.sv();

    if (tr_strvStartsWith(scrape_sv, "http://"sv) || tr_strvStartsWith(scrape_sv, "https://"sv))
    {
        tr_tracker_http_scrape(session, request, callback, callback_data);
    }
    else if (tr_strvStartsWith(scrape_sv, "udp://"sv))
    {
        tr_tracker_udp_scrape(session, request, callback, callback_data);
    }
    else
    {
        tr_logAddError("Unsupported url: %" TR_PRIsv, TR_PRIsv_ARG(scrape_sv));
    }
}

static void multiscrape(tr_announcer* announcer, std::vector<tr_tier*> const& tiers)
{
    size_t request_count = 0;
    time_t const now = tr_time();
    tr_scrape_request requests[MaxScrapesPerUpkeep] = {};

    /* batch as many info_hashes into a request as we can */
    for (auto* tier : tiers)
    {
        struct tr_scrape_info* const scrape_info = tier->currentTracker->scrape_info;
        bool found = false;

        TR_ASSERT(scrape_info != nullptr);

        /* if there's a request with this scrape URL and a free slot, use it */
        for (size_t j = 0; !found && j < request_count; ++j)
        {
            tr_scrape_request* req = &requests[j];

            if (req->info_hash_count >= scrape_info->multiscrape_max)
            {
                continue;
            }

            if (scrape_info->scrape_url != req->scrape_url)
            {
                continue;
            }

            req->info_hash[req->info_hash_count] = tier->tor->infoHash();
            ++req->info_hash_count;
            tier->isScraping = true;
            tier->lastScrapeStartTime = now;
            found = true;
        }

        /* otherwise, if there's room for another request, build a new one */
        if (!found && request_count < MaxScrapesPerUpkeep)
        {
            tr_scrape_request* req = &requests[request_count++];
            req->scrape_url = scrape_info->scrape_url;
            tier_build_log_name(tier, req->log_name, sizeof(req->log_name));

            req->info_hash[req->info_hash_count] = tier->tor->infoHash();
            ++req->info_hash_count;
            tier->isScraping = true;
            tier->lastScrapeStartTime = now;
        }
    }

    /* send the requests we just built */
    for (size_t i = 0; i < request_count; ++i)
    {
        scrape_request_delegate(announcer, &requests[i], on_scrape_done, announcer->session);
    }
}

static void flushCloseMessages(tr_announcer* announcer)
{
    auto& stops = announcer->stops;
    std::for_each(
        std::begin(stops),
        std::end(stops),
        [&announcer](auto* stop) { announce_request_delegate(announcer, stop, nullptr, nullptr); });
    stops.clear();
}

static constexpr bool tierNeedsToAnnounce(tr_tier const* tier, time_t const now)
{
    return !tier->isAnnouncing && !tier->isScraping && tier->announceAt != 0 && tier->announceAt <= now &&
        tier->announce_event_count > 0;
}

static constexpr bool tierNeedsToScrape(tr_tier const* tier, time_t const now)
{
    return !tier->isScraping && tier->scrapeAt != 0 && tier->scrapeAt <= now && tier->currentTracker != nullptr &&
        tier->currentTracker->scrape_info != nullptr;
}

static constexpr int countDownloaders(tr_tier const* tier)
{
    tr_tracker const* const tracker = tier->currentTracker;

    return tracker == nullptr ? 0 : tracker->downloaderCount + tracker->leecherCount;
}

static int compareAnnounceTiers(tr_tier const* a, tr_tier const* b)
{
    /* prefer higher-priority events */
    if (auto const priority_a = a->announce_event_priority, priority_b = b->announce_event_priority; priority_a != priority_b)
    {
        return priority_a > priority_b ? -1 : 1;
    }

    /* prefer swarms where we might upload */
    if (auto const leechers_a = countDownloaders(a), leechers_b = countDownloaders(b); leechers_a != leechers_b)
    {
        return leechers_a > leechers_b ? -1 : 1;
    }

    /* prefer swarms where we might download */
    if (auto const is_done_a = a->tor->isDone(), is_done_b = b->tor->isDone(); is_done_a != is_done_b)
    {
        return is_done_a ? 1 : -1;
    }

    /* prefer larger stats, to help ensure stats get recorded when stopping on shutdown */
    if (auto const xa = a->byteCounts[TR_ANN_UP] + a->byteCounts[TR_ANN_DOWN],
        xb = b->byteCounts[TR_ANN_UP] + b->byteCounts[TR_ANN_DOWN];
        xa != xb)
    {
        return xa > xb ? -1 : 1;
    }

    // announcements that have been waiting longer go first
    if (a->announceAt != b->announceAt)
    {
        return a->announceAt < b->announceAt ? -1 : 1;
    }

    // the tiers are effectively equal priority, but add an arbitrary
    // differentiation because ptrArray sorted mode hates equal items.
    return a < b ? -1 : 1;
}

static void scrapeAndAnnounceMore(tr_announcer* announcer)
{
    time_t const now = tr_time();

    /* build a list of tiers that need to be announced */
    auto announce_me = std::vector<tr_tier*>{};
    auto scrape_me = std::vector<tr_tier*>{};
    for (auto* tor : announcer->session->torrents)
    {
        struct tr_announcer_tiers* tt = tor->announcer_tiers;

        for (int i = 0; tt != nullptr && i < tt->tier_count; ++i)
        {
            tr_tier* tier = &tt->tiers[i];

            if (tierNeedsToAnnounce(tier, now))
            {
                announce_me.push_back(tier);
            }

            if (tierNeedsToScrape(tier, now))
            {
                scrape_me.push_back(tier);
            }
        }
    }

    /* First, scrape what we can. We handle scrapes first because
     * we can work through that queue much faster than announces
     * (thanks to multiscrape) _and_ the scrape responses will tell
     * us which swarms are interesting and should be announced next. */
    multiscrape(announcer, scrape_me);

    /* Second, announce what we can. If there aren't enough slots
     * available, use compareAnnounceTiers to prioritize. */
    if (announce_me.size() > MaxAnnouncesPerUpkeep)
    {
        std::partial_sort(
            std::begin(announce_me),
            std::begin(announce_me) + MaxAnnouncesPerUpkeep,
            std::end(announce_me),
            [](auto const* a, auto const* b) { return compareAnnounceTiers(a, b) < 0; });
        announce_me.resize(MaxAnnouncesPerUpkeep);
    }

    for (auto*& tier : announce_me)
    {
        tr_logAddTorDbg(tier->tor, "%s", "Announcing to tracker");
        tierAnnounce(announcer, tier);
    }
}

static void onUpkeepTimer(evutil_socket_t /*fd*/, short /*what*/, void* vannouncer)
{
    auto* announcer = static_cast<tr_announcer*>(vannouncer);
    tr_session* session = announcer->session;
    auto const lock = session->unique_lock();

    bool const is_closing = session->isClosed;
    time_t const now = tr_time();

    /* maybe send out some "stopped" messages for closed torrents */
    flushCloseMessages(announcer);

    /* maybe kick off some scrapes / announces whose time has come */
    if (!is_closing)
    {
        scrapeAndAnnounceMore(announcer);
    }

    /* TAU upkeep */
    if (announcer->tauUpkeepAt <= now)
    {
        announcer->tauUpkeepAt = now + TauUpkeepIntervalSecs;
        tr_tracker_udp_upkeep(session);
    }

    /* set up the next timer */
    tr_timerAddMsec(announcer->upkeepTimer, UpkeepIntervalMsec);
}

/***
****
***/

static tr_tracker_view trackerView(tr_torrent const& tor, int tier_index, tr_tier const& tier, tr_tracker const& tracker)
{
    auto const now = tr_time();
    auto view = tr_tracker_view{};

    view.host = tracker.key.c_str();
    view.announce = tracker.announce_url.c_str();
    view.scrape = tracker.scrape_info == nullptr ? "" : tracker.scrape_info->scrape_url.c_str();

    view.id = tracker.id;
    view.tier = tier_index;
    view.isBackup = &tracker != tier.currentTracker;
    view.lastScrapeStartTime = tier.lastScrapeStartTime;
    view.seederCount = tracker.seederCount;
    view.leecherCount = tracker.leecherCount;
    view.downloadCount = tracker.downloadCount;

    if (view.isBackup)
    {
        view.scrapeState = TR_TRACKER_INACTIVE;
        view.announceState = TR_TRACKER_INACTIVE;
        view.nextScrapeTime = 0;
        view.nextAnnounceTime = 0;
    }
    else
    {
        view.hasScraped = tier.lastScrapeTime;
        if (view.hasScraped != 0)
        {
            view.lastScrapeTime = tier.lastScrapeTime;
            view.lastScrapeSucceeded = tier.lastScrapeSucceeded;
            view.lastScrapeTimedOut = tier.lastScrapeTimedOut;
            tr_strlcpy(view.lastScrapeResult, tier.lastScrapeStr, sizeof(view.lastScrapeResult));
        }

        if (tier.isScraping)
        {
            view.scrapeState = TR_TRACKER_ACTIVE;
        }
        else if (tier.scrapeAt == 0)
        {
            view.scrapeState = TR_TRACKER_INACTIVE;
        }
        else if (tier.scrapeAt > now)
        {
            view.scrapeState = TR_TRACKER_WAITING;
            view.nextScrapeTime = tier.scrapeAt;
        }
        else
        {
            view.scrapeState = TR_TRACKER_QUEUED;
        }

        view.lastAnnounceStartTime = tier.lastAnnounceStartTime;

        view.hasAnnounced = tier.lastAnnounceTime;
        if (view.hasAnnounced != 0)
        {
            view.lastAnnounceTime = tier.lastAnnounceTime;
            view.lastAnnounceSucceeded = tier.lastAnnounceSucceeded;
            view.lastAnnounceTimedOut = tier.lastAnnounceTimedOut;
            view.lastAnnouncePeerCount = tier.lastAnnouncePeerCount;
            tr_strlcpy(view.lastAnnounceResult, tier.lastAnnounceStr, sizeof(view.lastAnnounceResult));
        }

        if (tier.isAnnouncing)
        {
            view.announceState = TR_TRACKER_ACTIVE;
        }
        else if (!tor.isRunning || tier.announceAt == 0)
        {
            view.announceState = TR_TRACKER_INACTIVE;
        }
        else if (tier.announceAt > now)
        {
            view.announceState = TR_TRACKER_WAITING;
            view.nextAnnounceTime = tier.announceAt;
        }
        else
        {
            view.announceState = TR_TRACKER_QUEUED;
        }
    }

    TR_ASSERT(0 <= view.tier);
    TR_ASSERT(view.tier < tor.announcer_tiers->tier_count);
    return view;
}

size_t tr_announcerTrackerCount(tr_torrent const* tor)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->announcer_tiers != nullptr);

    return tor->announcer_tiers->tracker_count;
}

tr_tracker_view tr_announcerTracker(tr_torrent const* tor, size_t nth)
{
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(tor->announcer_tiers != nullptr);

    // find the nth tracker
    struct tr_announcer_tiers const* const tt = tor->announcer_tiers;
    if (nth >= size_t(tt->tracker_count))
    {
        return {};
    }
    auto const& tracker = tt->trackers[nth];
    for (int i = 0; i < tt->tier_count; ++i)
    {
        tr_tier const& tier = tt->tiers[i];

        for (int j = 0; j < tier.tracker_count; ++j)
        {
            if (&tier.trackers[j] == &tracker)
            {
                return trackerView(*tor, i, tier, tracker);
            }
        }
    }
    return {};
}

/***
****
***/

static void copy_tier_attributes_impl(struct tr_tier* tgt, int trackerIndex, tr_tier const* src)
{
    /* sanity clause */
    TR_ASSERT(trackerIndex < tgt->tracker_count);
    TR_ASSERT(tgt->trackers[trackerIndex].announce_url == src->currentTracker->announce_url);

    tr_tier const keep = *tgt;

    /* bitwise copy will handle most of tr_tier's fields... */
    *tgt = *src;

    /* ...fix the fields that can't be cleanly bitwise-copied */
    tgt->wasCopied = true;
    tgt->trackers = keep.trackers;
    tgt->tracker_count = keep.tracker_count;
    tgt->announce_events = static_cast<tr_announce_event*>(
        tr_memdup(src->announce_events, sizeof(tr_announce_event) * src->announce_event_count));
    tgt->announce_event_priority = src->announce_event_priority;
    tgt->announce_event_count = src->announce_event_count;
    tgt->announce_event_alloc = src->announce_event_count;
    tgt->currentTrackerIndex = trackerIndex;
    tgt->currentTracker = &tgt->trackers[trackerIndex];
    tgt->currentTracker->seederCount = src->currentTracker->seederCount;
    tgt->currentTracker->leecherCount = src->currentTracker->leecherCount;
    tgt->currentTracker->downloadCount = src->currentTracker->downloadCount;
    tgt->currentTracker->downloaderCount = src->currentTracker->downloaderCount;
}

static void copy_tier_attributes(struct tr_announcer_tiers* tt, tr_tier const* src)
{
    bool found = false;

    /* find a tier (if any) which has a match for src->currentTracker */
    for (int i = 0; !found && i < tt->tier_count; ++i)
    {
        for (int j = 0; !found && j < tt->tiers[i].tracker_count; ++j)
        {
            if (src->currentTracker->announce_url == tt->tiers[i].trackers[j].announce_url)
            {
                found = true;
                copy_tier_attributes_impl(&tt->tiers[i], j, src);
            }
        }
    }
}

void tr_announcerResetTorrent(tr_announcer* /*announcer*/, tr_torrent* tor)
{
    TR_ASSERT(tor->announcer_tiers != nullptr);

    time_t const now = tr_time();

    tr_announcer_tiers* tt = tor->announcer_tiers;
    tr_announcer_tiers old = *tt;

    /* remove the old tiers / trackers */
    tt->tiers = nullptr;
    tt->trackers = nullptr;
    tt->tier_count = 0;
    tt->tracker_count = 0;

    /* create the new tiers / trackers */
    addTorrentToTier(tt, tor);

    /* copy the old tiers' states into their replacements */
    for (int i = 0; i < old.tier_count; ++i)
    {
        if (old.tiers[i].currentTracker != nullptr)
        {
            copy_tier_attributes(tt, &old.tiers[i]);
        }
    }

    /* kickstart any tiers that didn't get started */
    if (tor->isRunning)
    {
        for (int i = 0; i < tt->tier_count; ++i)
        {
            if (!tt->tiers[i].wasCopied)
            {
                tier_announce_event_push(&tt->tiers[i], TR_ANNOUNCE_EVENT_STARTED, now);
            }
        }
    }

    /* cleanup */
    tiersDestruct(&old);
}
