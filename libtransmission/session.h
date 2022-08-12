// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#define TR_NAME "Transmission"

#include <array>
#include <cstddef> // size_t
#include <cstdint> // uintX_t
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility> // std::pair
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "bandwidth.h"
#include "cache.h"
#include "interned-string.h"
#include "net.h" // tr_socket_t
#include "open-files.h"
#include "quark.h"
#include "session-id.h"
#include "stats.h"
#include "timer.h"
#include "torrents.h"
#include "web.h"

enum tr_auto_switch_state_t
{
    TR_AUTO_SWITCH_UNUSED,
    TR_AUTO_SWITCH_ON,
    TR_AUTO_SWITCH_OFF,
};

tr_peer_id_t tr_peerIdInit();

struct event_base;
struct evdns_base;

class tr_bitfield;
class tr_rpc_server;
class tr_web;
struct BlocklistFile;
struct struct_utp_context;
struct tr_address;
struct tr_announcer;
struct tr_announcer_udp;
struct tr_bandwidth;
struct tr_bindsockets;
struct tr_fdInfo;

struct tr_bindinfo
{
    explicit tr_bindinfo(tr_address addr)
        : addr_{ std::move(addr) }
    {
    }

    void bindAndListenForIncomingPeers(tr_session* session);

    void close();

    [[nodiscard]] auto readable() const
    {
        return addr_.readable();
    }

    tr_address addr_;
    struct event* ev_ = nullptr;
    tr_socket_t socket_ = TR_BAD_SOCKET;
};

struct tr_turtle_info
{
    /* TR_UP and TR_DOWN speed limits */
    unsigned int speedLimit_Bps[2] = {};

    /* is turtle mode on right now? */
    bool isEnabled = false;

    /* does turtle mode turn itself on and off at given times? */
    bool isClockEnabled = false;

    /* when clock mode is on, minutes after midnight to turn on turtle mode */
    int beginMinute = 0;

    /* when clock mode is on, minutes after midnight to turn off turtle mode */
    int endMinute = 0;

    /* only use clock mode on these days of the week */
    tr_sched_day days = {};

    /* called when isEnabled changes */
    tr_altSpeedFunc callback = nullptr;

    /* the callback's user_data argument */
    void* callbackUserData = nullptr;

    /* the callback's changedByUser argument.
     * indicates whether the change came from the user or from the clock. */
    bool changedByUser = false;

    /* bitfield of all the minutes in a week.
     * Each bit's value indicates whether the scheduler wants turtle
     * limits on or off at that given minute in the week. */
    // Changed to non-owning pointer temporarily till tr_turtle_info becomes C++-constructible and destructible
    // TODO: remove * and own the value
    tr_bitfield* minutes = nullptr;

    /* recent action that was done by turtle's automatic switch */
    tr_auto_switch_state_t autoTurtleState = TR_AUTO_SWITCH_UNUSED;
};

/** @brief handle to an active libtransmission session */
struct tr_session
{
public:
    explicit tr_session(std::string_view config_dir);

    [[nodiscard]] auto& topBandwidth() noexcept
    {
        return top_bandwidth_;
    }

    [[nodiscard]] auto const& topBandwidth() const noexcept
    {
        return top_bandwidth_;
    }

    [[nodiscard]] std::string_view sessionId() const noexcept
    {
        return session_id_.sv();
    }

    [[nodiscard]] auto& web() noexcept
    {
        return *web_;
    }

    [[nodiscard]] auto const& cache() const noexcept
    {
        return *cache_;
    }

    [[nodiscard]] auto& cache() noexcept
    {
        return *cache_;
    }

    [[nodiscard]] constexpr auto isClosing() const noexcept
    {
        return is_closing_;
    }

    [[nodiscard]] constexpr auto isClosed() const noexcept
    {
        return is_session_closed_;
    }

    [[nodiscard]] constexpr auto isDHTEnabled() const noexcept
    {
        return is_dht_enabled_;
    }

    [[nodiscard]] constexpr auto isLPDEnabled() const noexcept
    {
        return is_lpd_enabled_;
    }

    [[nodiscard]] constexpr auto isPexEnabled() const noexcept
    {
        return is_pex_enabled_;
    }

    [[nodiscard]] constexpr auto isPrefetchEnabled() const noexcept
    {
        return is_prefetch_enabled_;
    }

    [[nodiscard]] constexpr auto isUTPEnabled() const noexcept
    {
        return is_utp_enabled_;
    }

    [[nodiscard]] constexpr auto isIncompleteFileNamingEnabled() const noexcept
    {
        return is_incomplete_file_naming_enabled_;
    }

    [[nodiscard]] constexpr auto shouldScrapePausedTorrents() const noexcept
    {
        return should_scrape_paused_torrents_;
    }

    [[nodiscard]] event_base* eventBase() noexcept
    {
        return event_base_.get();
    }

    [[nodiscard]] auto& timerMaker() noexcept
    {
        return *timer_maker_;
    }

    [[nodiscard]] constexpr auto& torrents()
    {
        return torrents_;
    }

    [[nodiscard]] constexpr auto const& torrents() const
    {
        return torrents_;
    }

    [[nodiscard]] auto unique_lock() const
    {
        return std::unique_lock(session_mutex_);
    }

    // paths

    [[nodiscard]] constexpr auto const& configDir() const noexcept
    {
        return config_dir_;
    }

    [[nodiscard]] constexpr auto const& torrentDir() const noexcept
    {
        return torrent_dir_;
    }

    [[nodiscard]] constexpr auto const& resumeDir() const noexcept
    {
        return resume_dir_;
    }

    [[nodiscard]] constexpr auto const& downloadDir() const noexcept
    {
        return download_dir_;
    }

    void setDownloadDir(std::string_view dir)
    {
        download_dir_ = dir;
    }

    // default trackers
    // (trackers to apply automatically to public torrents)

    [[nodiscard]] constexpr auto const& defaultTrackersStr() const noexcept
    {
        return default_trackers_str_;
    }

    [[nodiscard]] constexpr auto const& defaultTrackers() const noexcept
    {
        return default_trackers_;
    }

    void setDefaultTrackers(std::string_view trackers);

    // incomplete dir

    [[nodiscard]] constexpr auto const& incompleteDir() const noexcept
    {
        return incomplete_dir_;
    }

    void setIncompleteDir(std::string_view dir)
    {
        incomplete_dir_ = dir;
    }

    [[nodiscard]] constexpr auto useIncompleteDir() const noexcept
    {
        return incomplete_dir_enabled_;
    }

    constexpr void useIncompleteDir(bool enabled) noexcept
    {
        incomplete_dir_enabled_ = enabled;
    }

    // scripts

    constexpr void useScript(TrScript i, bool enabled)
    {
        scripts_enabled_[i] = enabled;
    }

    [[nodiscard]] auto useScript(TrScript i) const
    {
        return scripts_enabled_[i];
    }

    void setScript(TrScript i, std::string_view path)
    {
        scripts_[i] = path;
    }

    [[nodiscard]] constexpr auto const& script(TrScript i) const
    {
        return scripts_[i];
    }

    // blocklist

    [[nodiscard]] constexpr auto useBlocklist() const noexcept
    {
        return blocklist_enabled_;
    }

    void useBlocklist(bool enabled);

    [[nodiscard]] constexpr auto const& blocklistUrl() const noexcept
    {
        return blocklist_url_;
    }

    void setBlocklistUrl(std::string_view url)
    {
        blocklist_url_ = url;
    }

    // RPC

    void setRpcWhitelist(std::string_view whitelist) const;

    void useRpcWhitelist(bool enabled) const;

    [[nodiscard]] bool useRpcWhitelist() const;

    [[nodiscard]] auto externalIP() const noexcept
    {
        return external_ip_;
    }

    void setExternalIP(tr_address external_ip)
    {
        external_ip_ = external_ip;
    }

    // peer networking

    [[nodiscard]] constexpr auto const& peerCongestionAlgorithm() const noexcept
    {
        return peer_congestion_algorithm_;
    }

    void setPeerCongestionAlgorithm(std::string_view algorithm)
    {
        peer_congestion_algorithm_ = algorithm;
    }

    void setSocketTOS(tr_socket_t sock, tr_address_type type) const
    {
        tr_netSetTOS(sock, peer_socket_tos_, type);
    }

    [[nodiscard]] constexpr bool incPeerCount() noexcept
    {
        if (this->peerCount >= this->peerLimit)
        {
            return false;
        }

        ++this->peerCount;
        return true;
    }

    constexpr void decPeerCount() noexcept
    {
        if (this->peerCount > 0)
        {
            --this->peerCount;
        }
    }

    // bandwidth

    [[nodiscard]] tr_bandwidth& getBandwidthGroup(std::string_view name);

    [[nodiscard]] constexpr auto& bandwidthGroups() const noexcept
    {
        return bandwidth_groups_;
    }

    //

    [[nodiscard]] constexpr auto& openFiles() noexcept
    {
        return open_files_;
    }

    void closeTorrentFiles(tr_torrent* tor) noexcept;
    void closeTorrentFile(tr_torrent* tor, tr_file_index_t file_num) noexcept;

    // announce ip

    [[nodiscard]] constexpr auto const& announceIP() const noexcept
    {
        return announce_ip_;
    }

    void setAnnounceIP(std::string_view ip)
    {
        announce_ip_ = ip;
    }

    [[nodiscard]] constexpr auto useAnnounceIP() const noexcept
    {
        return announce_ip_enabled_;
    }

    constexpr void useAnnounceIP(bool enabled) noexcept
    {
        announce_ip_enabled_ = enabled;
    }

    // callbacks

    using queue_start_callback_t = void (*)(tr_session*, tr_torrent*, void* user_data);

    void setQueueStartCallback(queue_start_callback_t cb, void* user_data)
    {
        queue_start_callback_ = cb;
        queue_start_user_data_ = user_data;
    }

    void onQueuedTorrentStarted(tr_torrent* tor)
    {
        if (queue_start_callback_ != nullptr)
        {
            queue_start_callback_(this, tor, queue_start_user_data_);
        }
    }

    void setIdleLimitHitCallback(tr_session_idle_limit_hit_func cb, void* user_data)
    {
        idle_limit_hit_callback_ = cb;
        idle_limit_hit_user_data_ = user_data;
    }

    void onIdleLimitHit(tr_torrent* tor)
    {
        if (idle_limit_hit_callback_ != nullptr)
        {
            idle_limit_hit_callback_(this, tor, idle_limit_hit_user_data_);
        }
    }

    void setRatioLimitHitCallback(tr_session_ratio_limit_hit_func cb, void* user_data)
    {
        ratio_limit_hit_cb_ = cb;
        ratio_limit_hit_user_data_ = user_data;
    }

    void onRatioLimitHit(tr_torrent* tor)
    {
        if (ratio_limit_hit_cb_ != nullptr)
        {
            ratio_limit_hit_cb_(this, tor, ratio_limit_hit_user_data_);
        }
    }

    void setMetadataCallback(tr_session_metadata_func cb, void* user_data)
    {
        got_metadata_cb_ = cb;
        got_metadata_user_data_ = user_data;
    }

    void onMetadataCompleted(tr_torrent* tor)
    {
        if (got_metadata_cb_ != nullptr)
        {
            got_metadata_cb_(this, tor, got_metadata_user_data_);
        }
    }

    void setTorrentCompletenessCallback(tr_torrent_completeness_func cb, void* user_data)
    {
        completeness_func_ = cb;
        completeness_func_user_data_ = user_data;
    }

    void onTorrentCompletenessChanged(tr_torrent* tor, tr_completeness completeness, bool was_running)
    {
        if (completeness_func_ != nullptr)
        {
            completeness_func_(tor, completeness, was_running, completeness_func_user_data_);
        }
    }

    /// stats

    [[nodiscard]] auto& stats() noexcept
    {
        return session_stats_;
    }

    [[nodiscard]] auto const& stats() const noexcept
    {
        return session_stats_;
    }

    void addUploaded(uint32_t n_bytes) noexcept
    {
        session_stats_.addUploaded(n_bytes);
    }

    void addDownloaded(uint32_t n_bytes) noexcept
    {
        session_stats_.addDownloaded(n_bytes);
    }

    void addFileCreated() noexcept
    {
        session_stats_.addFileCreated();
    }

public:
    static constexpr std::array<std::tuple<tr_quark, tr_quark, TrScript>, 3> Scripts{
        { { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_filename, TR_SCRIPT_ON_TORRENT_ADDED },
          { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_filename, TR_SCRIPT_ON_TORRENT_DONE },
          { TR_KEY_script_torrent_done_seeding_enabled,
            TR_KEY_script_torrent_done_seeding_filename,
            TR_SCRIPT_ON_TORRENT_DONE_SEEDING } }
    };

    uint8_t peer_id_ttl_hours = 0;

    bool stalledEnabled = false;
    bool queueEnabled[2] = { false, false };
    int queueSize[2] = { 0, 0 };
    int queueStalledMinutes = 0;

    int umask = 0;

    unsigned int speedLimit_Bps[2] = { 0, 0 };
    bool speedLimitEnabled[2] = { false, false };

    struct tr_turtle_info turtle;

    tr_encryption_mode encryptionMode;

    tr_preallocation_mode preallocationMode;

    struct evdns_base* evdns_base = nullptr;
    struct tr_event_handle* events = nullptr;

    uint16_t peerCount = 0;
    uint16_t peerLimit = 200;
    uint16_t peerLimitPerTorrent = 50;

    uint16_t upload_slots_per_torrent = 0;

    /* The UDP sockets used for the DHT and uTP. */
    tr_port udp_port;
    tr_socket_t udp_socket = TR_BAD_SOCKET;
    tr_socket_t udp6_socket = TR_BAD_SOCKET;
    unsigned char* udp6_bound = nullptr;
    struct event* udp_event = nullptr;
    struct event* udp6_event = nullptr;

    struct struct_utp_context* utp_context = nullptr;
    std::unique_ptr<libtransmission::Timer> utp_timer;

    void setPeerPort(tr_port public_port, tr_port private_port);

    void setPeerPort(tr_port port)
    {
        setPeerPort(port, port);
    }

    [[nodiscard]] constexpr auto publicPeerPort() const noexcept
    {
        return public_peer_port_;
    }

    [[nodiscard]] constexpr auto privatePeerPort() const noexcept
    {
        return private_peer_port_;
    }

    std::vector<std::unique_ptr<BlocklistFile>> blocklists;
    struct tr_peerMgr* peerMgr = nullptr;
    struct tr_shared* shared = nullptr;

    tr_rpc_func rpc_func = nullptr;
    void* rpc_func_user_data = nullptr;

    struct tr_announcer* announcer = nullptr;
    struct tr_announcer_udp* announcer_udp = nullptr;

    tr_bindinfo bind_ipv4 = tr_bindinfo{ tr_inaddr_any };
    tr_bindinfo bind_ipv6 = tr_bindinfo{ tr_in6addr_any };

private:
    friend bool tr_sessionGetAntiBruteForceEnabled(tr_session const* session);
    friend bool tr_sessionGetDeleteSource(tr_session const* session);
    friend bool tr_sessionGetPaused(tr_session const* session);
    friend bool tr_sessionGetPeerPortRandomOnStart(tr_session const* session);
    friend bool tr_sessionIsDHTEnabled(tr_session const* session);
    friend bool tr_sessionIsIdleLimited(tr_session const* session);
    friend bool tr_sessionIsIncompleteFileNamingEnabled(tr_session const* session);
    friend bool tr_sessionIsPexEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCPasswordEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCPasswordEnabled(tr_session const* session);
    friend bool tr_sessionIsRatioLimited(tr_session const* session);
    friend bool tr_sessionIsUTPEnabled(tr_session const* session);
    friend char const* tr_sessionGetRPCPassword(tr_session const* session);
    friend char const* tr_sessionGetRPCUrl(tr_session const* session);
    friend char const* tr_sessionGetRPCUsername(tr_session const* session);
    friend char const* tr_sessionGetRPCWhitelist(tr_session const* session);
    friend double tr_sessionGetRatioLimit(tr_session const* session);
    friend int tr_sessionGetAntiBruteForceThreshold(tr_session const* session);
    friend tr_session* tr_sessionInit(char const* config_dir, bool message_queueing_enabled, tr_variant* client_settings);
    friend uint16_t tr_sessionGetIdleLimit(tr_session const* session);
    friend uint16_t tr_sessionGetPeerPort(tr_session const* session);
    friend uint16_t tr_sessionGetRPCPort(tr_session const* session);
    friend uint16_t tr_sessionSetPeerPortRandom(tr_session* session);
    friend void tr_sessionClose(tr_session* session);
    friend void tr_sessionFetch(tr_session* session, tr_web::FetchOptions&& options);
    friend void tr_sessionGetSettings(tr_session const* s, tr_variant* setme_dictionary);
    friend void tr_sessionSet(tr_session* session, tr_variant* settings);
    friend void tr_sessionSetAntiBruteForceEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetAntiBruteForceThreshold(tr_session* session, int max_bad_requests);
    friend void tr_sessionSetDHTEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetDeleteSource(tr_session* session, bool delete_source);
    friend void tr_sessionSetIdleLimit(tr_session* session, uint16_t idle_minutes);
    friend void tr_sessionSetIdleLimited(tr_session* session, bool is_limited);
    friend void tr_sessionSetIncompleteFileNamingEnabled(tr_session* session, bool b);
    friend void tr_sessionSetLPDEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetPaused(tr_session* session, bool is_paused);
    friend void tr_sessionSetPeerPort(tr_session* session, uint16_t hport);
    friend void tr_sessionSetPeerPortRandomOnStart(tr_session* session, bool random);
    friend void tr_sessionSetPexEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetRPCEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetRPCPassword(tr_session* session, char const* password);
    friend void tr_sessionSetRPCPasswordEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetRPCPort(tr_session* session, uint16_t hport);
    friend void tr_sessionSetRPCUrl(tr_session* session, char const* url);
    friend void tr_sessionSetRPCUsername(tr_session* session, char const* username);
    friend void tr_sessionSetRatioLimit(tr_session* session, double desired_ratio);
    friend void tr_sessionSetRatioLimited(tr_session* session, bool is_limited);
    friend void tr_sessionSetUTPEnabled(tr_session* session, bool enabled);

    [[nodiscard]] tr_port randomPort() const;
    tr_port random_port_low_;
    tr_port random_port_high_;

    /* The open port on the local machine for incoming peer requests */
    tr_port private_peer_port_;

    /**
     * The open port on the public device for incoming peer requests.
     * This is usually the same as private_peer_port but can differ
     * if the public device is a router and it decides to use a different
     * port than the one requested by Transmission.
     */
    tr_port public_peer_port_;

    std::unique_ptr<Cache> cache_;

    // monitors the "global pool" speeds
    tr_bandwidth top_bandwidth_;

    tr_session_id session_id_;

    class WebMediator final : public tr_web::Mediator
    {
    public:
        explicit WebMediator(tr_session* session)
            : session_{ session }
        {
        }
        ~WebMediator() override = default;

        [[nodiscard]] std::optional<std::string> cookieFile() const override;
        [[nodiscard]] std::optional<std::string> publicAddress() const override;
        [[nodiscard]] std::optional<std::string_view> userAgent() const override;
        [[nodiscard]] unsigned int clamp(int torrent_id, unsigned int byte_count) const override;
        void notifyBandwidthConsumed(int torrent_id, size_t byte_count) override;
        // runs the tr_web::fetch response callback in the libtransmission thread
        void run(tr_web::FetchDoneFunc&& func, tr_web::FetchResponse&& response) const override;

    private:
        tr_session* const session_;
    };

    WebMediator web_mediator_{ this };
    std::unique_ptr<tr_web> web_;

    std::vector<std::pair<tr_interned_string, std::unique_ptr<tr_bandwidth>>> bandwidth_groups_;

    float desired_ratio_;

    uint16_t idle_limit_minutes_;

    std::unique_ptr<tr_rpc_server> rpc_server_;

    tr_announce_list default_trackers_;

    // One of <netinet/ip.h>'s IPTOS_ values.
    // See tr_netTos*() in libtransmission/net.h for more info
    // Only session.cc should use this.
    int peer_socket_tos_ = *tr_netTosFromName(TR_DEFAULT_PEER_SOCKET_TOS_STR);

    struct init_data;
    void initImpl(struct init_data&);
    void setImpl(struct init_data&);

    void closeImplStart();
    void closeImplWaitForIdleUdp();
    void closeImplFinish();

    bool delete_source_torrent_ = false;
    bool is_closing_ = false;
    bool is_dht_enabled_ = false;
    bool is_idle_limited_ = false;
    bool is_incomplete_file_naming_enabled_ = false;
    bool is_lpd_enabled_ = false;
    bool is_pex_enabled_ = false;
    bool is_port_random_ = false;
    bool is_prefetch_enabled_ = false;
    bool is_ratio_limited_ = false;
    bool is_session_closed_ = false;
    bool is_utp_enabled_ = false;
    bool pause_added_torrent_ = false;
    bool should_scrape_paused_torrents_ = false;

    static std::recursive_mutex session_mutex_;

    std::shared_ptr<event_base> const event_base_;
    std::unique_ptr<libtransmission::TimerMaker> const timer_maker_;

    void onNowTimer();
    std::unique_ptr<libtransmission::Timer> now_timer_;

    std::unique_ptr<libtransmission::Timer> save_timer_;

    tr_torrents torrents_;

    std::array<std::string, TR_SCRIPT_N_TYPES> scripts_;

    std::string const config_dir_;
    std::string const resume_dir_;
    std::string const torrent_dir_;
    std::string download_dir_;
    std::string incomplete_dir_;

    std::string blocklist_url_;
    std::string default_trackers_str_;
    std::string peer_congestion_algorithm_;

    tr_stats session_stats_;

    std::optional<tr_address> external_ip_;

    queue_start_callback_t queue_start_callback_ = nullptr;
    void* queue_start_user_data_ = nullptr;

    tr_session_idle_limit_hit_func idle_limit_hit_callback_ = nullptr;
    void* idle_limit_hit_user_data_ = nullptr;

    tr_session_ratio_limit_hit_func ratio_limit_hit_cb_ = nullptr;
    void* ratio_limit_hit_user_data_ = nullptr;

    tr_session_metadata_func got_metadata_cb_ = nullptr;
    void* got_metadata_user_data_ = nullptr;

    tr_torrent_completeness_func completeness_func_ = nullptr;
    void* completeness_func_user_data_ = nullptr;

    std::array<bool, TR_SCRIPT_N_TYPES> scripts_enabled_;
    bool blocklist_enabled_ = false;
    bool incomplete_dir_enabled_ = false;

    tr_open_files open_files_;

    std::string announce_ip_;
    bool announce_ip_enabled_ = false;
};

bool tr_sessionAllowsDHT(tr_session const* session);

bool tr_sessionAllowsLPD(tr_session const* session);

bool tr_sessionIsAddressBlocked(tr_session const* session, struct tr_address const* addr);

struct tr_address const* tr_sessionGetPublicAddress(tr_session const* session, int tr_af_type, bool* is_default_value);

struct tr_bindsockets* tr_sessionGetBindSockets(tr_session*);

int tr_sessionCountTorrents(tr_session const* session);

std::vector<tr_torrent*> tr_sessionGetTorrents(tr_session* session);

constexpr bool tr_isPriority(tr_priority_t p)
{
    return p == TR_PRI_LOW || p == TR_PRI_NORMAL || p == TR_PRI_HIGH;
}

/***
****
***/

unsigned int tr_sessionGetSpeedLimit_Bps(tr_session const*, tr_direction);
unsigned int tr_sessionGetPieceSpeed_Bps(tr_session const*, tr_direction);

bool tr_sessionGetActiveSpeedLimit_Bps(tr_session const* session, tr_direction dir, unsigned int* setme);

std::vector<tr_torrent*> tr_sessionGetNextQueuedTorrents(tr_session* session, tr_direction dir, size_t num_wanted);

int tr_sessionCountQueueFreeSlots(tr_session* session, tr_direction);
