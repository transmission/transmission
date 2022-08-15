// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::partial_sort(), std::min(), std::max()
#include <cerrno> /* ENOENT */
#include <climits> /* INT_MAX */
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdlib> // atoi()
#include <ctime>
#include <iterator> // for std::back_inserter
#include <list>
#include <memory>
#include <numeric> // for std::accumulate()
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <sys/types.h> /* umask() */
#include <sys/stat.h> /* umask() */
#endif

#include <event2/dns.h>
#include <event2/event.h>

#include <fmt/chrono.h>
#include <fmt/core.h>
#include <fmt/format.h> // fmt::ptr

#include "transmission.h"

#include "announcer.h"
#include "bandwidth.h"
#include "blocklist.h"
#include "cache.h"
#include "crypto-utils.h"
#include "error-types.h"
#include "error.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "port-forwarding.h"
#include "rpc-server.h"
#include "session-id.h"
#include "session.h"
#include "timer-ev.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-dht.h" /* tr_dhtUpkeep() */
#include "tr-lpd.h"
#include "tr-strbuf.h"
#include "tr-udp.h"
#include "tr-utp.h"
#include "trevent.h"
#include "utils.h"
#include "variant.h"
#include "verify.h"
#include "version.h"
#include "web.h"

using namespace std::literals;

std::recursive_mutex tr_session::session_mutex_;

#ifdef TR_LIGHTWEIGHT
static auto constexpr DefaultCacheSizeMB = int{ 2 };
static auto constexpr DefaultPrefetchEnabled = bool{ false };
#else
static auto constexpr DefaultCacheSizeMB = int{ 4 };
static auto constexpr DefaultPrefetchEnabled = bool{ true };
#endif
static auto constexpr DefaultUmask = int{ 022 };
static auto constexpr SaveIntervalSecs = 360s;

static void bandwidthGroupRead(tr_session* session, std::string_view config_dir);
static int bandwidthGroupWrite(tr_session const* session, std::string_view config_dir);
static auto constexpr BandwidthGroupsFilename = "bandwidth-groups.json"sv;

tr_port tr_session::randomPort() const
{
    auto const lower = std::min(random_port_low_.host(), random_port_high_.host());
    auto const upper = std::max(random_port_low_.host(), random_port_high_.host());
    auto const range = upper - lower;
    return tr_port::fromHost(lower + tr_rand_int_weak(range + 1));
}

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
tr_peer_id_t tr_peerIdInit()
{
    auto peer_id = tr_peer_id_t{};
    auto* it = std::data(peer_id);

    // starts with -TRXXXX-
    auto constexpr Prefix = std::string_view{ PEERID_PREFIX };
    auto const* const end = it + std::size(peer_id);
    it = std::copy_n(std::data(Prefix), std::size(Prefix), it);

    // remainder is randomly-generated characters
    auto constexpr Pool = std::string_view{ "0123456789abcdefghijklmnopqrstuvwxyz" };
    auto total = int{ 0 };
    tr_rand_buffer(it, end - it);
    while (it + 1 < end)
    {
        int const val = *it % std::size(Pool);
        total += val;
        *it++ = Pool[val];
    }
    int const val = total % std::size(Pool) != 0 ? std::size(Pool) - total % std::size(Pool) : 0;
    *it = Pool[val];

    return peer_id;
}

/***
****
***/

std::optional<std::string> tr_session::WebMediator::cookieFile() const
{
    auto const path = tr_pathbuf{ session_->configDir(), "/cookies.txt"sv };

    if (!tr_sys_path_exists(path))
    {
        return {};
    }

    return std::string{ path };
}

std::optional<std::string_view> tr_session::WebMediator::userAgent() const
{
    return TR_NAME "/" SHORT_VERSION_STRING;
}

std::optional<std::string> tr_session::WebMediator::publicAddress() const
{
    for (auto const type : { TR_AF_INET, TR_AF_INET6 })
    {
        auto const [addr, is_default_value] = session_->publicAddress(type);
        if (!is_default_value)
        {
            return addr.readable();
        }
    }

    return std::nullopt;
}

unsigned int tr_session::WebMediator::clamp(int torrent_id, unsigned int byte_count) const
{
    auto const lock = session_->unique_lock();

    auto const* const tor = session_->torrents().get(torrent_id);
    return tor == nullptr ? 0U : tor->bandwidth_.clamp(TR_DOWN, byte_count);
}

void tr_session::WebMediator::notifyBandwidthConsumed(int torrent_id, size_t byte_count)
{
    auto const lock = session_->unique_lock();

    if (auto* const tor = session_->torrents().get(torrent_id); tor != nullptr)
    {
        tor->bandwidth_.notifyBandwidthConsumed(TR_DOWN, byte_count, true, tr_time_msec());
    }
}

void tr_session::WebMediator::run(tr_web::FetchDoneFunc&& func, tr_web::FetchResponse&& response) const
{
    tr_runInEventThread(session_, std::move(func), std::move(response));
}

void tr_sessionFetch(tr_session* session, tr_web::FetchOptions&& options)
{
    session->web->fetch(std::move(options));
}

/***
****
***/

tr_encryption_mode tr_sessionGetEncryption(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->encryptionMode();
}

void tr_sessionSetEncryption(tr_session* session, tr_encryption_mode mode)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(mode == TR_ENCRYPTION_PREFERRED || mode == TR_ENCRYPTION_REQUIRED || mode == TR_CLEAR_PREFERRED);

    session->encryption_mode_ = mode;
}

/***
****
***/

void tr_bindinfo::close()
{
    if (ev_ != nullptr)
    {
        event_free(ev_);
        ev_ = nullptr;
    }

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(socket_);
        socket_ = TR_BAD_SOCKET;
    }
}

static void accept_incoming_peer(evutil_socket_t fd, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    auto clientAddr = tr_address{};
    auto clientPort = tr_port{};
    auto const clientSocket = tr_netAccept(session, fd, &clientAddr, &clientPort);

    if (clientSocket != TR_BAD_SOCKET)
    {
        tr_logAddTrace(fmt::format("new incoming connection {} ({})", clientSocket, clientAddr.readable(clientPort)));

        tr_peerMgrAddIncoming(session->peerMgr, &clientAddr, clientPort, tr_peer_socket_tcp_create(clientSocket));
    }
}

void tr_bindinfo::bindAndListenForIncomingPeers(tr_session* session)
{
    socket_ = tr_netBindTCP(&addr_, session->private_peer_port, false);

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_logAddInfo(fmt::format(
            _("Listening to incoming peer connections on {hostport}"),
            fmt::arg("hostport", addr_.readable(session->private_peer_port))));
        ev_ = event_new(session->eventBase(), socket_, EV_READ | EV_PERSIST, accept_incoming_peer, session);
        event_add(ev_, nullptr);
    }
}

static void close_incoming_peer_port(tr_session* session)
{
    session->bind_ipv4.close();
    session->bind_ipv6.close();
}

static void open_incoming_peer_port(tr_session* session)
{
    session->bind_ipv4.bindAndListenForIncomingPeers(session);

    if (tr_net_hasIPv6(session->private_peer_port))
    {
        session->bind_ipv6.bindAndListenForIncomingPeers(session);
    }
}

tr_session::PublicAddressResult tr_session::publicAddress(tr_address_type type) const noexcept
{
    switch (type)
    {
    case TR_AF_INET:
        return { bind_ipv4.addr_, bind_ipv4.addr_.readable() == TR_DEFAULT_BIND_ADDRESS_IPV4 };

    case TR_AF_INET6:
        return { bind_ipv6.addr_, bind_ipv6.addr_.readable() == TR_DEFAULT_BIND_ADDRESS_IPV6 };

    default:
        TR_ASSERT_MSG(false, "invalid type");
        return {};
    }
}

/***
****
***/

#ifdef TR_LIGHTWEIGHT
#define TR_DEFAULT_ENCRYPTION TR_CLEAR_PREFERRED
#else
#define TR_DEFAULT_ENCRYPTION TR_ENCRYPTION_PREFERRED
#endif

void tr_sessionGetDefaultSettings(tr_variant* setme_dictionary)
{
    auto* const download_dir = tr_getDefaultDownloadDir();

    auto* const d = setme_dictionary;
    TR_ASSERT(tr_variantIsDict(d));
    tr_variantDictReserve(d, 71);
    tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, false);
    tr_variantDictAddStrView(d, TR_KEY_blocklist_url, "http://www.example.com/blocklist"sv);
    tr_variantDictAddInt(d, TR_KEY_cache_size_mb, DefaultCacheSizeMB);
    tr_variantDictAddBool(d, TR_KEY_dht_enabled, true);
    tr_variantDictAddBool(d, TR_KEY_utp_enabled, true);
    tr_variantDictAddBool(d, TR_KEY_lpd_enabled, false);
    tr_variantDictAddStr(d, TR_KEY_download_dir, download_dir);
    tr_variantDictAddStr(d, TR_KEY_default_trackers, "");
    tr_variantDictAddInt(d, TR_KEY_speed_limit_down, 100);
    tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_encryption, TR_DEFAULT_ENCRYPTION);
    tr_variantDictAddInt(d, TR_KEY_idle_seeding_limit, 30);
    tr_variantDictAddBool(d, TR_KEY_idle_seeding_limit_enabled, false);
    tr_variantDictAddStr(d, TR_KEY_incomplete_dir, download_dir);
    tr_variantDictAddBool(d, TR_KEY_incomplete_dir_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_message_level, TR_LOG_INFO);
    tr_variantDictAddInt(d, TR_KEY_download_queue_size, 5);
    tr_variantDictAddBool(d, TR_KEY_download_queue_enabled, true);
    tr_variantDictAddInt(d, TR_KEY_peer_limit_global, atoi(TR_DEFAULT_PEER_LIMIT_GLOBAL_STR));
    tr_variantDictAddInt(d, TR_KEY_peer_limit_per_torrent, atoi(TR_DEFAULT_PEER_LIMIT_TORRENT_STR));
    tr_variantDictAddInt(d, TR_KEY_peer_port, atoi(TR_DEFAULT_PEER_PORT_STR));
    tr_variantDictAddBool(d, TR_KEY_peer_port_random_on_start, false);
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_low, 49152);
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_high, 65535);
    tr_variantDictAddStrView(d, TR_KEY_peer_socket_tos, TR_DEFAULT_PEER_SOCKET_TOS_STR);
    tr_variantDictAddBool(d, TR_KEY_pex_enabled, true);
    tr_variantDictAddBool(d, TR_KEY_port_forwarding_enabled, true);
    tr_variantDictAddInt(d, TR_KEY_preallocation, TR_PREALLOCATE_SPARSE);
    tr_variantDictAddBool(d, TR_KEY_prefetch_enabled, DefaultPrefetchEnabled);
    tr_variantDictAddInt(d, TR_KEY_peer_id_ttl_hours, 6);
    tr_variantDictAddBool(d, TR_KEY_queue_stalled_enabled, true);
    tr_variantDictAddInt(d, TR_KEY_queue_stalled_minutes, 30);
    tr_variantDictAddReal(d, TR_KEY_ratio_limit, 2.0);
    tr_variantDictAddBool(d, TR_KEY_ratio_limit_enabled, false);
    tr_variantDictAddBool(d, TR_KEY_rename_partial_files, true);
    tr_variantDictAddBool(d, TR_KEY_rpc_authentication_required, false);
    tr_variantDictAddStrView(d, TR_KEY_rpc_bind_address, "0.0.0.0");
    tr_variantDictAddBool(d, TR_KEY_rpc_enabled, false);
    tr_variantDictAddStrView(d, TR_KEY_rpc_password, "");
    tr_variantDictAddStrView(d, TR_KEY_rpc_username, "");
    tr_variantDictAddStrView(d, TR_KEY_rpc_whitelist, TR_DEFAULT_RPC_WHITELIST);
    tr_variantDictAddBool(d, TR_KEY_rpc_whitelist_enabled, true);
    tr_variantDictAddStrView(d, TR_KEY_rpc_host_whitelist, TR_DEFAULT_RPC_HOST_WHITELIST);
    tr_variantDictAddBool(d, TR_KEY_rpc_host_whitelist_enabled, true);
    tr_variantDictAddInt(d, TR_KEY_rpc_port, TR_DEFAULT_RPC_PORT);
    tr_variantDictAddStrView(d, TR_KEY_rpc_url, TR_DEFAULT_RPC_URL_STR);
    tr_variantDictAddStr(d, TR_KEY_rpc_socket_mode, fmt::format("{:03o}", tr_rpc_server::DefaultRpcSocketMode));
    tr_variantDictAddBool(d, TR_KEY_scrape_paused_torrents_enabled, true);
    tr_variantDictAddStrView(d, TR_KEY_script_torrent_added_filename, "");
    tr_variantDictAddBool(d, TR_KEY_script_torrent_added_enabled, false);
    tr_variantDictAddStrView(d, TR_KEY_script_torrent_done_filename, "");
    tr_variantDictAddBool(d, TR_KEY_script_torrent_done_enabled, false);
    tr_variantDictAddStrView(d, TR_KEY_script_torrent_done_seeding_filename, "");
    tr_variantDictAddBool(d, TR_KEY_script_torrent_done_seeding_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_seed_queue_size, 10);
    tr_variantDictAddBool(d, TR_KEY_seed_queue_enabled, false);
    tr_variantDictAddBool(d, TR_KEY_alt_speed_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_alt_speed_up, 50); /* half the regular */
    tr_variantDictAddInt(d, TR_KEY_alt_speed_down, 50); /* half the regular */
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_begin, 540); /* 9am */
    tr_variantDictAddBool(d, TR_KEY_alt_speed_time_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_end, 1020); /* 5pm */
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_day, TR_SCHED_ALL);
    tr_variantDictAddInt(d, TR_KEY_speed_limit_up, 100);
    tr_variantDictAddBool(d, TR_KEY_speed_limit_up_enabled, false);
    tr_variantDictAddStr(d, TR_KEY_umask, fmt::format("{:03o}", DefaultUmask));
    tr_variantDictAddInt(d, TR_KEY_upload_slots_per_torrent, 8);
    tr_variantDictAddStrView(d, TR_KEY_bind_address_ipv4, TR_DEFAULT_BIND_ADDRESS_IPV4);
    tr_variantDictAddStrView(d, TR_KEY_bind_address_ipv6, TR_DEFAULT_BIND_ADDRESS_IPV6);
    tr_variantDictAddBool(d, TR_KEY_start_added_torrents, true);
    tr_variantDictAddBool(d, TR_KEY_trash_original_torrent_files, false);
    tr_variantDictAddInt(d, TR_KEY_anti_brute_force_threshold, 100);
    tr_variantDictAddBool(d, TR_KEY_anti_brute_force_enabled, true);
    tr_variantDictAddStrView(d, TR_KEY_announce_ip, "");
    tr_variantDictAddBool(d, TR_KEY_announce_ip_enabled, false);

    tr_free(download_dir);
}

void tr_sessionGetSettings(tr_session const* s, tr_variant* setme_dictionary)
{
    auto* const d = setme_dictionary;
    TR_ASSERT(tr_variantIsDict(d));

    tr_variantDictReserve(d, 70);
    tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, s->useBlocklist());
    tr_variantDictAddStr(d, TR_KEY_blocklist_url, s->blocklistUrl());
    tr_variantDictAddInt(d, TR_KEY_cache_size_mb, tr_sessionGetCacheLimit_MB(s));
    tr_variantDictAddBool(d, TR_KEY_dht_enabled, s->allowsDHT());
    tr_variantDictAddBool(d, TR_KEY_utp_enabled, s->allowsUTP());
    tr_variantDictAddBool(d, TR_KEY_lpd_enabled, s->allowsLPD());
    tr_variantDictAddStr(d, TR_KEY_download_dir, tr_sessionGetDownloadDir(s));
    tr_variantDictAddStr(d, TR_KEY_default_trackers, s->defaultTrackersStr());
    tr_variantDictAddInt(d, TR_KEY_download_queue_size, s->queueSize(TR_DOWN));
    tr_variantDictAddBool(d, TR_KEY_download_queue_enabled, s->queueEnabled(TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_speed_limit_down, tr_sessionGetSpeedLimit_KBps(s, TR_DOWN));
    tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, s->isSpeedLimited(TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_encryption, s->encryptionMode());
    tr_variantDictAddInt(d, TR_KEY_idle_seeding_limit, s->idleLimitMinutes());
    tr_variantDictAddBool(d, TR_KEY_idle_seeding_limit_enabled, s->isIdleLimited());
    tr_variantDictAddStr(d, TR_KEY_incomplete_dir, tr_sessionGetIncompleteDir(s));
    tr_variantDictAddBool(d, TR_KEY_incomplete_dir_enabled, tr_sessionIsIncompleteDirEnabled(s));
    tr_variantDictAddInt(d, TR_KEY_message_level, tr_logGetLevel());
    tr_variantDictAddInt(d, TR_KEY_peer_limit_global, s->peerLimit());
    tr_variantDictAddInt(d, TR_KEY_peer_limit_per_torrent, s->peerLimitPerTorrent());
    tr_variantDictAddInt(d, TR_KEY_peer_port, s->peerPort().host());
    tr_variantDictAddBool(d, TR_KEY_peer_port_random_on_start, s->isPortRandom());
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_low, s->random_port_low_.host());
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_high, s->random_port_high_.host());
    tr_variantDictAddStr(d, TR_KEY_peer_socket_tos, tr_netTosToName(s->peer_socket_tos_));
    tr_variantDictAddStr(d, TR_KEY_peer_congestion_algorithm, s->peerCongestionAlgorithm());
    tr_variantDictAddBool(d, TR_KEY_pex_enabled, s->allowsPEX());
    tr_variantDictAddBool(d, TR_KEY_port_forwarding_enabled, tr_sessionIsPortForwardingEnabled(s));
    tr_variantDictAddInt(d, TR_KEY_preallocation, s->preallocationMode());
    tr_variantDictAddBool(d, TR_KEY_prefetch_enabled, s->allowsPrefetch());
    tr_variantDictAddInt(d, TR_KEY_peer_id_ttl_hours, s->peerIdTTLHours());
    tr_variantDictAddBool(d, TR_KEY_queue_stalled_enabled, s->queueStalledEnabled());
    tr_variantDictAddInt(d, TR_KEY_queue_stalled_minutes, s->queueStalledMinutes());
    tr_variantDictAddReal(d, TR_KEY_ratio_limit, s->desiredRatio());
    tr_variantDictAddBool(d, TR_KEY_ratio_limit_enabled, s->isRatioLimited());
    tr_variantDictAddBool(d, TR_KEY_rename_partial_files, s->isIncompleteFileNamingEnabled());
    tr_variantDictAddBool(d, TR_KEY_rpc_authentication_required, tr_sessionIsRPCPasswordEnabled(s));
    tr_variantDictAddStr(d, TR_KEY_rpc_bind_address, s->rpc_server_->getBindAddress());
    tr_variantDictAddBool(d, TR_KEY_rpc_enabled, tr_sessionIsRPCEnabled(s));
    tr_variantDictAddStr(d, TR_KEY_rpc_password, tr_sessionGetRPCPassword(s));
    tr_variantDictAddInt(d, TR_KEY_rpc_port, tr_sessionGetRPCPort(s));
    tr_variantDictAddStr(d, TR_KEY_rpc_socket_mode, fmt::format("{:#o}", s->rpc_server_->socket_mode_));
    tr_variantDictAddStr(d, TR_KEY_rpc_url, tr_sessionGetRPCUrl(s));
    tr_variantDictAddStr(d, TR_KEY_rpc_username, tr_sessionGetRPCUsername(s));
    tr_variantDictAddStr(d, TR_KEY_rpc_whitelist, tr_sessionGetRPCWhitelist(s));
    tr_variantDictAddBool(d, TR_KEY_rpc_whitelist_enabled, tr_sessionGetRPCWhitelistEnabled(s));
    tr_variantDictAddBool(d, TR_KEY_scrape_paused_torrents_enabled, s->shouldScrapePausedTorrents());
    tr_variantDictAddInt(d, TR_KEY_seed_queue_size, s->queueSize(TR_UP));
    tr_variantDictAddBool(d, TR_KEY_seed_queue_enabled, s->queueEnabled(TR_UP));
    tr_variantDictAddBool(d, TR_KEY_alt_speed_enabled, tr_sessionUsesAltSpeed(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_up, tr_sessionGetAltSpeed_KBps(s, TR_UP));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_down, tr_sessionGetAltSpeed_KBps(s, TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_begin, tr_sessionGetAltSpeedBegin(s));
    tr_variantDictAddBool(d, TR_KEY_alt_speed_time_enabled, tr_sessionUsesAltSpeedTime(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_end, tr_sessionGetAltSpeedEnd(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_day, tr_sessionGetAltSpeedDay(s));
    tr_variantDictAddInt(d, TR_KEY_speed_limit_up, tr_sessionGetSpeedLimit_KBps(s, TR_UP));
    tr_variantDictAddBool(d, TR_KEY_speed_limit_up_enabled, s->isSpeedLimited(TR_UP));
    tr_variantDictAddStr(d, TR_KEY_umask, fmt::format("{:#o}", s->umask_));
    tr_variantDictAddInt(d, TR_KEY_upload_slots_per_torrent, s->uploadSlotsPerTorrent());
    tr_variantDictAddStr(d, TR_KEY_bind_address_ipv4, s->bind_ipv4.readable());
    tr_variantDictAddStr(d, TR_KEY_bind_address_ipv6, s->bind_ipv6.readable());
    tr_variantDictAddBool(d, TR_KEY_start_added_torrents, !s->shouldPauseAddedTorrents());
    tr_variantDictAddBool(d, TR_KEY_trash_original_torrent_files, tr_sessionGetDeleteSource(s));
    tr_variantDictAddInt(d, TR_KEY_anti_brute_force_threshold, tr_sessionGetAntiBruteForceThreshold(s));
    tr_variantDictAddBool(d, TR_KEY_anti_brute_force_enabled, tr_sessionGetAntiBruteForceEnabled(s));
    tr_variantDictAddStr(d, TR_KEY_announce_ip, s->announceIP());
    tr_variantDictAddBool(d, TR_KEY_announce_ip_enabled, s->useAnnounceIP());
    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        tr_variantDictAddBool(d, enabled_key, s->useScript(script));
        tr_variantDictAddStr(d, script_key, s->script(script));
    }
}

static void getSettingsFilename(tr_pathbuf& setme, char const* config_dir, char const* appname)
{
    if (!tr_str_is_empty(config_dir))
    {
        setme.assign(std::string_view{ config_dir }, "/settings.json"sv);
        return;
    }

    auto* const default_config_dir = tr_getDefaultConfigDir(appname);
    setme.assign(std::string_view{ default_config_dir }, "/settings.json"sv);
    tr_free(default_config_dir);
}

bool tr_sessionLoadSettings(tr_variant* dict, char const* config_dir, char const* appName)
{
    TR_ASSERT(tr_variantIsDict(dict));

    /* initializing the defaults: caller may have passed in some app-level defaults.
     * preserve those and use the session defaults to fill in any missing gaps. */
    auto oldDict = *dict;
    tr_variantInitDict(dict, 0);
    tr_sessionGetDefaultSettings(dict);
    tr_variantMergeDicts(dict, &oldDict);
    tr_variantFree(&oldDict);

    /* file settings override the defaults */
    auto fileSettings = tr_variant{};
    auto success = bool{};
    auto filename = tr_pathbuf{};
    getSettingsFilename(filename, config_dir, appName);
    if (!tr_sys_path_exists(filename))
    {
        success = true;
    }
    else if (tr_variantFromFile(&fileSettings, TR_VARIANT_PARSE_JSON, filename))
    {
        tr_variantMergeDicts(dict, &fileSettings);
        tr_variantFree(&fileSettings);
        success = true;
    }
    else
    {
        success = false;
    }

    /* cleanup */
    return success;
}

void tr_sessionSaveSettings(tr_session* session, char const* config_dir, tr_variant const* client_settings)
{
    TR_ASSERT(tr_variantIsDict(client_settings));

    tr_variant settings;
    auto const filename = tr_pathbuf{ config_dir, "/settings.json"sv };

    tr_variantInitDict(&settings, 0);

    /* the existing file settings are the fallback values */
    if (auto file_settings = tr_variant{}; tr_variantFromFile(&file_settings, TR_VARIANT_PARSE_JSON, filename))
    {
        tr_variantMergeDicts(&settings, &file_settings);
        tr_variantFree(&file_settings);
    }

    /* the client's settings override the file settings */
    tr_variantMergeDicts(&settings, client_settings);

    /* the session's true values override the file & client settings */
    {
        tr_variant sessionSettings;
        tr_variantInitDict(&sessionSettings, 0);
        tr_sessionGetSettings(session, &sessionSettings);
        tr_variantMergeDicts(&settings, &sessionSettings);
        tr_variantFree(&sessionSettings);
    }

    /* save the result */
    tr_variantToFile(&settings, TR_VARIANT_FMT_JSON, filename);

    /* cleanup */
    tr_variantFree(&settings);

    /* Write bandwidth groups limits to file  */
    bandwidthGroupWrite(session, config_dir);
}

/***
****
***/

struct tr_session::init_data
{
    bool message_queuing_enabled;
    std::string_view config_dir;
    tr_variant* client_settings;
    std::condition_variable_any done_cv;
};

tr_session* tr_sessionInit(char const* config_dir, bool message_queueing_enabled, tr_variant* client_settings)
{
    TR_ASSERT(tr_variantIsDict(client_settings));

    tr_timeUpdate(time(nullptr));

    /* initialize the bare skeleton of the session object */
    auto* const session = new tr_session{ config_dir };
    session->udp_socket = TR_BAD_SOCKET;
    session->udp6_socket = TR_BAD_SOCKET;
    session->cache = std::make_unique<Cache>(session->torrents(), 1024 * 1024 * 2);
    bandwidthGroupRead(session, config_dir);

    /* nice to start logging at the very beginning */
    if (auto i = int64_t{}; tr_variantDictFindInt(client_settings, TR_KEY_message_level, &i))
    {
        tr_logSetLevel(tr_log_level(i));
    }

    /* start the libtransmission thread */
    tr_net_init(); /* must go before tr_eventInit */
    tr_eventInit(session);
    TR_ASSERT(session->events != nullptr);

    auto data = tr_session::init_data{};
    data.config_dir = config_dir;
    data.message_queuing_enabled = message_queueing_enabled;
    data.client_settings = client_settings;

    // run it in the libtransmission thread
    if (tr_amInEventThread(session))
    {
        session->initImpl(data);
    }
    else
    {
        auto lock = session->unique_lock();
        tr_runInEventThread(session, [&session, &data]() { session->initImpl(data); });
        data.done_cv.wait(lock); // wait for the session to be ready
    }

    return session;
}

static void turtleCheckClock(tr_session* s, struct tr_turtle_info* t);

void tr_session::onNowTimer()
{
    TR_ASSERT(now_timer_);

    // tr_session upkeep tasks to perform once per second
    tr_timeUpdate(time(nullptr));
    tr_dhtUpkeep(this);
    if (turtle.isClockEnabled)
    {
        turtleCheckClock(this, &this->turtle);
    }

    // TODO: this seems a little silly. Why do we increment this
    // every second instead of computing the value as needed by
    // subtracting the current time from a start time?
    for (auto* const tor : torrents())
    {
        if (tor->isRunning)
        {
            if (tor->isDone())
            {
                ++tor->secondsSeeding;
            }
            else
            {
                ++tor->secondsDownloading;
            }
        }
    }

    // set the timer to kick again right after (10ms after) the next second
    auto const now = std::chrono::system_clock::now();
    auto const target_time = std::chrono::time_point_cast<std::chrono::seconds>(now) + 1s + 10ms;
    auto target_interval = target_time - now;
    if (target_interval < 100ms)
    {
        target_interval += 1s;
    }
    now_timer_->setInterval(std::chrono::duration_cast<std::chrono::milliseconds>(target_interval));
}

void tr_session::initImpl(init_data& data)
{
    auto lock = unique_lock();
    TR_ASSERT(tr_amInEventThread(this));

    tr_variant const* const client_settings = data.client_settings;
    TR_ASSERT(tr_variantIsDict(client_settings));

    tr_logAddTrace(fmt::format("tr_sessionInit: the session's top-level bandwidth object is {}", fmt::ptr(&top_bandwidth_)));

    auto settings = tr_variant{};
    tr_variantInitDict(&settings, 0);
    tr_sessionGetDefaultSettings(&settings);
    tr_variantMergeDicts(&settings, client_settings);

#ifndef _WIN32
    /* Don't exit when writing on a broken socket */
    (void)signal(SIGPIPE, SIG_IGN);
#endif

    tr_logSetQueueEnabled(data.message_queuing_enabled);

    this->peerMgr = tr_peerMgrNew(this);

    this->shared = tr_sharedInit(*this);

    /**
    ***  Blocklist
    **/

    tr_sys_dir_create(tr_pathbuf{ configDir(), "/blocklists"sv }, TR_SYS_DIR_CREATE_PARENTS, 0777);
    loadBlocklists();

    tr_announcerInit(this);

    tr_logAddInfo(fmt::format(_("Transmission version {version} starting"), fmt::arg("version", LONG_VERSION_STRING)));

    tr_sessionSet(this, &settings);

    tr_udpInit(this);

    this->web = tr_web::create(this->web_mediator_);

    if (this->allowsLPD())
    {
        tr_lpdInit(this, &this->bind_ipv4.addr_);
    }

    tr_utpInit(this);

    /* cleanup */
    tr_variantFree(&settings);
    data.done_cv.notify_one();
}

static void turtleBootstrap(tr_session* /*session*/, struct tr_turtle_info* /*turtle*/);
static void setPeerPort(tr_session* session, tr_port port);

void tr_session::setImpl(init_data& data)
{
    TR_ASSERT(tr_amInEventThread(this));

    tr_variant* const settings = data.client_settings;
    TR_ASSERT(tr_variantIsDict(settings));

    auto boolVal = bool{};
    auto d = double{};
    auto i = int64_t{};
    auto sv = std::string_view{};

    if (tr_variantDictFindInt(settings, TR_KEY_message_level, &i))
    {
        tr_logSetLevel(tr_log_level(i));
    }

#ifndef _WIN32

    if (tr_variantDictFindStrView(settings, TR_KEY_umask, &sv))
    {
        /* Read a umask as a string representing an octal number. */
        this->umask_ = static_cast<mode_t>(tr_parseNum<uint32_t>(sv, 8).value_or(DefaultUmask));
        ::umask(this->umask_);
    }
    else if (tr_variantDictFindInt(settings, TR_KEY_umask, &i))
    {
        /* Or as a base 10 integer to remain compatible with the old settings format. */
        this->umask_ = (mode_t)i;
        ::umask(this->umask_);
    }

#endif

    /* misc features */
    if (tr_variantDictFindInt(settings, TR_KEY_cache_size_mb, &i))
    {
        tr_sessionSetCacheLimit_MB(this, i);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_default_trackers, &sv))
    {
        setDefaultTrackers(sv);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_limit_per_torrent, &i))
    {
        tr_sessionSetPeerLimitPerTorrent(this, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_pex_enabled, &boolVal))
    {
        tr_sessionSetPexEnabled(this, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_dht_enabled, &boolVal))
    {
        tr_sessionSetDHTEnabled(this, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_utp_enabled, &boolVal))
    {
        tr_sessionSetUTPEnabled(this, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_lpd_enabled, &boolVal))
    {
        tr_sessionSetLPDEnabled(this, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_encryption, &i))
    {
        tr_sessionSetEncryption(this, tr_encryption_mode(i));
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_socket_tos, &i))
    {
        peer_socket_tos_ = i;
    }
    else if (tr_variantDictFindStrView(settings, TR_KEY_peer_socket_tos, &sv))
    {
        if (auto ip_tos = tr_netTosFromName(sv); ip_tos)
        {
            peer_socket_tos_ = *ip_tos;
        }
    }

    sv = ""sv;
    (void)tr_variantDictFindStrView(settings, TR_KEY_peer_congestion_algorithm, &sv);
    setPeerCongestionAlgorithm(sv);

    if (tr_variantDictFindBool(settings, TR_KEY_blocklist_enabled, &boolVal))
    {
        useBlocklist(boolVal);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_blocklist_url, &sv))
    {
        setBlocklistUrl(sv);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_start_added_torrents, &boolVal))
    {
        tr_sessionSetPaused(this, !boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_trash_original_torrent_files, &boolVal))
    {
        tr_sessionSetDeleteSource(this, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_id_ttl_hours, &i))
    {
        this->peer_id_ttl_hours_ = i;
    }

    /* torrent queues */
    if (tr_variantDictFindInt(settings, TR_KEY_queue_stalled_minutes, &i))
    {
        tr_sessionSetQueueStalledMinutes(this, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_queue_stalled_enabled, &boolVal))
    {
        tr_sessionSetQueueStalledEnabled(this, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_download_queue_size, &i))
    {
        tr_sessionSetQueueSize(this, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_download_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(this, TR_DOWN, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_seed_queue_size, &i))
    {
        tr_sessionSetQueueSize(this, TR_UP, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_seed_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(this, TR_UP, boolVal);
    }

    /* files and directories */
    if (tr_variantDictFindBool(settings, TR_KEY_prefetch_enabled, &boolVal))
    {
        this->is_prefetch_enabled_ = boolVal;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_preallocation, &i))
    {
        this->preallocation_mode_ = tr_preallocation_mode(i);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_download_dir, &sv))
    {
        this->setDownloadDir(sv);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_incomplete_dir, &sv))
    {
        this->setIncompleteDir(sv);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_incomplete_dir_enabled, &boolVal))
    {
        this->useIncompleteDir(boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_rename_partial_files, &boolVal))
    {
        tr_sessionSetIncompleteFileNamingEnabled(this, boolVal);
    }

    /* rpc server */
    this->rpc_server_ = std::make_unique<tr_rpc_server>(this, settings);

    /* public addresses */

    close_incoming_peer_port(this);

    auto address = tr_inaddr_any;

    if (tr_variantDictFindStrView(settings, TR_KEY_bind_address_ipv4, &sv))
    {
        if (auto const addr = tr_address::fromString(sv); addr && addr->isIPv4())
        {
            address = *addr;
        }
    }

    this->bind_ipv4 = tr_bindinfo{ address };

    address = tr_in6addr_any;

    if (tr_variantDictFindStrView(settings, TR_KEY_bind_address_ipv6, &sv))
    {
        if (auto const addr = tr_address::fromString(sv); addr && addr->isIPv6())
        {
            address = *addr;
        }
    }

    this->bind_ipv6 = tr_bindinfo{ address };

    /* incoming peer port */
    if (tr_variantDictFindInt(settings, TR_KEY_peer_port_random_low, &i))
    {
        this->random_port_low_.setHost(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_port_random_high, &i))
    {
        this->random_port_high_.setHost(i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_peer_port_random_on_start, &boolVal))
    {
        tr_sessionSetPeerPortRandomOnStart(this, boolVal);
    }

    {
        auto peer_port = this->private_peer_port;

        if (auto port = int64_t{}; tr_variantDictFindInt(settings, TR_KEY_peer_port, &port))
        {
            peer_port.setHost(static_cast<uint16_t>(port));
        }

        ::setPeerPort(this, boolVal ? randomPort() : peer_port);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_port_forwarding_enabled, &boolVal))
    {
        tr_sessionSetPortForwardingEnabled(this, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_limit_global, &i))
    {
        this->peer_limit_ = i;
    }

    /**
    **/

    if (tr_variantDictFindInt(settings, TR_KEY_upload_slots_per_torrent, &i))
    {
        this->upload_slots_per_torrent_ = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_speed_limit_up, &i))
    {
        tr_sessionSetSpeedLimit_KBps(this, TR_UP, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_speed_limit_up_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(this, TR_UP, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_speed_limit_down, &i))
    {
        tr_sessionSetSpeedLimit_KBps(this, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_speed_limit_down_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(this, TR_DOWN, boolVal);
    }

    if (tr_variantDictFindReal(settings, TR_KEY_ratio_limit, &d))
    {
        tr_sessionSetRatioLimit(this, d);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_ratio_limit_enabled, &boolVal))
    {
        tr_sessionSetRatioLimited(this, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_idle_seeding_limit, &i))
    {
        tr_sessionSetIdleLimit(this, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_idle_seeding_limit_enabled, &boolVal))
    {
        tr_sessionSetIdleLimited(this, boolVal);
    }

    /**
    ***  Turtle Mode
    **/

    /* update the turtle mode's fields */
    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_up, &i))
    {
        turtle.speedLimit_Bps[TR_UP] = tr_toSpeedBytes(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_down, &i))
    {
        turtle.speedLimit_Bps[TR_DOWN] = tr_toSpeedBytes(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_begin, &i))
    {
        turtle.beginMinute = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_end, &i))
    {
        turtle.endMinute = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_day, &i))
    {
        turtle.days = tr_sched_day(i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_alt_speed_time_enabled, &boolVal))
    {
        turtle.isClockEnabled = boolVal;
    }

    if (tr_variantDictFindBool(settings, TR_KEY_alt_speed_enabled, &boolVal))
    {
        turtle.isEnabled = boolVal;
    }

    turtleBootstrap(this, &turtle);

    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        if (auto enabled = bool{}; tr_variantDictFindBool(settings, enabled_key, &enabled))
        {
            this->useScript(script, enabled);
        }

        if (auto file = std::string_view{}; tr_variantDictFindStrView(settings, script_key, &file))
        {
            this->setScript(script, file);
        }
    }

    if (tr_variantDictFindBool(settings, TR_KEY_scrape_paused_torrents_enabled, &boolVal))
    {
        this->should_scrape_paused_torrents_ = boolVal;
    }

    /**
    ***  BruteForce
    **/

    if (tr_variantDictFindInt(settings, TR_KEY_anti_brute_force_threshold, &i))
    {
        tr_sessionSetAntiBruteForceThreshold(this, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_anti_brute_force_enabled, &boolVal))
    {
        tr_sessionSetAntiBruteForceEnabled(this, boolVal);
    }

    /*
     * Announce IP.
     */
    if (tr_variantDictFindStrView(settings, TR_KEY_announce_ip, &sv))
    {
        this->setAnnounceIP(sv);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_announce_ip_enabled, &boolVal))
    {
        this->useAnnounceIP(boolVal);
    }

    data.done_cv.notify_one();
}

void tr_sessionSet(tr_session* session, tr_variant* settings)
{
    auto data = tr_session::init_data{};
    data.client_settings = settings;

    // run it in the libtransmission thread

    if (tr_amInEventThread(session))
    {
        session->setImpl(data);
    }
    else
    {
        auto lock = session->unique_lock();
        tr_runInEventThread(session, [&session, &data]() { session->setImpl(data); });
        data.done_cv.wait(lock);
    }
}

/***
****
***/

void tr_sessionSetDownloadDir(tr_session* session, char const* dir)
{
    TR_ASSERT(session != nullptr);

    session->setDownloadDir(dir != nullptr ? dir : "");
}

char const* tr_sessionGetDownloadDir(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->downloadDir().c_str();
}

char const* tr_sessionGetConfigDir(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->configDir().c_str();
}

/***
****
***/

void tr_sessionSetIncompleteFileNamingEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->is_incomplete_file_naming_enabled_ = enabled;
}

bool tr_sessionIsIncompleteFileNamingEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->isIncompleteFileNamingEnabled();
}

/***
****
***/

void tr_sessionSetIncompleteDir(tr_session* session, char const* dir)
{
    TR_ASSERT(session != nullptr);

    session->setIncompleteDir(dir != nullptr ? dir : "");
}

char const* tr_sessionGetIncompleteDir(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->incompleteDir().c_str();
}

void tr_sessionSetIncompleteDirEnabled(tr_session* session, bool b)
{
    TR_ASSERT(session != nullptr);

    session->useIncompleteDir(b);
}

bool tr_sessionIsIncompleteDirEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->useIncompleteDir();
}

/***
****  Peer Port
***/

static void peerPortChanged(tr_session* const session)
{
    TR_ASSERT(session != nullptr);

    close_incoming_peer_port(session);
    open_incoming_peer_port(session);
    tr_sharedPortChanged(*session);

    for (auto* const tor : session->torrents())
    {
        tr_torrentChangeMyPort(tor);
    }
}

static void setPeerPort(tr_session* session, tr_port port)
{
    session->private_peer_port = port;
    session->public_peer_port = port;

    tr_runInEventThread(session, peerPortChanged, session);
}

void tr_sessionSetPeerPort(tr_session* session, uint16_t hport)
{
    if (auto const port = tr_port::fromHost(hport); session != nullptr && session->private_peer_port != port)
    {
        setPeerPort(session, port);
    }
}

uint16_t tr_sessionGetPeerPort(tr_session const* session)
{
    return session != nullptr ? session->public_peer_port.host() : 0U;
}

uint16_t tr_sessionSetPeerPortRandom(tr_session* session)
{
    auto const p = session->randomPort();
    tr_sessionSetPeerPort(session, p.host());
    return p.host();
}

void tr_sessionSetPeerPortRandomOnStart(tr_session* session, bool random)
{
    TR_ASSERT(session != nullptr);

    session->is_port_random_ = random;
}

bool tr_sessionGetPeerPortRandomOnStart(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->isPortRandom();
}

tr_port_forwarding tr_sessionGetPortForwarding(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return tr_port_forwarding(tr_sharedTraversalStatus(session->shared));
}

/***
****
***/

void tr_sessionSetRatioLimited(tr_session* session, bool is_limited)
{
    TR_ASSERT(session != nullptr);

    session->is_ratio_limited_ = is_limited;
}

void tr_sessionSetRatioLimit(tr_session* session, double desired_ratio)
{
    TR_ASSERT(session != nullptr);

    session->desired_ratio_ = desired_ratio;
}

bool tr_sessionIsRatioLimited(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->isRatioLimited();
}

double tr_sessionGetRatioLimit(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->desiredRatio();
}

/***
****
***/

void tr_sessionSetIdleLimited(tr_session* session, bool is_limited)
{
    TR_ASSERT(session != nullptr);

    session->is_idle_limited_ = is_limited;
}

void tr_sessionSetIdleLimit(tr_session* session, uint16_t idle_minutes)
{
    TR_ASSERT(session != nullptr);

    session->idle_limit_minutes_ = idle_minutes;
}

bool tr_sessionIsIdleLimited(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->isIdleLimited();
}

uint16_t tr_sessionGetIdleLimit(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->idleLimitMinutes();
}

/***
****
****  SPEED LIMITS
****
***/

static unsigned int tr_sessionGetAltSpeed_Bps(tr_session const* s, tr_direction d);

std::optional<unsigned int> tr_session::activeSpeedLimitBps(tr_direction dir) const noexcept
{
    if (tr_sessionUsesAltSpeed(this))
    {
        return tr_sessionGetAltSpeed_Bps(this, dir);
    }

    if (this->isSpeedLimited(dir))
    {
        return speedLimitBps(dir);
    }

    return {};
}

static void updateBandwidth(tr_session* session, tr_direction dir)
{
    if (auto const limit_Bps = session->activeSpeedLimitBps(dir); limit_Bps)
    {
        session->top_bandwidth_.setLimited(dir, *limit_Bps > 0U);
        session->top_bandwidth_.setDesiredSpeedBytesPerSecond(dir, *limit_Bps);
    }
    else
    {
        session->top_bandwidth_.setLimited(dir, false);
    }
}

static auto constexpr MinutesPerHour = int{ 60 };
static auto constexpr MinutesPerDay = int{ MinutesPerHour * 24 };
static auto constexpr MinutesPerWeek = int{ MinutesPerDay * 7 };

static void turtleUpdateTable(struct tr_turtle_info* t)
{
    t->minutes.setHasNone();

    for (int day = 0; day < 7; ++day)
    {
        if ((t->days & (1 << day)) != 0)
        {
            time_t const begin = t->beginMinute;
            time_t end = t->endMinute;

            if (end <= begin)
            {
                end += MinutesPerDay;
            }

            for (time_t i = begin; i < end; ++i)
            {
                t->minutes.set((i + day * MinutesPerDay) % MinutesPerWeek);
            }
        }
    }
}

static void altSpeedToggled(tr_session* const session)
{
    TR_ASSERT(session != nullptr);

    updateBandwidth(session, TR_UP);
    updateBandwidth(session, TR_DOWN);

    struct tr_turtle_info* t = &session->turtle;

    if (t->callback != nullptr)
    {
        (*t->callback)(session, t->isEnabled, t->changedByUser, t->callbackUserData);
    }
}

static void useAltSpeed(tr_session* s, struct tr_turtle_info* t, bool enabled, bool byUser)
{
    TR_ASSERT(s != nullptr);
    TR_ASSERT(t != nullptr);

    if (t->isEnabled != enabled)
    {
        t->isEnabled = enabled;
        t->changedByUser = byUser;
        tr_runInEventThread(s, altSpeedToggled, s);
    }
}

/**
 * @return whether turtle should be on/off according to the scheduler
 */
static bool getInTurtleTime(struct tr_turtle_info const* t)
{
    auto const now = tr_time();
    struct tm const tm = fmt::localtime(now);

    size_t minute_of_the_week = tm.tm_wday * MinutesPerDay + tm.tm_hour * MinutesPerHour + tm.tm_min;

    if (minute_of_the_week >= MinutesPerWeek) /* leap minutes? */
    {
        minute_of_the_week = MinutesPerWeek - 1;
    }

    return t->minutes.test(minute_of_the_week);
}

static constexpr tr_auto_switch_state_t autoSwitchState(bool enabled)
{
    return enabled ? TR_AUTO_SWITCH_ON : TR_AUTO_SWITCH_OFF;
}

static void turtleCheckClock(tr_session* s, struct tr_turtle_info* t)
{
    TR_ASSERT(t->isClockEnabled);

    bool const enabled = getInTurtleTime(t);
    tr_auto_switch_state_t const newAutoTurtleState = autoSwitchState(enabled);
    bool const alreadySwitched = t->autoTurtleState == newAutoTurtleState;

    if (!alreadySwitched)
    {
        tr_logAddInfo(enabled ? _("Time to turn on turtle mode") : _("Time to turn off turtle mode"));
        t->autoTurtleState = newAutoTurtleState;
        useAltSpeed(s, t, enabled, false);
    }
}

/* Called after the turtle's fields are loaded from an outside source.
 * It initializes the implementation fields
 * and turns on turtle mode if the clock settings say to. */
static void turtleBootstrap(tr_session* session, struct tr_turtle_info* turtle)
{
    turtle->changedByUser = false;
    turtle->autoTurtleState = TR_AUTO_SWITCH_UNUSED;
    turtle->minutes.setHasNone();

    turtleUpdateTable(turtle);

    if (turtle->isClockEnabled)
    {
        turtle->isEnabled = getInTurtleTime(turtle);
        turtle->autoTurtleState = autoSwitchState(turtle->isEnabled);
    }

    altSpeedToggled(session);
}

/***
****  Primary session speed limits
***/

void tr_sessionSetSpeedLimit_Bps(tr_session* session, tr_direction dir, unsigned int Bps)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    session->speed_limit_Bps_[dir] = Bps;

    updateBandwidth(session, dir);
}

void tr_sessionSetSpeedLimit_KBps(tr_session* session, tr_direction dir, unsigned int KBps)
{
    tr_sessionSetSpeedLimit_Bps(session, dir, tr_toSpeedBytes(KBps));
}

unsigned int tr_sessionGetSpeedLimit_KBps(tr_session const* s, tr_direction d)
{
    return tr_toSpeedKBps(s->speedLimitBps(d));
}

void tr_sessionLimitSpeed(tr_session* session, tr_direction dir, bool limited)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    session->speed_limit_enabled_[dir] = limited;

    updateBandwidth(session, dir);
}

bool tr_sessionIsSpeedLimited(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return session->isSpeedLimited(dir);
}

/***
****  Alternative speed limits that are used during scheduled times
***/

static void tr_sessionSetAltSpeed_Bps(tr_session* s, tr_direction d, unsigned int Bps)
{
    TR_ASSERT(s != nullptr);
    TR_ASSERT(tr_isDirection(d));

    s->turtle.speedLimit_Bps[d] = Bps;

    updateBandwidth(s, d);
}

void tr_sessionSetAltSpeed_KBps(tr_session* s, tr_direction d, unsigned int KBps)
{
    tr_sessionSetAltSpeed_Bps(s, d, tr_toSpeedBytes(KBps));
}

static unsigned int tr_sessionGetAltSpeed_Bps(tr_session const* s, tr_direction d)
{
    TR_ASSERT(s != nullptr);
    TR_ASSERT(tr_isDirection(d));

    return s->turtle.speedLimit_Bps[d];
}

unsigned int tr_sessionGetAltSpeed_KBps(tr_session const* s, tr_direction d)
{
    return tr_toSpeedKBps(tr_sessionGetAltSpeed_Bps(s, d));
}

static void userPokedTheClock(tr_session* s, struct tr_turtle_info* t)
{
    tr_logAddTrace("Refreshing the turtle mode clock due to user changes");

    t->autoTurtleState = TR_AUTO_SWITCH_UNUSED;

    turtleUpdateTable(t);

    if (t->isClockEnabled)
    {
        bool const enabled = getInTurtleTime(t);
        useAltSpeed(s, t, enabled, true);
        t->autoTurtleState = autoSwitchState(enabled);
    }
}

void tr_sessionUseAltSpeedTime(tr_session* s, bool b)
{
    TR_ASSERT(s != nullptr);

    struct tr_turtle_info* t = &s->turtle;

    if (t->isClockEnabled != b)
    {
        t->isClockEnabled = b;
        userPokedTheClock(s, t);
    }
}

bool tr_sessionUsesAltSpeedTime(tr_session const* s)
{
    TR_ASSERT(s != nullptr);

    return s->turtle.isClockEnabled;
}

void tr_sessionSetAltSpeedBegin(tr_session* s, int minutes_since_midnight)
{
    TR_ASSERT(s != nullptr);
    TR_ASSERT(minutes_since_midnight >= 0);
    TR_ASSERT(minutes_since_midnight < 60 * 24);

    if (s->turtle.beginMinute != minutes_since_midnight)
    {
        s->turtle.beginMinute = minutes_since_midnight;
        userPokedTheClock(s, &s->turtle);
    }
}

int tr_sessionGetAltSpeedBegin(tr_session const* s)
{
    TR_ASSERT(s != nullptr);

    return s->turtle.beginMinute;
}

void tr_sessionSetAltSpeedEnd(tr_session* s, int minutes_since_midnight)
{
    TR_ASSERT(s != nullptr);
    TR_ASSERT(minutes_since_midnight >= 0);
    TR_ASSERT(minutes_since_midnight < 60 * 24);

    if (s->turtle.endMinute != minutes_since_midnight)
    {
        s->turtle.endMinute = minutes_since_midnight;
        userPokedTheClock(s, &s->turtle);
    }
}

int tr_sessionGetAltSpeedEnd(tr_session const* s)
{
    TR_ASSERT(s != nullptr);

    return s->turtle.endMinute;
}

void tr_sessionSetAltSpeedDay(tr_session* s, tr_sched_day days)
{
    TR_ASSERT(s != nullptr);

    if (s->turtle.days != days)
    {
        s->turtle.days = days;
        userPokedTheClock(s, &s->turtle);
    }
}

tr_sched_day tr_sessionGetAltSpeedDay(tr_session const* s)
{
    TR_ASSERT(s != nullptr);

    return s->turtle.days;
}

void tr_sessionUseAltSpeed(tr_session* session, bool enabled)
{
    useAltSpeed(session, &session->turtle, enabled, true);
}

bool tr_sessionUsesAltSpeed(tr_session const* s)
{
    TR_ASSERT(s != nullptr);

    return s->turtle.isEnabled;
}

void tr_sessionSetAltSpeedFunc(tr_session* session, tr_altSpeedFunc func, void* userData)
{
    TR_ASSERT(session != nullptr);

    session->turtle.callback = func;
    session->turtle.callbackUserData = userData;
}

/***
****
***/

void tr_sessionSetPeerLimit(tr_session* session, uint16_t max_global_peers)
{
    TR_ASSERT(session != nullptr);

    session->peer_limit_ = max_global_peers;
}

uint16_t tr_sessionGetPeerLimit(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->peerLimit();
}

void tr_sessionSetPeerLimitPerTorrent(tr_session* session, uint16_t max_peers)
{
    TR_ASSERT(session != nullptr);

    session->peer_limit_per_torrent_ = max_peers;
}

uint16_t tr_sessionGetPeerLimitPerTorrent(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->peerLimitPerTorrent();
}

/***
****
***/

void tr_sessionSetPaused(tr_session* session, bool is_paused)
{
    TR_ASSERT(session != nullptr);

    session->should_pause_added_torrents_ = is_paused;
}

bool tr_sessionGetPaused(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->shouldPauseAddedTorrents();
}

void tr_sessionSetDeleteSource(tr_session* session, bool delete_source)
{
    TR_ASSERT(session != nullptr);

    session->should_delete_source_torrents_ = delete_source;
}

bool tr_sessionGetDeleteSource(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->shouldDeleteSource();
}

/***
****
***/

static unsigned int tr_sessionGetRawSpeed_Bps(tr_session const* session, tr_direction dir)
{
    return session != nullptr ? session->top_bandwidth_.getRawSpeedBytesPerSecond(0, dir) : 0;
}

double tr_sessionGetRawSpeed_KBps(tr_session const* session, tr_direction dir)
{
    return tr_toSpeedKBps(tr_sessionGetRawSpeed_Bps(session, dir));
}

void tr_session::closeImplStart()
{
    is_closing_ = true;

    if (this->allowsLPD())
    {
        tr_lpdUninit(this);
    }

    tr_dhtUninit(this);

    save_timer_.reset();
    now_timer_.reset();

    tr_verifyClose(this);
    tr_sharedClose(*this);

    close_incoming_peer_port(this);
    this->rpc_server_.reset();

    /* Close the torrents. Get the most active ones first so that
     * if we can't get them all closed in a reasonable amount of time,
     * at least we get the most important ones first. */
    auto torrents = getAllTorrents();
    std::sort(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* a, auto const* b)
        {
            auto const aCur = a->downloadedCur + a->uploadedCur;
            auto const bCur = b->downloadedCur + b->uploadedCur;
            return aCur > bCur; // larger xfers go first
        });

    for (auto* tor : torrents)
    {
        tr_torrentFree(tor);
    }

    torrents.clear();

    /* Close the announcer *after* closing the torrents
       so that all the &event=stopped messages will be
       queued to be sent by tr_announcerClose() */
    tr_announcerClose(this);

    /* and this goes *after* announcer close so that
       it won't be idle until the announce events are sent... */
    this->web->closeSoon();

    this->cache.reset();

    /* saveTimer is not used at this point, reusing for UDP shutdown wait */
    TR_ASSERT(!save_timer_);
    save_timer_ = timerMaker().create([this]() { closeImplWaitForIdleUdp(); });
    save_timer_->start(1ms);
}

void tr_session::closeImplWaitForIdleUdp()
{
    /* gotta keep udp running long enough to send out all
       the &event=stopped UDP tracker messages */
    if (!tr_tracker_udp_is_idle(this))
    {
        tr_tracker_udp_upkeep(this);
        save_timer_->start(100ms);
        return;
    }

    closeImplFinish();
}

void tr_session::closeImplFinish()
{
    save_timer_.reset();

    /* we had to wait until UDP trackers were closed before closing these: */
    tr_tracker_udp_close(this);
    tr_udpUninit(this);

    stats().saveIfDirty();
    tr_peerMgrFree(peerMgr);
    tr_utpClose(this);
    blocklists_.clear();
    openFiles().closeAll();
    is_closed_ = true;
}

static bool deadlineReached(time_t const deadline)
{
    return time(nullptr) >= deadline;
}

static auto constexpr ShutdownMaxSeconds = time_t{ 20 };

void tr_sessionClose(tr_session* session)
{
    TR_ASSERT(session != nullptr);

    time_t const deadline = time(nullptr) + ShutdownMaxSeconds;

    tr_logAddInfo(fmt::format(_("Transmission version {version} shutting down"), fmt::arg("version", LONG_VERSION_STRING)));
    tr_logAddDebug(fmt::format("now is {}, deadline is {}", time(nullptr), deadline));

    /* close the session */
    tr_runInEventThread(session, [session]() { session->closeImplStart(); });

    while (!session->isClosed() && !deadlineReached(deadline))
    {
        tr_logAddTrace("waiting for the libtransmission thread to finish");
        tr_wait_msec(10);
    }

    /* "shared" and "tracker" have live sockets,
     * so we need to keep the transmission thread alive
     * for a bit while they tell the router & tracker
     * that we're closing now */
    while ((session->shared != nullptr || !session->web->isClosed() || session->announcer != nullptr ||
            session->announcer_udp != nullptr) &&
           !deadlineReached(deadline))
    {
        tr_logAddTrace(fmt::format(
            "waiting on port unmap ({}) or announcer ({})... now {} deadline {}",
            fmt::ptr(session->shared),
            fmt::ptr(session->announcer),
            time(nullptr),
            deadline));
        tr_wait_msec(50);
    }

    session->web.reset();

    /* close the libtransmission thread */
    tr_eventClose(session);

    while (session->events != nullptr)
    {
        static bool forced = false;
        tr_logAddTrace(
            fmt::format("waiting for libtransmission thread to finish... now {} deadline {}", time(nullptr), deadline));
        tr_wait_msec(10);

        if (deadlineReached(deadline) && !forced)
        {
            tr_logAddTrace("calling event_loopbreak()");
            forced = true;
            event_base_loopbreak(session->eventBase());
        }

        if (deadlineReached(deadline + 3))
        {
            tr_logAddTrace("deadline+3 reached... calling break...");
            break;
        }
    }

    delete session;
}

struct sessionLoadTorrentsData
{
    tr_session* session;
    tr_ctor* ctor;
    int* setmeCount;
    tr_torrent** torrents;
    bool done;
};

static void sessionLoadTorrents(struct sessionLoadTorrentsData* const data)
{
    TR_ASSERT(data->session != nullptr);

    auto const& dirname = data->session->torrentDir();
    auto const info = tr_sys_path_get_info(dirname);
    auto const odir = info && info->isFolder() ? tr_sys_dir_open(dirname.c_str()) : TR_BAD_SYS_DIR;

    auto torrents = std::list<tr_torrent*>{};
    if (odir != TR_BAD_SYS_DIR)
    {
        char const* name = nullptr;
        while ((name = tr_sys_dir_read_name(odir)) != nullptr)
        {
            if (!tr_strvEndsWith(name, ".torrent"sv) && !tr_strvEndsWith(name, ".magnet"sv))
            {
                continue;
            }

            auto const path = tr_pathbuf{ dirname, '/', name };

            // is a magnet link?
            if (!tr_ctorSetMetainfoFromFile(data->ctor, path.sv(), nullptr))
            {
                if (auto buf = std::vector<char>{}; tr_loadFile(path, buf))
                {
                    tr_ctorSetMetainfoFromMagnetLink(data->ctor, std::string_view{ std::data(buf), std::size(buf) }, nullptr);
                }
            }

            if (tr_torrent* const tor = tr_torrentNew(data->ctor, nullptr); tor != nullptr)
            {
                torrents.push_back(tor);
            }
        }

        tr_sys_dir_close(odir);
    }

    int const n = std::size(torrents);
    data->torrents = tr_new(tr_torrent*, n); // NOLINT(bugprone-sizeof-expression)
    std::copy(std::begin(torrents), std::end(torrents), data->torrents);

    if (n != 0)
    {
        tr_logAddInfo(fmt::format(ngettext("Loaded {count} torrent", "Loaded {count} torrents", n), fmt::arg("count", n)));
    }

    if (data->setmeCount != nullptr)
    {
        *data->setmeCount = n;
    }

    data->done = true;
}

tr_torrent** tr_sessionLoadTorrents(tr_session* session, tr_ctor* ctor, int* setmeCount)
{
    struct sessionLoadTorrentsData data;

    data.session = session;
    data.ctor = ctor;
    data.setmeCount = setmeCount;
    data.torrents = nullptr;
    data.done = false;

    tr_runInEventThread(session, sessionLoadTorrents, &data);

    while (!data.done)
    {
        tr_wait_msec(100);
    }

    return data.torrents;
}

/***
****
***/

void tr_sessionSetPexEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->is_pex_enabled_ = enabled;
}

bool tr_sessionIsPexEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->allowsPEX();
}

bool tr_sessionIsDHTEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->allowsDHT();
}

void tr_sessionSetDHTEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    if (enabled == session->allowsDHT())
    {
        return;
    }

    tr_runInEventThread(
        session,
        [session, enabled]()
        {
            tr_udpUninit(session);
            session->is_dht_enabled_ = enabled;
            tr_udpInit(session);
        });
}

/***
****
***/

bool tr_session::allowsUTP() const noexcept
{
#ifdef WITH_UTP
    return is_utp_enabled_;
#else
    return false;
#endif
}

bool tr_sessionIsUTPEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->allowsUTP();
}

void tr_sessionSetUTPEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    if (enabled == session->allowsUTP())
    {
        return;
    }
    tr_runInEventThread(
        session,
        [session, enabled]()
        {
            session->is_utp_enabled_ = enabled;
            tr_udpSetSocketBuffers(session);
            tr_udpSetSocketTOS(session);
            // But don't call tr_utpClose --
            // see reset_timer in tr-utp.c for an explanation.
        });
}

/***
****
***/

void tr_sessionSetLPDEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    if (enabled == session->allowsLPD())
    {
        return;
    }

    tr_runInEventThread(
        session,
        [session, enabled]()
        {
            if (session->allowsLPD())
            {
                tr_lpdUninit(session);
            }

            session->is_lpd_enabled_ = enabled;

            if (session->allowsLPD())
            {
                tr_lpdInit(session, &session->bind_ipv4.addr_);
            }
        });
}

bool tr_sessionIsLPDEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->allowsLPD();
}

/***
****
***/

void tr_sessionSetCacheLimit_MB(tr_session* session, int mb)
{
    TR_ASSERT(session != nullptr);

    session->cache->setLimit(tr_toMemBytes(mb));
}

int tr_sessionGetCacheLimit_MB(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return tr_toMemMB(session->cache->getLimit());
}

/***
****
***/

void tr_session::setDefaultTrackers(std::string_view trackers)
{
    auto const oldval = default_trackers_;

    default_trackers_str_ = trackers;
    default_trackers_.parse(trackers);

    // if the list changed, update all the public torrents
    if (default_trackers_ != oldval)
    {
        for (auto* const tor : torrents())
        {
            if (tor->isPublic())
            {
                tr_announcerResetTorrent(announcer, tor);
            }
        }
    }
}

void tr_sessionSetDefaultTrackers(tr_session* session, char const* trackers)
{
    TR_ASSERT(session != nullptr);

    session->setDefaultTrackers(trackers != nullptr ? trackers : "");
}

/***
****
***/

tr_bandwidth& tr_session::getBandwidthGroup(std::string_view name)
{
    auto& groups = this->bandwidth_groups_;

    for (auto const& [group_name, group] : groups)
    {
        if (group_name == name)
        {
            return *group;
        }
    }

    auto& [group_name, group] = groups.emplace_back(name, std::make_unique<tr_bandwidth>(new tr_bandwidth(&top_bandwidth_)));
    return *group;
}

/***
****
***/

void tr_sessionSetPortForwardingEnabled(tr_session* session, bool enabled)
{
    tr_runInEventThread(session, tr_sharedTraversalEnable, session->shared, enabled);
}

bool tr_sessionIsPortForwardingEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return tr_sharedTraversalIsEnabled(session->shared);
}

/***
****
***/

void tr_session::loadBlocklists()
{
    auto loadme = std::unordered_set<std::string>{};
    auto const is_enabled = useBlocklist();

    /* walk the blocklist directory... */
    auto const dirname = tr_pathbuf{ configDir(), "/blocklists"sv };
    auto const odir = tr_sys_dir_open(dirname);

    if (odir == TR_BAD_SYS_DIR)
    {
        return;
    }

    char const* name = nullptr;
    while ((name = tr_sys_dir_read_name(odir)) != nullptr)
    {
        auto load = std::string{};

        if (name[0] == '.') /* ignore dotfiles */
        {
            continue;
        }

        if (auto const path = tr_pathbuf{ dirname, '/', name }; tr_strvEndsWith(path, ".bin"sv))
        {
            load = path;
        }
        else
        {
            auto const binname = tr_pathbuf{ dirname, '/', name, ".bin"sv };

            if (auto const bininfo = tr_sys_path_get_info(binname); !bininfo)
            {
                // create it
                auto b = BlocklistFile{ binname, is_enabled };
                if (auto const n = b.setContent(path); n > 0)
                {
                    load = binname;
                }
            }
            else if (auto const pathinfo = tr_sys_path_get_info(path);
                     pathinfo && pathinfo->last_modified_at >= bininfo->last_modified_at)
            {
                // update it
                auto const old = tr_pathbuf{ binname, ".old"sv };
                tr_sys_path_remove(old);
                tr_sys_path_rename(binname, old);

                BlocklistFile b(binname, is_enabled);

                if (b.setContent(path) > 0)
                {
                    tr_sys_path_remove(old);
                }
                else
                {
                    tr_sys_path_remove(binname);
                    tr_sys_path_rename(old, binname);
                }
            }
        }

        if (!std::empty(load))
        {
            loadme.emplace(load);
        }
    }

    blocklists_.clear();
    std::transform(
        std::begin(loadme),
        std::end(loadme),
        std::back_inserter(blocklists_),
        [&is_enabled](auto const& path) { return std::make_unique<BlocklistFile>(path.c_str(), is_enabled); });

    /* cleanup */
    tr_sys_dir_close(odir);
}

void tr_session::useBlocklist(bool enabled)
{
    this->blocklist_enabled_ = enabled;

    std::for_each(
        std::begin(blocklists_),
        std::end(blocklists_),
        [enabled](auto& blocklist) { blocklist->setEnabled(enabled); });
}

bool tr_session::addressIsBlocked(tr_address const& addr) const noexcept
{
    return std::any_of(
        std::begin(blocklists_),
        std::end(blocklists_),
        [&addr](auto& blocklist) { return blocklist->hasAddress(addr); });
}

void tr_sessionReloadBlocklists(tr_session* session)
{
    session->blocklists_.clear();
    session->loadBlocklists();

    tr_peerMgrOnBlocklistChanged(session->peerMgr);
}

size_t tr_blocklistGetRuleCount(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    auto& src = session->blocklists_;
    return std::accumulate(std::begin(src), std::end(src), 0, [](int sum, auto& cur) { return sum + cur->getRuleCount(); });
}

bool tr_blocklistIsEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->useBlocklist();
}

void tr_blocklistSetEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->useBlocklist(enabled);
}

bool tr_blocklistExists(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return !std::empty(session->blocklists_);
}

size_t tr_blocklistSetContent(tr_session* session, char const* content_filename)
{
    auto const lock = session->unique_lock();

    // find (or add) the default blocklist
    auto& src = session->blocklists_;
    char const* const name = DEFAULT_BLOCKLIST_FILENAME;
    auto const it = std::find_if(
        std::begin(src),
        std::end(src),
        [&name](auto const& blocklist) { return tr_strvEndsWith(blocklist->getFilename(), name); });

    BlocklistFile* b = nullptr;
    if (it == std::end(src))
    {
        auto path = tr_pathbuf{ session->configDir(), "/blocklists/"sv, name };
        src.push_back(std::make_unique<BlocklistFile>(path, session->useBlocklist()));
        b = std::rbegin(src)->get();
    }
    else
    {
        b = it->get();
    }

    // set the default blocklist's content
    auto const rule_count = b->setContent(content_filename);
    return rule_count;
}

void tr_blocklistSetURL(tr_session* session, char const* url)
{
    session->setBlocklistUrl(url != nullptr ? url : "");
}

char const* tr_blocklistGetURL(tr_session const* session)
{
    return session->blocklistUrl().c_str();
}

/***
****
***/

void tr_session::setRpcWhitelist(std::string_view whitelist) const
{
    this->rpc_server_->setWhitelist(whitelist);
}

void tr_session::useRpcWhitelist(bool enabled) const
{
    this->rpc_server_->setWhitelistEnabled(enabled);
}

bool tr_session::useRpcWhitelist() const
{
    return this->rpc_server_->isWhitelistEnabled();
}

void tr_sessionSetRPCEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setEnabled(is_enabled);
}

bool tr_sessionIsRPCEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->isEnabled();
}

void tr_sessionSetRPCPort(tr_session* session, uint16_t hport)
{
    TR_ASSERT(session != nullptr);

    if (session->rpc_server_)
    {
        session->rpc_server_->setPort(tr_port::fromHost(hport));
    }
}

uint16_t tr_sessionGetRPCPort(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_ ? session->rpc_server_->port().host() : uint16_t{};
}

void tr_sessionSetRPCUrl(tr_session* session, char const* url)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setUrl(url != nullptr ? url : "");
}

char const* tr_sessionGetRPCUrl(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->url().c_str();
}

void tr_sessionSetRPCCallback(tr_session* session, tr_rpc_func func, void* user_data)
{
    TR_ASSERT(session != nullptr);

    session->rpc_func_ = func;
    session->rpc_func_user_data_ = user_data;
}

void tr_sessionSetRPCWhitelist(tr_session* session, char const* whitelist)
{
    TR_ASSERT(session != nullptr);

    session->setRpcWhitelist(whitelist != nullptr ? whitelist : "");
}

char const* tr_sessionGetRPCWhitelist(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->whitelist().c_str();
}

void tr_sessionSetRPCWhitelistEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->useRpcWhitelist(enabled);
}

bool tr_sessionGetRPCWhitelistEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->useRpcWhitelist();
}

void tr_sessionSetRPCPassword(tr_session* session, char const* password)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setPassword(password != nullptr ? password : "");
}

char const* tr_sessionGetRPCPassword(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->getSaltedPassword().c_str();
}

void tr_sessionSetRPCUsername(tr_session* session, char const* username)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setUsername(username != nullptr ? username : "");
}

char const* tr_sessionGetRPCUsername(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->username().c_str();
}

void tr_sessionSetRPCPasswordEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setPasswordEnabled(enabled);
}

bool tr_sessionIsRPCPasswordEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->isPasswordEnabled();
}

/****
*****
****/

void tr_sessionSetScriptEnabled(tr_session* session, TrScript type, bool enabled)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    session->useScript(type, enabled);
}

bool tr_sessionIsScriptEnabled(tr_session const* session, TrScript type)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    return session->useScript(type);
}

void tr_sessionSetScript(tr_session* session, TrScript type, char const* script)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    session->setScript(type, script != nullptr ? script : "");
}

char const* tr_sessionGetScript(tr_session const* session, TrScript type)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    return session->script(type).c_str();
}

/***
****
***/

void tr_sessionSetQueueSize(tr_session* session, tr_direction dir, int max_simultaneous_seed_torrents)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    session->queue_size_[dir] = max_simultaneous_seed_torrents;
}

int tr_sessionGetQueueSize(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return session->queueSize(dir);
}

void tr_sessionSetQueueEnabled(tr_session* session, tr_direction dir, bool do_limit_simultaneous_seed_torrents)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    session->queue_enabled_[dir] = do_limit_simultaneous_seed_torrents;
}

bool tr_sessionGetQueueEnabled(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return session->queueEnabled(dir);
}

void tr_sessionSetQueueStalledMinutes(tr_session* session, int minutes)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(minutes > 0);

    session->queue_stalled_minutes_ = minutes;
}

void tr_sessionSetQueueStalledEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(session != nullptr);

    session->queue_stalled_enabled_ = is_enabled;
}

bool tr_sessionGetQueueStalledEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->queueStalledEnabled();
}

int tr_sessionGetQueueStalledMinutes(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->queueStalledMinutes();
}

void tr_sessionSetAntiBruteForceThreshold(tr_session* session, int max_bad_requests)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(max_bad_requests > 0);

    session->rpc_server_->setAntiBruteForceLimit(max_bad_requests);
}

int tr_sessionGetAntiBruteForceThreshold(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->getAntiBruteForceLimit();
}

void tr_sessionSetAntiBruteForceEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(session != nullptr);

    session->rpc_server_->setAntiBruteForceEnabled(is_enabled);
}

bool tr_sessionGetAntiBruteForceEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->rpc_server_->isAntiBruteForceEnabled();
}

std::vector<tr_torrent*> tr_session::getNextQueuedTorrents(tr_direction dir, size_t num_wanted) const
{
    TR_ASSERT(tr_isDirection(dir));

    // build an array of the candidates
    auto candidates = std::vector<tr_torrent*>{};
    candidates.reserve(std::size(torrents()));
    for (auto* const tor : torrents())
    {
        if (tor->isQueued() && (dir == tor->queueDirection()))
        {
            candidates.push_back(tor);
        }
    }

    // find the best n candidates
    num_wanted = std::min(num_wanted, std::size(candidates));
    if (num_wanted < candidates.size())
    {
        std::partial_sort(
            std::begin(candidates),
            std::begin(candidates) + num_wanted,
            std::end(candidates),
            [](auto const* a, auto const* b) { return tr_torrentGetQueuePosition(a) < tr_torrentGetQueuePosition(b); });
        candidates.resize(num_wanted);
    }

    return candidates;
}

size_t tr_session::countQueueFreeSlots(tr_direction dir) const noexcept
{
    if (!queueEnabled(dir))
    {
        return std::numeric_limits<size_t>::max();
    }

    auto const max = queueSize(dir);
    auto const activity = dir == TR_UP ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;

    /* count how many torrents are active */
    int active_count = 0;
    bool const stalled_enabled = queueStalledEnabled();
    int const stalled_if_idle_for_n_seconds = queueStalledMinutes() * 60;
    time_t const now = tr_time();
    for (auto const* const tor : torrents())
    {
        /* is it the right activity? */
        if (activity != tr_torrentGetActivity(tor))
        {
            continue;
        }

        /* is it stalled? */
        if (stalled_enabled)
        {
            auto const idle_secs = int(difftime(now, std::max(tor->startDate, tor->activityDate)));
            if (idle_secs >= stalled_if_idle_for_n_seconds)
            {
                continue;
            }
        }

        ++active_count;

        /* if we've reached the limit, no need to keep counting */
        if (active_count >= max)
        {
            return 0;
        }
    }

    return max - active_count;
}

static void bandwidthGroupRead(tr_session* session, std::string_view config_dir)
{
    auto const filename = tr_pathbuf{ config_dir, '/', BandwidthGroupsFilename };
    auto groups_dict = tr_variant{};
    if (!tr_sys_path_exists(filename) || !tr_variantFromFile(&groups_dict, TR_VARIANT_PARSE_JSON, filename, nullptr) ||
        !tr_variantIsDict(&groups_dict))
    {
        return;
    }

    auto idx = size_t{ 0 };
    auto key = tr_quark{};
    tr_variant* dict = nullptr;
    while (tr_variantDictChild(&groups_dict, idx, &key, &dict))
    {
        ++idx;

        auto name = tr_interned_string(key);
        auto& group = session->getBandwidthGroup(name);

        auto limits = tr_bandwidth_limits{};
        tr_variantDictFindBool(dict, TR_KEY_uploadLimited, &limits.up_limited);
        tr_variantDictFindBool(dict, TR_KEY_downloadLimited, &limits.down_limited);

        if (auto limit = int64_t{}; tr_variantDictFindInt(dict, TR_KEY_uploadLimit, &limit))
        {
            limits.up_limit_KBps = limit;
        }

        if (auto limit = int64_t{}; tr_variantDictFindInt(dict, TR_KEY_downloadLimit, &limit))
        {
            limits.down_limit_KBps = limit;
        }

        group.setLimits(&limits);

        if (auto honors = bool{}; tr_variantDictFindBool(dict, TR_KEY_honorsSessionLimits, &honors))
        {
            group.honorParentLimits(TR_UP, honors);
            group.honorParentLimits(TR_DOWN, honors);
        }
    }
    tr_variantFree(&groups_dict);
}

static int bandwidthGroupWrite(tr_session const* session, std::string_view config_dir)
{
    auto const& groups = session->bandwidth_groups_;

    auto groups_dict = tr_variant{};
    tr_variantInitDict(&groups_dict, std::size(groups));

    for (auto const& [name, group] : groups)
    {
        auto const limits = group->getLimits();

        auto* const dict = tr_variantDictAddDict(&groups_dict, name.quark(), 5);
        tr_variantDictAddStrView(dict, TR_KEY_name, name.sv());
        tr_variantDictAddBool(dict, TR_KEY_uploadLimited, limits.up_limited);
        tr_variantDictAddInt(dict, TR_KEY_uploadLimit, limits.up_limit_KBps);
        tr_variantDictAddBool(dict, TR_KEY_downloadLimited, limits.down_limited);
        tr_variantDictAddInt(dict, TR_KEY_downloadLimit, limits.down_limit_KBps);
        tr_variantDictAddBool(dict, TR_KEY_honorsSessionLimits, group->areParentLimitsHonored(TR_UP));
    }

    auto const filename = tr_pathbuf{ config_dir, '/', BandwidthGroupsFilename };
    auto const ret = tr_variantToFile(&groups_dict, TR_VARIANT_FMT_JSON, filename);
    tr_variantFree(&groups_dict);
    return ret;
}

///

void tr_session::closeTorrentFiles(tr_torrent* tor) noexcept
{
    this->cache->flushTorrent(tor);
    openFiles().closeTorrent(tor->id());
}

void tr_session::closeTorrentFile(tr_torrent* tor, tr_file_index_t file_num) noexcept
{
    this->cache->flushFile(tor, file_num);
    openFiles().closeFile(tor->id(), file_num);
}

///

void tr_sessionSetQueueStartCallback(tr_session* session, void (*callback)(tr_session*, tr_torrent*, void*), void* user_data)
{
    session->setQueueStartCallback(callback, user_data);
}

void tr_sessionSetRatioLimitHitCallback(tr_session* session, tr_session_ratio_limit_hit_func callback, void* user_data)
{
    session->setRatioLimitHitCallback(callback, user_data);
}

void tr_sessionSetIdleLimitHitCallback(tr_session* session, tr_session_idle_limit_hit_func callback, void* user_data)
{
    session->setIdleLimitHitCallback(callback, user_data);
}

void tr_sessionSetMetadataCallback(tr_session* session, tr_session_metadata_func callback, void* user_data)
{
    session->setMetadataCallback(callback, user_data);
}

void tr_sessionSetCompletenessCallback(tr_session* session, tr_torrent_completeness_func callback, void* user_data)
{
    session->setTorrentCompletenessCallback(callback, user_data);
}

tr_session_stats tr_sessionGetStats(tr_session const* session)
{
    return session->stats().current();
}

tr_session_stats tr_sessionGetCumulativeStats(tr_session const* session)
{
    return session->stats().cumulative();
}

void tr_sessionClearStats(tr_session* session)
{
    session->stats().clear();
}

namespace
{

auto makeResumeDir(std::string_view config_dir)
{
#if defined(__APPLE__) || defined(_WIN32)
    auto dir = fmt::format("{:s}/Resume"sv, config_dir);
#else
    auto dir = fmt::format("{:s}/resume"sv, config_dir);
#endif
    tr_sys_dir_create(dir.c_str(), TR_SYS_DIR_CREATE_PARENTS, 0777);
    return dir;
}

auto makeTorrentDir(std::string_view config_dir)
{
#if defined(__APPLE__) || defined(_WIN32)
    auto dir = fmt::format("{:s}/Torrents"sv, config_dir);
#else
    auto dir = fmt::format("{:s}/torrents"sv, config_dir);
#endif
    tr_sys_dir_create(dir.c_str(), TR_SYS_DIR_CREATE_PARENTS, 0777);
    return dir;
}

auto makeEventBase()
{
    tr_evthread_init();
    return std::shared_ptr<event_base>{ event_base_new(), event_base_free };
}

} // namespace

tr_session::tr_session(std::string_view config_dir)
    : session_id_{ tr_time }
    , event_base_{ makeEventBase() }
    , evdns_base_{ evdns_base_new(eventBase(), EVDNS_BASE_INITIALIZE_NAMESERVERS),
                   [](evdns_base* dns)
                   {
                       // if zero, active requests will be aborted
                       evdns_base_free(dns, 0);
                   } }
    , timer_maker_{ std::make_unique<libtransmission::EvTimerMaker>(eventBase()) }
    , config_dir_{ config_dir }
    , resume_dir_{ makeResumeDir(config_dir) }
    , torrent_dir_{ makeTorrentDir(config_dir) }
    , session_stats_{ config_dir, time(nullptr) }
{
    now_timer_ = timerMaker().create([this]() { onNowTimer(); });
    now_timer_->startRepeating(1s);

    // Periodically save the .resume files of any torrents whose
    // status has recently changed. This prevents loss of metadata
    // in the case of a crash, unclean shutdown, clumsy user, etc.
    save_timer_ = timerMaker().create(
        [this]()
        {
            for (auto* const tor : torrents())
            {
                tr_torrentSave(tor);
            }

            stats().saveIfDirty();
        });
    save_timer_->startRepeating(SaveIntervalSecs);
}
