// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::partial_sort(), std::min(), std::max()
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
#include <utility>
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
#include "port-forwarding.h"
#include "rpc-server.h"
#include "session-id.h"
#include "session.h"
#include "timer-ev.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-lpd.h"
#include "tr-strbuf.h"
#include "tr-utp.h"
#include "utils.h"
#include "variant.h"
#include "verify.h"
#include "version.h"
#include "web.h"

using namespace std::literals;

std::recursive_mutex tr_session::session_mutex_;

static auto constexpr DefaultBindAddressIpv4 = "0.0.0.0"sv;
static auto constexpr DefaultBindAddressIpv6 = "::"sv;
static auto constexpr SaveIntervalSecs = 360s;

static void bandwidthGroupRead(tr_session* session, std::string_view config_dir);
static int bandwidthGroupWrite(tr_session const* session, std::string_view config_dir);
static auto constexpr BandwidthGroupsFilename = "bandwidth-groups.json"sv;

tr_port tr_session::randomPort() const
{
    auto const lower = std::min(settings_.peer_port_random_low.host(), settings_.peer_port_random_high.host());
    auto const upper = std::max(settings_.peer_port_random_low.host(), settings_.peer_port_random_high.host());
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

bool tr_session::LpdMediator::onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port)
{
    auto const digest = tr_sha1_from_string(info_hash_str);
    if (!digest)
    {
        return false;
    }

    tr_torrent* const tor = session_.torrents_.get(*digest);
    if (!tr_isTorrent(tor) || !tor->allowsLpd())
    {
        return false;
    }

    // we found a suitable peer, add it to the torrent
    auto pex = tr_pex{ address, port };
    tr_peerMgrAddPex(tor, TR_PEER_FROM_LPD, &pex, 1U);
    tr_logAddDebugTor(tor, fmt::format(FMT_STRING("Found a local peer from LPD ({:s})"), address.readable(port)));
    return true;
}

std::vector<tr_lpd::Mediator::TorrentInfo> tr_session::LpdMediator::torrents() const
{
    auto ret = std::vector<tr_lpd::Mediator::TorrentInfo>{};
    ret.reserve(std::size(session_.torrents()));
    for (auto const* const tor : session_.torrents())
    {
        auto info = tr_lpd::Mediator::TorrentInfo{};
        info.info_hash_str = tor->infoHashString();
        info.activity = tr_torrentGetActivity(tor);
        info.allows_lpd = tor->allowsLpd();
        info.announce_after = tor->lpdAnnounceAt;
        ret.emplace_back(info);
    }
    return ret;
}

void tr_session::LpdMediator::setNextAnnounceTime(std::string_view info_hash_str, time_t announce_after)
{
    if (auto digest = tr_sha1_from_string(info_hash_str); digest)
    {
        if (tr_torrent* const tor = session_.torrents_.get(*digest); tr_isTorrent(tor))
        {
            tor->lpdAnnounceAt = announce_after;
        }
    }
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

std::optional<std::string> tr_session::WebMediator::publicAddressV4() const
{
    auto const [addr, is_default_value] = session_->publicAddress(TR_AF_INET);
    if (!is_default_value)
    {
        return addr.readable();
    }

    return std::nullopt;
}

std::optional<std::string> tr_session::WebMediator::publicAddressV6() const
{
    auto const [addr, is_default_value] = session_->publicAddress(TR_AF_INET6);
    if (!is_default_value)
    {
        return addr.readable();
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
    session_->runInSessionThread(std::move(func), std::move(response));
}

void tr_sessionFetch(tr_session* session, tr_web::FetchOptions&& options)
{
    session->fetch(std::move(options));
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

    session->settings_.encryption_mode = mode;
}

/***
****
***/

void tr_session::tr_bindinfo::close()
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

static void acceptIncomingPeer(evutil_socket_t fd, short /*what*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    if (auto const incoming_info = tr_netAccept(session, fd); incoming_info)
    {
        auto const& [addr, port, sock] = *incoming_info;
        tr_logAddTrace(fmt::format("new incoming connection {} ({})", sock, addr.readable(port)));
        session->addIncoming(addr, port, tr_peer_socket_tcp_create(sock));
    }
}

void tr_session::tr_bindinfo::bindAndListenForIncomingPeers(tr_session* session)
{
    TR_ASSERT(session->allowsTCP());

    auto const& port = session->localPeerPort();

    socket_ = tr_netBindTCP(&addr_, port, false);

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_logAddInfo(
            fmt::format(_("Listening to incoming peer connections on {hostport}"), fmt::arg("hostport", addr_.readable(port))));
        ev_ = event_new(session->eventBase(), socket_, EV_READ | EV_PERSIST, acceptIncomingPeer, session);
        event_add(ev_, nullptr);
    }
}

tr_session::PublicAddressResult tr_session::publicAddress(tr_address_type type) const noexcept
{
    switch (type)
    {
    case TR_AF_INET:
        return { bind_ipv4_.addr_, bind_ipv4_.addr_.readable() == DefaultBindAddressIpv4 };

    case TR_AF_INET6:
        return { bind_ipv6_.addr_, bind_ipv6_.addr_.readable() == DefaultBindAddressIpv6 };

    default:
        TR_ASSERT_MSG(false, "invalid type");
        return {};
    }
}

/***
****
***/

void tr_sessionGetDefaultSettings(tr_variant* setme_dictionary)
{
    tr_session_settings{}.save(setme_dictionary);
    tr_rpc_server::defaultSettings(setme_dictionary);
    tr_session_alt_speeds::defaultSettings(setme_dictionary);
}

void tr_sessionGetSettings(tr_session const* session, tr_variant* setme_dictionary)
{
    session->settings_.save(setme_dictionary);
    session->alt_speeds_.save(setme_dictionary);
    session->rpc_server_->save(setme_dictionary);
}

static void getSettingsFilename(tr_pathbuf& setme, char const* config_dir, char const* appname)
{
    if (!tr_str_is_empty(config_dir))
    {
        setme.assign(std::string_view{ config_dir }, "/settings.json"sv);
        return;
    }

    auto const default_config_dir = tr_getDefaultConfigDir(appname);
    setme.assign(std::string_view{ default_config_dir }, "/settings.json"sv);
}

bool tr_sessionLoadSettings(tr_variant* dict, char const* config_dir, char const* app_name)
{
    TR_ASSERT(tr_variantIsDict(dict));

    /* initializing the defaults: caller may have passed in some app-level defaults.
     * preserve those and use the session defaults to fill in any missing gaps. */
    auto old_dict = *dict;
    tr_variantInitDict(dict, 0);
    tr_sessionGetDefaultSettings(dict);
    tr_variantMergeDicts(dict, &old_dict);
    tr_variantClear(&old_dict);

    /* file settings override the defaults */
    auto success = bool{};
    auto filename = tr_pathbuf{};
    getSettingsFilename(filename, config_dir, app_name);
    if (!tr_sys_path_exists(filename))
    {
        success = true;
    }
    else if (auto file_settings = tr_variant{}; tr_variantFromFile(&file_settings, TR_VARIANT_PARSE_JSON, filename))
    {
        tr_variantMergeDicts(dict, &file_settings);
        tr_variantClear(&file_settings);
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
        tr_variantClear(&file_settings);
    }

    /* the client's settings override the file settings */
    tr_variantMergeDicts(&settings, client_settings);

    /* the session's true values override the file & client settings */
    {
        auto session_settings = tr_variant{};
        tr_variantInitDict(&session_settings, 0);
        tr_sessionGetSettings(session, &session_settings);
        tr_variantMergeDicts(&settings, &session_settings);
        tr_variantClear(&session_settings);
    }

    /* save the result */
    tr_variantToFile(&settings, TR_VARIANT_FMT_JSON, filename);

    /* cleanup */
    tr_variantClear(&settings);

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
    bandwidthGroupRead(session, config_dir);

    // nice to start logging at the very beginning
    if (auto val = int64_t{}; tr_variantDictFindInt(client_settings, TR_KEY_message_level, &val))
    {
        tr_logSetLevel(static_cast<tr_log_level>(val));
    }

    auto data = tr_session::init_data{};
    data.config_dir = config_dir;
    data.message_queuing_enabled = message_queueing_enabled;
    data.client_settings = client_settings;

    // run initImpl() in the libtransmission thread
    auto lock = session->unique_lock();
    session->runInSessionThread([&session, &data]() { session->initImpl(data); });
    data.done_cv.wait(lock); // wait for the session to be ready

    return session;
}

void tr_session::onNowTimer()
{
    TR_ASSERT(now_timer_);

    // tr_session upkeep tasks to perform once per second
    tr_timeUpdate(time(nullptr));
    udp_core_->dhtUpkeep();
    alt_speeds_.checkScheduler();

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
    TR_ASSERT(amInSessionThread());

    auto* const client_settings = data.client_settings;
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

    this->blocklists_ = libtransmission::Blocklist::loadBlocklists(blocklist_dir_, useBlocklist());

    tr_announcerInit(this);

    tr_logAddInfo(fmt::format(_("Transmission version {version} starting"), fmt::arg("version", LONG_VERSION_STRING)));

    setSettings(client_settings, true);

    this->udp_core_ = std::make_unique<tr_session::tr_udp_core>(*this, udpPort());

    if (this->allowsLPD())
    {
        this->lpd_ = tr_lpd::create(lpd_mediator_, eventBase());
    }

    tr_utpInit(this);

    /* cleanup */
    tr_variantClear(&settings);
    data.done_cv.notify_one();
}

static void updateBandwidth(tr_session* session, tr_direction dir);

void tr_session::setSettings(tr_variant* settings_dict, bool force)
{
    TR_ASSERT(amInSessionThread());

    auto* const settings = settings_dict;
    TR_ASSERT(tr_variantIsDict(settings));

    // update the `settings_` field
    auto const old_settings = settings_;
    auto& new_settings = settings_;
    new_settings.load(settings);

    // the rest of the func is session_ responding to settings changes

    if (auto const& val = new_settings.log_level; force || val != old_settings.log_level)
    {
        tr_logSetLevel(val);
    }

#ifndef _WIN32
    if (auto const& val = new_settings.umask; force || val != old_settings.umask)
    {
        ::umask(val);
    }
#endif

    if (auto const& val = new_settings.cache_size_mb; force || val != old_settings.cache_size_mb)
    {
        tr_sessionSetCacheLimit_MB(this, val);
    }

    if (auto const& val = new_settings.default_trackers_str; force || val != old_settings.default_trackers_str)
    {
        setDefaultTrackers(val);
    }

    if (auto val = bool{}; tr_variantDictFindBool(settings, TR_KEY_dht_enabled, &val))
    {
        tr_sessionSetDHTEnabled(this, val);
    }

    if (auto const& val = new_settings.utp_enabled; force || val != old_settings.utp_enabled)
    {
        tr_sessionSetUTPEnabled(this, val);
    }

    if (auto const& val = new_settings.lpd_enabled; force || val != old_settings.lpd_enabled)
    {
        tr_sessionSetLPDEnabled(this, val);
    }

    useBlocklist(new_settings.blocklist_enabled);

    /// bound addresses, peer port, port forwarding
    {
        auto port_needs_update = force;

        if (auto const& val = new_settings.bind_address_ipv4; force || val != old_settings.bind_address_ipv4)
        {
            if (auto const addr = tr_address::fromString(val); addr && addr->isIPv4())
            {
                this->bind_ipv4_ = tr_bindinfo{ *addr };
                port_needs_update |= true;
            }
        }

        if (auto const& val = new_settings.bind_address_ipv6; force || val != old_settings.bind_address_ipv6)
        {
            if (auto const addr = tr_address::fromString(val); addr && addr->isIPv6())
            {
                this->bind_ipv6_ = tr_bindinfo{ *addr };
                port_needs_update |= true;
            }
        }

        port_needs_update |= (new_settings.port_forwarding_enabled != old_settings.port_forwarding_enabled);

        if (port_needs_update)
        {
            setPeerPort(isPortRandom() ? randomPort() : new_settings.peer_port);
            tr_sessionSetPortForwardingEnabled(this, new_settings.port_forwarding_enabled);
        }
    }

    // We need to update bandwidth if speed settings changed.
    // It's a harmless call, so just call it instead of checking for settings changes
    updateBandwidth(this, TR_UP);
    updateBandwidth(this, TR_DOWN);

    alt_speeds_.load(settings);
    rpc_server_->load(settings);
}

void tr_sessionSet(tr_session* session, tr_variant* settings)
{
    // run it in the libtransmission thread

    if (session->amInSessionThread())
    {
        session->setSettings(settings, false);
    }
    else
    {
        auto lock = session->unique_lock();

        auto done_cv = std::condition_variable_any{};
        session->runInSessionThread(
            [&session, &settings, &done_cv]()
            {
                session->setSettings(settings, false);
                done_cv.notify_one();
            });
        done_cv.wait(lock);
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

    session->settings_.is_incomplete_file_naming_enabled = enabled;
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

void tr_session::setPeerPort(tr_port port_in)
{
    auto const in_session_thread = [this](tr_port port)
    {
        auto const lock = unique_lock();

        auto& private_peer_port = settings_.peer_port;
        private_peer_port = port;
        advertised_peer_port_ = port;

        closePeerPort();

        if (allowsTCP())
        {
            bind_ipv4_.bindAndListenForIncomingPeers(this);

            if (tr_net_hasIPv6(private_peer_port))
            {
                bind_ipv6_.bindAndListenForIncomingPeers(this);
            }
        }

        port_forwarding_->portChanged();

        for (auto* const tor : torrents())
        {
            tr_torrentChangeMyPort(tor);
        }
    };

    runInSessionThread(in_session_thread, port_in);
}

void tr_sessionSetPeerPort(tr_session* session, uint16_t hport)
{
    TR_ASSERT(session != nullptr);

    session->setPeerPort(tr_port::fromHost(hport));
}

uint16_t tr_sessionGetPeerPort(tr_session const* session)
{
    return session != nullptr ? session->localPeerPort().host() : 0U;
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

    session->settings_.peer_port_random_on_start = random;
}

bool tr_sessionGetPeerPortRandomOnStart(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->isPortRandom();
}

tr_port_forwarding_state tr_sessionGetPortForwarding(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->port_forwarding_->state();
}

/***
****
***/

void tr_sessionSetRatioLimited(tr_session* session, bool is_limited)
{
    TR_ASSERT(session != nullptr);

    session->settings_.ratio_limit_enabled = is_limited;
}

void tr_sessionSetRatioLimit(tr_session* session, double desired_ratio)
{
    TR_ASSERT(session != nullptr);

    session->settings_.ratio_limit = desired_ratio;
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

    session->settings_.idle_seeding_limit_enabled = is_limited;
}

void tr_sessionSetIdleLimit(tr_session* session, uint16_t idle_minutes)
{
    TR_ASSERT(session != nullptr);

    session->settings_.idle_seeding_limit_minutes = idle_minutes;
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

std::optional<tr_bytes_per_second_t> tr_session::activeSpeedLimitBps(tr_direction dir) const noexcept
{
    if (tr_sessionUsesAltSpeed(this))
    {
        return tr_toSpeedBytes(tr_sessionGetAltSpeed_KBps(this, dir));
    }

    if (this->isSpeedLimited(dir))
    {
        return tr_toSpeedBytes(tr_sessionGetSpeedLimit_KBps(this, dir));
    }

    return {};
}

static void updateBandwidth(tr_session* session, tr_direction dir)
{
    if (auto const limit_bytes_per_second = session->activeSpeedLimitBps(dir); limit_bytes_per_second)
    {
        session->top_bandwidth_.setLimited(dir, *limit_bytes_per_second > 0U);
        session->top_bandwidth_.setDesiredSpeedBytesPerSecond(dir, *limit_bytes_per_second);
    }
    else
    {
        session->top_bandwidth_.setLimited(dir, false);
    }
}

time_t tr_session::AltSpeedMediator::time()
{
    return tr_time();
}

void tr_session::AltSpeedMediator::isActiveChanged(bool is_active, tr_session_alt_speeds::ChangeReason reason)
{
    auto const in_session_thread = [session = &session_, is_active, reason]()
    {
        updateBandwidth(session, TR_UP);
        updateBandwidth(session, TR_DOWN);

        if (session->alt_speed_active_changed_func_ != nullptr)
        {
            session->alt_speed_active_changed_func_(
                session,
                is_active,
                reason == tr_session_alt_speeds::ChangeReason::User,
                session->alt_speed_active_changed_func_user_data_);
        }
    };

    session_.runInSessionThread(in_session_thread);
}

/***
****  Primary session speed limits
***/

void tr_sessionSetSpeedLimit_KBps(tr_session* session, tr_direction dir, tr_kilobytes_per_second_t limit)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    if (dir == TR_DOWN)
    {
        session->settings_.speed_limit_down = limit;
    }
    else
    {
        session->settings_.speed_limit_up = limit;
    }

    updateBandwidth(session, dir);
}

tr_kilobytes_per_second_t tr_sessionGetSpeedLimit_KBps(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return dir == TR_DOWN ? session->settings_.speed_limit_down : session->settings_.speed_limit_up;
}

void tr_sessionLimitSpeed(tr_session* session, tr_direction dir, bool limited)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    if (dir == TR_DOWN)
    {
        session->settings_.speed_limit_down_enabled = limited;
    }
    else
    {
        session->settings_.speed_limit_up_enabled = limited;
    }

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

void tr_sessionSetAltSpeed_KBps(tr_session* session, tr_direction dir, tr_kilobytes_per_second_t limit)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    session->alt_speeds_.setLimitKBps(dir, limit);
    updateBandwidth(session, dir);
}

tr_kilobytes_per_second_t tr_sessionGetAltSpeed_KBps(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return session->alt_speeds_.limitKBps(dir);
}

void tr_sessionUseAltSpeedTime(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->alt_speeds_.setSchedulerEnabled(enabled);
}

bool tr_sessionUsesAltSpeedTime(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->alt_speeds_.isSchedulerEnabled();
}

void tr_sessionSetAltSpeedBegin(tr_session* session, size_t minutes_since_midnight)
{
    TR_ASSERT(session != nullptr);

    session->alt_speeds_.setStartMinute(minutes_since_midnight);
}

size_t tr_sessionGetAltSpeedBegin(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->alt_speeds_.startMinute();
}
void tr_sessionSetAltSpeedEnd(tr_session* session, size_t minutes_since_midnight)
{
    TR_ASSERT(session != nullptr);

    session->alt_speeds_.setEndMinute(minutes_since_midnight);
}

size_t tr_sessionGetAltSpeedEnd(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->alt_speeds_.endMinute();
}

void tr_sessionSetAltSpeedDay(tr_session* session, tr_sched_day days)
{
    TR_ASSERT(session != nullptr);

    session->alt_speeds_.setWeekdays(days);
}

tr_sched_day tr_sessionGetAltSpeedDay(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->alt_speeds_.weekdays();
}

void tr_sessionUseAltSpeed(tr_session* session, bool enabled)
{
    session->alt_speeds_.setActive(enabled, tr_session_alt_speeds::ChangeReason::User);
}

bool tr_sessionUsesAltSpeed(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->alt_speeds_.isActive();
}

void tr_sessionSetAltSpeedFunc(tr_session* session, tr_altSpeedFunc func, void* user_data)
{
    TR_ASSERT(session != nullptr);

    session->alt_speed_active_changed_func_ = func;
    session->alt_speed_active_changed_func_user_data_ = user_data;
}

/***
****
***/

void tr_sessionSetPeerLimit(tr_session* session, uint16_t max_global_peers)
{
    TR_ASSERT(session != nullptr);

    session->settings_.peer_limit_global = max_global_peers;
}

uint16_t tr_sessionGetPeerLimit(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->peerLimit();
}

void tr_sessionSetPeerLimitPerTorrent(tr_session* session, uint16_t max_peers)
{
    TR_ASSERT(session != nullptr);

    session->settings_.peer_limit_per_torrent = max_peers;
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

    session->settings_.should_start_added_torrents = !is_paused;
}

bool tr_sessionGetPaused(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->shouldPauseAddedTorrents();
}

void tr_sessionSetDeleteSource(tr_session* session, bool delete_source)
{
    TR_ASSERT(session != nullptr);

    session->settings_.should_delete_source_torrents = delete_source;
}

bool tr_sessionGetDeleteSource(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->shouldDeleteSource();
}

/***
****
***/

static tr_kilobytes_per_second_t tr_sessionGetRawSpeed_Bps(tr_session const* session, tr_direction dir)
{
    return session != nullptr ? session->top_bandwidth_.getRawSpeedBytesPerSecond(0, dir) : 0;
}

double tr_sessionGetRawSpeed_KBps(tr_session const* session, tr_direction dir)
{
    return tr_toSpeedKBps(tr_sessionGetRawSpeed_Bps(session, dir));
}

void tr_session::closeImplPart1()
{
    is_closing_ = true;

    // close the low-hanging fruit that can be closed immediately w/o consequences
    verifier_.reset();
    save_timer_.reset();
    now_timer_.reset();
    rpc_server_.reset();
    lpd_.reset();
    port_forwarding_.reset();
    closePeerPort();

    // tell other items to start shutting down
    udp_core_->startShutdown();
    announcer_udp_->startShutdown();

    // Close the torrents in order of most active to least active
    // so that the most important announce=stopped events are
    // fired out first...
    auto torrents = getAllTorrents();
    std::sort(
        std::begin(torrents),
        std::end(torrents),
        [](auto const* a, auto const* b)
        {
            auto const a_cur = a->downloadedCur + a->uploadedCur;
            auto const b_cur = b->downloadedCur + b->uploadedCur;
            return a_cur > b_cur; // larger xfers go first
        });
    for (auto* tor : torrents)
    {
        tr_torrentFreeInSessionThread(tor);
    }
    torrents.clear();
    // ...and now that all the torrents have been closed, any
    // remaining `event=stopped` announce messages are queued in
    // the announcer. The announcer's destructor sends all those
    // out via `web_`...
    tr_announcerClose(this);
    // ...and now that those are queued, tell web_ that we're
    // shutting down soon. This leaves the `event=stopped` messages
    // in the queue but refuses to take any _new_ tasks
    this->web_->startShutdown();
    this->cache.reset();

    // recycle the now-unused save_timer_ here to wait for UDP shutdown
    TR_ASSERT(!save_timer_);
    save_timer_ = timerMaker().create([this]() { closeImplPart2(); });
    save_timer_->startRepeating(50ms);
}

void tr_session::closeImplPart2()
{
    // try to keep the UDP announcer alive long enough to send out
    // all the &event=stopped tracker announces
    if (announcer_udp_ && !announcer_udp_->isIdle())
    {
        announcer_udp_->upkeep();
        return;
    }

    save_timer_.reset();

    this->announcer_udp_.reset();
    this->udp_core_.reset();

    stats().saveIfDirty();
    peer_mgr_.reset();
    tr_utpClose(this);
    openFiles().closeAll();
    is_closed_ = true;
}

void tr_sessionClose(tr_session* session)
{
    TR_ASSERT(session != nullptr);

    static auto constexpr DeadlineSecs = 10s;
    auto const deadline = std::chrono::steady_clock::now() + DeadlineSecs;
    auto const deadline_reached = [deadline]()
    {
        return std::chrono::steady_clock::now() >= deadline;
    };

    tr_logAddInfo(fmt::format(_("Transmission version {version} shutting down"), fmt::arg("version", LONG_VERSION_STRING)));

    /* close the session */
    session->runInSessionThread([session]() { session->closeImplPart1(); });

    while (!session->isClosed() && !deadline_reached())
    {
        tr_logAddTrace("waiting for the libtransmission thread to finish");
        tr_wait_msec(10);
    }

    // There's usually a bit of housekeeping to do during shutdown,
    // e.g. sending out `event=stopped` announcements to trackers,
    // so wait a bit for the session thread to close.
    while (!deadline_reached() && (!session->web_->isClosed() || session->announcer != nullptr || session->announcer_udp_))
    {
        tr_logAddTrace(fmt::format(
            "waiting on port unmap ({}) or announcer ({})... now {}",
            fmt::ptr(session->port_forwarding_.get()),
            fmt::ptr(session->announcer),
            time(nullptr)));
        tr_wait_msec(50);
    }

    session->web_.reset();

    delete session;
}

struct sessionLoadTorrentsData
{
    tr_session* session;
    tr_ctor* ctor;
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

    if (auto const n = std::size(torrents); n != 0U)
    {
        tr_logAddInfo(fmt::format(ngettext("Loaded {count} torrent", "Loaded {count} torrents", n), fmt::arg("count", n)));
    }

    data->done = true;
}

size_t tr_sessionLoadTorrents(tr_session* session, tr_ctor* ctor)
{
    auto data = sessionLoadTorrentsData{};
    data.session = session;
    data.ctor = ctor;
    data.done = false;
    session->runInSessionThread(sessionLoadTorrents, &data);
    while (!data.done)
    {
        tr_wait_msec(100);
    }

    return std::size(session->torrents());
}

size_t tr_sessionGetAllTorrents(tr_session* session, tr_torrent** buf, size_t buflen)
{
    auto& torrents = session->torrents();
    auto const n = std::size(torrents);

    if (buflen >= n)
    {
        std::copy_n(std::begin(torrents), n, buf);
    }

    return n;
}

/***
****
***/

void tr_sessionSetPexEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    session->settings_.pex_enabled = enabled;
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

    session->runInSessionThread(
        [session, enabled]()
        {
            session->udp_core_.reset();
            session->settings_.dht_enabled = enabled;
            session->udp_core_ = std::make_unique<tr_session::tr_udp_core>(*session, session->udpPort());
        });
}

/***
****
***/

bool tr_session::allowsUTP() const noexcept
{
#ifdef WITH_UTP
    return settings_.utp_enabled;
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

    session->settings_.utp_enabled = enabled;
}

void tr_sessionSetLPDEnabled(tr_session* session, bool enabled)
{
    TR_ASSERT(session != nullptr);

    if (enabled == session->allowsLPD())
    {
        return;
    }

    session->runInSessionThread(
        [session, enabled]()
        {
            session->lpd_.reset();
            session->settings_.lpd_enabled = enabled;
            if (enabled)
            {
                session->lpd_ = tr_lpd::create(session->lpd_mediator_, session->eventBase());
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

void tr_sessionSetCacheLimit_MB(tr_session* session, size_t mb)
{
    TR_ASSERT(session != nullptr);

    session->settings_.cache_size_mb = mb;
    session->cache->setLimit(tr_toMemBytes(mb));
}

size_t tr_sessionGetCacheLimit_MB(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->settings_.cache_size_mb;
}

/***
****
***/

void tr_session::setDefaultTrackers(std::string_view trackers)
{
    auto const oldval = default_trackers_;

    settings_.default_trackers_str = trackers;
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
    session->runInSessionThread([session, enabled]() { session->port_forwarding_->setEnabled(enabled); });
}

bool tr_sessionIsPortForwardingEnabled(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    return session->port_forwarding_->isEnabled();
}

/***
****
***/

void tr_session::useBlocklist(bool enabled)
{
    settings_.blocklist_enabled = enabled;

    std::for_each(
        std::begin(blocklists_),
        std::end(blocklists_),
        [enabled](auto& blocklist) { blocklist.setEnabled(enabled); });
}

bool tr_session::addressIsBlocked(tr_address const& addr) const noexcept
{
    return std::any_of(
        std::begin(blocklists_),
        std::end(blocklists_),
        [&addr](auto& blocklist) { return blocklist.contains(addr); });
}

void tr_sessionReloadBlocklists(tr_session* session)
{
    session->blocklists_ = libtransmission::Blocklist::loadBlocklists(session->blocklist_dir_, session->useBlocklist());

    if (session->peer_mgr_)
    {
        tr_peerMgrOnBlocklistChanged(session->peer_mgr_.get());
    }
}

size_t tr_blocklistGetRuleCount(tr_session const* session)
{
    TR_ASSERT(session != nullptr);

    auto& src = session->blocklists_;
    return std::accumulate(std::begin(src), std::end(src), 0, [](int sum, auto& cur) { return sum + std::size(cur); });
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

    // These rules will replace the default blocklist.
    // Build the path of the default blocklist .bin file where we'll save these rules.
    auto const bin_file = tr_pathbuf{ session->blocklist_dir_, '/', DEFAULT_BLOCKLIST_FILENAME };

    // Try to save it
    auto added = libtransmission::Blocklist::saveNew(content_filename, bin_file, session->useBlocklist());
    if (!added)
    {
        return 0U;
    }

    auto const n_rules = std::size(*added);

    // Add (or replace) it in our blocklists_ vector
    auto& src = session->blocklists_;
    if (auto iter = std::find_if(
            std::begin(src),
            std::end(src),
            [&bin_file](auto const& candidate) { return bin_file == candidate.binFile(); });
        iter != std::end(src))
    {
        *iter = std::move(*added);
    }
    else
    {
        src.emplace_back(std::move(*added));
    }

    return n_rules;
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

void tr_sessionSetQueueSize(tr_session* session, tr_direction dir, size_t max_simultaneous_torrents)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    if (dir == TR_DOWN)
    {
        session->settings_.download_queue_size = max_simultaneous_torrents;
    }
    else
    {
        session->settings_.seed_queue_size = max_simultaneous_torrents;
    }
}

size_t tr_sessionGetQueueSize(tr_session const* session, tr_direction dir)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    return session->queueSize(dir);
}

void tr_sessionSetQueueEnabled(tr_session* session, tr_direction dir, bool do_limit_simultaneous_torrents)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_isDirection(dir));

    if (dir == TR_DOWN)
    {
        session->settings_.download_queue_enabled = do_limit_simultaneous_torrents;
    }
    else
    {
        session->settings_.seed_queue_enabled = do_limit_simultaneous_torrents;
    }
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

    session->settings_.queue_stalled_minutes = minutes;
}

void tr_sessionSetQueueStalledEnabled(tr_session* session, bool is_enabled)
{
    TR_ASSERT(session != nullptr);

    session->settings_.queue_stalled_enabled = is_enabled;
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
    auto active_count = size_t{};
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
            limits.up_limit_KBps = static_cast<tr_kilobytes_per_second_t>(limit);
        }

        if (auto limit = int64_t{}; tr_variantDictFindInt(dict, TR_KEY_downloadLimit, &limit))
        {
            limits.down_limit_KBps = static_cast<tr_kilobytes_per_second_t>(limit);
        }

        group.setLimits(&limits);

        if (auto honors = bool{}; tr_variantDictFindBool(dict, TR_KEY_honorsSessionLimits, &honors))
        {
            group.honorParentLimits(TR_UP, honors);
            group.honorParentLimits(TR_DOWN, honors);
        }
    }
    tr_variantClear(&groups_dict);
}

static int bandwidthGroupWrite(tr_session const* session, std::string_view config_dir)
{
    auto const& groups = session->bandwidthGroups();

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
    tr_variantClear(&groups_dict);
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

auto makeBlocklistDir(std::string_view config_dir)
{
    auto dir = fmt::format("{:s}/blocklists"sv, config_dir);
    tr_sys_dir_create(dir.c_str(), TR_SYS_DIR_CREATE_PARENTS, 0777);
    return dir;
}

} // namespace

tr_session::tr_session(std::string_view config_dir, tr_variant* settings_dict)
    : config_dir_{ config_dir }
    , resume_dir_{ makeResumeDir(config_dir) }
    , torrent_dir_{ makeTorrentDir(config_dir) }
    , blocklist_dir_{ makeBlocklistDir(config_dir) }
    , session_thread_{ tr_session_thread::create() }
    , timer_maker_{ std::make_unique<libtransmission::EvTimerMaker>(eventBase()) }
    , settings_{ settings_dict }
    , session_id_{ tr_time }
    , peer_mgr_{ tr_peerMgrNew(this), tr_peerMgrFree }
    , rpc_server_{ std::make_unique<tr_rpc_server>(this, settings_dict) }
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

    verifier_->addCallback(tr_torrentOnVerifyDone);
}

void tr_session::addIncoming(tr_address const& addr, tr_port port, struct tr_peer_socket const socket)
{
    tr_peerMgrAddIncoming(peer_mgr_.get(), addr, port, socket);
}

void tr_session::addTorrent(tr_torrent* tor)
{
    tor->unique_id_ = torrents().add(tor);

    tr_peerMgrAddTorrent(peer_mgr_.get(), tor);
}
