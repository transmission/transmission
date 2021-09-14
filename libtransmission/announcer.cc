/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <climits> /* INT_MAX */
#include <cstdlib> /* qsort() */
#include <cstring> /* strcmp(), memcpy(), strncmp() */

#include <event2/buffer.h>
#include <event2/event.h> /* evtimer */
#include <algorithm>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include "transmission.h"
#include "announcer.h"
#include "announcer-common.h"
#include "crypto-utils.h" /* tr_rand_int(), tr_rand_int_weak() */
#include "log.h"
#include "peer-mgr.h" /* tr_peerMgrCompactToPex() */
#include "ptrarray.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "utils.h"

struct tr_tier;

#define dbgmsg(tier, ...) \
    do \
    { \
        if (tr_logGetDeepEnabled()) \
        { \
            char name[128]; \
            tier->build_log_name(name, TR_N_ELEMENTS(name)); \
            tr_logAddDeep(__FILE__, __LINE__, name, __VA_ARGS__); \
        } \
    } while (0)

enum tr_announcer_defaults: int
{
    /* unless the tracker says otherwise, rescrape this frequently */
    DEFAULT_SCRAPE_INTERVAL_SEC = (60 * 30),
    /* unless the tracker says otherwise, this is the announce interval */
    DEFAULT_ANNOUNCE_INTERVAL_SEC = (60 * 10),
    /* unless the tracker says otherwise, this is the announce min_interval */
    DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC = (60 * 2),
    /* the value of the 'numwant' argument passed in tracker requests. */
    NUMWANT = 80,

    /* how often to announce & scrape */
    UPKEEP_INTERVAL_MSEC = 500,
    MAX_ANNOUNCES_PER_UPKEEP = 20,
    MAX_SCRAPES_PER_UPKEEP = 20,

    /* this is how often to call the UDP tracker upkeep */
    TAU_UPKEEP_INTERVAL_SECS = 5,

    /* how many infohashes to remove when we get a scrape-too-long error */
    TR_MULTISCRAPE_STEP = 5
};

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

/***
****
***/

static int compareTransfer(uint64_t a_uploaded, uint64_t a_downloaded, uint64_t b_uploaded, uint64_t b_downloaded)
{
    /* higher upload count goes first */
    if (a_uploaded != b_uploaded)
    {
        return a_uploaded > b_uploaded ? -1 : 1;
    }

    /* then higher download count goes first */
    if (a_downloaded != b_downloaded)
    {
        return a_downloaded > b_downloaded ? -1 : 1;
    }

    return 0;
}

/**
 * Comparison function for tr_announce_requests.
 *
 * The primary key (amount of data transferred) is used to prioritize
 * tracker announcements of active torrents. The remaining keys are
 * used to satisfy the uniqueness requirement of a sorted tr_ptrArray.
 */
int tr_announce_request::compareStops(void const* va, void const* vb)
{
    int i;
    auto const* a = static_cast<tr_announce_request const*>(va);
    auto const* b = static_cast<tr_announce_request const*>(vb);

    /* primary key: volume of data transferred. */
    if ((i = compareTransfer(a->up, a->down, b->up, b->down)) != 0)
    {
        return i;
    }

    /* secondary key: the torrent's info_hash */
    if ((i = std::memcmp(a->info_hash, b->info_hash, SHA_DIGEST_LENGTH)) != 0)
    {
        return i;
    }

    /* tertiary key: the tracker's announce url */
    return tr_strcmp0(a->url, b->url);
}

/***
****
***/

struct tr_scrape_info
{
    char* url; // TODO: [C++] Owned container (string or unique_ptr)

    int multiscrape_max;

    static void scrapeInfoFree(void* va);
    static int compareScrapeInfo(void const* va, void const* vb);
};

void tr_scrape_info::scrapeInfoFree(void* va) // TODO: [C++] this is a destructor
{
    auto* a = static_cast<struct tr_scrape_info*>(va);

    tr_free(a->url);
    tr_free(a);
}

int tr_scrape_info::compareScrapeInfo(void const* va, void const* vb)
{
    auto const* a = static_cast<struct tr_scrape_info const*>(va);
    auto const* b = static_cast<struct tr_scrape_info const*>(vb);
    return tr_strcmp0(a->url, b->url);
}

/**
 * "global" (per-tr_session) fields
 */
typedef struct tr_announcer
{
    tr_ptrArray stops; /* tr_announce_request */
    tr_ptrArray scrape_info; /* struct tr_scrape_info */

    tr_session* session;
    struct event* upkeepTimer;
    int key;
    time_t tauUpkeepAt;

    static void tr_announcerInit(tr_session* session); // TODO: [C++] Convert to a constructor, create only with new/delete

    struct tr_scrape_info* getScrapeInfo(char const* url);
    void flushCloseMessages();
    tr_tier* getTier(uint8_t const* info_hash, int tierId);
} tr_announcer;

// TODO: session.cc is calling this. Remove when C++ constructor is invoked via operator new
void tr_announcerInit(tr_session* session) {
    tr_announcer::tr_announcerInit(session);
}

struct tr_scrape_info* tr_announcer::getScrapeInfo(char const* url)
{
    struct tr_scrape_info* info = nullptr;

    if (!tr_str_is_empty(url))
    {
        bool found;
        auto const key1 = tr_scrape_info{ const_cast<char*>(url), {} };
        int const pos = tr_ptrArrayLowerBound(&this->scrape_info, &key1, tr_scrape_info::compareScrapeInfo, &found);
        if (found)
        {
            info = static_cast<struct tr_scrape_info*>(tr_ptrArrayNth(&this->scrape_info, pos));
        }
        else
        {
            info = tr_new0(struct tr_scrape_info, 1);
            info->url = tr_strdup(url);
            info->multiscrape_max = TR_MULTISCRAPE_MAX;
            tr_ptrArrayInsert(&this->scrape_info, info, pos);
        }
    }

    return info;
}

static void onUpkeepTimer(evutil_socket_t fd, short what, void* vannouncer);

void tr_announcer::tr_announcerInit(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    tr_announcer* a = tr_new0(tr_announcer, 1);
    a->stops = {};
    a->key = tr_rand_int(INT_MAX);
    a->session = session;
    a->upkeepTimer = evtimer_new(session->event_base, onUpkeepTimer, a);
    tr_timerAddMsec(a->upkeepTimer, UPKEEP_INTERVAL_MSEC);

    session->announcer = a;
}

void tr_announcerClose(tr_session* session)
{
    tr_announcer* announcer = session->announcer;

    announcer->flushCloseMessages();

    tr_tracker_udp_start_shutdown(session);

    event_free(announcer->upkeepTimer);
    announcer->upkeepTimer = nullptr;

    tr_ptrArrayDestruct(&announcer->stops, nullptr);
    tr_ptrArrayDestruct(&announcer->scrape_info, tr_scrape_info::scrapeInfoFree);

    session->announcer = nullptr;
    tr_free(announcer);
}

/***
****
***/

/* a row in tr_tier's list of trackers */
struct tr_tracker
{
    char* key = nullptr;
    char* announce = nullptr;
    struct tr_scrape_info* scrape_info = nullptr;

    char* tracker_id_str = nullptr;

    int seederCount = 0;
    int leecherCount = 0;
    int downloadCount = 0;
    int downloaderCount = 0;

    int consecutiveFailures = 0;

    uint32_t id = 0;

    static char* getKey(char const* url);

    static void trackerConstruct(tr_announcer* announcer, tr_tracker* tracker, tr_tracker_info const* inf);
    static void trackerDestruct(tr_tracker* tracker);
};

/* format: host+':'+ port */
char* tr_tracker::getKey(char const* url)
{
    char* ret;
    char* scheme = nullptr;
    char* host = nullptr;
    int port = 0;

    tr_urlParse(url, TR_BAD_SIZE, &scheme, &host, &port, nullptr);
    ret = tr_strdup_printf("%s://%s:%d", scheme != nullptr ? scheme : "invalid", host != nullptr ? host : "invalid", port);

    tr_free(host);
    tr_free(scheme);
    return ret;
}

void tr_tracker::trackerConstruct(tr_announcer* announcer, tr_tracker* tracker, tr_tracker_info const* inf)
{
    *tracker = {}; // TODO: [C++] remove when this function is promoted to constructor

    tracker->key = tr_tracker::getKey(inf->announce);
    tracker->announce = tr_strdup(inf->announce);
    tracker->scrape_info = announcer->getScrapeInfo(inf->scrape);
    tracker->id = inf->id;
    tracker->seederCount = -1;
    tracker->leecherCount = -1;
    tracker->downloadCount = -1;
}

void tr_tracker::trackerDestruct(tr_tracker* tracker)
{
    tr_free(tracker->tracker_id_str);
    tr_free(tracker->announce);
    tr_free(tracker->key);
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

    tr_tracker* trackers = nullptr;
    size_t tracker_count = 0;
    tr_tracker* currentTracker = nullptr;
    size_t currentTrackerIndex = 0;

    tr_torrent* tor = nullptr;

    time_t scrapeAt = 0;
    time_t lastScrapeStartTime = 0;
    time_t lastScrapeTime = 0;
    bool lastScrapeSucceeded = false;
    bool lastScrapeTimedOut = false;

    time_t announceAt = 0;
    time_t manualAnnounceAllowedAt = 0;
    time_t lastAnnounceStartTime = 0;
    time_t lastAnnounceTime = 0;
    bool lastAnnounceSucceeded = false;
    bool lastAnnounceTimedOut = false;

    tr_announce_event* announce_events = nullptr;
    int announce_event_priority = 0;
    int announce_event_count = 0;
    int announce_event_alloc = 0;

    /* unique lookup key */
    int key = 0;

    int scrapeIntervalSec = 0;
    int announceIntervalSec = 0;
    int announceMinIntervalSec = 0;

    size_t lastAnnouncePeerCount = 0;

    bool isRunning = false;
    bool isAnnouncing = false;
    bool isScraping = false;
    bool wasCopied = false;

    char lastAnnounceStr[128];
    char lastScrapeStr[128];

    static void tierConstruct(tr_tier* tier, tr_torrent* tor);
    static void tierDestruct(tr_tier* tier);

    [[nodiscard]] bool needsToAnnounce(time_t now) const;
    time_t get_next_scrape_time(tr_session const* session, int interval) const;
    void build_log_name(char* buf, size_t buflen) const;
    void incrementTracker();
    void publishMessage(char const* msg, TrackerEventType type);
    void publishErrorClear();
    void publishWarning(const char* msg);
    void publishError(const char* msg);
} tr_tier;

time_t tr_tier::get_next_scrape_time(tr_session const* session, int interval) const
{
    time_t ret;
    time_t const now = tr_time();

    /* Maybe don't scrape paused torrents */
    if (!this->isRunning && !session->scrapePausedTorrents)
    {
        ret = 0;
    }
    /* Add the interval, and then increment to the nearest 10th second.
     * The latter step is to increase the odds of several torrents coming
     * due at the same time to improve multiscrape. */
    else
    {
        ret = now + interval;

        while (ret % 10 != 0)
        {
            ++ret;
        }
    }

    return ret;
}

void tr_tier::tierConstruct(tr_tier* tier, tr_torrent* tor)
{
    static int nextKey = 1;

    *tier = {}; // TODO: [C++] remove when this function is promoted to constructor

    tier->key = nextKey++;
    tier->currentTrackerIndex = -1;
    tier->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    tier->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    tier->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    tier->scrapeAt = tier->get_next_scrape_time(tor->session, 0);
    tier->tor = tor;
}

void tr_tier::tierDestruct(tr_tier* tier)
{
    tr_free(tier->announce_events);
}

void tr_tier::build_log_name(char* buf, size_t buflen) const
{
    TR_ASSERT(this != nullptr);
    tr_snprintf(
        buf,
        buflen,
        "[%s---%s]",
        this->tor != nullptr ? tr_torrentName(this->tor) : "?",
        this->currentTracker != nullptr ? this->currentTracker->key : "?");
}

void tr_tier::incrementTracker()
{
    /* move our index to the next tracker in the tier */
    size_t const i = this->currentTracker == nullptr ? 0 : (this->currentTrackerIndex + 1) % this->tracker_count;
    this->currentTrackerIndex = i;
    this->currentTracker = &this->trackers[i];

    /* reset some of the tier's fields */
    this->scrapeIntervalSec = DEFAULT_SCRAPE_INTERVAL_SEC;
    this->announceIntervalSec = DEFAULT_ANNOUNCE_INTERVAL_SEC;
    this->announceMinIntervalSec = DEFAULT_ANNOUNCE_MIN_INTERVAL_SEC;
    this->isAnnouncing = false;
    this->isScraping = false;
    this->lastAnnounceStartTime = 0;
    this->lastScrapeStartTime = 0;
}

/***
****
***/

/**
 * @brief Opaque, per-torrent data structure for tracker announce information
 *
 * this opaque data structure can be found in tr_torrent.tiers
 */
struct tr_torrent_tiers
{
    tr_tier* tiers = nullptr;
    size_t tier_count = 0;

    tr_tracker* trackers = nullptr;
    size_t tracker_count = 0;

    tr_tracker_callback callback = nullptr;
    void* callbackData = nullptr;

    static tr_torrent_tiers *tiersNew(); // TODO: [C++] upgrade to operator new
    static void tiersDestruct(tr_torrent_tiers* tt);
    static void tiersFree(tr_torrent_tiers* tt);
};

tr_torrent_tiers* tr_torrent_tiers::tiersNew()
{
    return tr_new0(tr_torrent_tiers, 1);
}

void tr_torrent_tiers::tiersDestruct(tr_torrent_tiers* tt)
{
    for (size_t i = 0; i < tt->tracker_count; ++i)
    {
        tr_tracker::trackerDestruct(&tt->trackers[i]);
    }

    tr_free(tt->trackers);

    for (size_t i = 0; i < tt->tier_count; ++i)
    {
        tr_tier::tierDestruct(&tt->tiers[i]);
    }

    tr_free(tt->tiers);
}

void tr_torrent_tiers::tiersFree(tr_torrent_tiers* tt)
{
    tiersDestruct(tt);
    tr_free(tt);
}

tr_tier* tr_announcer::getTier(uint8_t const* info_hash, int tierId)
{
    tr_tier* tier = nullptr;

    tr_torrent* tor = tr_torrentFindFromHash(this->session, info_hash);

    if (tor != nullptr && tor->tiers != nullptr)
    {
        tr_torrent_tiers* tt = tor->tiers;

        for (size_t i = 0; tier == nullptr && i < tt->tier_count; ++i)
        {
            if (tt->tiers[i].key == tierId)
            {
                tier = &tt->tiers[i];
            }
        }
    }

    return tier;
}

/***
****  PUBLISH
***/

void tr_tier::publishMessage(char const* msg, TrackerEventType type)
{
    if (this != nullptr && this->tor != nullptr && this->tor->tiers != nullptr && this->tor->tiers->callback != nullptr)
    {
        tr_torrent_tiers* tiers = this->tor->tiers;
        auto event = tr_tracker_event{};
        event.messageType = type;
        event.text = msg;

        if (this->currentTracker != nullptr)
        {
            event.tracker = this->currentTracker->announce;
        }

        (*tiers->callback)(this->tor, &event, tiers->callbackData);
    }
}

void tr_tier::publishErrorClear()
{
    this->publishMessage(nullptr, TR_TRACKER_ERROR_CLEAR);
}

void tr_tier::publishWarning(char const* msg)
{
    this->publishMessage(msg, TR_TRACKER_WARNING);
}

void tr_tier::publishError(char const* msg)
{
    this->publishMessage(msg, TR_TRACKER_ERROR);
}

static void publishPeerCounts(tr_tier* tier, int seeders, int leechers)
{
    if (tier->tor->tiers->callback != nullptr)
    {
        auto e = tr_tracker_event{};
        e.messageType = TR_TRACKER_COUNTS;
        e.seeders = seeders;
        e.leechers = leechers;
        dbgmsg(tier, "peer counts: %d seeders, %d leechers.", seeders, leechers);

        (*tier->tor->tiers->callback)(tier->tor, &e, nullptr);
    }
}

static void publishPeersPex(tr_tier* tier, int seeders, int leechers, tr_pex const* pex, size_t n)
{
    if (tier->tor->tiers->callback != nullptr)
    {
        auto e = tr_tracker_event{};
        e.messageType = TR_TRACKER_PEERS;
        e.seeders = seeders;
        e.leechers = leechers;
        e.pex = pex;
        e.pexCount = n;
        dbgmsg(tier, "tracker knows of %d seeders and %d leechers and gave a list of %zu peers.", seeders, leechers, n);

        (*tier->tor->tiers->callback)(tier->tor, &e, nullptr);
    }
}

/***
****
***/

struct ann_tracker_info
{
    tr_tracker_info info;

    char* scheme;
    char* host;
    char* path;
    int port;
};

/* primary key: tier
 * secondary key: udp comes before http */
static int filter_trackers_compare_func(void const* va, void const* vb)
{
    auto* a = static_cast<struct ann_tracker_info const*>(va);
    auto* b = static_cast<struct ann_tracker_info const*>(vb);

    if (a->info.tier != b->info.tier)
    {
        return a->info.tier - b->info.tier;
    }

    return -std::strcmp(a->scheme, b->scheme);
}

/**
 * Massages the incoming list of trackers into something we can use.
 */
static tr_tracker_info* filter_trackers(tr_tracker_info const* input, size_t input_count, size_t* setme_count)
{
    auto n = 0;
    struct tr_tracker_info* ret;
    struct ann_tracker_info* tmp = tr_new0(struct ann_tracker_info, input_count);

    /* build a list of valid trackers */
    for (size_t i = 0; i < input_count; ++i)
    {
        if (tr_urlIsValidTracker(input[i].announce))
        {
            int port;
            char* scheme;
            char* host;
            char* path;
            bool is_duplicate = false;
            tr_urlParse(input[i].announce, TR_BAD_SIZE, &scheme, &host, &port, &path);

            /* weed out one common source of duplicates:
             * "http://tracker/announce" +
             * "http://tracker:80/announce"
             */
            for (int j = 0; !is_duplicate && j < n; ++j)
            {
                is_duplicate = tmp[j].port == port && std::strcmp(tmp[j].scheme, scheme) == 0 &&
                    std::strcmp(tmp[j].host, host) == 0 && std::strcmp(tmp[j].path, path) == 0;
            }

            if (is_duplicate)
            {
                tr_free(path);
                tr_free(host);
                tr_free(scheme);
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
    for (auto i = 0; i < n; ++i)
    {
        for (auto j = i + 1; j < n; ++j)
        {
            if (tmp[i].info.tier != tmp[j].info.tier && tmp[i].port == tmp[j].port &&
                tr_strcmp0(tmp[i].host, tmp[j].host) == 0 && tr_strcmp0(tmp[i].path, tmp[j].path) == 0)
            {
                tmp[j].info.tier = tmp[i].info.tier;
            }
        }
    }

    /* sort them, for two reasons:
     * (1) unjumble the tiers from the previous step
     * (2) move the UDP trackers to the front of each tier */
    std::qsort(tmp, n, sizeof(struct ann_tracker_info), filter_trackers_compare_func);

    /* build the output */
    *setme_count = n;
    ret = tr_new0(tr_tracker_info, n);

    for (auto i = 0; i < n; ++i)
    {
        ret[i] = tmp[i].info;
    }

    /* cleanup */
    for (auto i = 0; i < n; ++i)
    {
        tr_free(tmp[i].path);
        tr_free(tmp[i].host);
        tr_free(tmp[i].scheme);
    }

    tr_free(tmp);

    return ret;
}

static void addTorrentToTier(tr_torrent_tiers* tt, tr_torrent* tor)
{
    size_t n;
    int tier_count;
    tr_tier* tier;
    tr_tracker_info* infos = filter_trackers(tor->info.trackers, tor->info.trackerCount, &n);

    /* build the array of trackers */
    tt->trackers = tr_new0(tr_tracker, n);
    tt->tracker_count = n;

    for (size_t i = 0; i < n; ++i)
    {
        tr_tracker::trackerConstruct(tor->session->announcer, &tt->trackers[i], &infos[i]);
    }

    /* count how many tiers there are */
    tier_count = 0;

    for (size_t i = 0; i < n; ++i)
    {
        if (i == 0 || infos[i].tier != infos[i - 1].tier)
        {
            ++tier_count;
        }
    }

    /* build the array of tiers */
    tier = nullptr;
    tt->tiers = tr_new0(tr_tier, tier_count);
    tt->tier_count = 0;

    for (size_t i = 0; i < n; ++i)
    {
        if (i != 0 && infos[i].tier == infos[i - 1].tier)
        {
            ++tier->tracker_count;
        }
        else
        {
            tier = &tt->tiers[tt->tier_count++];
            tr_tier::tierConstruct(tier, tor);
            tier->trackers = &tt->trackers[i];
            tier->tracker_count = 1;
            tier->incrementTracker();
        }
    }

    /* cleanup */
    tr_free(infos);
}

tr_torrent_tiers* tr_announcerAddTorrent(tr_torrent* tor, tr_tracker_callback callback, void* callbackData)
{
    TR_ASSERT(tr_isTorrent(tor));

    auto tiers = tr_torrent_tiers::tiersNew();
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
    TR_ASSERT(tor->tiers != nullptr);

    struct tr_torrent_tiers const* tt = nullptr;

    if (tor->isRunning)
    {
        tt = tor->tiers;
    }

    /* return true if any tier can manual announce */
    for (size_t i = 0; tt != nullptr && i < tt->tier_count; ++i)
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
    struct tr_torrent_tiers const* tt = tor->tiers;

    /* find the earliest manual announce time from all peers */
    for (size_t i = 0; tt != nullptr && i < tt->tier_count; ++i)
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
        char* message;
        struct evbuffer* buf = evbuffer_new();

        tier->build_log_name(name, sizeof(name));

        for (int i = 0; i < tier->announce_event_count; ++i)
        {
            tr_announce_event const e = tier->announce_events[i];
            char const* str = tr_announce_event_get_string(e);
            evbuffer_add_printf(buf, "[%d:%s]", i, str);
        }

        message = evbuffer_free_to_str(buf, nullptr);
        tr_logAddDeep(__FILE__, __LINE__, name, "announce queue is %s", message);
        tr_free(message);
    }
}

// higher priorities go to the front of the announce queue
static void tier_update_announce_priority(tr_tier* tier)
{
    int priority = -1;

    for (int i = 0; i < tier->announce_event_count; ++i)
    {
        priority = std::max(priority, (int)tier->announce_events[i]);
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
    struct tr_torrent_tiers* tt = tor->tiers;

    /* walk through each tier and tell them to announce */
    for (size_t i = 0; i < tt->tier_count; ++i)
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

    struct tr_torrent_tiers* tt = tor->tiers;

    for (size_t i = 0; i < tt->tier_count; ++i)
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
    req->url = tr_strdup(tier->currentTracker->announce);
    req->tracker_id_str = tr_strdup(tier->currentTracker->tracker_id_str);
    std::memcpy(req->info_hash, tor->info.hash, SHA_DIGEST_LENGTH);
    std::memcpy(req->peer_id, tr_torrentGetPeerId(tor), PEER_ID_LEN);
    req->up = tier->byteCounts[TR_ANN_UP];
    req->down = tier->byteCounts[TR_ANN_DOWN];
    req->corrupt = tier->byteCounts[TR_ANN_CORRUPT];
    req->leftUntilComplete = tr_torrentHasMetadata(tor) ? tor->info.totalSize - tr_torrentHaveTotal(tor) : INT64_MAX;
    req->event = event;
    req->numwant = event == TR_ANNOUNCE_EVENT_STOPPED ? 0 : NUMWANT;
    req->key = announcer->key;
    req->partial_seed = tr_torrentGetCompleteness(tor) == TR_PARTIAL_SEED;
    tier->build_log_name(req->log_name, sizeof(req->log_name));
    return req;
}

static void announce_request_free(tr_announce_request* req);

void tr_announcerRemoveTorrent(tr_announcer* announcer, tr_torrent* tor)
{
    struct tr_torrent_tiers const* tt = tor->tiers;

    if (tt != nullptr)
    {
        for (size_t i = 0; i < tt->tier_count; ++i)
        {
            tr_tier const* tier = &tt->tiers[i];

            if (tier->isRunning)
            {
                tr_announce_event const e = TR_ANNOUNCE_EVENT_STOPPED;
                tr_announce_request* req = announce_request_new(announcer, tor, tier, e);

                if (tr_ptrArrayFindSorted(&announcer->stops, req, tr_announce_request::compareStops) != nullptr)
                {
                    announce_request_free(req);
                }
                else
                {
                    tr_ptrArrayInsertSorted(&announcer->stops, req, tr_announce_request::compareStops);
                }
            }
        }

        tr_torrent_tiers::tiersFree(tor->tiers);
        tor->tiers = nullptr;
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
    int interval;

    /* increment the error count */
    if (tier->currentTracker != nullptr)
    {
        ++tier->currentTracker->consecutiveFailures;
    }

    /* set the error message */
    dbgmsg(tier, "%s", err);
    tr_logAddTorInfo(tier->tor, "%s", err);
    tr_strlcpy(tier->lastAnnounceStr, err, sizeof(tier->lastAnnounceStr));

    /* switch to the next tracker */
    tier->incrementTracker();

    /* schedule a reannounce */
    interval = getRetryInterval(tier->currentTracker);
    dbgmsg(tier, "Retrying announce in %d seconds.", interval);
    tr_logAddTorInfo(tier->tor, "Retrying announce in %d seconds.", interval);
    tier_announce_event_push(tier, e, tr_time() + interval);
}

static void on_announce_done(tr_announce_response const* response, void* vdata)
{
    auto* data = static_cast<struct announce_data*>(vdata);
    tr_announcer* announcer = data->session->announcer;
    tr_tier* tier = announcer->getTier(response->info_hash, data->tierId);
    time_t const now = tr_time();
    tr_announce_event const event = data->event;

    if (tier != nullptr)
    {
        tr_tracker* tracker;

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
            response->errmsg != nullptr ? response->errmsg : "none",
            response->warning != nullptr ? response->warning : "none");

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
        else if (response->errmsg != nullptr)
        {
            /* If the torrent's only tracker returned an error, publish it.
               Don't bother publishing if there are other trackers -- it's
               all too common for people to load up dozens of dead trackers
               in a torrent's metainfo... */
            if (tier->tor->info.trackerCount < 2)
            {
                tier->publishError(response->errmsg);
            }

            on_announce_error(tier, response->errmsg, event);
        }
        else
        {
            int i;
            char const* str;
            int scrape_fields = 0;
            int seeders = 0;
            int leechers = 0;
            bool const isStopped = event == TR_ANNOUNCE_EVENT_STOPPED;

            tier->publishErrorClear();

            if ((tracker = tier->currentTracker) != nullptr)
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

                if ((str = response->tracker_id_str) != nullptr)
                {
                    tr_free(tracker->tracker_id_str);
                    tracker->tracker_id_str = tr_strdup(str);
                }
            }

            if ((str = response->warning) != nullptr)
            {
                tr_strlcpy(tier->lastAnnounceStr, str, sizeof(tier->lastAnnounceStr));
                dbgmsg(tier, "tracker gave \"%s\"", str);
                tier->publishWarning(str);
            }
            else
            {
                tr_strlcpy(tier->lastAnnounceStr, _("Success"), sizeof(tier->lastAnnounceStr));
            }

            if ((i = response->min_interval) != 0)
            {
                tier->announceMinIntervalSec = i;
            }

            if ((i = response->interval) != 0)
            {
                tier->announceIntervalSec = i;
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
                tier->scrapeAt = tier->get_next_scrape_time(announcer->session, tier->scrapeIntervalSec);
                tier->lastScrapeTime = now;
                tier->lastScrapeSucceeded = true;
            }
            else if (tier->lastScrapeTime + tier->scrapeIntervalSec <= now)
            {
                tier->scrapeAt = tier->get_next_scrape_time(announcer->session, 0);
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
                i = tier->announceIntervalSec;
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
    tr_free(req->url);
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

    if (std::strncmp(request->url, "http", 4) == 0)
    {
        tr_tracker_http_announce(session, request, callback, callback_data);
    }
    else if (std::strncmp(request->url, "udp://", 6) == 0)
    {
        tr_tracker_udp_announce(session, request, callback, callback_data);
    }
    else
    {
        tr_logAddError("Unsupported url: %s", request->url);
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

static bool multiscrape_too_big(char const* errmsg)
{
    /* Found a tracker that returns some bespoke string for this case?
       Add your patch here and open a PR */
    static char const* const too_long_errors[] = {
        "Bad Request",
        "GET string too long",
        "Request-URI Too Long",
    };

    if (errmsg == nullptr)
    {
        return false;
    }

    return std::any_of(
        std::begin(too_long_errors),
        std::end(too_long_errors),
        [errmsg](const char* too_long_error) { return std::strstr(errmsg, too_long_error) != nullptr; });
}

static void on_scrape_error(tr_session const* session, tr_tier* tier, char const* errmsg)
{
    int interval;

    /* increment the error count */
    if (tier->currentTracker != nullptr)
    {
        ++tier->currentTracker->consecutiveFailures;
    }

    /* set the error message */
    dbgmsg(tier, "Scrape error: %s", errmsg);
    tr_logAddTorInfo(tier->tor, "Scrape error: %s", errmsg);
    tr_strlcpy(tier->lastScrapeStr, errmsg, sizeof(tier->lastScrapeStr));

    /* switch to the next tracker */
    tier->incrementTracker();

    /* schedule a rescrape */
    interval = getRetryInterval(tier->currentTracker);
    dbgmsg(tier, "Retrying scrape in %zu seconds.", (size_t)interval);
    tr_logAddTorInfo(tier->tor, "Retrying scrape in %zu seconds.", (size_t)interval);
    tier->lastScrapeSucceeded = false;
    tier->scrapeAt = tier->get_next_scrape_time(session, interval);
}

static tr_tier* find_tier(tr_torrent* tor, char const* scrape)
{
    struct tr_torrent_tiers* tt = tor->tiers;

    for (size_t i = 0; tt != nullptr && i < tt->tier_count; ++i)
    {
        tr_tracker const* const tracker = tt->tiers[i].currentTracker;

        if (tracker != nullptr && tracker->scrape_info != nullptr && tr_strcmp0(scrape, tracker->scrape_info->url) == 0)
        {
            return &tt->tiers[i];
        }
    }

    return nullptr;
}

static void on_scrape_done(tr_scrape_response const* response, void* vsession)
{
    time_t const now = tr_time();
    auto* session = static_cast<tr_session*>(vsession);
    tr_announcer* announcer = session->announcer;

    for (int i = 0; i < response->row_count; ++i)
    {
        struct tr_scrape_response_row const* row = &response->rows[i];
        tr_torrent* tor = tr_torrentFindFromHash(session, row->info_hash);

        if (tor != nullptr)
        {
            tr_tier* tier = find_tier(tor, response->url);

            if (tier != nullptr)
            {
                dbgmsg(
                    tier,
                    "scraped url:%s -- "
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
                    response->errmsg != nullptr ? response->errmsg : "none");

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
                else if (response->errmsg != nullptr)
                {
                    on_scrape_error(session, tier, response->errmsg);
                }
                else
                {
                    tier->lastScrapeSucceeded = true;
                    tier->scrapeIntervalSec = std::max((int)DEFAULT_SCRAPE_INTERVAL_SEC, response->min_request_interval);
                    tier->scrapeAt = tier->get_next_scrape_time(session, tier->scrapeIntervalSec);
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

    /* Maybe reduce the number of torrents in a multiscrape req */
    if (multiscrape_too_big(response->errmsg))
    {
        char const* url = response->url;
        struct tr_scrape_info* const scrape_info = announcer->getScrapeInfo(url);
        if (scrape_info != nullptr)
        {
            int* multiscrape_max = &scrape_info->multiscrape_max;

            /* Lower the max only if it hasn't already lowered for a similar error.
               For example if N parallel multiscrapes all have the same `max` and
               error out, lower the value once for that batch, not N times. */
            if (*multiscrape_max >= response->row_count)
            {
                int const n = std::max(1, *multiscrape_max - TR_MULTISCRAPE_STEP);
                if (*multiscrape_max != n)
                {
                    char* scheme = nullptr;
                    char* host = nullptr;
                    int port;
                    if (tr_urlParse(url, std::strlen(url), &scheme, &host, &port, nullptr))
                    {
                        /* don't log the full URL, since that might have a personal announce id */
                        char* sanitized_url = tr_strdup_printf("%s://%s:%d", scheme, host, port);
                        tr_logAddNamedInfo(sanitized_url, "Reducing multiscrape max to %d", n);
                        tr_free(sanitized_url);
                        tr_free(host);
                        tr_free(scheme);
                    }

                    *multiscrape_max = n;
                }
            }
        }
    }
}

static void scrape_request_delegate(
    tr_announcer* announcer,
    tr_scrape_request const* request,
    tr_scrape_response_func callback,
    void* callback_data)
{
    tr_session* session = announcer->session;

    if (strncmp(request->url, "http", 4) == 0)
    {
        tr_tracker_http_scrape(session, request, callback, callback_data);
    }
    else if (strncmp(request->url, "udp://", 6) == 0)
    {
        tr_tracker_udp_scrape(session, request, callback, callback_data);
    }
    else
    {
        tr_logAddError("Unsupported url: %s", request->url);
    }
}

static void multiscrape(tr_announcer* announcer, tr_ptrArray* tiers)
{
    size_t request_count = 0;
    time_t const now = tr_time();
    size_t const tier_count = tr_ptrArraySize(tiers);
    tr_scrape_request requests[MAX_SCRAPES_PER_UPKEEP] = {};

    /* batch as many info_hashes into a request as we can */
    for (size_t i = 0; i < tier_count; ++i)
    {
        auto* tier = static_cast<tr_tier*>(tr_ptrArrayNth(tiers, i));
        struct tr_scrape_info* const scrape_info = tier->currentTracker->scrape_info;
        uint8_t const* hash = tier->tor->info.hash;
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

            if (tr_strcmp0(req->url, scrape_info->url) != 0)
            {
                continue;
            }

            std::memcpy(req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH);
            tier->isScraping = true;
            tier->lastScrapeStartTime = now;
            found = true;
        }

        /* otherwise, if there's room for another request, build a new one */
        if (!found && request_count < MAX_SCRAPES_PER_UPKEEP)
        {
            tr_scrape_request* req = &requests[request_count++];
            req->url = scrape_info->url;
            tier->build_log_name(req->log_name, sizeof(req->log_name));

            std::memcpy(req->info_hash[req->info_hash_count++], hash, SHA_DIGEST_LENGTH);
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

void tr_announcer::flushCloseMessages()
{
    for (size_t i = 0, n = tr_ptrArraySize(&this->stops); i < n; ++i)
    {
        announce_request_delegate(
            this,
            static_cast<tr_announce_request*>(tr_ptrArrayNth(&this->stops, i)),
            nullptr,
            nullptr);
    }

    tr_ptrArrayClear(&this->stops);
}

inline bool tr_tier::needsToAnnounce(time_t now) const
{
    return !this->isAnnouncing && !this->isScraping && this->announceAt != 0 && this->announceAt <= now &&
        this->announce_event_count > 0;
}

static inline bool tierNeedsToScrape(tr_tier const* tier, time_t const now)
{
    return !tier->isScraping && tier->scrapeAt != 0 && tier->scrapeAt <= now && tier->currentTracker != nullptr &&
        tier->currentTracker->scrape_info != nullptr;
}

static inline int countDownloaders(tr_tier const* tier)
{
    tr_tracker const* const tracker = tier->currentTracker;

    return tracker == nullptr ? 0 : tracker->downloaderCount + tracker->leecherCount;
}

static int compareAnnounceTiers(void const* va, void const* vb)
{
    auto const* a = reinterpret_cast<tr_tier const*>(va);
    auto const* b = reinterpret_cast<tr_tier const*>(vb);

    /* prefer higher-priority events */
    int const priority_a = a->announce_event_priority;
    int const priority_b = b->announce_event_priority;
    if (priority_a != priority_b)
    {
        return priority_a > priority_b ? -1 : 1;
    }

    /* prefer swarms where we might upload */
    int const downloader_count_a = countDownloaders(a);
    int const downloader_count_b = countDownloaders(b);
    if (downloader_count_a != downloader_count_b)
    {
        return downloader_count_a > downloader_count_b ? -1 : 1;
    }

    /* prefer swarms where we might download */
    bool const is_seed_a = tr_torrentIsSeed(a->tor);
    bool const is_seed_b = tr_torrentIsSeed(b->tor);
    if (is_seed_a != is_seed_b)
    {
        return is_seed_a ? 1 : -1;
    }

    /* prefer larger stats, to help ensure stats get recorded when stopping on shutdown */
    int const xfer = compareTransfer(
        a->byteCounts[TR_ANN_UP],
        a->byteCounts[TR_ANN_DOWN],
        b->byteCounts[TR_ANN_UP],
        b->byteCounts[TR_ANN_DOWN]);
    if (xfer)
    {
        return xfer;
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
    auto announceMe = tr_ptrArray{};
    auto scrapeMe = tr_ptrArray{};
    tr_torrent* tor = nullptr;
    while ((tor = tr_torrentNext(announcer->session, tor)) != nullptr)
    {
        struct tr_torrent_tiers* tt = tor->tiers;

        for (size_t i = 0; tt != nullptr && i < tt->tier_count; ++i)
        {
            tr_tier* tier = &tt->tiers[i];

            if (tier->needsToAnnounce(now))
            {
                tr_ptrArrayInsertSorted(&announceMe, tier, compareAnnounceTiers);
            }

            if (tierNeedsToScrape(tier, now))
            {
                tr_ptrArrayAppend(&scrapeMe, tier);
            }
        }
    }

    /* First, scrape what we can. We handle scrapes first because
     * we can work through that queue much faster than announces
     * (thanks to multiscrape) _and_ the scrape responses will tell
     * us which swarms are interesting and should be announced next. */
    multiscrape(announcer, &scrapeMe);

    /* Second, announce what we can. If there aren't enough slots
     * available, use compareAnnounceTiers to prioritize. */
    size_t n = std::min(tr_ptrArraySize(&announceMe), (int)MAX_ANNOUNCES_PER_UPKEEP);
    for (size_t i = 0; i < n; ++i)
    {
        auto* tier = static_cast<tr_tier*>(tr_ptrArrayNth(&announceMe, i));
        tr_logAddTorDbg(tier->tor, "%s", "Announcing to tracker");
        dbgmsg(tier, "announcing tier %zu of %zu", i, n);
        tierAnnounce(announcer, tier);
    }

    /* cleanup */
    tr_ptrArrayDestruct(&scrapeMe, nullptr);
    tr_ptrArrayDestruct(&announceMe, nullptr);
}

static void onUpkeepTimer(evutil_socket_t fd, short what, void* vannouncer)
{
    TR_UNUSED(fd);
    TR_UNUSED(what);

    auto* announcer = static_cast<tr_announcer*>(vannouncer);
    tr_session* session = announcer->session;
    bool const is_closing = session->isClosed;
    time_t const now = tr_time();

    tr_sessionLock(session);

    /* maybe send out some "stopped" messages for closed torrents */
    announcer->flushCloseMessages();

    /* maybe kick off some scrapes / announces whose time has come */
    if (!is_closing)
    {
        scrapeAndAnnounceMore(announcer);
    }

    /* TAU upkeep */
    if (announcer->tauUpkeepAt <= now)
    {
        announcer->tauUpkeepAt = now + TAU_UPKEEP_INTERVAL_SECS;
        tr_tracker_udp_upkeep(session);
    }

    /* set up the next timer */
    tr_timerAddMsec(announcer->upkeepTimer, UPKEEP_INTERVAL_MSEC);

    tr_sessionUnlock(session);
}

/***
****
***/

tr_tracker_stat* tr_announcerStats(tr_torrent const* torrent, int* setmeTrackerCount)
{
    TR_ASSERT(tr_isTorrent(torrent));

    time_t const now = tr_time();

    int out = 0;
    struct tr_torrent_tiers const* const tt = torrent->tiers;

    /* alloc the stats */
    *setmeTrackerCount = tt->tracker_count;
    tr_tracker_stat* const ret = tr_new0(tr_tracker_stat, tt->tracker_count);

    /* populate the stats */
    for (size_t i = 0; i < tt->tier_count; ++i)
    {
        tr_tier const* const tier = &tt->tiers[i];

        for (size_t j = 0; j < tier->tracker_count; ++j)
        {
            tr_tracker const* const tracker = &tier->trackers[j];
            tr_tracker_stat* st = &ret[out++];

            st->id = tracker->id;
            tr_strlcpy(st->host, tracker->key, sizeof(st->host));
            tr_strlcpy(st->announce, tracker->announce, sizeof(st->announce));
            st->tier = static_cast<int>(i);
            st->isBackup = tracker != tier->currentTracker;
            st->lastScrapeStartTime = tier->lastScrapeStartTime;

            if (tracker->scrape_info != nullptr)
            {
                tr_strlcpy(st->scrape, tracker->scrape_info->url, sizeof(st->scrape));
            }
            else
            {
                st->scrape[0] = '\0';
            }

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
                if ((st->hasScraped = tier->lastScrapeTime != 0))
                {
                    st->lastScrapeTime = tier->lastScrapeTime;
                    st->lastScrapeSucceeded = tier->lastScrapeSucceeded;
                    st->lastScrapeTimedOut = tier->lastScrapeTimedOut;
                    tr_strlcpy(st->lastScrapeResult, tier->lastScrapeStr, sizeof(st->lastScrapeResult));
                }

                if (tier->isScraping)
                {
                    st->scrapeState = TR_TRACKER_ACTIVE;
                }
                else if (tier->scrapeAt == 0)
                {
                    st->scrapeState = TR_TRACKER_INACTIVE;
                }
                else if (tier->scrapeAt > now)
                {
                    st->scrapeState = TR_TRACKER_WAITING;
                    st->nextScrapeTime = tier->scrapeAt;
                }
                else
                {
                    st->scrapeState = TR_TRACKER_QUEUED;
                }

                st->lastAnnounceStartTime = tier->lastAnnounceStartTime;

                if ((st->hasAnnounced = tier->lastAnnounceTime != 0))
                {
                    st->lastAnnounceTime = tier->lastAnnounceTime;
                    tr_strlcpy(st->lastAnnounceResult, tier->lastAnnounceStr, sizeof(st->lastAnnounceResult));
                    st->lastAnnounceSucceeded = tier->lastAnnounceSucceeded;
                    st->lastAnnounceTimedOut = tier->lastAnnounceTimedOut;
                    st->lastAnnouncePeerCount = static_cast<int>(tier->lastAnnouncePeerCount);
                }

                if (tier->isAnnouncing)
                {
                    st->announceState = TR_TRACKER_ACTIVE;
                }
                else if (!torrent->isRunning || tier->announceAt == 0)
                {
                    st->announceState = TR_TRACKER_INACTIVE;
                }
                else if (tier->announceAt > now)
                {
                    st->announceState = TR_TRACKER_WAITING;
                    st->nextAnnounceTime = tier->announceAt;
                }
                else
                {
                    st->announceState = TR_TRACKER_QUEUED;
                }
            }
        }
    }

    return ret;
}

void tr_announcerStatsFree(tr_tracker_stat* trackers, int trackerCount)
{
    TR_UNUSED(trackerCount);

    tr_free(trackers);
}

/***
****
***/

static void copy_tier_attributes_impl(struct tr_tier* tgt, size_t trackerIndex, tr_tier const* src)
{
    /* sanity clause */
    TR_ASSERT(trackerIndex < tgt->tracker_count);
    TR_ASSERT(tr_strcmp0(tgt->trackers[trackerIndex].announce, src->currentTracker->announce) == 0);

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

static void copy_tier_attributes(struct tr_torrent_tiers* tt, tr_tier const* src)
{
    bool found = false;

    /* find a tier (if any) which has a match for src->currentTracker */
    for (size_t i = 0; !found && i < tt->tier_count; ++i)
    {
        for (size_t j = 0; !found && j < tt->tiers[i].tracker_count; ++j)
        {
            if (tr_strcmp0(src->currentTracker->announce, tt->tiers[i].trackers[j].announce) == 0)
            {
                found = true;
                copy_tier_attributes_impl(&tt->tiers[i], j, src);
            }
        }
    }
}

void tr_announcerResetTorrent(tr_announcer* announcer, tr_torrent* tor)
{
    TR_UNUSED(announcer);

    TR_ASSERT(tor->tiers != nullptr);

    time_t const now = tr_time();

    struct tr_torrent_tiers* tt = tor->tiers;
    tr_torrent_tiers old = *tt;

    /* remove the old tiers / trackers */
    tt->tiers = nullptr;
    tt->trackers = nullptr;
    tt->tier_count = 0;
    tt->tracker_count = 0;

    /* create the new tiers / trackers */
    addTorrentToTier(tt, tor);

    /* copy the old tiers' states into their replacements */
    for (size_t i = 0; i < old.tier_count; ++i)
    {
        if (old.tiers[i].currentTracker != nullptr)
        {
            copy_tier_attributes(tt, &old.tiers[i]);
        }
    }

    /* kickstart any tiers that didn't get started */
    if (tor->isRunning)
    {
        for (size_t i = 0; i < tt->tier_count; ++i)
        {
            if (!tt->tiers[i].wasCopied)
            {
                tier_announce_event_push(&tt->tiers[i], TR_ANNOUNCE_EVENT_STARTED, now);
            }
        }
    }

    /* cleanup */
    tr_torrent_tiers::tiersDestruct(&old);
}
