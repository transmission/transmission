// This file Copyright © 2008-2023 Mnemosyne LLC.
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
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility> // for std::pair
#include <vector>

#include <event2/util.h> // for evutil_socket_t

#include "transmission.h"

#include "announce-list.h"
#include "announcer.h"
#include "bandwidth.h"
#include "bitfield.h"
#include "cache.h"
#include "interned-string.h"
#include "net.h" // tr_socket_t
#include "open-files.h"
#include "port-forwarding.h"
#include "quark.h"
#include "session-alt-speeds.h"
#include "session-id.h"
#include "session-settings.h"
#include "session-thread.h"
#include "stats.h"
#include "torrents.h"
#include "tr-dht.h"
#include "tr-lpd.h"
#include "utils-ev.h"
#include "verify.h"
#include "web.h"

tr_peer_id_t tr_peerIdInit();

struct event_base;

class tr_lpd;
class tr_peer_socket;
class tr_port_forwarding;
class tr_rpc_server;
class tr_session_thread;
class tr_web;
struct struct_utp_context;
struct tr_variant;

namespace libtransmission
{
class Blocklist;
class Dns;
class Timer;
class TimerMaker;
} // namespace libtransmission

namespace libtransmission::test
{

class SessionTest;

} // namespace libtransmission::test

/** @brief handle to an active libtransmission session */
struct tr_session
{
private:
    class BoundSocket
    {
    public:
        using IncomingCallback = void (*)(tr_socket_t, void*);
        BoundSocket(struct event_base* base, tr_address const& addr, tr_port port, IncomingCallback cb, void* cb_data);
        BoundSocket(BoundSocket&&) = delete;
        BoundSocket(BoundSocket const&) = delete;
        BoundSocket operator=(BoundSocket&&) = delete;
        BoundSocket operator=(BoundSocket const&) = delete;
        ~BoundSocket();

    private:
        static void onCanRead(evutil_socket_t fd, short /*what*/, void* vself)
        {
            auto* const self = static_cast<BoundSocket*>(vself);
            self->cb_(fd, self->cb_data_);
        }

        IncomingCallback cb_;
        void* cb_data_;
        tr_socket_t socket_ = TR_BAD_SOCKET;
        libtransmission::evhelpers::event_unique_ptr ev_;
    };

    class AltSpeedMediator final : public tr_session_alt_speeds::Mediator
    {
    public:
        explicit AltSpeedMediator(tr_session& session) noexcept
            : session_{ session }
        {
        }

        void isActiveChanged(bool is_active, tr_session_alt_speeds::ChangeReason reason) override;

        [[nodiscard]] time_t time() override;

        ~AltSpeedMediator() noexcept override = default;

    private:
        tr_session& session_;
    };

    class AnnouncerUdpMediator final : public tr_announcer_udp::Mediator
    {
    public:
        explicit AnnouncerUdpMediator(tr_session& session) noexcept
            : session_{ session }
        {
        }

        ~AnnouncerUdpMediator() noexcept override = default;

        void sendto(void const* buf, size_t buflen, sockaddr const* addr, socklen_t addrlen) override
        {
            session_.udp_core_->sendto(buf, buflen, addr, addrlen);
        }

        [[nodiscard]] std::optional<tr_address> announceIP() const override
        {
            if (!session_.useAnnounceIP())
            {
                return {};
            }

            return tr_address::from_string(session_.announceIP());
        }

    private:
        tr_session& session_;
    };

    class DhtMediator : public tr_dht::Mediator
    {
    public:
        DhtMediator(tr_session& session) noexcept
            : session_{ session }
        {
        }

        ~DhtMediator() noexcept override = default;

        [[nodiscard]] std::vector<tr_torrent_id_t> torrentsAllowingDHT() const override;

        [[nodiscard]] tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t id) const override;

        [[nodiscard]] std::string_view configDir() const override
        {
            return session_.config_dir_;
        }

        [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
        {
            return session_.timerMaker();
        }

        void addPex(tr_sha1_digest_t const&, tr_pex const* pex, size_t n_pex) override;

    private:
        tr_session& session_;
    };

    class PortForwardingMediator final : public tr_port_forwarding::Mediator
    {
    public:
        explicit PortForwardingMediator(tr_session& session) noexcept
            : session_{ session }
        {
        }

        [[nodiscard]] tr_address incomingPeerAddress() const override
        {
            return session_.publicAddress(TR_AF_INET).address;
        }

        [[nodiscard]] tr_port localPeerPort() const override
        {
            return session_.localPeerPort();
        }

        [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
        {
            return session_.timerMaker();
        }

        void onPortForwarded(tr_port public_port) override
        {
            if (session_.advertised_peer_port_ != public_port)
            {
                session_.advertised_peer_port_ = public_port;
                session_.onAdvertisedPeerPortChanged();
            }
        }

    private:
        tr_session& session_;
    };

    class WebMediator final : public tr_web::Mediator
    {
    public:
        explicit WebMediator(tr_session* session) noexcept
            : session_{ session }
        {
        }

        [[nodiscard]] std::optional<std::string> cookieFile() const override;
        [[nodiscard]] std::optional<std::string> publicAddressV4() const override;
        [[nodiscard]] std::optional<std::string> publicAddressV6() const override;
        [[nodiscard]] std::optional<std::string_view> userAgent() const override;
        [[nodiscard]] size_t clamp(int torrent_id, size_t byte_count) const override;
        [[nodiscard]] time_t now() const override;
        void notifyBandwidthConsumed(int torrent_id, size_t byte_count) override;
        // runs the tr_web::fetch response callback in the libtransmission thread
        void run(tr_web::FetchDoneFunc&& func, tr_web::FetchResponse&& response) const override;

    private:
        tr_session* const session_;
    };

    class LpdMediator final : public tr_lpd::Mediator
    {
    public:
        explicit LpdMediator(tr_session& session) noexcept
            : session_{ session }
        {
        }

        [[nodiscard]] tr_address bind_address(tr_address_type type) const override
        {
            return session_.publicAddress(type).address;
        }

        [[nodiscard]] tr_port port() const override
        {
            return session_.advertisedPeerPort();
        }

        [[nodiscard]] bool allowsLPD() const override
        {
            return session_.allowsLPD();
        }

        [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
        {
            return session_.timerMaker();
        }

        [[nodiscard]] std::vector<TorrentInfo> torrents() const override;

        bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) override;

        void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_after) override;

    private:
        tr_session& session_;
    };

    // UDP connectivity used for the DHT and µTP
    class tr_udp_core
    {
    public:
        tr_udp_core(tr_session& session, tr_port udp_port);
        ~tr_udp_core();

        void sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t tolen) const;

        [[nodiscard]] constexpr auto socket4() const noexcept
        {
            return udp4_socket_;
        }

        [[nodiscard]] constexpr auto socket6() const noexcept
        {
            return udp6_socket_;
        }

    private:
        tr_port const udp_port_;
        tr_session& session_;
        tr_socket_t udp4_socket_ = TR_BAD_SOCKET;
        tr_socket_t udp6_socket_ = TR_BAD_SOCKET;
        libtransmission::evhelpers::event_unique_ptr udp4_event_;
        libtransmission::evhelpers::event_unique_ptr udp6_event_;
    };

public:
    explicit tr_session(std::string_view config_dir, tr_variant* settings_dict = nullptr);

    [[nodiscard]] std::string_view sessionId() const noexcept
    {
        return session_id_.sv();
    }

    [[nodiscard]] libtransmission::TimerMaker& timerMaker() noexcept
    {
        return *timer_maker_;
    }

    [[nodiscard]] auto amInSessionThread() const noexcept
    {
        return session_thread_->amInSessionThread();
    }

    void runInSessionThread(std::function<void(void)>&& func)
    {
        session_thread_->run(std::move(func));
    }

    template<typename Func, typename... Args>
    void runInSessionThread(Func&& func, Args&&... args)
    {
        session_thread_->run(std::forward<Func&&>(func), std::forward<Args>(args)...);
    }

    [[nodiscard]] auto eventBase() noexcept
    {
        return session_thread_->eventBase();
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
        return settings_.download_dir;
    }

    void setDownloadDir(std::string_view dir)
    {
        settings_.download_dir = dir;
    }

    // default trackers
    // (trackers to apply automatically to public torrents)

    [[nodiscard]] constexpr auto const& defaultTrackersStr() const noexcept
    {
        return settings_.default_trackers_str;
    }

    [[nodiscard]] constexpr auto const& defaultTrackers() const noexcept
    {
        return default_trackers_;
    }

    void setDefaultTrackers(std::string_view trackers);

    // incomplete dir

    [[nodiscard]] constexpr auto const& incompleteDir() const noexcept
    {
        return settings_.incomplete_dir;
    }

    void setIncompleteDir(std::string_view dir)
    {
        settings_.incomplete_dir = dir;
    }

    [[nodiscard]] constexpr auto useIncompleteDir() const noexcept
    {
        return settings_.incomplete_dir_enabled;
    }

    constexpr void useIncompleteDir(bool enabled) noexcept
    {
        settings_.incomplete_dir_enabled = enabled;
    }

    // scripts

    constexpr void useScript(TrScript i, bool enabled)
    {
        scriptEnabledFlag(i) = enabled;
    }

    [[nodiscard]] constexpr bool useScript(TrScript i) const
    {
        return const_cast<tr_session*>(this)->scriptEnabledFlag(i);
    }

    void setScript(TrScript i, std::string_view path)
    {
        scriptFilename(i) = path;
    }

    [[nodiscard]] constexpr auto const& script(TrScript i) const
    {
        return const_cast<tr_session*>(this)->scriptFilename(i);
    }

    // blocklist

    [[nodiscard]] constexpr auto useBlocklist() const noexcept
    {
        return settings_.blocklist_enabled;
    }

    void useBlocklist(bool enabled);

    [[nodiscard]] constexpr auto const& blocklistUrl() const noexcept
    {
        return settings_.blocklist_url;
    }

    void setBlocklistUrl(std::string_view url)
    {
        settings_.blocklist_url = url;
    }

    // RPC

    void setRpcWhitelist(std::string_view whitelist) const;

    void useRpcWhitelist(bool enabled) const;

    [[nodiscard]] bool useRpcWhitelist() const;

    void setExternalIP(tr_address external_ip)
    {
        external_ip_ = external_ip;
    }

    // peer networking

    [[nodiscard]] constexpr auto const& peerCongestionAlgorithm() const noexcept
    {
        return settings_.peer_congestion_algorithm;
    }

    void setPeerCongestionAlgorithm(std::string_view algorithm)
    {
        settings_.peer_congestion_algorithm = algorithm;
    }

    void setSocketTOS(tr_socket_t sock, tr_address_type type) const
    {
        tr_netSetTOS(sock, settings_.peer_socket_tos, type);
    }

    [[nodiscard]] constexpr auto peerLimit() const noexcept
    {
        return settings_.peer_limit_global;
    }

    [[nodiscard]] constexpr auto peerLimitPerTorrent() const noexcept
    {
        return settings_.peer_limit_per_torrent;
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

    [[nodiscard]] constexpr std::string const& announceIP() const noexcept
    {
        return settings_.announce_ip;
    }

    void setAnnounceIP(std::string_view ip)
    {
        settings_.announce_ip = ip;
    }

    [[nodiscard]] constexpr bool useAnnounceIP() const noexcept
    {
        return settings_.announce_ip_enabled;
    }

    constexpr void useAnnounceIP(bool enabled) noexcept
    {
        settings_.announce_ip_enabled = enabled;
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

    [[nodiscard]] constexpr auto& stats() noexcept
    {
        return session_stats_;
    }

    [[nodiscard]] constexpr auto const& stats() const noexcept
    {
        return session_stats_;
    }

    constexpr void addUploaded(uint32_t n_bytes) noexcept
    {
        stats().addUploaded(n_bytes);
    }

    constexpr void addDownloaded(uint32_t n_bytes) noexcept
    {
        stats().addDownloaded(n_bytes);
    }

    constexpr void addFileCreated() noexcept
    {
        stats().addFileCreated();
    }

    // The incoming peer port that's been opened on the local machine
    // that Transmission is running on.
    [[nodiscard]] constexpr tr_port localPeerPort() const noexcept
    {
        return local_peer_port_;
    }

    [[nodiscard]] constexpr tr_port udpPort() const noexcept
    {
        // Always use the same port number that's used for incoming TCP connections.
        // This simplifies port forwarding and reduces the chance of confusion,
        // since incoming UDP and TCP connections will use the same port number
        return localPeerPort();
    }

    // The incoming peer port that's been opened on the public-facing
    // device. This is usually the same as localPeerPort() but can differ,
    // e.g. if the public device is a router that chose to use a different
    // port than the one requested by Transmission.
    [[nodiscard]] constexpr tr_port advertisedPeerPort() const noexcept
    {
        return advertised_peer_port_;
    }

    [[nodiscard]] constexpr auto queueEnabled(tr_direction dir) const noexcept
    {
        return dir == TR_DOWN ? settings_.download_queue_enabled : settings_.seed_queue_enabled;
    }

    [[nodiscard]] constexpr auto queueSize(tr_direction dir) const noexcept
    {
        return dir == TR_DOWN ? settings_.download_queue_size : settings_.seed_queue_size;
    }

    [[nodiscard]] constexpr auto queueStalledEnabled() const noexcept
    {
        return settings_.queue_stalled_enabled;
    }

    [[nodiscard]] constexpr auto queueStalledMinutes() const noexcept
    {
        return settings_.queue_stalled_minutes;
    }

    [[nodiscard]] constexpr auto uploadSlotsPerTorrent() const noexcept
    {
        return settings_.upload_slots_per_torrent;
    }

    [[nodiscard]] constexpr auto isClosing() const noexcept
    {
        return is_closing_;
    }

    [[nodiscard]] constexpr auto encryptionMode() const noexcept
    {
        return settings_.encryption_mode;
    }

    [[nodiscard]] constexpr auto preallocationMode() const noexcept
    {
        return settings_.preallocation_mode;
    }

    [[nodiscard]] constexpr auto shouldScrapePausedTorrents() const noexcept
    {
        return settings_.should_scrape_paused_torrents;
    }

    [[nodiscard]] constexpr auto shouldPauseAddedTorrents() const noexcept
    {
        return !settings_.should_start_added_torrents;
    }

    [[nodiscard]] constexpr auto shouldFullyVerifyAddedTorrents() const noexcept
    {
        return settings_.torrent_added_verify_mode == TR_VERIFY_ADDED_FULL;
    }

    [[nodiscard]] constexpr auto shouldDeleteSource() const noexcept
    {
        return settings_.should_delete_source_torrents;
    }

    [[nodiscard]] constexpr auto allowsDHT() const noexcept
    {
        return settings_.dht_enabled;
    }

    [[nodiscard]] constexpr bool allowsLPD() const noexcept
    {
        return settings_.lpd_enabled;
    }

    [[nodiscard]] constexpr auto allowsPEX() const noexcept
    {
        return settings_.pex_enabled;
    }

    [[nodiscard]] constexpr auto allowsTCP() const noexcept
    {
        return settings_.tcp_enabled;
    }

    [[nodiscard]] bool allowsUTP() const noexcept;

    [[nodiscard]] constexpr auto allowsPrefetch() const noexcept
    {
        return settings_.is_prefetch_enabled;
    }

    [[nodiscard]] constexpr auto isIdleLimited() const noexcept
    {
        return settings_.idle_seeding_limit_enabled;
    }

    [[nodiscard]] constexpr auto idleLimitMinutes() const noexcept
    {
        return settings_.idle_seeding_limit_minutes;
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
        bool is_any_addr;
    };

    [[nodiscard]] PublicAddressResult publicAddress(tr_address_type type) const noexcept;

    [[nodiscard]] constexpr auto speedLimitKBps(tr_direction dir) const noexcept
    {
        return dir == TR_DOWN ? settings_.speed_limit_down : settings_.speed_limit_up;
    }

    [[nodiscard]] constexpr auto isSpeedLimited(tr_direction dir) const noexcept
    {
        return dir == TR_DOWN ? settings_.speed_limit_down_enabled : settings_.speed_limit_up_enabled;
    }

    [[nodiscard]] auto pieceSpeedBps(tr_direction dir) const noexcept
    {
        return top_bandwidth_.getPieceSpeedBytesPerSecond(0, dir);
    }

    [[nodiscard]] std::optional<tr_bytes_per_second_t> activeSpeedLimitBps(tr_direction dir) const noexcept;

    [[nodiscard]] constexpr auto isIncompleteFileNamingEnabled() const noexcept
    {
        return settings_.is_incomplete_file_naming_enabled;
    }

    [[nodiscard]] constexpr auto isPortRandom() const noexcept
    {
        return settings_.peer_port_random_on_start;
    }

    [[nodiscard]] constexpr auto isRatioLimited() const noexcept
    {
        return settings_.ratio_limit_enabled;
    }

    [[nodiscard]] constexpr auto desiredRatio() const noexcept
    {
        return settings_.ratio_limit;
    }

    void verifyRemove(tr_torrent* tor)
    {
        if (verifier_)
        {
            verifier_->remove(tor);
        }
    }

    void verifyAdd(tr_torrent* tor)
    {
        if (verifier_)
        {
            verifier_->add(tor);
        }
    }

    void fetch(tr_web::FetchOptions&& options) const
    {
        if (web_)
        {
            web_->fetch(std::move(options));
        }
    }

    [[nodiscard]] constexpr auto const& bandwidthGroups() const noexcept
    {
        return bandwidth_groups_;
    }

    void addIncoming(tr_peer_socket&& socket);

    void addTorrent(tr_torrent* tor);

    void addDhtNode(tr_address const& addr, tr_port port)
    {
        if (dht_)
        {
            dht_->addNode(addr, port);
        }
    }

private:
    constexpr bool& scriptEnabledFlag(TrScript i)
    {
        if (i == TR_SCRIPT_ON_TORRENT_ADDED)
        {
            return settings_.script_torrent_added_enabled;
        }

        if (i == TR_SCRIPT_ON_TORRENT_DONE)
        {
            return settings_.script_torrent_done_enabled;
        }

        return settings_.script_torrent_done_seeding_enabled;
    }

    constexpr std::string& scriptFilename(TrScript i)
    {
        if (i == TR_SCRIPT_ON_TORRENT_ADDED)
        {
            return settings_.script_torrent_added_filename;
        }

        if (i == TR_SCRIPT_ON_TORRENT_DONE)
        {
            return settings_.script_torrent_done_filename;
        }

        return settings_.script_torrent_done_seeding_filename;
    }

    [[nodiscard]] tr_port randomPort() const;

    void onAdvertisedPeerPortChanged();

    struct init_data;
    void initImpl(init_data&);
    void setSettings(tr_variant* settings_dict, bool force);
    void setSettings(tr_session_settings&& settings, bool force);

    void closeImplPart1(std::promise<void>* closed_promise, std::chrono::time_point<std::chrono::steady_clock> deadline);
    void closeImplPart2(std::promise<void>* closed_promise, std::chrono::time_point<std::chrono::steady_clock> deadline);

    void onNowTimer();

    static void onIncomingPeerConnection(tr_socket_t fd, void* vsession);

    friend class libtransmission::test::SessionTest;
    friend struct tr_bindinfo;

    friend bool tr_blocklistExists(tr_session const* session);
    friend bool tr_sessionGetAntiBruteForceEnabled(tr_session const* session);
    friend bool tr_sessionIsPortForwardingEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCEnabled(tr_session const* session);
    friend bool tr_sessionIsRPCPasswordEnabled(tr_session const* session);
    friend bool tr_sessionUsesAltSpeed(tr_session const* session);
    friend bool tr_sessionUsesAltSpeedTime(tr_session const* session);
    friend char const* tr_sessionGetRPCPassword(tr_session const* session);
    friend char const* tr_sessionGetRPCUsername(tr_session const* session);
    friend char const* tr_sessionGetRPCWhitelist(tr_session const* session);
    friend int tr_sessionGetAntiBruteForceThreshold(tr_session const* session);
    friend size_t tr_blocklistGetRuleCount(tr_session const* session);
    friend size_t tr_blocklistSetContent(tr_session* session, char const* content_filename);
    friend size_t tr_sessionGetAltSpeedBegin(tr_session const* session);
    friend size_t tr_sessionGetAltSpeedEnd(tr_session const* session);
    friend size_t tr_sessionGetCacheLimit_MB(tr_session const* session);
    friend tr_kilobytes_per_second_t tr_sessionGetAltSpeed_KBps(tr_session const* session, tr_direction dir);
    friend tr_kilobytes_per_second_t tr_sessionGetSpeedLimit_KBps(tr_session const* session, tr_direction dir);
    friend tr_port_forwarding_state tr_sessionGetPortForwarding(tr_session const* session);
    friend tr_sched_day tr_sessionGetAltSpeedDay(tr_session const* session);
    friend tr_session* tr_sessionInit(char const* config_dir, bool message_queueing_enabled, tr_variant* client_settings);
    friend uint16_t tr_sessionGetPeerPort(tr_session const* session);
    friend uint16_t tr_sessionGetRPCPort(tr_session const* session);
    friend uint16_t tr_sessionSetPeerPortRandom(tr_session* session);
    friend void tr_sessionClose(tr_session* session, size_t timeout_secs);
    friend void tr_sessionGetSettings(tr_session const* s, tr_variant* setme_dictionary);
    friend void tr_sessionLimitSpeed(tr_session* session, tr_direction dir, bool limited);
    friend void tr_sessionReloadBlocklists(tr_session* session);
    friend void tr_sessionSet(tr_session* session, tr_variant* settings);
    friend void tr_sessionSetAltSpeedBegin(tr_session* session, size_t minutes_since_midnight);
    friend void tr_sessionSetAltSpeedDay(tr_session* session, tr_sched_day days);
    friend void tr_sessionSetAltSpeedEnd(tr_session* session, size_t minutes_since_midnight);
    friend void tr_sessionSetAltSpeedFunc(tr_session* session, tr_altSpeedFunc func, void* user_data);
    friend void tr_sessionSetAltSpeed_KBps(tr_session* session, tr_direction dir, tr_bytes_per_second_t limit);
    friend void tr_sessionSetAntiBruteForceEnabled(tr_session* session, bool is_enabled);
    friend void tr_sessionSetAntiBruteForceThreshold(tr_session* session, int max_bad_requests);
    friend void tr_sessionSetCacheLimit_MB(tr_session* session, size_t mb);
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
    friend void tr_sessionSetPeerPort(tr_session* session, uint16_t hport);
    friend void tr_sessionSetPeerPortRandomOnStart(tr_session* session, bool random);
    friend void tr_sessionSetPexEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetPortForwardingEnabled(tr_session* session, bool enabled);
    friend void tr_sessionSetQueueEnabled(tr_session* session, tr_direction dir, bool do_limit_simultaneous_torrents);
    friend void tr_sessionSetQueueSize(tr_session* session, tr_direction dir, size_t max_simultaneous_torrents);
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
    friend void tr_sessionSetSpeedLimit_KBps(tr_session* session, tr_direction dir, tr_kilobytes_per_second_t limit);
    friend void tr_sessionSetUTPEnabled(tr_session* session, bool enabled);
    friend void tr_sessionUseAltSpeed(tr_session* session, bool enabled);
    friend void tr_sessionUseAltSpeedTime(tr_session* session, bool enabled);

public:
    /// constexpr fields

    static constexpr std::array<std::tuple<tr_quark, tr_quark, TrScript>, 3> Scripts{
        { { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_filename, TR_SCRIPT_ON_TORRENT_ADDED },
          { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_filename, TR_SCRIPT_ON_TORRENT_DONE },
          { TR_KEY_script_torrent_done_seeding_enabled,
            TR_KEY_script_torrent_done_seeding_filename,
            TR_SCRIPT_ON_TORRENT_DONE_SEEDING } }
    };

private:
    /// const fields

    std::string const config_dir_;
    std::string const resume_dir_;
    std::string const torrent_dir_;
    std::string const blocklist_dir_;

    std::unique_ptr<tr_session_thread> const session_thread_;

    // depends-on: session_thread_
    std::unique_ptr<libtransmission::TimerMaker> const timer_maker_;

    /// trivial type fields

    tr_session_settings settings_;
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

    tr_rpc_func rpc_func_ = nullptr;
    void* rpc_func_user_data_ = nullptr;

    tr_altSpeedFunc alt_speed_active_changed_func_ = nullptr;
    void* alt_speed_active_changed_func_user_data_ = nullptr;

    // The local peer port that we bind a socket to for listening
    // to incoming peer connections. Usually the same as
    // `settings_.peer_port` but can differ if
    // `settings_.peer_port_random_on_start` is enabled.
    tr_port local_peer_port_;

    // The incoming peer port that's been opened on the public-facing
    // device. This is usually the same as localPeerPort() but can differ,
    // e.g. if the public device is a router that chose to use a different
    // port than the one requested by Transmission.
    tr_port advertised_peer_port_;

    bool is_closing_ = false;

    /// fields that aren't trivial,
    /// but are self-contained / don't hold references to others

    // used during shutdown:
    // how many &event=stopped announces are still being sent to trackers
    std::atomic<size_t> n_pending_stops_ = {};

    mutable std::recursive_mutex session_mutex_;

    tr_stats session_stats_{ config_dir_, time(nullptr) };

    tr_announce_list default_trackers_;

    tr_session_id session_id_;

    tr_open_files open_files_;

    std::vector<libtransmission::Blocklist> blocklists_;

    /// other fields

    // depends-on: session_thread_, settings_.bind_address_ipv4, local_peer_port_
    std::optional<BoundSocket> bound_ipv4_;

    // depends-on: session_thread_, settings_.bind_address_ipv6, local_peer_port_
    std::optional<BoundSocket> bound_ipv6_;

public:
    // depends-on: settings_, announcer_udp_
    // FIXME(ckerr): circular dependency udp_core -> announcer_udp -> announcer_udp_mediator -> udp_core
    std::unique_ptr<tr_udp_core> udp_core_;

    // monitors the "global pool" speeds
    tr_bandwidth top_bandwidth_;

private:
    // depends-on: top_bandwidth_
    std::vector<std::pair<tr_interned_string, std::unique_ptr<tr_bandwidth>>> bandwidth_groups_;

    // depends-on: timer_maker_, settings_, local_peer_port_
    PortForwardingMediator port_forwarding_mediator_{ *this };
    std::unique_ptr<tr_port_forwarding> port_forwarding_ = tr_port_forwarding::create(port_forwarding_mediator_);

    // depends-on: session_thread_, top_bandwidth_
    AltSpeedMediator alt_speed_mediator_{ *this };
    tr_session_alt_speeds alt_speeds_{ alt_speed_mediator_ };

public:
    // depends-on: udp_core_
    struct struct_utp_context* utp_context = nullptr;

private:
    // depends-on: open_files_
    tr_torrents torrents_;

    // depends-on: settings_, session_thread_, torrents_
    WebMediator web_mediator_{ this };
    std::unique_ptr<tr_web> web_ = tr_web::create(this->web_mediator_);

public:
    // depends-on: settings_, open_files_, torrents_
    std::unique_ptr<Cache> cache = std::make_unique<Cache>(torrents_, 1024 * 1024 * 2);

private:
    // depends-on: timer_maker_, top_bandwidth_, utp_context, torrents_, web_
    std::unique_ptr<struct tr_peerMgr, void (*)(struct tr_peerMgr*)> peer_mgr_;

    // depends-on: peer_mgr_, advertised_peer_port_, torrents_
    LpdMediator lpd_mediator_{ *this };

    // depends-on: lpd_mediator_
    std::unique_ptr<tr_lpd> lpd_;

    // depends-on: udp_core_
    AnnouncerUdpMediator announcer_udp_mediator_{ *this };

    // depends-on: timer_maker_, torrents_, peer_mgr_
    DhtMediator dht_mediator_{ *this };

public:
    // depends-on: announcer_udp_mediator_
    std::unique_ptr<tr_announcer_udp> announcer_udp_ = tr_announcer_udp::create(announcer_udp_mediator_);

    // depends-on: settings_, torrents_, web_, announcer_udp_
    std::unique_ptr<tr_announcer> announcer_ = tr_announcer::create(this, *announcer_udp_, n_pending_stops_);

    // depends-on: public_peer_port_, udp_core_, dht_mediator_
    std::unique_ptr<tr_dht> dht_;

private:
    // depends-on: session_thread_, timer_maker_, settings_, torrents_, web_
    std::unique_ptr<tr_rpc_server> rpc_server_;

    // depends-on: alt_speeds_, udp_core_, torrents_
    std::unique_ptr<libtransmission::Timer> now_timer_;

    // depends-on: torrents_
    std::unique_ptr<libtransmission::Timer> save_timer_;

    std::unique_ptr<tr_verify_worker> verifier_ = std::make_unique<tr_verify_worker>();

public:
    std::unique_ptr<libtransmission::Timer> utp_timer;
};

constexpr bool tr_isPriority(tr_priority_t p)
{
    return p == TR_PRI_LOW || p == TR_PRI_NORMAL || p == TR_PRI_HIGH;
}
