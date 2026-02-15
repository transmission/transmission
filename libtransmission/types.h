// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t, std::byte
#include <cstdint> // uint16_t, uint32_t, uint64_t
#include <ctime> // time_t
#include <functional>
#include <string_view>
#include <string>

#include "libtransmission/values.h"

struct tr_ctor;
struct tr_error;
struct tr_session;
struct tr_torrent;
struct tr_torrent_metainfo;
struct tr_variant;

// https://www.bittorrent.org/beps/bep_0007.html
// "The client SHOULD include a key parameter in its announces. The key
// should remain the same for a particular infohash during a torrent
// session. Together with the peer_id this allows trackers to uniquely
// identify clients for the purpose of statistics-keeping when they
// announce from multiple IP.
// The key should be generated so it has at least 32bits worth of entropy."
//
// https://www.bittorrent.org/beps/bep_0015.html
// "Clients that resolve hostnames to v4 and v6 and then announce to both
// should use the same [32-bit integer] key for both so that trackers that
// care about accurate statistics-keeping can match the two announces."
using tr_announce_key_t = uint32_t;

// Assuming a 16 KiB block (tr_block_info::BlockSize), a 32-bit block
// index gives us a maximum torrent size of 64 TiB. When we ever need to
// grow past that, change tr_block_index_t and  tr_piece_index_t to uint64_t.
using tr_block_index_t = uint32_t;

using tr_byte_index_t = uint64_t;

using tr_file_index_t = size_t;

using tr_mode_t = uint16_t;

// https://www.bittorrent.org/beps/bep_0003.html
// A string of length 20 which this downloader uses as its id. Each
// downloader generates its own id at random at the start of a new
// download. This value will also almost certainly have to be escaped.
using tr_peer_id_t = std::array<char, 20>;

using tr_piece_index_t = uint32_t;

using tr_sha1_digest_t = std::array<std::byte, 20>;

using tr_sha256_digest_t = std::array<std::byte, 32>;

using tr_torrent_id_t = int;

using tr_tracker_id_t = uint32_t;

using tr_tracker_tier_t = uint32_t;

enum : uint8_t
{
    TR_LOC_MOVING,
    TR_LOC_DONE,
    TR_LOC_ERROR
};

enum : int8_t
{
    TR_RATIO_NA = -1,
    TR_RATIO_INF = -2
};

enum TrScript : uint8_t
{
    TR_SCRIPT_ON_TORRENT_ADDED,
    TR_SCRIPT_ON_TORRENT_DONE,
    TR_SCRIPT_ON_TORRENT_DONE_SEEDING,

    TR_SCRIPT_N_TYPES
};

enum tr_completeness : uint8_t
{
    TR_LEECH, /* doesn't have all the desired pieces */
    TR_SEED, /* has the entire torrent */
    TR_PARTIAL_SEED /* has the desired pieces, but not the entire torrent */
};

enum tr_ctorMode : uint8_t
{
    TR_FALLBACK, /* indicates the ctor value should be used only in case of missing resume settings */
    TR_FORCE /* indicates the ctor value should be used regardless of what's in the resume settings */
};

enum class tr_direction : uint8_t
{
    ClientToPeer = 0,
    Up = 0,
    PeerToClient = 1,
    Down = 1,
};

enum tr_encryption_mode : uint8_t
{
    TR_CLEAR_PREFERRED,
    TR_ENCRYPTION_PREFERRED,
    TR_ENCRYPTION_REQUIRED
};

enum tr_eta : time_t // NOLINT(performance-enum-size)
{
    TR_ETA_NOT_AVAIL = -1,
    TR_ETA_UNKNOWN = -2,
};

enum tr_idlelimit : uint8_t
{
    /* follow the global settings */
    TR_IDLELIMIT_GLOBAL = 0,
    /* override the global settings, seeding until a certain idle time */
    TR_IDLELIMIT_SINGLE = 1,
    /* override the global settings, seeding regardless of activity */
    TR_IDLELIMIT_UNLIMITED = 2
};

enum tr_peer_from : uint8_t
{
    TR_PEER_FROM_INCOMING = 0, /* connections made to the listening port */
    TR_PEER_FROM_LPD, /* peers found by local announcements */
    TR_PEER_FROM_TRACKER, /* peers found from a tracker */
    TR_PEER_FROM_DHT, /* peers found from the DHT */
    TR_PEER_FROM_PEX, /* peers found from PEX */
    TR_PEER_FROM_RESUME, /* peers found in the .resume file */
    TR_PEER_FROM_LTEP, /* peer address provided in an LTEP handshake */
    TR_PEER_FROM_N_TYPES
};

enum tr_priority_t : int8_t
{
    TR_PRI_LOW = -1,
    TR_PRI_NORMAL = 0, /* since Normal is 0, memset initializes nicely */
    TR_PRI_HIGH = 1
};

enum tr_port_forwarding_state : uint8_t
{
    TR_PORT_ERROR,
    TR_PORT_UNMAPPED,
    TR_PORT_UNMAPPING,
    TR_PORT_MAPPING,
    TR_PORT_MAPPED
};

enum tr_ratiolimit : uint8_t
{
    /* follow the global settings */
    TR_RATIOLIMIT_GLOBAL = 0,
    /* override the global settings, seeding until a certain ratio */
    TR_RATIOLIMIT_SINGLE = 1,
    /* override the global settings, seeding regardless of ratio */
    TR_RATIOLIMIT_UNLIMITED = 2
};

enum tr_rpc_callback_type : uint8_t
{
    TR_RPC_TORRENT_ADDED,
    TR_RPC_TORRENT_STARTED,
    TR_RPC_TORRENT_STOPPED,
    TR_RPC_TORRENT_REMOVING,
    TR_RPC_TORRENT_TRASHING, /* _REMOVING + delete local data */
    TR_RPC_TORRENT_CHANGED, /* catch-all for the "torrent_set" rpc method */
    TR_RPC_TORRENT_MOVED,
    TR_RPC_SESSION_CHANGED,
    TR_RPC_SESSION_QUEUE_POSITIONS_CHANGED, /* catch potentially multiple torrents being moved in the queue */
    TR_RPC_SESSION_CLOSE
};

enum tr_rpc_callback_status : uint8_t
{
    /* no special handling is needed by the caller */
    TR_RPC_OK = 0,
    /* indicates to the caller that the client will take care of
     * removing the torrent itself. For example the client may
     * need to keep the torrent alive long enough to cleanly close
     * some resources in another thread. */
    TR_RPC_NOREMOVE = (1 << 1)
};

enum tr_sched_day : uint8_t
{
    TR_SCHED_SUN = (1 << 0),
    TR_SCHED_MON = (1 << 1),
    TR_SCHED_TUES = (1 << 2),
    TR_SCHED_WED = (1 << 3),
    TR_SCHED_THURS = (1 << 4),
    TR_SCHED_FRI = (1 << 5),
    TR_SCHED_SAT = (1 << 6),
    TR_SCHED_WEEKDAY = (TR_SCHED_MON | TR_SCHED_TUES | TR_SCHED_WED | TR_SCHED_THURS | TR_SCHED_FRI),
    TR_SCHED_WEEKEND = (TR_SCHED_SUN | TR_SCHED_SAT),
    TR_SCHED_ALL = (TR_SCHED_WEEKDAY | TR_SCHED_WEEKEND)
};

/**
 * What the torrent is doing right now.
 *
 * Note: these values will become a straight enum at some point in the future.
 * Do not rely on their current `bitfield` implementation
 */
enum tr_torrent_activity : uint8_t
{
    TR_STATUS_STOPPED = 0, /* Torrent is stopped */
    TR_STATUS_CHECK_WAIT = 1, /* Queued to check files */
    TR_STATUS_CHECK = 2, /* Checking files */
    TR_STATUS_DOWNLOAD_WAIT = 3, /* Queued to download */
    TR_STATUS_DOWNLOAD = 4, /* Downloading */
    TR_STATUS_SEED_WAIT = 5, /* Queued to seed */
    TR_STATUS_SEED = 6 /* Seeding */
};

enum tr_tracker_state : uint8_t
{
    /* we won't (announce,scrape) this torrent to this tracker because
     * the torrent is stopped, or because of an error, or whatever */
    TR_TRACKER_INACTIVE = 0,
    /* we will (announce,scrape) this torrent to this tracker, and are
     * waiting for enough time to pass to satisfy the tracker's interval */
    TR_TRACKER_WAITING = 1,
    /* it's time to (announce,scrape) this torrent, and we're waiting on a
     * free slot to open up in the announce manager */
    TR_TRACKER_QUEUED = 2,
    /* we're (announcing,scraping) this torrent right now */
    TR_TRACKER_ACTIVE = 3
};

enum tr_verify_added_mode : uint8_t
{
    // See discussion @ https://github.com/transmission/transmission/pull/2626
    // Let newly-added torrents skip upfront verify do it on-demand later.
    TR_VERIFY_ADDED_FAST = 0,

    // Force torrents to be fully verified as they are added.
    TR_VERIFY_ADDED_FULL = 1
};

struct tr_block_span_t
{
    tr_block_index_t begin;
    tr_block_index_t end;
};

struct tr_byte_span_t
{
    uint64_t begin;
    uint64_t end;
};

/*
 * This view structure is intended for short-term use. Its pointers are owned
 * by the torrent and may be invalidated if the torrent is edited or removed.
 */
struct tr_file_view
{
    char const* name; // This file's name. Includes the full subpath in the torrent.
    uint64_t have; // the current size of the file, i.e. how much we've downloaded
    uint64_t length; // the total size of the file
    double progress; // have / length
    tr_piece_index_t beginPiece; // piece index where this file starts
    tr_piece_index_t endPiece; // piece index where this file ends (exclusive)
    tr_priority_t priority; // the file's priority
    bool wanted; // do we want to download this file?
};

struct tr_peer_stat
{
    std::string addr;
    std::string flag_str;

    // The user agent, e.g. `BitTorrent 7.9.1`.
    // Will be an empty string if the agent cannot be determined.
    std::string user_agent;

    tr::Values::Speed rate_to_peer;
    tr::Values::Speed rate_to_client;

    // how many requests the peer has made that we haven't responded to yet
    size_t active_reqs_to_client = {};

    // how many requests we've made and are currently awaiting a response for
    size_t active_reqs_to_peer = {};

    size_t bytes_to_peer = {};
    size_t bytes_to_client = {};

    tr_peer_id_t peer_id = {};

    float progress = {};

    // THESE NEXT FOUR FIELDS ARE EXPERIMENTAL.
    // Don't rely on them; they'll probably go away
    // how many blocks we've sent to this peer in the last 120 seconds
    uint32_t blocks_to_peer = {};
    // how many blocks this client's sent to us in the last 120 seconds
    uint32_t blocks_to_client = {};
    // how many requests to this peer that we've cancelled in the last 120 seconds
    uint32_t cancels_to_peer = {};
    // how many requests this peer made of us, then cancelled, in the last 120 seconds
    uint32_t cancels_to_client = {};

    uint16_t port = {};
    uint8_t from = {};

    bool client_is_choked = {};
    bool client_is_interested = {};
    bool is_downloading_from = {};
    bool is_encrypted = {};
    bool is_incoming = {};
    bool is_seed = {};
    bool is_uploading_to = {};
    bool is_utp = {};
    bool peer_is_choked = {};
    bool peer_is_interested = {};
};

/** @brief Used by `tr_sessionGetStats()` and `tr_sessionGetCumulativeStats()` */
struct tr_session_stats
{
    float ratio; /* TR_RATIO_INF, TR_RATIO_NA, or total up/down */
    uint64_t uploadedBytes; /* total up */
    uint64_t downloadedBytes; /* total down */
    uint64_t filesAdded; /* number of files added */
    uint64_t sessionCount; /* program started N times */
    uint64_t secondsActive; /* how long Transmission's been running */
};

struct tr_stat
{
    /** A warning or error message regarding the torrent.
        @see error */
    std::string error_string;

    /** Byte count of all the piece data we'll have downloaded when we're done,
        whether or not we have it yet. If we only want some of the files,
        this may be less than `tr_torrent_view.total_size`.
        [0...tr_torrent_view.total_size] */
    uint64_t size_when_done = {};

    /** Byte count of how much data is left to be downloaded until we've got
        all the pieces that we want. [0...tr_stat.sizeWhenDone] */
    uint64_t left_until_done = {};

    /** Byte count of all the piece data we want and don't have yet,
        but that a connected peer does have. [0...leftUntilDone] */
    uint64_t desired_available = {};

    /** Byte count of all the corrupt data you've ever downloaded for
        this torrent. If you're on a poisoned torrent, this number can
        grow very large. */
    uint64_t corrupt_ever = {};

    /** Byte count of all data you've ever uploaded for this torrent. */
    uint64_t uploaded_ever = {};

    /** Byte count of all the non-corrupt data you've ever downloaded
        for this torrent. If you deleted the files and downloaded a second
        time, this will be `2*totalSize`.. */
    uint64_t downloaded_ever = {};

    /** Byte count of all the checksum-verified data we have for this torrent.
      */
    uint64_t have_valid = {};

    /** Byte count of all the partial piece data we have for this torrent.
        As pieces become complete, this value may decrease as portions of it
        are moved to `corrupt` or `haveValid`. */
    uint64_t have_unchecked = {};

    // Speed of all piece being sent for this torrent.
    // This ONLY counts piece data.
    tr::Values::Speed piece_upload_speed;

    // Speed of all piece being received for this torrent.
    // This ONLY counts piece data.
    tr::Values::Speed piece_download_speed;

    // When the torrent was first added
    time_t added_date = {};

    // When the torrent finished downloading
    time_t done_date = {};

    // When the torrent was last started
    time_t start_date = {};

    // The last time we uploaded or downloaded piece data on this torrent
    time_t activity_date = {};

    // The last time during this session that a rarely-changing field
    // changed -- e.g. any `tr_torrent_metainfo` field (trackers, filenames, name)
    // or download directory. RPC clients can monitor this to know when
    // to reload fields that rarely change.
    time_t edit_date = {};

    // Number of seconds since the last activity (or since started).
    // -1 if activity is not seeding or downloading.
    time_t idle_secs = {};

    // Cumulative seconds the torrent's ever spent downloading
    time_t seconds_downloading = {};

    // Cumulative seconds the torrent's ever spent seeding
    time_t seconds_seeding = {};

    // If downloading, estimated number of seconds left until the torrent is done.
    // If seeding, estimated number of seconds left until seed ratio is reached.
    time_t eta = {};

    // If seeding, number of seconds left until the idle time limit is reached.
    time_t eta_idle = {};

    // This torrent's queue position.
    // All torrents have a queue position, even if it's not queued.
    size_t queue_position = {};

    // When `tr_stat.activity` is `TR_STATUS_CHECK` or `TR_STATUS_CHECK_WAIT`,
    // this is the percentage of how much of the files has been
    // verified. When it gets to 1, the verify process is done.
    // Range is [0..1]
    // @see `tr_stat.activity`
    float recheck_progress = {};

    // How much has been downloaded of the entire torrent.
    // Range is [0..1]
    float percent_complete = {};

    // How much of the metadata the torrent has.
    // For torrents added from a torrent this will always be 1.
    // For magnet links, this number will from from 0 to 1 as the metadata is downloaded.
    // Range is [0..1]
    float metadata_percent_complete = {};

    // How much has been downloaded of the files the user wants. This differs
    // from percentComplete if the user wants only some of the torrent's files.
    // Range is [0..1]
    // @see tr_stat.left_until_done
    float percent_done = {};

    // How much has been uploaded to satisfy the seed ratio.
    // This is 1 if the ratio is reached or the torrent is set to seed forever.
    // Range is [0..1] */
    float seed_ratio_percent_done = {};

    // Total uploaded bytes / size_when_done.
    // NB: In Transmission 3.00 and earlier, this was total upload / download,
    // which caused edge cases when total download was less than size_when_done.
    float upload_ratio = {};

    // The torrent's unique Id.
    // @see `tr_torrentId()`
    tr_torrent_id_t id = {};

    // Number of peers that we're connected to
    uint16_t peers_connected = {};

    // How many connected peers we found out about from the tracker, or from pex,
    // or from incoming connections, or from our resume file.
    std::array<uint16_t, TR_PEER_FROM_N_TYPES> peers_from = {};

    // How many known peers we found out about from the tracker, or from pex,
    // or from incoming connections, or from our resume file.
    std::array<uint16_t, TR_PEER_FROM_N_TYPES> known_peers_from = {};

    // Number of peers that are sending data to us.
    uint16_t peers_sending_to_us = {};

    // Number of peers that we're sending data to
    uint16_t peers_getting_from_us = {};

    // Number of webseeds that are sending data to us.
    uint16_t webseeds_sending_to_us = {};

    // What is this torrent doing right now?
    tr_torrent_activity activity = {};

    enum class Error : uint8_t
    {
        Ok, // everything's fine
        TrackerWarning, // tracker returned a warning
        TrackerError, // tracker returned an error
        LocalError // local non-tracker error, e.g. disk full or file permissions
    };

    // Defines what kind of text is in error_string.
    // @see errorString
    Error error = Error::Ok;

    // A torrent is considered finished if it has met its seed ratio.
    // As a result, only paused torrents can be finished.
    bool finished = {};

    // True if the torrent is running, but has been idle for long enough
    // to be considered stalled.  @see `tr_sessionGetQueueStalledMinutes()`
    bool is_stalled = {};
};

/*
 * This view structure is intended for short-term use. Its pointers are owned
 * by the torrent and may be invalidated if the torrent is edited or removed.
 */
struct tr_torrent_view
{
    char const* name;
    char const* hash_string;

    char const* comment; // optional; may be nullptr
    char const* creator; // optional; may be nullptr
    char const* source; // optional; may be nullptr

    uint64_t total_size; // total size of the torrent, in bytes

    time_t date_created;

    uint32_t piece_size;
    tr_piece_index_t n_pieces;

    bool is_private;
    bool is_folder;
};

// NOLINTBEGIN(modernize-avoid-c-arrays)
/*
 * Unlike other _view structs, it is safe to keep a tr_tracker_view copy.
 * The announce and scrape strings are interned & never go out-of-scope.
 */
struct tr_tracker_view
{
    char const* announce; // full announce URL
    char const* scrape; // full scrape URL
    char host_and_port[72]; // uniquely-identifying tracker name (`${host}:${port}`)

    // The tracker site's name. Uses the first label before the public suffix
    // (https://publicsuffix.org/) in the announce URL's host.
    // e.g. "https://www.example.co.uk/announce/"'s sitename is "example"
    // RFC 1034 says labels must be less than 64 chars
    char sitename[64];

    char lastAnnounceResult[128]; // if hasAnnounced, the human-readable result of latest announce
    char lastScrapeResult[128]; // if hasScraped, the human-readable result of the latest scrape

    time_t lastAnnounceStartTime; // if hasAnnounced, when the latest announce request was sent
    time_t lastAnnounceTime; // if hasAnnounced, when the latest announce reply was received
    time_t nextAnnounceTime; // if announceState == TR_TRACKER_WAITING, time of next announce

    time_t lastScrapeStartTime; // if hasScraped, when the latest scrape request was sent
    time_t lastScrapeTime; // if hasScraped, when the latest scrape reply was received
    time_t nextScrapeTime; // if scrapeState == TR_TRACKER_WAITING, time of next scrape

    int downloadCount; // number of times this torrent's been downloaded, or -1 if unknown
    int lastAnnouncePeerCount; // if hasAnnounced, the number of peers the tracker gave us
    int leecherCount; // number of leechers the tracker knows of, or -1 if unknown
    int seederCount; // number of seeders the tracker knows of, or -1 if unknown
    int downloader_count; // number of downloaders (BEP-21) the tracker knows of, or -1 if unknown

    size_t tier; // which tier this tracker is in
    tr_tracker_id_t id; // unique transmission-generated ID for use in libtransmission API

    tr_tracker_state announceState; // whether we're announcing, waiting to announce, etc.
    tr_tracker_state scrapeState; // whether we're scraping, waiting to scrape, etc.

    bool hasAnnounced; // true iff we've announced to this tracker during this session
    bool hasScraped; // true iff we've scraped this tracker during this session
    bool isBackup; // only one tracker per tier is used; the others are kept as backups
    bool lastAnnounceSucceeded; // if hasAnnounced, whether or not the latest announce succeeded
    bool lastAnnounceTimedOut; // true iff the latest announce request timed out
    bool lastScrapeSucceeded; // if hasScraped, whether or not the latest scrape succeeded
    bool lastScrapeTimedOut; // true iff the latest scrape request timed out
};
// NOLINTEND(modernize-avoid-c-arrays)

/*
 * This view structure is intended for short-term use. Its pointers are owned
 * by the torrent and may be invalidated if the torrent is edited or removed.
 */
struct tr_webseed_view
{
    char const* url; // the url to download from
    bool is_downloading; // can be true even if speed is 0, e.g. slow download
    uint64_t download_bytes_per_second; // current download speed
};

using tr_altSpeedFunc = std::function<void(bool active, bool user_driven)>;

using tr_session_queue_start_func = std::function<void(tr_torrent_id_t)>;

using tr_session_idle_limit_hit_func = std::function<void(tr_torrent_id_t)>;

using tr_session_metadata_func = std::function<void(tr_torrent_id_t)>;

using tr_session_ratio_limit_hit_func = std::function<void(tr_torrent_id_t)>;

/**
 * @param was_running whether or not the torrent was running when
 *                    it changed its completeness state
 */
using tr_torrent_completeness_func = std::function<void(tr_torrent_id_t, tr_completeness completeness, bool was_running)>;

using tr_torrent_remove_func = std::function<bool(std::string_view filename, tr_error* error)>;

using tr_rpc_func = std::function<tr_rpc_callback_status(tr_rpc_callback_type type, std::optional<tr_torrent_id_t>)>;

using tr_torrent_rename_done_func = std::function<
    void(tr_torrent_id_t, std::string_view oldpath, std::string_view newname, tr_error const&)>;
