// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
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
struct tr_bandwidth;
struct tr_address;
struct tr_announcer;
struct tr_announcer_udp;
struct tr_bindsockets;
struct BlocklistFile;
struct tr_fdInfo;

struct tr_bindinfo
{
    tr_socket_t socket = TR_BAD_SOCKET;
    tr_address addr = {};
    struct event* ev = nullptr;
};

struct tr_turtle_info
{
    /* TR_UP and TR_DOWN speed limits */
    unsigned int speedLimit_Bps[2];

    /* is turtle mode on right now? */
    bool isEnabled;

    /* does turtle mode turn itself on and off at given times? */
    bool isClockEnabled;

    /* when clock mode is on, minutes after midnight to turn on turtle mode */
    int beginMinute;

    /* when clock mode is on, minutes after midnight to turn off turtle mode */
    int endMinute;

    /* only use clock mode on these days of the week */
    tr_sched_day days;

    /* called when isEnabled changes */
    tr_altSpeedFunc callback;

    /* the callback's user_data argument */
    void* callbackUserData;

    /* the callback's changedByUser argument.
     * indicates whether the change came from the user or from the clock. */
    bool changedByUser;

    /* bitfield of all the minutes in a week.
     * Each bit's value indicates whether the scheduler wants turtle
     * limits on or off at that given minute in the week. */
    // Changed to non-owning pointer temporarily till tr_turtle_info becomes C++-constructible and destructible
    // TODO: remove * and own the value
    tr_bitfield* minutes = nullptr;

    /* recent action that was done by turtle's automatic switch */
    tr_auto_switch_state_t autoTurtleState;
};

/** @brief handle to an active libtransmission session */
struct tr_session
{
public:
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

    [[nodiscard]] constexpr auto isClosing() const noexcept
    {
        return is_closing_;
    }

    // download dir

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

    //

    [[nodiscard]] constexpr auto& openFiles() noexcept
    {
        return open_files_;
    }

    void closeTorrentFiles(tr_torrent* tor) noexcept;
    void closeTorrentFile(tr_torrent* tor, tr_file_index_t file_num) noexcept;

    [[nodiscard]] auto maxObservedDlSpeedBps() const noexcept
    {
        return max_observed_dl_speed_Bps_;
    }

public:
    static constexpr std::array<std::tuple<tr_quark, tr_quark, TrScript>, 3> Scripts{
        { { TR_KEY_script_torrent_added_enabled, TR_KEY_script_torrent_added_filename, TR_SCRIPT_ON_TORRENT_ADDED },
          { TR_KEY_script_torrent_done_enabled, TR_KEY_script_torrent_done_filename, TR_SCRIPT_ON_TORRENT_DONE },
          { TR_KEY_script_torrent_done_seeding_enabled,
            TR_KEY_script_torrent_done_seeding_filename,
            TR_SCRIPT_ON_TORRENT_DONE_SEEDING } }
    };

    bool isPortRandom = false;
    bool isPexEnabled = false;
    bool isDHTEnabled = false;
    bool isUTPEnabled = false;
    bool isLPDEnabled = false;
    bool isPrefetchEnabled = false;
    bool is_closing_ = false;
    bool isClosed = false;
    bool isRatioLimited = false;
    bool isIdleLimited = false;
    bool isIncompleteFileNamingEnabled = false;
    bool pauseAddedTorrent = false;
    bool deleteSourceTorrent = false;
    bool scrapePausedTorrents = false;

    uint8_t peer_id_ttl_hours = 0;

    bool stalledEnabled = false;
    bool queueEnabled[2] = { false, false };
    int queueSize[2] = { 0, 0 };
    int queueStalledMinutes = 0;

    int umask = 0;

    unsigned int speedLimit_Bps[2] = { 0, 0 };
    bool speedLimitEnabled[2] = { false, false };

    struct tr_turtle_info turtle;

    int magicNumber = 0;

    tr_encryption_mode encryptionMode;

    tr_preallocation_mode preallocationMode;

    struct event_base* event_base = nullptr;
    struct evdns_base* evdns_base = nullptr;
    struct tr_event_handle* events = nullptr;

    uint16_t peerCount = 0;
    uint16_t peerLimit = 200;
    uint16_t peerLimitPerTorrent = 50;

    int uploadSlotsPerTorrent = 0;

    /* The UDP sockets used for the DHT and uTP. */
    tr_port udp_port;
    tr_socket_t udp_socket = TR_BAD_SOCKET;
    tr_socket_t udp6_socket = TR_BAD_SOCKET;
    unsigned char* udp6_bound = nullptr;
    struct event* udp_event = nullptr;
    struct event* udp6_event = nullptr;

    struct event* utp_timer = nullptr;

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

    tr_port randomPortLow;
    tr_port randomPortHigh;

    std::string config_dir;
    std::string resume_dir;
    std::string torrent_dir;

    std::vector<std::unique_ptr<BlocklistFile>> blocklists;
    struct tr_peerMgr* peerMgr = nullptr;
    struct tr_shared* shared = nullptr;

    std::unique_ptr<Cache> cache;

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
        [[nodiscard]] unsigned int clamp(int bandwidth_tag, unsigned int byte_count) const override;
        void notifyBandwidthConsumed(int torrent_id, size_t byte_count) override;
        // runs the tr_web::fetch response callback in the libtransmission thread
        void run(tr_web::FetchDoneFunc&& func, tr_web::FetchResponse&& response) const override;

    private:
        tr_session* const session_;
    };

    WebMediator web_mediator{ this };
    std::unique_ptr<tr_web> web;

    struct tr_session_id* session_id;

    tr_rpc_func rpc_func;
    void* rpc_func_user_data;

    struct tr_stats_handle* sessionStats;

    struct tr_announcer* announcer;
    struct tr_announcer_udp* announcer_udp;

    struct event* nowTimer;
    struct event* saveTimer;

    // monitors the "global pool" speeds
    tr_bandwidth top_bandwidth_;

    std::vector<std::pair<tr_interned_string, std::unique_ptr<tr_bandwidth>>> bandwidth_groups_;

    float desiredRatio;

    uint16_t idleLimitMinutes;

    struct tr_bindinfo* bind_ipv4;
    struct tr_bindinfo* bind_ipv6;

    std::unique_ptr<tr_rpc_server> rpc_server_;

    tr_announce_list default_trackers_;

    // One of <netinet/ip.h>'s IPTOS_ values.
    // See tr_netTos*() in libtransmission/net.h for more info
    // Only session.cc should use this.
    int peer_socket_tos_ = *tr_netTosFromName(TR_DEFAULT_PEER_SOCKET_TOS_STR);

    uint32_t max_observed_dl_speed_Bps_ = 0;

private:
    static std::recursive_mutex session_mutex_;

    tr_torrents torrents_;

    std::array<std::string, TR_SCRIPT_N_TYPES> scripts_;
    std::string blocklist_url_;
    std::string download_dir_;
    std::string default_trackers_str_;
    std::string incomplete_dir_;
    std::string peer_congestion_algorithm_;
    std::optional<tr_address> external_ip_;

    std::array<bool, TR_SCRIPT_N_TYPES> scripts_enabled_;
    bool blocklist_enabled_ = false;
    bool incomplete_dir_enabled_ = false;

    tr_open_files open_files_;
};

bool tr_sessionAllowsDHT(tr_session const* session);

bool tr_sessionAllowsLPD(tr_session const* session);

bool tr_sessionIsAddressBlocked(tr_session const* session, struct tr_address const* addr);

struct tr_address const* tr_sessionGetPublicAddress(tr_session const* session, int tr_af_type, bool* is_default_value);

struct tr_bindsockets* tr_sessionGetBindSockets(tr_session*);

int tr_sessionCountTorrents(tr_session const* session);

std::vector<tr_torrent*> tr_sessionGetTorrents(tr_session* session);

enum
{
    SESSION_MAGIC_NUMBER = 3845,
};

constexpr bool tr_isSession(tr_session const* session)
{
    return session != nullptr && session->magicNumber == SESSION_MAGIC_NUMBER;
}

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

std::vector<tr_torrent*> tr_sessionGetNextQueuedTorrents(tr_session* session, tr_direction dir, size_t numwanted);

int tr_sessionCountQueueFreeSlots(tr_session* session, tr_direction);
