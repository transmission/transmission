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
#include <ctime>
#include <iterator> // std::back_inserter
#include <list>
#include <memory>
#include <numeric> // std::accumulate()
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <sys/types.h> /* umask() */
#include <sys/stat.h> /* umask() */
#endif

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
#include "platform-quota.h" /* tr_device_info_free() */
#include "platform.h" /* tr_getTorrentDir() */
#include "port-forwarding.h"
#include "rpc-server.h"
#include "session-id.h"
#include "session.h"
#include "stats.h"
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
static auto constexpr SaveIntervalSecs = int{ 360 };

static void bandwidthGroupRead(tr_session* session, std::string_view config_dir);
static int bandwidthGroupWrite(tr_session const* session, std::string_view config_dir);
static auto constexpr BandwidthGroupsFilename = "bandwidth-groups.json"sv;

static tr_port getRandomPort(tr_session const* s)
{
    auto const lower = std::min(s->randomPortLow.host(), s->randomPortHigh.host());
    auto const upper = std::max(s->randomPortLow.host(), s->randomPortHigh.host());
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
    auto const path = tr_pathbuf{ session_->config_dir, "/cookies.txt" };

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
        auto is_default_value = bool{};
        tr_address const* addr = tr_sessionGetPublicAddress(session_, type, &is_default_value);
        if (addr != nullptr && !is_default_value)
        {
            return addr->readable();
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

    return session->encryptionMode;
}

void tr_sessionSetEncryption(tr_session* session, tr_encryption_mode mode)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(mode == TR_ENCRYPTION_PREFERRED || mode == TR_ENCRYPTION_REQUIRED || mode == TR_CLEAR_PREFERRED);

    session->encryptionMode = mode;
}

/***
****
***/

static void close_bindinfo(struct tr_bindinfo* b)
{
    if (b != nullptr && b->socket != TR_BAD_SOCKET)
    {
        event_free(b->ev);
        b->ev = nullptr;
        tr_netCloseSocket(b->socket);
    }
}

static void close_incoming_peer_port(tr_session* session)
{
    close_bindinfo(session->bind_ipv4);
    close_bindinfo(session->bind_ipv6);
}

static void free_incoming_peer_port(tr_session* session)
{
    close_bindinfo(session->bind_ipv4);
    tr_free(session->bind_ipv4);
    session->bind_ipv4 = nullptr;

    close_bindinfo(session->bind_ipv6);
    tr_free(session->bind_ipv6);
    session->bind_ipv6 = nullptr;
}

static void accept_incoming_peer(evutil_socket_t fd, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    auto clientAddr = tr_address{};
    auto clientPort = tr_port{};
    auto const clientSocket = tr_netAccept(session, fd, &clientAddr, &clientPort);

    if (clientSocket != TR_BAD_SOCKET)
    {
        char addrstr[TR_ADDRSTRLEN];
        tr_address_and_port_to_string(addrstr, sizeof(addrstr), &clientAddr, clientPort);
        tr_logAddTrace(fmt::format("new incoming connection {} ({})", clientSocket, addrstr));

        tr_peerMgrAddIncoming(session->peerMgr, &clientAddr, clientPort, tr_peer_socket_tcp_create(clientSocket));
    }
}

static void open_incoming_peer_port(tr_session* session)
{
    /* bind an ipv4 port to listen for incoming peers... */
    auto* b = session->bind_ipv4;
    b->socket = tr_netBindTCP(&b->addr, session->private_peer_port, false);

    if (b->socket != TR_BAD_SOCKET)
    {
        b->ev = event_new(session->event_base, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session);
        event_add(b->ev, nullptr);
    }

    /* and do the exact same thing for ipv6, if it's supported... */
    if (tr_net_hasIPv6(session->private_peer_port))
    {
        b = session->bind_ipv6;
        b->socket = tr_netBindTCP(&b->addr, session->private_peer_port, false);

        if (b->socket != TR_BAD_SOCKET)
        {
            b->ev = event_new(session->event_base, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session);
            event_add(b->ev, nullptr);
        }
    }
}

tr_address const* tr_sessionGetPublicAddress(tr_session const* session, int tr_af_type, bool* is_default_value)
{
    char const* default_value = "";
    tr_bindinfo const* bindinfo = nullptr;

    switch (tr_af_type)
    {
    case TR_AF_INET:
        bindinfo = session->bind_ipv4;
        default_value = TR_DEFAULT_BIND_ADDRESS_IPV4;
        break;

    case TR_AF_INET6:
        bindinfo = session->bind_ipv6;
        default_value = TR_DEFAULT_BIND_ADDRESS_IPV6;
        break;

    default:
        break;
    }

    if (is_default_value != nullptr && bindinfo != nullptr)
    {
        *is_default_value = bindinfo->addr.readable() == default_value;
    }

    return bindinfo != nullptr ? &bindinfo->addr : nullptr;
}

/***
****
***/

#ifdef TR_LIGHTWEIGHT
#define TR_DEFAULT_ENCRYPTION TR_CLEAR_PREFERRED
#else
#define TR_DEFAULT_ENCRYPTION TR_ENCRYPTION_PREFERRED
#endif

void tr_sessionGetDefaultSettings(tr_variant* d)
{
    TR_ASSERT(tr_variantIsDict(d));

    tr_variantDictReserve(d, 71);
    tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, false);
    tr_variantDictAddStrView(d, TR_KEY_blocklist_url, "http://www.example.com/blocklist"sv);
    tr_variantDictAddInt(d, TR_KEY_cache_size_mb, DefaultCacheSizeMB);
    tr_variantDictAddBool(d, TR_KEY_dht_enabled, true);
    tr_variantDictAddBool(d, TR_KEY_utp_enabled, true);
    tr_variantDictAddBool(d, TR_KEY_lpd_enabled, false);
    tr_variantDictAddStr(d, TR_KEY_download_dir, tr_getDefaultDownloadDir());
    tr_variantDictAddStr(d, TR_KEY_default_trackers, "");
    tr_variantDictAddInt(d, TR_KEY_speed_limit_down, 100);
    tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, false);
    tr_variantDictAddInt(d, TR_KEY_encryption, TR_DEFAULT_ENCRYPTION);
    tr_variantDictAddInt(d, TR_KEY_idle_seeding_limit, 30);
    tr_variantDictAddBool(d, TR_KEY_idle_seeding_limit_enabled, false);
    tr_variantDictAddStr(d, TR_KEY_incomplete_dir, tr_getDefaultDownloadDir());
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
    tr_variantDictAddInt(d, TR_KEY_upload_slots_per_torrent, 14);
    tr_variantDictAddStrView(d, TR_KEY_bind_address_ipv4, TR_DEFAULT_BIND_ADDRESS_IPV4);
    tr_variantDictAddStrView(d, TR_KEY_bind_address_ipv6, TR_DEFAULT_BIND_ADDRESS_IPV6);
    tr_variantDictAddBool(d, TR_KEY_start_added_torrents, true);
    tr_variantDictAddBool(d, TR_KEY_trash_original_torrent_files, false);
    tr_variantDictAddInt(d, TR_KEY_anti_brute_force_threshold, 100);
    tr_variantDictAddBool(d, TR_KEY_anti_brute_force_enabled, true);
}

void tr_sessionGetSettings(tr_session const* s, tr_variant* d)
{
    TR_ASSERT(tr_variantIsDict(d));

    tr_variantDictReserve(d, 70);
    tr_variantDictAddBool(d, TR_KEY_blocklist_enabled, s->useBlocklist());
    tr_variantDictAddStr(d, TR_KEY_blocklist_url, s->blocklistUrl());
    tr_variantDictAddInt(d, TR_KEY_cache_size_mb, tr_sessionGetCacheLimit_MB(s));
    tr_variantDictAddBool(d, TR_KEY_dht_enabled, s->isDHTEnabled);
    tr_variantDictAddBool(d, TR_KEY_utp_enabled, s->isUTPEnabled);
    tr_variantDictAddBool(d, TR_KEY_lpd_enabled, s->isLPDEnabled);
    tr_variantDictAddStr(d, TR_KEY_download_dir, tr_sessionGetDownloadDir(s));
    tr_variantDictAddStr(d, TR_KEY_default_trackers, s->defaultTrackersStr());
    tr_variantDictAddInt(d, TR_KEY_download_queue_size, tr_sessionGetQueueSize(s, TR_DOWN));
    tr_variantDictAddBool(d, TR_KEY_download_queue_enabled, tr_sessionGetQueueEnabled(s, TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_speed_limit_down, tr_sessionGetSpeedLimit_KBps(s, TR_DOWN));
    tr_variantDictAddBool(d, TR_KEY_speed_limit_down_enabled, tr_sessionIsSpeedLimited(s, TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_encryption, s->encryptionMode);
    tr_variantDictAddInt(d, TR_KEY_idle_seeding_limit, tr_sessionGetIdleLimit(s));
    tr_variantDictAddBool(d, TR_KEY_idle_seeding_limit_enabled, tr_sessionIsIdleLimited(s));
    tr_variantDictAddStr(d, TR_KEY_incomplete_dir, tr_sessionGetIncompleteDir(s));
    tr_variantDictAddBool(d, TR_KEY_incomplete_dir_enabled, tr_sessionIsIncompleteDirEnabled(s));
    tr_variantDictAddInt(d, TR_KEY_message_level, tr_logGetLevel());
    tr_variantDictAddInt(d, TR_KEY_peer_limit_global, s->peerLimit);
    tr_variantDictAddInt(d, TR_KEY_peer_limit_per_torrent, s->peerLimitPerTorrent);
    tr_variantDictAddInt(d, TR_KEY_peer_port, s->peerPort().host());
    tr_variantDictAddBool(d, TR_KEY_peer_port_random_on_start, s->isPortRandom);
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_low, s->randomPortLow.host());
    tr_variantDictAddInt(d, TR_KEY_peer_port_random_high, s->randomPortHigh.host());
    tr_variantDictAddStr(d, TR_KEY_peer_socket_tos, tr_netTosToName(s->peer_socket_tos_));
    tr_variantDictAddStr(d, TR_KEY_peer_congestion_algorithm, s->peerCongestionAlgorithm());
    tr_variantDictAddBool(d, TR_KEY_pex_enabled, s->isPexEnabled);
    tr_variantDictAddBool(d, TR_KEY_port_forwarding_enabled, tr_sessionIsPortForwardingEnabled(s));
    tr_variantDictAddInt(d, TR_KEY_preallocation, s->preallocationMode);
    tr_variantDictAddBool(d, TR_KEY_prefetch_enabled, s->isPrefetchEnabled);
    tr_variantDictAddInt(d, TR_KEY_peer_id_ttl_hours, s->peer_id_ttl_hours);
    tr_variantDictAddBool(d, TR_KEY_queue_stalled_enabled, tr_sessionGetQueueStalledEnabled(s));
    tr_variantDictAddInt(d, TR_KEY_queue_stalled_minutes, tr_sessionGetQueueStalledMinutes(s));
    tr_variantDictAddReal(d, TR_KEY_ratio_limit, s->desiredRatio);
    tr_variantDictAddBool(d, TR_KEY_ratio_limit_enabled, s->isRatioLimited);
    tr_variantDictAddBool(d, TR_KEY_rename_partial_files, tr_sessionIsIncompleteFileNamingEnabled(s));
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
    tr_variantDictAddBool(d, TR_KEY_scrape_paused_torrents_enabled, s->scrapePausedTorrents);
    tr_variantDictAddInt(d, TR_KEY_seed_queue_size, tr_sessionGetQueueSize(s, TR_UP));
    tr_variantDictAddBool(d, TR_KEY_seed_queue_enabled, tr_sessionGetQueueEnabled(s, TR_UP));
    tr_variantDictAddBool(d, TR_KEY_alt_speed_enabled, tr_sessionUsesAltSpeed(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_up, tr_sessionGetAltSpeed_KBps(s, TR_UP));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_down, tr_sessionGetAltSpeed_KBps(s, TR_DOWN));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_begin, tr_sessionGetAltSpeedBegin(s));
    tr_variantDictAddBool(d, TR_KEY_alt_speed_time_enabled, tr_sessionUsesAltSpeedTime(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_end, tr_sessionGetAltSpeedEnd(s));
    tr_variantDictAddInt(d, TR_KEY_alt_speed_time_day, tr_sessionGetAltSpeedDay(s));
    tr_variantDictAddInt(d, TR_KEY_speed_limit_up, tr_sessionGetSpeedLimit_KBps(s, TR_UP));
    tr_variantDictAddBool(d, TR_KEY_speed_limit_up_enabled, tr_sessionIsSpeedLimited(s, TR_UP));
    tr_variantDictAddStr(d, TR_KEY_umask, fmt::format("{:#o}", s->umask));
    tr_variantDictAddInt(d, TR_KEY_upload_slots_per_torrent, s->uploadSlotsPerTorrent);
    tr_variantDictAddStr(d, TR_KEY_bind_address_ipv4, tr_address_to_string(&s->bind_ipv4->addr));
    tr_variantDictAddStr(d, TR_KEY_bind_address_ipv6, tr_address_to_string(&s->bind_ipv6->addr));
    tr_variantDictAddBool(d, TR_KEY_start_added_torrents, !tr_sessionGetPaused(s));
    tr_variantDictAddBool(d, TR_KEY_trash_original_torrent_files, tr_sessionGetDeleteSource(s));
    tr_variantDictAddInt(d, TR_KEY_anti_brute_force_threshold, tr_sessionGetAntiBruteForceThreshold(s));
    tr_variantDictAddBool(d, TR_KEY_anti_brute_force_enabled, tr_sessionGetAntiBruteForceEnabled(s));
    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        tr_variantDictAddBool(d, enabled_key, s->useScript(script));
        tr_variantDictAddStr(d, script_key, s->script(script));
    }
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

    /* if caller didn't specify a config dir, use the default */
    if (tr_str_is_empty(config_dir))
    {
        config_dir = tr_getDefaultConfigDir(appName);
    }

    /* file settings override the defaults */
    auto fileSettings = tr_variant{};
    auto const filename = tr_pathbuf{ config_dir, "/settings.json"sv };
    auto success = bool{};
    if (tr_error* error = nullptr; tr_variantFromFile(&fileSettings, TR_VARIANT_PARSE_JSON, filename, &error))
    {
        tr_variantMergeDicts(dict, &fileSettings);
        tr_variantFree(&fileSettings);
        success = true;
    }
    else
    {
        success = TR_ERROR_IS_ENOENT(error->code);
        tr_error_free(error);
    }

    /* cleanup */
    return success;
}

void tr_sessionSaveSettings(tr_session* session, char const* config_dir, tr_variant const* clientSettings)
{
    TR_ASSERT(tr_variantIsDict(clientSettings));

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
    tr_variantMergeDicts(&settings, clientSettings);

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

/**
 * Periodically save the .resume files of any torrents whose
 * status has recently changed. This prevents loss of metadata
 * in the case of a crash, unclean shutdown, clumsy user, etc.
 */
static void onSaveTimer(evutil_socket_t /*fd*/, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    for (auto* const tor : session->torrents())
    {
        tr_torrentSave(tor);
    }

    tr_statsSaveDirty(session);

    tr_timerAdd(*session->saveTimer, SaveIntervalSecs, 0);
}

/***
****
***/

struct init_data
{
    bool messageQueuingEnabled;
    tr_session* session;
    char const* config_dir;
    tr_variant* clientSettings;
    std::condition_variable_any done_cv;
};

static void tr_sessionInitImpl(init_data* data);

tr_session* tr_sessionInit(char const* config_dir, bool messageQueuingEnabled, tr_variant* clientSettings)
{
    TR_ASSERT(tr_variantIsDict(clientSettings));

    tr_timeUpdate(time(nullptr));

    /* initialize the bare skeleton of the session object */
    auto* session = new tr_session{};
    session->udp_socket = TR_BAD_SOCKET;
    session->udp6_socket = TR_BAD_SOCKET;
    session->cache = std::make_unique<Cache>(session->torrents(), 1024 * 1024 * 2);
    session->magicNumber = SESSION_MAGIC_NUMBER;
    session->session_id = tr_session_id_new();
    bandwidthGroupRead(session, config_dir);

    /* nice to start logging at the very beginning */
    if (auto i = int64_t{}; tr_variantDictFindInt(clientSettings, TR_KEY_message_level, &i))
    {
        tr_logSetLevel(tr_log_level(i));
    }

    /* start the libtransmission thread */
    tr_net_init(); /* must go before tr_eventInit */
    tr_eventInit(session);
    TR_ASSERT(session->events != nullptr);

    auto data = init_data{};
    data.session = session;
    data.config_dir = config_dir;
    data.messageQueuingEnabled = messageQueuingEnabled;
    data.clientSettings = clientSettings;

    // run it in the libtransmission thread
    if (tr_amInEventThread(session))
    {
        tr_sessionInitImpl(&data);
    }
    else
    {
        auto lock = session->unique_lock();
        tr_runInEventThread(session, tr_sessionInitImpl, &data);
        data.done_cv.wait(lock); // wait for the session to be ready
    }

    return session;
}

static void turtleCheckClock(tr_session* s, struct tr_turtle_info* t);

static void onNowTimer(evutil_socket_t /*fd*/, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(session->nowTimer != nullptr);

    time_t const now = time(nullptr);

    /**
    ***  tr_session things to do once per second
    **/

    auto& max = session->max_observed_dl_speed_Bps_;
    max = std::max(max, tr_sessionGetPieceSpeed_Bps(session, TR_PEER_TO_CLIENT));

    tr_timeUpdate(now);

    tr_dhtUpkeep(session);

    if (session->turtle.isClockEnabled)
    {
        turtleCheckClock(session, &session->turtle);
    }

    // TODO: this seems a little silly. Why do we increment this
    // every second instead of computing the value as needed by
    // subtracting the current time from a start time?
    for (auto* const tor : session->torrents())
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

    /**
    ***  Set the timer
    **/

    /* schedule the next timer for right after the next second begins */
    auto const tv = tr_gettimeofday();
    int constexpr Min = 100;
    int constexpr Max = 999999;
    int const usec = std::clamp(int(1000000 - tv.tv_usec), Min, Max);

    tr_timerAdd(*session->nowTimer, 0, usec);
}

static void loadBlocklists(tr_session* session);

static void tr_sessionInitImpl(init_data* data)
{
    tr_variant const* const clientSettings = data->clientSettings;
    tr_session* session = data->session;
    auto lock = session->unique_lock();

    TR_ASSERT(tr_amInEventThread(session));
    TR_ASSERT(tr_variantIsDict(clientSettings));

    tr_logAddTrace(
        fmt::format("tr_sessionInit: the session's top-level bandwidth object is {}", fmt::ptr(&session->top_bandwidth_)));

    tr_variant settings;

    tr_variantInitDict(&settings, 0);
    tr_sessionGetDefaultSettings(&settings);
    tr_variantMergeDicts(&settings, clientSettings);

    TR_ASSERT(session->event_base != nullptr);
    session->nowTimer = evtimer_new(session->event_base, onNowTimer, session);
    onNowTimer(0, 0, session);

#ifndef _WIN32
    /* Don't exit when writing on a broken socket */
    (void)signal(SIGPIPE, SIG_IGN);
#endif

    tr_logSetQueueEnabled(data->messageQueuingEnabled);

    tr_setConfigDir(session, data->config_dir);

    session->peerMgr = tr_peerMgrNew(session);

    session->shared = tr_sharedInit(session);

    /**
    ***  Blocklist
    **/

    tr_sys_dir_create(tr_pathbuf{ session->config_dir, "/blocklists"sv }, TR_SYS_DIR_CREATE_PARENTS, 0777);
    loadBlocklists(session);

    TR_ASSERT(tr_isSession(session));

    session->saveTimer = evtimer_new(session->event_base, onSaveTimer, session);
    tr_timerAdd(*session->saveTimer, SaveIntervalSecs, 0);

    tr_announcerInit(session);

    tr_logAddInfo(fmt::format(_("Transmission version {version} starting"), fmt::arg("version", LONG_VERSION_STRING)));

    tr_statsInit(session);

    tr_sessionSet(session, &settings);

    tr_udpInit(session);

    session->web = tr_web::create(session->web_mediator);

    if (session->isLPDEnabled)
    {
        tr_lpdInit(session, &session->bind_ipv4->addr);
    }

    /* cleanup */
    tr_variantFree(&settings);
    data->done_cv.notify_one();
}

static void turtleBootstrap(tr_session* /*session*/, struct tr_turtle_info* /*turtle*/);
static void setPeerPort(tr_session* session, tr_port port);

static void sessionSetImpl(struct init_data* const data)
{
    tr_session* const session = data->session;
    tr_variant* const settings = data->clientSettings;

    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_variantIsDict(settings));
    TR_ASSERT(tr_amInEventThread(session));

    auto b = tr_bindinfo{};
    auto boolVal = bool{};
    auto d = double{};
    auto i = int64_t{};
    auto sv = std::string_view{};
    tr_turtle_info* const turtle = &session->turtle;

    if (tr_variantDictFindInt(settings, TR_KEY_message_level, &i))
    {
        tr_logSetLevel(tr_log_level(i));
    }

#ifndef _WIN32

    if (tr_variantDictFindStrView(settings, TR_KEY_umask, &sv))
    {
        /* Read a umask as a string representing an octal number. */
        session->umask = static_cast<mode_t>(tr_parseNum<uint32_t>(sv, 8).value_or(DefaultUmask));
        umask(session->umask);
    }
    else if (tr_variantDictFindInt(settings, TR_KEY_umask, &i))
    {
        /* Or as a base 10 integer to remain compatible with the old settings format. */
        session->umask = (mode_t)i;
        umask(session->umask);
    }

#endif

    /* misc features */
    if (tr_variantDictFindInt(settings, TR_KEY_cache_size_mb, &i))
    {
        tr_sessionSetCacheLimit_MB(session, i);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_default_trackers, &sv))
    {
        session->setDefaultTrackers(sv);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_limit_per_torrent, &i))
    {
        tr_sessionSetPeerLimitPerTorrent(session, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_pex_enabled, &boolVal))
    {
        tr_sessionSetPexEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_dht_enabled, &boolVal))
    {
        tr_sessionSetDHTEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_utp_enabled, &boolVal))
    {
        tr_sessionSetUTPEnabled(session, boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_lpd_enabled, &boolVal))
    {
        tr_sessionSetLPDEnabled(session, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_encryption, &i))
    {
        tr_sessionSetEncryption(session, tr_encryption_mode(i));
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_socket_tos, &i))
    {
        session->peer_socket_tos_ = i;
    }
    else if (tr_variantDictFindStrView(settings, TR_KEY_peer_socket_tos, &sv))
    {
        if (auto ip_tos = tr_netTosFromName(sv); ip_tos)
        {
            session->peer_socket_tos_ = *ip_tos;
        }
    }

    sv = ""sv;
    (void)tr_variantDictFindStrView(settings, TR_KEY_peer_congestion_algorithm, &sv);
    session->setPeerCongestionAlgorithm(sv);

    if (tr_variantDictFindBool(settings, TR_KEY_blocklist_enabled, &boolVal))
    {
        session->useBlocklist(boolVal);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_blocklist_url, &sv))
    {
        session->setBlocklistUrl(sv);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_start_added_torrents, &boolVal))
    {
        tr_sessionSetPaused(session, !boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_trash_original_torrent_files, &boolVal))
    {
        tr_sessionSetDeleteSource(session, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_id_ttl_hours, &i))
    {
        session->peer_id_ttl_hours = i;
    }

    /* torrent queues */
    if (tr_variantDictFindInt(settings, TR_KEY_queue_stalled_minutes, &i))
    {
        tr_sessionSetQueueStalledMinutes(session, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_queue_stalled_enabled, &boolVal))
    {
        tr_sessionSetQueueStalledEnabled(session, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_download_queue_size, &i))
    {
        tr_sessionSetQueueSize(session, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_download_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_DOWN, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_seed_queue_size, &i))
    {
        tr_sessionSetQueueSize(session, TR_UP, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_seed_queue_enabled, &boolVal))
    {
        tr_sessionSetQueueEnabled(session, TR_UP, boolVal);
    }

    /* files and directories */
    if (tr_variantDictFindBool(settings, TR_KEY_prefetch_enabled, &boolVal))
    {
        session->isPrefetchEnabled = boolVal;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_preallocation, &i))
    {
        session->preallocationMode = tr_preallocation_mode(i);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_download_dir, &sv))
    {
        session->setDownloadDir(sv);
    }

    if (tr_variantDictFindStrView(settings, TR_KEY_incomplete_dir, &sv))
    {
        session->setIncompleteDir(sv);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_incomplete_dir_enabled, &boolVal))
    {
        session->useIncompleteDir(boolVal);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_rename_partial_files, &boolVal))
    {
        tr_sessionSetIncompleteFileNamingEnabled(session, boolVal);
    }

    /* rpc server */
    session->rpc_server_ = std::make_unique<tr_rpc_server>(session, settings);

    /* public addresses */

    free_incoming_peer_port(session);

    if (!tr_variantDictFindStrView(settings, TR_KEY_bind_address_ipv4, &sv) || !tr_address_from_string(&b.addr, sv) ||
        b.addr.type != TR_AF_INET)
    {
        b.addr = tr_inaddr_any;
    }

    b.socket = TR_BAD_SOCKET;
    session->bind_ipv4 = static_cast<struct tr_bindinfo*>(tr_memdup(&b, sizeof(struct tr_bindinfo)));

    if (!tr_variantDictFindStrView(settings, TR_KEY_bind_address_ipv6, &sv) || !tr_address_from_string(&b.addr, sv) ||
        b.addr.type != TR_AF_INET6)
    {
        b.addr = tr_in6addr_any;
    }

    b.socket = TR_BAD_SOCKET;
    session->bind_ipv6 = static_cast<tr_bindinfo*>(tr_memdup(&b, sizeof(struct tr_bindinfo)));

    /* incoming peer port */
    if (tr_variantDictFindInt(settings, TR_KEY_peer_port_random_low, &i))
    {
        session->randomPortLow.setHost(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_port_random_high, &i))
    {
        session->randomPortHigh.setHost(i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_peer_port_random_on_start, &boolVal))
    {
        tr_sessionSetPeerPortRandomOnStart(session, boolVal);
    }

    {
        auto peer_port = session->private_peer_port;

        if (auto port = int64_t{}; tr_variantDictFindInt(settings, TR_KEY_peer_port, &port))
        {
            peer_port.setHost(static_cast<uint16_t>(port));
        }

        setPeerPort(session, boolVal ? getRandomPort(session) : peer_port);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_port_forwarding_enabled, &boolVal))
    {
        tr_sessionSetPortForwardingEnabled(session, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_peer_limit_global, &i))
    {
        session->peerLimit = i;
    }

    /**
    **/

    if (tr_variantDictFindInt(settings, TR_KEY_upload_slots_per_torrent, &i))
    {
        session->uploadSlotsPerTorrent = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_speed_limit_up, &i))
    {
        tr_sessionSetSpeedLimit_KBps(session, TR_UP, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_speed_limit_up_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(session, TR_UP, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_speed_limit_down, &i))
    {
        tr_sessionSetSpeedLimit_KBps(session, TR_DOWN, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_speed_limit_down_enabled, &boolVal))
    {
        tr_sessionLimitSpeed(session, TR_DOWN, boolVal);
    }

    if (tr_variantDictFindReal(settings, TR_KEY_ratio_limit, &d))
    {
        tr_sessionSetRatioLimit(session, d);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_ratio_limit_enabled, &boolVal))
    {
        tr_sessionSetRatioLimited(session, boolVal);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_idle_seeding_limit, &i))
    {
        tr_sessionSetIdleLimit(session, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_idle_seeding_limit_enabled, &boolVal))
    {
        tr_sessionSetIdleLimited(session, boolVal);
    }

    /**
    ***  Turtle Mode
    **/

    /* update the turtle mode's fields */
    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_up, &i))
    {
        turtle->speedLimit_Bps[TR_UP] = tr_toSpeedBytes(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_down, &i))
    {
        turtle->speedLimit_Bps[TR_DOWN] = tr_toSpeedBytes(i);
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_begin, &i))
    {
        turtle->beginMinute = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_end, &i))
    {
        turtle->endMinute = i;
    }

    if (tr_variantDictFindInt(settings, TR_KEY_alt_speed_time_day, &i))
    {
        turtle->days = tr_sched_day(i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_alt_speed_time_enabled, &boolVal))
    {
        turtle->isClockEnabled = boolVal;
    }

    if (tr_variantDictFindBool(settings, TR_KEY_alt_speed_enabled, &boolVal))
    {
        turtle->isEnabled = boolVal;
    }

    turtleBootstrap(session, turtle);

    for (auto const& [enabled_key, script_key, script] : tr_session::Scripts)
    {
        if (auto enabled = bool{}; tr_variantDictFindBool(settings, enabled_key, &enabled))
        {
            session->useScript(script, enabled);
        }

        if (auto file = std::string_view{}; tr_variantDictFindStrView(settings, script_key, &file))
        {
            session->setScript(script, file);
        }
    }

    if (tr_variantDictFindBool(settings, TR_KEY_scrape_paused_torrents_enabled, &boolVal))
    {
        session->scrapePausedTorrents = boolVal;
    }

    /**
    ***  BruteForce
    **/

    if (tr_variantDictFindInt(settings, TR_KEY_anti_brute_force_threshold, &i))
    {
        tr_sessionSetAntiBruteForceThreshold(session, i);
    }

    if (tr_variantDictFindBool(settings, TR_KEY_anti_brute_force_enabled, &boolVal))
    {
        tr_sessionSetAntiBruteForceEnabled(session, boolVal);
    }

    data->done_cv.notify_one();
}

void tr_sessionSet(tr_session* session, tr_variant* settings)
{
    auto data = init_data{};
    data.session = session;
    data.clientSettings = settings;

    // run it in the libtransmission thread

    if (tr_amInEventThread(session))
    {
        sessionSetImpl(&data);
    }
    else
    {
        auto lock = session->unique_lock();
        tr_runInEventThread(session, sessionSetImpl, &data);
        data.done_cv.wait(lock);
    }
}

/***
****
***/

void tr_sessionSetDownloadDir(tr_session* session, char const* dir)
{
    TR_ASSERT(tr_isSession(session));

    session->setDownloadDir(dir != nullptr ? dir : "");
}

char const* tr_sessionGetDownloadDir(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->downloadDir().c_str();
}

/***
****
***/

void tr_sessionSetIncompleteFileNamingEnabled(tr_session* session, bool b)
{
    TR_ASSERT(tr_isSession(session));

    session->isIncompleteFileNamingEnabled = b;
}

bool tr_sessionIsIncompleteFileNamingEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isIncompleteFileNamingEnabled;
}

/***
****
***/

void tr_sessionSetIncompleteDir(tr_session* session, char const* dir)
{
    TR_ASSERT(tr_isSession(session));

    session->setIncompleteDir(dir != nullptr ? dir : "");
}

char const* tr_sessionGetIncompleteDir(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->incompleteDir().c_str();
}

void tr_sessionSetIncompleteDirEnabled(tr_session* session, bool b)
{
    TR_ASSERT(tr_isSession(session));

    session->useIncompleteDir(b);
}

bool tr_sessionIsIncompleteDirEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->useIncompleteDir();
}

/***
****  Peer Port
***/

static void peerPortChanged(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

    close_incoming_peer_port(session);
    open_incoming_peer_port(session);
    tr_sharedPortChanged(session);

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
    if (auto const port = tr_port::fromHost(hport); tr_isSession(session) && session->private_peer_port != port)
    {
        setPeerPort(session, port);
    }
}

uint16_t tr_sessionGetPeerPort(tr_session const* session)
{
    return tr_isSession(session) ? session->public_peer_port.host() : 0U;
}

uint16_t tr_sessionSetPeerPortRandom(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    session->setPeerPort(getRandomPort(session));
    return session->private_peer_port.host();
}

void tr_sessionSetPeerPortRandomOnStart(tr_session* session, bool random)
{
    TR_ASSERT(tr_isSession(session));

    session->isPortRandom = random;
}

bool tr_sessionGetPeerPortRandomOnStart(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isPortRandom;
}

tr_port_forwarding tr_sessionGetPortForwarding(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return tr_port_forwarding(tr_sharedTraversalStatus(session->shared));
}

/***
****
***/

void tr_sessionSetRatioLimited(tr_session* session, bool isLimited)
{
    TR_ASSERT(tr_isSession(session));

    session->isRatioLimited = isLimited;
}

void tr_sessionSetRatioLimit(tr_session* session, double desiredRatio)
{
    TR_ASSERT(tr_isSession(session));

    session->desiredRatio = desiredRatio;
}

bool tr_sessionIsRatioLimited(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isRatioLimited;
}

double tr_sessionGetRatioLimit(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->desiredRatio;
}

/***
****
***/

void tr_sessionSetIdleLimited(tr_session* session, bool isLimited)
{
    TR_ASSERT(tr_isSession(session));

    session->isIdleLimited = isLimited;
}

void tr_sessionSetIdleLimit(tr_session* session, uint16_t idleMinutes)
{
    TR_ASSERT(tr_isSession(session));

    session->idleLimitMinutes = idleMinutes;
}

bool tr_sessionIsIdleLimited(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isIdleLimited;
}

uint16_t tr_sessionGetIdleLimit(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->idleLimitMinutes;
}

/***
****
****  SPEED LIMITS
****
***/

static unsigned int tr_sessionGetAltSpeed_Bps(tr_session const* s, tr_direction d);

bool tr_sessionGetActiveSpeedLimit_Bps(tr_session const* session, tr_direction dir, unsigned int* setme_Bps)
{
    bool isLimited = true;

    if (!tr_isSession(session))
    {
        return false;
    }

    if (tr_sessionUsesAltSpeed(session))
    {
        *setme_Bps = tr_sessionGetAltSpeed_Bps(session, dir);
    }
    else if (tr_sessionIsSpeedLimited(session, dir))
    {
        *setme_Bps = tr_sessionGetSpeedLimit_Bps(session, dir);
    }
    else
    {
        isLimited = false;
    }

    return isLimited;
}

bool tr_sessionGetActiveSpeedLimit_KBps(tr_session const* session, tr_direction dir, double* setme_KBps)
{
    unsigned int Bps = 0;
    bool const is_active = tr_sessionGetActiveSpeedLimit_Bps(session, dir, &Bps);
    *setme_KBps = tr_toSpeedKBps(Bps);
    return is_active;
}

static void updateBandwidth(tr_session* session, tr_direction dir)
{
    unsigned int limit_Bps = 0;
    bool const isLimited = tr_sessionGetActiveSpeedLimit_Bps(session, dir, &limit_Bps);
    bool const zeroCase = isLimited && limit_Bps == 0;

    session->top_bandwidth_.setLimited(dir, isLimited && !zeroCase);
    session->top_bandwidth_.setDesiredSpeedBytesPerSecond(dir, limit_Bps);
}

static auto constexpr MinutesPerHour = int{ 60 };
static auto constexpr MinutesPerDay = int{ MinutesPerHour * 24 };
static auto constexpr MinutesPerWeek = int{ MinutesPerDay * 7 };

static void turtleUpdateTable(struct tr_turtle_info* t)
{
    t->minutes->setHasNone();

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
                t->minutes->set((i + day * MinutesPerDay) % MinutesPerWeek);
            }
        }
    }
}

static void altSpeedToggled(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

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
    TR_ASSERT(tr_isSession(s));
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

    return t->minutes->test(minute_of_the_week);
}

static constexpr tr_auto_switch_state_t autoSwitchState(bool enabled)
{
    return enabled ? TR_AUTO_SWITCH_ON : TR_AUTO_SWITCH_OFF;
}

static void turtleCheckClock(tr_session* s, struct tr_turtle_info* t)
{
    TR_ASSERT(t->isClockEnabled);

    bool enabled = getInTurtleTime(t);
    tr_auto_switch_state_t newAutoTurtleState = autoSwitchState(enabled);
    bool alreadySwitched = t->autoTurtleState == newAutoTurtleState;

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

    turtle->minutes = new tr_bitfield(MinutesPerWeek);

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

static void tr_sessionSetSpeedLimit_Bps(tr_session* s, tr_direction d, unsigned int Bps)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(tr_isDirection(d));

    s->speedLimit_Bps[d] = Bps;

    updateBandwidth(s, d);
}

void tr_sessionSetSpeedLimit_KBps(tr_session* s, tr_direction d, unsigned int KBps)
{
    tr_sessionSetSpeedLimit_Bps(s, d, tr_toSpeedBytes(KBps));
}

unsigned int tr_sessionGetSpeedLimit_Bps(tr_session const* s, tr_direction d)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(tr_isDirection(d));

    return s->speedLimit_Bps[d];
}

unsigned int tr_sessionGetSpeedLimit_KBps(tr_session const* s, tr_direction d)
{
    return tr_toSpeedKBps(tr_sessionGetSpeedLimit_Bps(s, d));
}

void tr_sessionLimitSpeed(tr_session* s, tr_direction d, bool b)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(tr_isDirection(d));

    s->speedLimitEnabled[d] = b;

    updateBandwidth(s, d);
}

bool tr_sessionIsSpeedLimited(tr_session const* s, tr_direction d)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(tr_isDirection(d));

    return s->speedLimitEnabled[d];
}

/***
****  Alternative speed limits that are used during scheduled times
***/

static void tr_sessionSetAltSpeed_Bps(tr_session* s, tr_direction d, unsigned int Bps)
{
    TR_ASSERT(tr_isSession(s));
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
    TR_ASSERT(tr_isSession(s));
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
    TR_ASSERT(tr_isSession(s));

    struct tr_turtle_info* t = &s->turtle;

    if (t->isClockEnabled != b)
    {
        t->isClockEnabled = b;
        userPokedTheClock(s, t);
    }
}

bool tr_sessionUsesAltSpeedTime(tr_session const* s)
{
    TR_ASSERT(tr_isSession(s));

    return s->turtle.isClockEnabled;
}

void tr_sessionSetAltSpeedBegin(tr_session* s, int minute)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(minute >= 0);
    TR_ASSERT(minute < 60 * 24);

    if (s->turtle.beginMinute != minute)
    {
        s->turtle.beginMinute = minute;
        userPokedTheClock(s, &s->turtle);
    }
}

int tr_sessionGetAltSpeedBegin(tr_session const* s)
{
    TR_ASSERT(tr_isSession(s));

    return s->turtle.beginMinute;
}

void tr_sessionSetAltSpeedEnd(tr_session* s, int minute)
{
    TR_ASSERT(tr_isSession(s));
    TR_ASSERT(minute >= 0);
    TR_ASSERT(minute < 60 * 24);

    if (s->turtle.endMinute != minute)
    {
        s->turtle.endMinute = minute;
        userPokedTheClock(s, &s->turtle);
    }
}

int tr_sessionGetAltSpeedEnd(tr_session const* s)
{
    TR_ASSERT(tr_isSession(s));

    return s->turtle.endMinute;
}

void tr_sessionSetAltSpeedDay(tr_session* s, tr_sched_day days)
{
    TR_ASSERT(tr_isSession(s));

    if (s->turtle.days != days)
    {
        s->turtle.days = days;
        userPokedTheClock(s, &s->turtle);
    }
}

tr_sched_day tr_sessionGetAltSpeedDay(tr_session const* s)
{
    TR_ASSERT(tr_isSession(s));

    return s->turtle.days;
}

void tr_sessionUseAltSpeed(tr_session* session, bool enabled)
{
    useAltSpeed(session, &session->turtle, enabled, true);
}

bool tr_sessionUsesAltSpeed(tr_session const* s)
{
    TR_ASSERT(tr_isSession(s));

    return s->turtle.isEnabled;
}

void tr_sessionSetAltSpeedFunc(tr_session* session, tr_altSpeedFunc func, void* userData)
{
    TR_ASSERT(tr_isSession(session));

    session->turtle.callback = func;
    session->turtle.callbackUserData = userData;
}

/***
****
***/

void tr_sessionSetPeerLimit(tr_session* session, uint16_t n)
{
    TR_ASSERT(tr_isSession(session));

    session->peerLimit = n;
}

uint16_t tr_sessionGetPeerLimit(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->peerLimit;
}

void tr_sessionSetPeerLimitPerTorrent(tr_session* session, uint16_t n)
{
    TR_ASSERT(tr_isSession(session));

    session->peerLimitPerTorrent = n;
}

uint16_t tr_sessionGetPeerLimitPerTorrent(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->peerLimitPerTorrent;
}

/***
****
***/

void tr_sessionSetPaused(tr_session* session, bool isPaused)
{
    TR_ASSERT(tr_isSession(session));

    session->pauseAddedTorrent = isPaused;
}

bool tr_sessionGetPaused(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->pauseAddedTorrent;
}

void tr_sessionSetDeleteSource(tr_session* session, bool deleteSource)
{
    TR_ASSERT(tr_isSession(session));

    session->deleteSourceTorrent = deleteSource;
}

bool tr_sessionGetDeleteSource(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->deleteSourceTorrent;
}

/***
****
***/

unsigned int tr_sessionGetPieceSpeed_Bps(tr_session const* session, tr_direction dir)
{
    return tr_isSession(session) ? session->top_bandwidth_.getPieceSpeedBytesPerSecond(0, dir) : 0;
}

static unsigned int tr_sessionGetRawSpeed_Bps(tr_session const* session, tr_direction dir)
{
    return tr_isSession(session) ? session->top_bandwidth_.getRawSpeedBytesPerSecond(0, dir) : 0;
}

double tr_sessionGetRawSpeed_KBps(tr_session const* session, tr_direction dir)
{
    return tr_toSpeedKBps(tr_sessionGetRawSpeed_Bps(session, dir));
}

int tr_sessionCountTorrents(tr_session const* session)
{
    return tr_isSession(session) ? std::size(session->torrents()) : 0;
}

std::vector<tr_torrent*> tr_sessionGetTorrents(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    auto const n = std::size(session->torrents());
    auto torrents = std::vector<tr_torrent*>{ n };
    std::copy(std::begin(session->torrents()), std::end(session->torrents()), std::begin(torrents));
    return torrents;
}

static void closeBlocklists(tr_session* /*session*/);

static void sessionCloseImplWaitForIdleUdp(evutil_socket_t fd, short what, void* vsession);

static void sessionCloseImplStart(tr_session* session)
{
    session->is_closing_ = true;

    if (session->isLPDEnabled)
    {
        tr_lpdUninit(session);
    }

    tr_utpClose(session);
    tr_dhtUninit(session);

    event_free(session->saveTimer);
    session->saveTimer = nullptr;

    event_free(session->nowTimer);
    session->nowTimer = nullptr;

    tr_verifyClose(session);
    tr_sharedClose(session);

    free_incoming_peer_port(session);
    session->rpc_server_.reset();

    /* Close the torrents. Get the most active ones first so that
     * if we can't get them all closed in a reasonable amount of time,
     * at least we get the most important ones first. */
    auto torrents = tr_sessionGetTorrents(session);
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
    tr_announcerClose(session);

    /* and this goes *after* announcer close so that
       it won't be idle until the announce events are sent... */
    session->web->closeSoon();

    session->cache.reset();

    /* saveTimer is not used at this point, reusing for UDP shutdown wait */
    TR_ASSERT(session->saveTimer == nullptr);
    session->saveTimer = evtimer_new(session->event_base, sessionCloseImplWaitForIdleUdp, session);
    tr_timerAdd(*session->saveTimer, 0, 0);
}

static void sessionCloseImplFinish(tr_session* session);

static void sessionCloseImplWaitForIdleUdp(evutil_socket_t /*fd*/, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    TR_ASSERT(tr_isSession(session));

    /* gotta keep udp running long enough to send out all
       the &event=stopped UDP tracker messages */
    if (!tr_tracker_udp_is_idle(session))
    {
        tr_tracker_udp_upkeep(session);
        tr_timerAdd(*session->saveTimer, 0, 100000);
        return;
    }

    sessionCloseImplFinish(session);
}

static void sessionCloseImplFinish(tr_session* session)
{
    event_free(session->saveTimer);
    session->saveTimer = nullptr;

    /* we had to wait until UDP trackers were closed before closing these: */
    tr_tracker_udp_close(session);
    tr_udpUninit(session);

    tr_statsClose(session);
    tr_peerMgrFree(session->peerMgr);

    closeBlocklists(session);

    session->openFiles().closeAll();

    session->isClosed = true;
}

static void sessionCloseImpl(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

    sessionCloseImplStart(session);
}

static bool deadlineReached(time_t const deadline)
{
    return time(nullptr) >= deadline;
}

static auto constexpr ShutdownMaxSeconds = time_t{ 20 };

void tr_sessionClose(tr_session* session)
{
    TR_ASSERT(tr_isSession(session));

    time_t const deadline = time(nullptr) + ShutdownMaxSeconds;

    tr_logAddInfo(fmt::format(_("Transmission version {version} shutting down"), fmt::arg("version", LONG_VERSION_STRING)));
    tr_logAddDebug(fmt::format("now is {}, deadline is {}", time(nullptr), deadline));

    /* close the session */
    tr_runInEventThread(session, sessionCloseImpl, session);

    while (!session->isClosed && !deadlineReached(deadline))
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
            event_base_loopbreak(session->event_base);
        }

        if (deadlineReached(deadline + 3))
        {
            tr_logAddTrace("deadline+3 reached... calling break...");
            break;
        }
    }

    /* free the session memory */
    delete session->turtle.minutes;
    tr_session_id_free(session->session_id);

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
    TR_ASSERT(tr_isSession(data->session));

    tr_sys_path_info info;
    char const* const dirname = tr_getTorrentDir(data->session);
    tr_sys_dir_t odir = (tr_sys_path_get_info(dirname, 0, &info) && info.type == TR_SYS_PATH_IS_DIRECTORY) ?
        tr_sys_dir_open(dirname) :
        TR_BAD_SYS_DIR;

    auto torrents = std::list<tr_torrent*>{};
    if (odir != TR_BAD_SYS_DIR)
    {
        auto const dirname_sv = std::string_view{ dirname };

        char const* name = nullptr;
        while ((name = tr_sys_dir_read_name(odir)) != nullptr)
        {
            if (!tr_strvEndsWith(name, ".torrent"sv) && !tr_strvEndsWith(name, ".magnet"sv))
            {
                continue;
            }

            auto const path = tr_pathbuf{ dirname_sv, "/"sv, name };

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
    TR_ASSERT(tr_isSession(session));

    session->isPexEnabled = enabled;
}

bool tr_sessionIsPexEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isPexEnabled;
}

bool tr_sessionAllowsDHT(tr_session const* session)
{
    return tr_sessionIsDHTEnabled(session);
}

bool tr_sessionIsDHTEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isDHTEnabled;
}

static void toggleDHTImpl(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

    tr_udpUninit(session);
    session->isDHTEnabled = !session->isDHTEnabled;
    tr_udpInit(session);
}

void tr_sessionSetDHTEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    if (enabled != session->isDHTEnabled)
    {
        tr_runInEventThread(session, toggleDHTImpl, session);
    }
}

/***
****
***/

bool tr_sessionIsUTPEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

#ifdef WITH_UTP
    return session->isUTPEnabled;
#else
    return false;
#endif
}

static void toggle_utp(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

    session->isUTPEnabled = !session->isUTPEnabled;

    tr_udpSetSocketBuffers(session);

    tr_udpSetSocketTOS(session);

    /* But don't call tr_utpClose -- see reset_timer in tr-utp.c for an
       explanation. */
}

void tr_sessionSetUTPEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    if (enabled != session->isUTPEnabled)
    {
        tr_runInEventThread(session, toggle_utp, session);
    }
}

/***
****
***/

static void toggleLPDImpl(tr_session* const session)
{
    TR_ASSERT(tr_isSession(session));

    if (session->isLPDEnabled)
    {
        tr_lpdUninit(session);
    }

    session->isLPDEnabled = !session->isLPDEnabled;

    if (session->isLPDEnabled)
    {
        tr_lpdInit(session, &session->bind_ipv4->addr);
    }
}

void tr_sessionSetLPDEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    if (enabled != session->isLPDEnabled)
    {
        tr_runInEventThread(session, toggleLPDImpl, session);
    }
}

bool tr_sessionIsLPDEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->isLPDEnabled;
}

bool tr_sessionAllowsLPD(tr_session const* session)
{
    return tr_sessionIsLPDEnabled(session);
}

/***
****
***/

void tr_sessionSetCacheLimit_MB(tr_session* session, int max_bytes)
{
    TR_ASSERT(tr_isSession(session));

    session->cache->setLimit(tr_toMemBytes(max_bytes));
}

int tr_sessionGetCacheLimit_MB(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

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
    TR_ASSERT(tr_isSession(session));

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

struct port_forwarding_data
{
    bool enabled;
    struct tr_shared* shared;
};

static void setPortForwardingEnabled(struct port_forwarding_data* const data)
{
    tr_sharedTraversalEnable(data->shared, data->enabled);
    tr_free(data);
}

void tr_sessionSetPortForwardingEnabled(tr_session* session, bool enabled)
{
    auto* const d = tr_new0(struct port_forwarding_data, 1);
    d->shared = session->shared;
    d->enabled = enabled;
    tr_runInEventThread(session, setPortForwardingEnabled, d);
}

bool tr_sessionIsPortForwardingEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return tr_sharedTraversalIsEnabled(session->shared);
}

/***
****
***/

static void loadBlocklists(tr_session* session)
{
    auto loadme = std::unordered_set<std::string>{};
    auto const isEnabled = session->useBlocklist();

    /* walk the blocklist directory... */
    auto const dirname = tr_pathbuf{ session->config_dir, "/blocklists"sv };
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
            tr_sys_path_info path_info;
            tr_sys_path_info binname_info;

            auto const binname = tr_pathbuf{ dirname, '/', name, ".bin"sv };

            if (!tr_sys_path_get_info(binname, 0, &binname_info)) /* create it */
            {
                BlocklistFile b(binname, isEnabled);
                if (auto const n = b.setContent(path); n > 0)
                {
                    load = binname;
                }
            }
            else if (
                tr_sys_path_get_info(path, 0, &path_info) &&
                path_info.last_modified_at >= binname_info.last_modified_at) /* update it */
            {
                auto const old = tr_pathbuf{ binname, ".old"sv };
                tr_sys_path_remove(old);
                tr_sys_path_rename(binname, old);

                BlocklistFile b(binname, isEnabled);

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

    session->blocklists.clear();
    std::transform(
        std::begin(loadme),
        std::end(loadme),
        std::back_inserter(session->blocklists),
        [&isEnabled](auto const& path) { return std::make_unique<BlocklistFile>(path.c_str(), isEnabled); });

    /* cleanup */
    tr_sys_dir_close(odir);
}

static void closeBlocklists(tr_session* session)
{
    session->blocklists.clear();
}

void tr_sessionReloadBlocklists(tr_session* session)
{
    closeBlocklists(session);
    loadBlocklists(session);

    tr_peerMgrOnBlocklistChanged(session->peerMgr);
}

size_t tr_blocklistGetRuleCount(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    auto& src = session->blocklists;
    return std::accumulate(std::begin(src), std::end(src), 0, [](int sum, auto& cur) { return sum + cur->getRuleCount(); });
}

bool tr_blocklistIsEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->useBlocklist();
}

void tr_session::useBlocklist(bool enabled)
{
    this->blocklist_enabled_ = enabled;

    std::for_each(std::begin(blocklists), std::end(blocklists), [enabled](auto& blocklist) { blocklist->setEnabled(enabled); });
}

void tr_blocklistSetEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    session->useBlocklist(enabled);
}

bool tr_blocklistExists(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return !std::empty(session->blocklists);
}

size_t tr_blocklistSetContent(tr_session* session, char const* contentFilename)
{
    auto const lock = session->unique_lock();

    // find (or add) the default blocklist
    auto& src = session->blocklists;
    char const* const name = DEFAULT_BLOCKLIST_FILENAME;
    auto const it = std::find_if(
        std::begin(src),
        std::end(src),
        [&name](auto const& blocklist) { return tr_strvEndsWith(blocklist->getFilename(), name); });

    BlocklistFile* b = nullptr;
    if (it == std::end(src))
    {
        auto path = tr_pathbuf{ session->config_dir, "/blocklists/"sv, name };
        src.push_back(std::make_unique<BlocklistFile>(path, session->useBlocklist()));
        b = std::rbegin(src)->get();
    }
    else
    {
        b = it->get();
    }

    // set the default blocklist's content
    int const ruleCount = b->setContent(contentFilename);
    return ruleCount;
}

bool tr_sessionIsAddressBlocked(tr_session const* session, tr_address const* addr)
{
    auto const& src = session->blocklists;
    return std::any_of(std::begin(src), std::end(src), [&addr](auto& blocklist) { return blocklist->hasAddress(*addr); });
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
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setEnabled(is_enabled);
}

bool tr_sessionIsRPCEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->isEnabled();
}

void tr_sessionSetRPCPort(tr_session* session, uint16_t hport)
{
    TR_ASSERT(tr_isSession(session));

    if (session->rpc_server_)
    {
        session->rpc_server_->setPort(tr_port::fromHost(hport));
    }
}

uint16_t tr_sessionGetRPCPort(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_ ? session->rpc_server_->port().host() : uint16_t{};
}

void tr_sessionSetRPCUrl(tr_session* session, char const* url)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setUrl(url != nullptr ? url : "");
}

char const* tr_sessionGetRPCUrl(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->url().c_str();
}

void tr_sessionSetRPCCallback(tr_session* session, tr_rpc_func func, void* user_data)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_func = func;
    session->rpc_func_user_data = user_data;
}

void tr_sessionSetRPCWhitelist(tr_session* session, char const* whitelist)
{
    TR_ASSERT(tr_isSession(session));

    session->setRpcWhitelist(whitelist != nullptr ? whitelist : "");
}

char const* tr_sessionGetRPCWhitelist(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->whitelist().c_str();
}

void tr_sessionSetRPCWhitelistEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    session->useRpcWhitelist(enabled);
}

bool tr_sessionGetRPCWhitelistEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->useRpcWhitelist();
}

void tr_sessionSetRPCPassword(tr_session* session, char const* password)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setPassword(password != nullptr ? password : "");
}

char const* tr_sessionGetRPCPassword(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->getSaltedPassword().c_str();
}

void tr_sessionSetRPCUsername(tr_session* session, char const* username)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setUsername(username != nullptr ? username : "");
}

char const* tr_sessionGetRPCUsername(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->username().c_str();
}

void tr_sessionSetRPCPasswordEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setPasswordEnabled(enabled);
}

bool tr_sessionIsRPCPasswordEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->isPasswordEnabled();
}

/****
*****
****/

void tr_sessionSetScriptEnabled(tr_session* session, TrScript type, bool enabled)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    session->useScript(type, enabled);
}

bool tr_sessionIsScriptEnabled(tr_session const* session, TrScript type)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    return session->useScript(type);
}

void tr_sessionSetScript(tr_session* session, TrScript type, char const* script)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    session->setScript(type, script != nullptr ? script : "");
}

char const* tr_sessionGetScript(tr_session const* session, TrScript type)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(type < TR_SCRIPT_N_TYPES);

    return session->script(type).c_str();
}

/***
****
***/

void tr_sessionSetQueueSize(tr_session* session, tr_direction dir, int n)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    session->queueSize[dir] = n;
}

int tr_sessionGetQueueSize(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    return session->queueSize[dir];
}

void tr_sessionSetQueueEnabled(tr_session* session, tr_direction dir, bool is_enabled)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    session->queueEnabled[dir] = is_enabled;
}

bool tr_sessionGetQueueEnabled(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(dir));

    return session->queueEnabled[dir];
}

void tr_sessionSetQueueStalledMinutes(tr_session* session, int minutes)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(minutes > 0);

    session->queueStalledMinutes = minutes;
}

void tr_sessionSetQueueStalledEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(tr_isSession(session));

    session->stalledEnabled = is_enabled;
}

bool tr_sessionGetQueueStalledEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->stalledEnabled;
}

int tr_sessionGetQueueStalledMinutes(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->queueStalledMinutes;
}

void tr_sessionSetAntiBruteForceThreshold(tr_session* session, int limit)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(limit > 0);

    session->rpc_server_->setAntiBruteForceLimit(limit);
}

int tr_sessionGetAntiBruteForceThreshold(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->getAntiBruteForceLimit();
}

void tr_sessionSetAntiBruteForceEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(tr_isSession(session));

    session->rpc_server_->setAntiBruteForceEnabled(is_enabled);
}

bool tr_sessionGetAntiBruteForceEnabled(tr_session const* session)
{
    TR_ASSERT(tr_isSession(session));

    return session->rpc_server_->isAntiBruteForceEnabled();
}

std::vector<tr_torrent*> tr_sessionGetNextQueuedTorrents(tr_session* session, tr_direction direction, size_t num_wanted)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(tr_isDirection(direction));

    // build an array of the candidates
    auto candidates = std::vector<tr_torrent*>{};
    candidates.reserve(tr_sessionCountTorrents(session));
    for (auto* const tor : session->torrents())
    {
        if (tor->isQueued() && (direction == tor->queueDirection()))
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

int tr_sessionCountQueueFreeSlots(tr_session* session, tr_direction dir)
{
    int const max = tr_sessionGetQueueSize(session, dir);
    tr_torrent_activity const activity = dir == TR_UP ? TR_STATUS_SEED : TR_STATUS_DOWNLOAD;

    if (!tr_sessionGetQueueEnabled(session, dir))
    {
        return INT_MAX;
    }

    /* count how many torrents are active */
    int active_count = 0;
    bool const stalled_enabled = tr_sessionGetQueueStalledEnabled(session);
    int const stalled_if_idle_for_n_seconds = tr_sessionGetQueueStalledMinutes(session) * 60;
    time_t const now = tr_time();
    for (auto const* const tor : session->torrents())
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
    auto const filename = tr_pathbuf{ config_dir, "/"sv, BandwidthGroupsFilename };
    auto groups_dict = tr_variant{};
    if (!tr_variantFromFile(&groups_dict, TR_VARIANT_PARSE_JSON, filename, nullptr) || !tr_variantIsDict(&groups_dict))
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

    auto const filename = tr_pathbuf{ config_dir, "/"sv, BandwidthGroupsFilename };
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
