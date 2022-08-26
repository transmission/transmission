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
#include "bitfield.h"
#include "cache.h"
#include "interned-string.h"
#include "net.h" // tr_socket_t
#include "open-files.h"
#include "quark.h"
#include "session-id.h"
#include "stats.h"
#include "timer.h"
#include "torrents.h"
#include "tr-lpd.h"
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

class tr_rpc_server;
class tr_web;
class tr_lpd;
struct BlocklistFile;
struct struct_utp_context;
struct tr_announcer;
struct tr_announcer_udp;

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
    std::array<unsigned int, 2> speedLimit_Bps = {};

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
    tr_bitfield minutes{ 10080 };

    /* recent action that was done by turtle's automatic switch */
    tr_auto_switch_state_t autoTurtleState = TR_AUTO_SWITCH_UNUSED;
};

/** @brief handle to an active libtransmission session */
struct tr_session
{
public:
    explicit tr_session(std::string_view config_dir);

    [[nodiscard]] std::string_view sessionId() const noexcept
    {
        return session_id_.sv();
    }

    [[nodiscard]] event_base* eventBase() noexcept
    {
        return event_base_.get();
    }

    [[nodiscard]] evdns_base* evdnsBase() noexcept
    {
        return evdns_base_.get();
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
        if (this->peer_count_ >= this->peer_limit_)
        {
            return false;
        }

        ++this->peer_count_;
        return true;
    }

    constexpr void decPeerCount() noexcept
    {
        if (this->peer_count_ > 0)
        {
            --this->peer_count_;
        }
    }

    // bandwidth

    [[nodiscard]] tr_bandwidth& getBandwidthGroup(std::string_view name);

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

    constexpr void setQueueStartCallback(queue_start_callback_t cb, void* user_data)
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

    constexpr void setIdleLimitHitCallback(tr_session_idle_limit_hit_func cb, void* user_data)
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

    constexpr void setRatioLimitHitCallback(tr_session_ratio_limit_hit_func cb, void* user_data)
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

    constexpr void setMetadataCallback(tr_session_metadata_func cb, void* user_data)
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

    constexpr void setTorrentCompletenessCallback(tr_torrent_completeness_func cb, void* user_data)
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

    struct tr_turtle_info turtle;

    struct tr_event_handle* events = nullptr;

    /* The UDP sockets used for the DHT and uTP. */
    tr_port udp_port;
    tr_socket_t udp_socket = TR_BAD_SOCKET;
    tr_socket_t udp6_socket = TR_BAD_SOCKET;
    unsigned char* udp6_bound = nullptr;
    struct event* udp_event = nullptr;
    struct event* udp6_event = nullptr;

    /* The open port on the local machine for incoming peer requests */
    tr_port private_peer_port;

    /**
     * The open port on the public device for incoming peer requests.
     * This is usually the same as private_peer_port but can differ
     * if the public device is a router and it decides to use a different
     * port than the one requested by Transmission.
     */
    tr_port public_peer_port;

    [[nodiscard]] constexpr auto peerPort() const noexcept
    {
        return public_peer_port;
    }

    constexpr auto setPeerPort(tr_port port) noexcept
    {
        public_peer_port = port;
    }

    struct tr_peerMgr* peerMgr = nullptr;
    struct tr_shared* shared = nullptr;

    std::unique_ptr<Cache> cache;

    std::unique_ptr<tr_web> web;
    std::unique_ptr<tr_lpd> lpd_;

    struct tr_announcer* announcer = nullptr;
    struct tr_announcer_udp* announcer_udp = nullptr;

    // monitors the "global pool" speeds
    tr_bandwidth top_bandwidth_;

    std::vector<std::pair<tr_interned_string, std::unique_ptr<tr_bandwidth>>> bandwidth_groups_;

    tr_bindinfo bind_ipv4 = tr_bindinfo{ tr_inaddr_any };
    tr_bindinfo bind_ipv6 = tr_bindinfo{ tr_in6addr_any };

    [[nodiscard]] auto constexpr queueEnabled(tr_direction dir) const noexcept
    {
        return queue_enabled_[dir];
    }

    [[nodiscard]] auto constexpr queueSize(tr_direction dir) const noexcept
    {
        return queue_size_[dir];
    }

    [[nodiscard]] auto constexpr queueStalledEnabled() const noexcept
    {
        return queue_stalled_enabled_;
    }

    [[nodiscard]] auto constexpr queueStalledMinutes() const noexcept
    {
        return queue_stalled_minutes_;
    }

    [[nodiscard]] auto constexpr peerLimit() const noexcept
    {
        return peer_limit_;
    }

    [[nodiscard]] auto constexpr peerLimitPerTorrent() const noexcept
    {
        return peer_limit_per_torrent_;
    }

    [[nodiscard]] auto constexpr uploadSlotsPerTorrent() const noexcept
    {
        return upload_slots_per_torrent_;
    }

    [[nodiscard]] auto constexpr isClosing() const noexcept
    {
        return is_closing_;
    }

    [[nodiscard]] auto constexpr isClosed() const noexcept
    {
        return is_closed_;
    }

    [[nodiscard]] auto constexpr encryptionMode() const noexcept
    {
        return encryption_mode_;
    }

    [[nodiscard]] auto constexpr preallocationMode() const noexcept
    {
        return preallocation_mode_;
    }

    [[nodiscard]] auto constexpr shouldScrapePausedTorrents() const noexcept
    {
        return should_scrape_paused_torrents_;
    }

    [[nodiscard]] auto constexpr shouldPauseAddedTorrents() const noexcept
    {
        return should_pause_added_torrents_;
    }

    [[nodiscard]] auto constexpr shouldDeleteSource() const noexcept
    {
        return should_pause_added_torrents_;
    }

    [[nodiscard]] auto constexpr allowsDHT() const noexcept
    {
        return is_dht_enabled_;
    }

    [[nodiscard]] auto constexpr allowsLPD() const noexcept
    {
        return is_lpd_enabled_;
    }

    [[nodiscard]] auto constexpr allowsPEX() const noexcept
    {
        return is_pex_enabled_;
    }

    [[nodiscard]] auto constexpr allowsTCP() const noexcept
    {
        return is_tcp_enabled_;
    }

    [[nodiscard]] bool allowsUTP() const noexcept;

    [[nodiscard]] auto constexpr allowsPrefetch() const noexcept
    {
        return is_prefetch_enabled_;
    }

    [[nodiscard]] auto constexpr isIdleLimited() const noexcept
    {
        return is_idle_limited_;
    }

    [[nodiscard]] auto constexpr idleLimitMinutes() const noexcept
    {
        return idle_limit_minutes_;
    }

    [[nodiscard]] std::vector<tr_torrent*> getAllTorrents() const
    {
        return std::vector<tr_torrent*>{ std::begin(torrents()), std::end(torrents()) };
    }

    /*module_visible*/

    auto rpcNotify(tr_rpc_callback_type type, tr_torrent* tor = nullptr)
    {
        if (rpc_func_ != nullptr)
        {
            return (*rpc_func_)(this, type, tor, rpc_func_user_data_);
        }

        return TR_RPC_OK;
    }

    [[nodiscard]] size_t countQueueFreeSlots(tr_direction dir) const noexcept;

    [[nodiscard]] std::vector<tr_torrent*> getNextQueuedTorrents(tr_direction dir, size_t num_wanted) const;

    [[nodiscard]] bool addressIsBlocked(tr_address const& addr) const noexcept;

    struct PublicAddressResult
    {
        tr_address address;
        bool is_default_value;
    };

    [[nodiscard]] PublicAddressResult publicAddress(tr_address_type type) const noexcept;

    [[nodiscard]] constexpr auto speedLimitBps(tr_direction dir) const noexcept
    {
        return speed_limit_Bps_[dir];
    }

    [[nodiscard]] constexpr auto isSpeedLimited(tr_direction dir) const noexcept
    {
        return speed_limit_enabled_[dir];
    }

    [[nodiscard]] auto pieceSpeedBps(tr_direction dir) const noexcept
    {
        return top_bandwidth_.getPieceSpeedBytesPerSecond(0, dir);
    }

    [[nodiscard]] std::optional<unsigned int> activeSpeedLimitBps(tr_direction dir) const noexcept;

    [[nodiscard]] auto isIncompleteFileNamingEnabled() const noexcept
    {
        return is_incomplete_file_naming_enabled_;
    }

    [[nodiscard]] constexpr auto isPortRandom() const noexcept
    {
        return is_port_random_;
    }

    [[nodiscard]] auto constexpr isRatioLimited() const noexcept
    {
        return is_ratio_limited_;
    }

    [[nodiscard]] constexpr auto desiredRatio() const noexcept
    {
        return desired_ratio_;
    }

    [[nodiscard]] constexpr auto peerIdTTLHours() const noexcept
    {
        return peer_id_ttl_hours_;
    }

private:
    [[nodiscard]] tr_port randomPort() const;

    void loadBlocklists();

    struct init_data;
    void initImpl(init_data&);
    void setImpl(init_data&);
    void closeImplStart();
    void closeImplWaitForIdleUdp();
    void closeImplFinish();

    friend bool tr_blocklistExists(tr_session const* session);
    friend bool tr_sessionGetAntiBruteForceEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCPasswordEnabled(tr_session const* session);
    friend char const* tr_sessionGetRPCPassword(tr_session const* session);
    friend char const* tr_sessionGetRPCUsername(tr_session const* session);
    friend char const* tr_sessionGetRPCWhitelist(tr_session const* session);
    friend int tr_sessionGetAntiBruteForceThreshold(tr_session const* session);
    friend size_t tr_blocklistGetRuleCount(tr_session const* session);
    friend size_t tr_blocklistSetContent(tr_session* session, char const* content_filename);
    friend tr_session* tr_sessionInit(char const* config_dir, bool message_queueing_enabled, tr_variant* client_settings);
    friend uint16_t tr_sessionGetRPCPort(tr_session const* session);
    friend uint16_t tr_sessionSetPeerPortRandom(tr_session* session);
    friend void tr_sessionClose(tr_session* session);
    friend void tr_sessionGetSettings(tr_session const* s, tr_variant* setme_dictionary);
    friend void tr_sessionLimitSpeed(tr_session* session, tr_direction dir, bool limited);
    friend void tr_sessionReloadBlocklists(tr_session* session);
    friend void tr_sessionSet(tr_session* session, tr_variant* settings);
    friend void tr_sessionSetAntiBruteForceEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetAntiBruteForceThreshold(tr_session* session, int max_bad_requests);
    friend void tr_sessionSetDHTEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetDeleteSource(tr_session* session, bool delete_source);
    friend void tr_sessionSetEncryption(tr_session* session, tr_encryption_mode mode);
    friend void tr_sessionSetIdleLimit(tr_session* session, uint16_t idle_minutes);
    friend void tr_sessionSetIdleLimited(tr_session* session, bool is_limited);
    friend void tr_sessionSetIncompleteFileNamingEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetLPDEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetPaused(tr_session* session, bool is_paused);
    friend void tr_sessionSetPeerLimit(tr_session* session, uint16_t max_global_peers);
    friend void tr_sessionSetPeerLimitPerTorrent(tr_session* session, uint16_t max_peers);
    friend void tr_sessionSetPeerPortRandomOnStart(tr_session* session, bool random);
    friend void tr_sessionSetPexEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetQueueEnabled(tr_session* session, tr_direction dir, bool do_limit_simultaneous_seed_torrents);
    friend void tr_sessionSetQueueSize(tr_session* session, tr_direction dir, int max_simultaneous_seed_torrents);
    friend void tr_sessionSetQueueStalledEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetQueueStalledMinutes(tr_session* session, int minutes);
    friend void tr_sessionSetRPCCallback(tr_session* session, tr_rpc_func func, void* user_data);
    friend void tr_sessionSetRPCEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetRPCPassword(tr_session* session, char const* password);
    friend void tr_sessionSetRPCPasswordEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetRPCPort(tr_session* session, uint16_t hport);
    friend void tr_sessionSetRPCUsername(tr_session* session, char const* username);
    friend void tr_sessionSetRatioLimit(tr_session* session, double desired_ratio);
    friend void tr_sessionSetRatioLimited(tr_session* session, bool is_limited);
    friend void tr_sessionSetSpeedLimit_Bps(tr_session* session, tr_direction dir, unsigned int Bps);
    friend void tr_sessionSetUTPEnabled(tr_session* session, bool enabled);

    static std::recursive_mutex session_mutex_;

    std::vector<std::unique_ptr<BlocklistFile>> blocklists_;

    std::unique_ptr<tr_rpc_server> rpc_server_;

    tr_announce_list default_trackers_;

    tr_session_id session_id_;

    std::array<unsigned int, 2> speed_limit_Bps_ = { 0U, 0U };
    std::array<bool, 2> speed_limit_enabled_ = { false, false };

    std::array<bool, 2> queue_enabled_ = { false, false };
    std::array<int, 2> queue_size_ = { 0, 0 };

    tr_rpc_func rpc_func_ = nullptr;
    void* rpc_func_user_data_ = nullptr;

    float desired_ratio_ = 2.0F;

    int umask_ = 022;

    // One of <netinet/ip.h>'s IPTOS_ values.
    // See tr_netTos*() in libtransmission/net.h for more info
    int peer_socket_tos_ = *tr_netTosFromName(TR_DEFAULT_PEER_SOCKET_TOS_STR);

    int queue_stalled_minutes_ = 0;

    tr_encryption_mode encryption_mode_ = TR_ENCRYPTION_PREFERRED;

    tr_preallocation_mode preallocation_mode_ = TR_PREALLOCATE_SPARSE;

    tr_port random_port_low_;
    tr_port random_port_high_;

    uint16_t peer_count_ = 0;
    uint16_t peer_limit_ = 200;
    uint16_t peer_limit_per_torrent_ = 50;

    uint16_t idle_limit_minutes_;

    uint16_t upload_slots_per_torrent_ = 8;

    uint8_t peer_id_ttl_hours_ = 6;

    bool is_closing_ = false;
    bool is_closed_ = false;

    bool is_utp_enabled_ = false;
    bool is_pex_enabled_ = false;
    bool is_dht_enabled_ = false;
    bool is_lpd_enabled_ = false;
    bool is_tcp_enabled_ = true;

    bool is_idle_limited_ = false;
    bool is_prefetch_enabled_ = false;
    bool is_ratio_limited_ = false;
    bool queue_stalled_enabled_ = false;

    bool is_port_random_ = false;

    bool should_pause_added_torrents_ = false;
    bool should_delete_source_torrents_ = false;
    bool should_scrape_paused_torrents_ = false;
    bool is_incomplete_file_naming_enabled_ = false;

    class WebMediator final : public tr_web::Mediator
    {
    public:
        explicit WebMediator(tr_session* session)
            : session_{ session }
        {
        }
        ~WebMediator() override = default;

        [[nodiscard]] std::optional<std::string> cookieFile() const override;
        [[nodiscard]] std::optional<std::string> publicAddressV4() const override;
        [[nodiscard]] std::optional<std::string> publicAddressV6() const override;
        [[nodiscard]] std::optional<std::string_view> userAgent() const override;
        [[nodiscard]] unsigned int clamp(int torrent_id, unsigned int byte_count) const override;
        void notifyBandwidthConsumed(int torrent_id, size_t byte_count) override;
        // runs the tr_web::fetch response callback in the libtransmission thread
        void run(tr_web::FetchDoneFunc&& func, tr_web::FetchResponse&& response) const override;

    private:
        tr_session* const session_;
    };

    WebMediator web_mediator_{ this };

    class LpdMediator final : public tr_lpd::Mediator
    {
    public:
        explicit LpdMediator(tr_session& session)
            : session_{ session }
        {
        }
        ~LpdMediator() override = default;

        [[nodiscard]] tr_port port() const override
        {
            return session_.peerPort();
        }

        [[nodiscard]] bool allowsLPD() const override
        {
            return session_.allowsLPD();
        }

        [[nodiscard]] std::vector<TorrentInfo> torrents() const override;

        bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) override;

        void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_after) override;

    private:
        tr_session& session_;
    };

    LpdMediator lpd_mediator_{ *this };

    std::shared_ptr<event_base> const event_base_;
    std::shared_ptr<evdns_base> const evdns_base_;
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

public:
    struct struct_utp_context* utp_context = nullptr;
    std::unique_ptr<libtransmission::Timer> utp_timer;
};

constexpr bool tr_isPriority(tr_priority_t p)
{
    return p == TR_PRI_LOW || p == TR_PRI_NORMAL || p == TR_PRI_HIGH;
}
