// This file Copyright © 2009-2022 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <csignal> /* sig_atomic_t */
#include <cstdio>
#include <cstring> /* memcpy(), memset() */
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#undef gai_strerror
#define gai_strerror gai_strerrorA
#else
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netdb.h>
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <dht/dht.h>

#include <event2/event.h>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h"
#include "session.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-strbuf.h"
#include "trevent.h"
#include "variant.h"
#include "utils.h" // tr_time(), _()

using namespace std::literals;

static struct event* dht_timer = nullptr;
static unsigned char myid[20];
static tr_session* session_ = nullptr;

static void timer_callback(evutil_socket_t s, short type, void* session);

static bool bootstrap_done(tr_session* session, int af)
{
    if (af == 0)
    {
        return bootstrap_done(session, AF_INET) && bootstrap_done(session, AF_INET6);
    }

    int const status = tr_dhtStatus(session, af, nullptr);
    return status == TR_DHT_STOPPED || status >= TR_DHT_FIREWALLED;
}

static void nap(int roughly_sec)
{
    int const roughly_msec = roughly_sec * 1000;
    int const msec = roughly_msec / 2 + tr_rand_int_weak(roughly_msec);
    tr_wait_msec(msec);
}

static int bootstrap_af(tr_session* session)
{
    if (bootstrap_done(session, AF_INET6))
    {
        return AF_INET;
    }

    if (bootstrap_done(session, AF_INET))
    {
        return AF_INET6;
    }

    return 0;
}

static void bootstrap_from_name(char const* name, tr_port port, int af)
{
    auto hints = addrinfo{};
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_family = af;

    auto port_str = std::array<char, 16>{};
    *fmt::format_to(std::data(port_str), FMT_STRING("{:d}"), port.host()) = '\0';

    addrinfo* info = nullptr;
    if (int const rc = getaddrinfo(name, std::data(port_str), &hints, &info); rc != 0)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't look up '{address}:{port}': {error} ({error_code})"),
            fmt::arg("address", name),
            fmt::arg("port", port.host()),
            fmt::arg("error", gai_strerror(rc)),
            fmt::arg("error_code", rc)));
        return;
    }

    addrinfo* infop = info;
    while (infop != nullptr)
    {
        dht_ping_node(infop->ai_addr, infop->ai_addrlen);

        nap(15);

        if (bootstrap_done(session_, af))
        {
            break;
        }

        infop = infop->ai_next;
    }

    freeaddrinfo(info);
}

static void dht_boostrap_from_file(tr_session* session)
{
    if (bootstrap_done(session, 0))
    {
        return;
    }

    // check for a manual bootstrap file.
    auto in = std::ifstream{ tr_pathbuf{ session->configDir(), "/dht.bootstrap"sv } };
    if (!in.is_open())
    {
        return;
    }

    // format is each line has address, a space char, and port number
    tr_logAddTrace("Attempting manual bootstrap");
    auto line = std::string{};
    while (!bootstrap_done(session, 0) && std::getline(in, line))
    {
        auto line_stream = std::istringstream{ line };
        auto addrstr = std::string{};
        auto hport = uint16_t{};
        line_stream >> addrstr >> hport;

        if (line_stream.bad() || std::empty(addrstr))
        {
            tr_logAddWarn(fmt::format(_("Couldn't parse line: '{line}'"), fmt::arg("line", line)));
        }
        else
        {
            bootstrap_from_name(addrstr.c_str(), tr_port::fromHost(hport), bootstrap_af(session_));
        }
    }
}

static void dht_bootstrap(tr_session* session, std::vector<uint8_t> nodes, std::vector<uint8_t> nodes6)
{
    if (session_ != session)
    {
        return;
    }

    auto const num = std::size(nodes) / 6;
    if (num > 0)
    {
        tr_logAddDebug(fmt::format("Bootstrapping from {} IPv4 nodes", num));
    }

    auto const num6 = std::size(nodes6) / 18;
    if (num6 > 0)
    {
        tr_logAddDebug(fmt::format("Bootstrapping from {} IPv6 nodes", num6));
    }

    for (size_t i = 0; i < std::max(num, num6); ++i)
    {
        if (i < num && !bootstrap_done(session, AF_INET))
        {
            auto addr = tr_address{};
            memset(&addr, 0, sizeof(addr));
            addr.type = TR_AF_INET;
            memcpy(&addr.addr.addr4, &nodes[i * 6], 4);
            auto const [port, out] = tr_port::fromCompact(&nodes[i * 6 + 4]);
            tr_dhtAddNode(session, &addr, port, true);
        }

        if (i < num6 && !bootstrap_done(session, AF_INET6))
        {
            auto addr = tr_address{};
            memset(&addr, 0, sizeof(addr));
            addr.type = TR_AF_INET6;
            memcpy(&addr.addr.addr6, &nodes6[i * 18], 16);
            auto const [port, out] = tr_port::fromCompact(&nodes6[i * 18 + 16]);
            tr_dhtAddNode(session, &addr, port, true);
        }

        /* Our DHT code is able to take up to 9 nodes in a row without
           dropping any. After that, it takes some time to split buckets.
           So ping the first 8 nodes quickly, then slow down. */
        if (i < 8U)
        {
            nap(2);
        }
        else
        {
            nap(15);
        }

        if (bootstrap_done(session_, 0))
        {
            break;
        }
    }

    if (!bootstrap_done(session, 0))
    {
        dht_boostrap_from_file(session);
    }

    if (!bootstrap_done(session, 0))
    {
        for (int i = 0; i < 6; ++i)
        {
            /* We don't want to abuse our bootstrap nodes, so be very
               slow.  The initial wait is to give other nodes a chance
               to contact us before we attempt to contact a bootstrap
               node, for example because we've just been restarted. */
            nap(40);

            if (bootstrap_done(session, 0))
            {
                break;
            }

            if (i == 0)
            {
                tr_logAddDebug("Attempting bootstrap from dht.transmissionbt.com");
            }

            bootstrap_from_name("dht.transmissionbt.com", tr_port::fromHost(6881), bootstrap_af(session_));
        }
    }

    tr_logAddTrace("Finished bootstrapping");
}

int tr_dhtInit(tr_session* ss)
{
    if (session_ != nullptr) /* already initialized */
    {
        return -1;
    }

    tr_logAddInfo(_("Initializing DHT"));

    if (tr_env_key_exists("TR_DHT_VERBOSE"))
    {
        dht_debug = stderr;
    }

    auto benc = tr_variant{};
    auto const dat_file = tr_pathbuf{ ss->configDir(), "/dht.dat"sv };
    auto const ok = tr_variantFromFile(&benc, TR_VARIANT_PARSE_BENC, dat_file.sv());

    bool have_id = false;
    auto nodes = std::vector<uint8_t>{};
    auto nodes6 = std::vector<uint8_t>{};
    if (ok)
    {
        auto sv = std::string_view{};
        have_id = tr_variantDictFindStrView(&benc, TR_KEY_id, &sv);
        if (have_id && std::size(sv) == 20)
        {
            std::copy(std::begin(sv), std::end(sv), myid);
        }

        size_t raw_len = 0U;
        uint8_t const* raw = nullptr;
        if (ss->udp_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes, &raw, &raw_len) && raw_len % 6 == 0)
        {
            nodes.assign(raw, raw + raw_len);
        }

        if (ss->udp6_socket != TR_BAD_SOCKET && tr_variantDictFindRaw(&benc, TR_KEY_nodes6, &raw, &raw_len) &&
            raw_len % 18 == 0)
        {
            nodes6.assign(raw, raw + raw_len);
        }

        tr_variantFree(&benc);
    }

    if (have_id)
    {
        tr_logAddTrace("Reusing old id");
    }
    else
    {
        /* Note that DHT ids need to be distributed uniformly,
         * so it should be something truly random. */
        tr_logAddTrace("Generating new id");
        tr_rand_buffer(myid, 20);
    }

    if (int const rc = dht_init(ss->udp_socket, ss->udp6_socket, myid, nullptr); rc < 0)
    {
        auto const errcode = errno;
        tr_logAddDebug(fmt::format("DHT initialization failed: {} ({})", tr_strerror(errcode), errcode));
        session_ = nullptr;
        return -1;
    }

    session_ = ss;

    std::thread(dht_bootstrap, session_, nodes, nodes6).detach();

    dht_timer = evtimer_new(session_->event_base, timer_callback, session_);
    tr_timerAdd(*dht_timer, 0, tr_rand_int_weak(1000000));

    tr_logAddDebug("DHT initialized");

    return 1;
}

void tr_dhtUninit(tr_session* ss)
{
    if (session_ != ss)
    {
        return;
    }

    tr_logAddTrace("Uninitializing DHT");

    if (dht_timer != nullptr)
    {
        event_free(dht_timer);
        dht_timer = nullptr;
    }

    /* Since we only save known good nodes, avoid erasing older data if we
       don't know enough nodes. */
    if (tr_dhtStatus(ss, AF_INET, nullptr) < TR_DHT_FIREWALLED && tr_dhtStatus(ss, AF_INET6, nullptr) < TR_DHT_FIREWALLED)
    {
        tr_logAddTrace("Not saving nodes, DHT not ready");
    }
    else
    {
        auto constexpr MaxNodes = size_t{ 300 };
        auto constexpr PortLen = size_t{ 2 };
        auto constexpr CompactAddrLen = size_t{ 4 };
        auto constexpr CompactLen = size_t{ CompactAddrLen + PortLen };
        auto constexpr Compact6AddrLen = size_t{ 16 };
        auto constexpr Compact6Len = size_t{ Compact6AddrLen + PortLen };

        struct sockaddr_in sins[MaxNodes];
        struct sockaddr_in6 sins6[MaxNodes];
        int num = MaxNodes;
        int num6 = MaxNodes;
        int const n = dht_get_nodes(sins, &num, sins6, &num6);
        tr_logAddTrace(fmt::format("Saving {} ({} + {}) nodes", n, num, num6));

        tr_variant benc;
        tr_variantInitDict(&benc, 3);
        tr_variantDictAddRaw(&benc, TR_KEY_id, myid, 20);

        if (num > 0)
        {
            char compact[MaxNodes * CompactLen];
            char* out = compact;
            for (struct sockaddr_in const* in = sins; in < sins + num; ++in)
            {
                memcpy(out, &in->sin_addr, CompactAddrLen);
                out += CompactAddrLen;
                memcpy(out, &in->sin_port, PortLen);
                out += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes, compact, out - compact);
        }

        if (num6 > 0)
        {
            char compact6[MaxNodes * Compact6Len];
            char* out6 = compact6;
            for (struct sockaddr_in6 const* in = sins6; in < sins6 + num6; ++in)
            {
                memcpy(out6, &in->sin6_addr, Compact6AddrLen);
                out6 += Compact6AddrLen;
                memcpy(out6, &in->sin6_port, PortLen);
                out6 += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes6, compact6, out6 - compact6);
        }

        auto const dat_file = tr_pathbuf{ ss->configDir(), "/dht.dat"sv };
        tr_variantToFile(&benc, TR_VARIANT_FMT_BENC, dat_file.sv());
        tr_variantFree(&benc);
    }

    dht_uninit();
    tr_logAddTrace("Done uninitializing DHT");

    session_ = nullptr;
}

bool tr_dhtEnabled(tr_session const* ss)
{
    return ss != nullptr && ss == session_;
}

struct getstatus_closure
{
    int af;
    sig_atomic_t status;
    sig_atomic_t count;
};

static void getstatus(getstatus_closure* const closure)
{
    int good = 0;
    int dubious = 0;
    int incoming = 0;
    dht_nodes(closure->af, &good, &dubious, nullptr, &incoming);

    closure->count = good + dubious;

    if (good < 4 || good + dubious <= 8)
    {
        closure->status = TR_DHT_BROKEN;
    }
    else if (good < 40)
    {
        closure->status = TR_DHT_POOR;
    }
    else if (incoming < 8)
    {
        closure->status = TR_DHT_FIREWALLED;
    }
    else
    {
        closure->status = TR_DHT_GOOD;
    }
}

int tr_dhtStatus(tr_session* session, int af, int* setme_node_count)
{
    auto closure = getstatus_closure{ af, -1, -1 };

    if (!tr_dhtEnabled(session) || (af == AF_INET && session->udp_socket == TR_BAD_SOCKET) ||
        (af == AF_INET6 && session->udp6_socket == TR_BAD_SOCKET))
    {
        if (setme_node_count != nullptr)
        {
            *setme_node_count = 0;
        }

        return TR_DHT_STOPPED;
    }

    tr_runInEventThread(session, getstatus, &closure);

    while (closure.status < 0)
    {
        tr_wait_msec(50 /*msec*/);
    }

    if (setme_node_count != nullptr)
    {
        *setme_node_count = closure.count;
    }

    return closure.status;
}

tr_port tr_dhtPort(tr_session* ss)
{
    return tr_dhtEnabled(ss) ? ss->udp_port : tr_port{};
}

bool tr_dhtAddNode(tr_session* ss, tr_address const* address, tr_port port, bool bootstrap)
{
    int const af = address->isIPv4() ? AF_INET : AF_INET6;

    if (!tr_dhtEnabled(ss))
    {
        return false;
    }

    /* Since we don't want to abuse our bootstrap nodes,
     * we don't ping them if the DHT is in a good state. */

    if (bootstrap && (tr_dhtStatus(ss, af, nullptr) >= TR_DHT_FIREWALLED))
    {
        return false;
    }

    if (address->isIPv4())
    {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        memcpy(&sin.sin_addr, &address->addr.addr4, 4);
        sin.sin_port = port.network();
        dht_ping_node((struct sockaddr*)&sin, sizeof(sin));
        return true;
    }

    if (address->isIPv6())
    {
        struct sockaddr_in6 sin6;
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        memcpy(&sin6.sin6_addr, &address->addr.addr6, 16);
        sin6.sin6_port = port.network();
        dht_ping_node((struct sockaddr*)&sin6, sizeof(sin6));
        return true;
    }

    return false;
}

char const* tr_dhtPrintableStatus(int status)
{
    switch (status)
    {
    case TR_DHT_STOPPED:
        return "stopped";

    case TR_DHT_BROKEN:
        return "broken";

    case TR_DHT_POOR:
        return "poor";

    case TR_DHT_FIREWALLED:
        return "firewalled";

    case TR_DHT_GOOD:
        return "good";

    default:
        return "???";
    }
}

static void callback(void* /*ignore*/, int event, unsigned char const* info_hash, void const* data, size_t data_len)
{
    auto hash = tr_sha1_digest_t{};
    std::copy_n(reinterpret_cast<std::byte const*>(info_hash), std::size(hash), std::data(hash));
    auto const lock = session_->unique_lock();
    auto* const tor = session_->torrents().get(hash);

    if (event == DHT_EVENT_VALUES || event == DHT_EVENT_VALUES6)
    {
        if (tor != nullptr && tor->allowsDht())
        {
            auto const pex = event == DHT_EVENT_VALUES ? tr_peerMgrCompactToPex(data, data_len, nullptr, 0) :
                                                         tr_peerMgrCompact6ToPex(data, data_len, nullptr, 0);
            tr_peerMgrAddPex(tor, TR_PEER_FROM_DHT, std::data(pex), std::size(pex));
            tr_logAddDebugTor(
                tor,
                fmt::format("Learned {} {} peers from DHT", std::size(pex), event == DHT_EVENT_VALUES6 ? "IPv6" : "IPv4"));
        }
    }
    else if (event == DHT_EVENT_SEARCH_DONE || event == DHT_EVENT_SEARCH_DONE6)
    {
        if (tor != nullptr)
        {
            if (event == DHT_EVENT_SEARCH_DONE)
            {
                tr_logAddTraceTor(tor, "IPv4 DHT announce done");
            }
            else
            {
                tr_logAddTraceTor(tor, "IPv6 DHT announce done");
            }
        }
    }
}

enum class AnnounceResult
{
    INVALID,
    OK,
    FAILED
};

static AnnounceResult tr_dhtAnnounce(tr_torrent* tor, int af, bool announce)
{
    if (!tor->allowsDht())
    {
        return AnnounceResult::INVALID;
    }

    int numnodes = 0;
    int const status = tr_dhtStatus(tor->session, af, &numnodes);
    if (status == TR_DHT_STOPPED)
    {
        // let the caller believe everything is all right.
        return AnnounceResult::OK;
    }

    if (status < TR_DHT_POOR)
    {
        tr_logAddTraceTor(
            tor,
            fmt::format(
                "{} DHT not ready ({}, {} nodes)",
                af == AF_INET6 ? "IPv6" : "IPv4",
                tr_dhtPrintableStatus(status),
                numnodes));
        return AnnounceResult::FAILED;
    }

    auto const* dht_hash = reinterpret_cast<unsigned char const*>(std::data(tor->infoHash()));
    auto const hport = announce ? session_->peerPort().host() : 0;
    int const rc = dht_search(dht_hash, hport, af, callback, nullptr);
    if (rc < 0)
    {
        auto const error_code = errno;
        tr_logAddWarnTor(
            tor,
            fmt::format(
                _("Unable to announce torrent in DHT with {type}: {error} ({error_code}); state is {state}"),
                fmt::arg("type", af == AF_INET6 ? "IPv6" : "IPv4"),
                fmt::arg("state", tr_dhtPrintableStatus(status)),
                fmt::arg("error_code", error_code),
                fmt::arg("error", tr_strerror(error_code))));
        return AnnounceResult::FAILED;
    }

    tr_logAddTraceTor(
        tor,
        fmt::format(
            "Starting {} DHT announce ({}, {} nodes)",
            af == AF_INET6 ? "IPv6" : "IPv4",
            tr_dhtPrintableStatus(status),
            numnodes));

    return AnnounceResult::OK;
}

void tr_dhtUpkeep(tr_session* session)
{
    time_t const now = tr_time();

    for (auto* const tor : session->torrents())
    {
        if (!tor->isRunning || !tor->allowsDht())
        {
            continue;
        }

        if (tor->dhtAnnounceAt <= now)
        {
            auto const rc = tr_dhtAnnounce(tor, AF_INET, true);

            tor->dhtAnnounceAt = now +
                ((rc == AnnounceResult::FAILED) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }

        if (tor->dhtAnnounce6At <= now)
        {
            auto const rc = tr_dhtAnnounce(tor, AF_INET6, true);

            tor->dhtAnnounce6At = now +
                ((rc == AnnounceResult::FAILED) ? 5 + tr_rand_int_weak(5) : 25 * 60 + tr_rand_int_weak(3 * 60));
        }
    }
}

void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen, void* sv)
{
    TR_ASSERT(tr_isSession(static_cast<tr_session*>(sv)));

    if (sv != session_)
    {
        return;
    }

    time_t tosleep = 0;
    int const rc = dht_periodic(buf, buflen, from, fromlen, &tosleep, callback, nullptr);

    if (rc < 0)
    {
        if (errno == EINTR)
        {
            tosleep = 0;
        }
        else
        {
            auto const errcode = errno;
            tr_logAddDebug(fmt::format("dht_periodic failed: {} ({})", tr_strerror(errcode), errcode));
            if (errcode == EINVAL || errcode == EFAULT)
            {
                // TODO: maybe just turn it off instead of crashing?
                abort();
            }

            tosleep = 1;
        }
    }

    /* Being slightly late is fine,
       and has the added benefit of adding some jitter. */
    tr_timerAdd(*dht_timer, (int)tosleep, tr_rand_int_weak(1000000));
}

static void timer_callback(evutil_socket_t /*s*/, short /*type*/, void* session)
{
    tr_dhtCallback(nullptr, 0, nullptr, 0, session);
}

/* This function should return true when a node is blacklisted.  We do
   not support using a blacklist with the DHT in Transmission, since
   massive (ab)use of this feature could harm the DHT.  However, feel
   free to add support to your private copy as long as you don't
   redistribute it. */

int dht_blacklisted(sockaddr const* /*sa*/, int /*salen*/)
{
    return 0;
}

void dht_hash(void* hash_return, int hash_size, void const* v1, int len1, void const* v2, int len2, void const* v3, int len3)
{
    auto* setme = reinterpret_cast<std::byte*>(hash_return);
    std::fill_n(static_cast<char*>(hash_return), hash_size, '\0');

    auto const sv1 = std::string_view{ static_cast<char const*>(v1), size_t(len1) };
    auto const sv2 = std::string_view{ static_cast<char const*>(v2), size_t(len2) };
    auto const sv3 = std::string_view{ static_cast<char const*>(v3), size_t(len3) };
    auto const digest = tr_sha1::digest(sv1, sv2, sv3);
    std::copy_n(std::data(digest), std::min(size_t(hash_size), std::size(digest)), setme);
}

int dht_random_bytes(void* buf, size_t size)
{
    tr_rand_buffer(buf, size);
    return size;
}

int dht_sendto(int sockfd, void const* buf, int len, int flags, struct sockaddr const* to, int tolen)
{
    return sendto(sockfd, static_cast<char const*>(buf), len, flags, to, tolen);
}

#if defined(_WIN32) && !defined(__MINGW32__)

extern "C" int dht_gettimeofday(struct timeval* tv, [[maybe_unused]] timezone* tz)
{
    TR_ASSERT(tz == nullptr);
    *tv = tr_gettimeofday();
    return 0;
}

#endif
