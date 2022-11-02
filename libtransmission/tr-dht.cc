// This file Copyright © 2009-2022 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib> // for abort()
#include <cstring> // for memcpy()
#include <ctime>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <tuple> // for std::tie()
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#undef gai_strerror
#define gai_strerror gai_strerrorA
#else
#include <sys/time.h> // for `struct timezone`
#include <sys/types.h>
#include <sys/socket.h> /* socket(), bind() */
#include <netdb.h>
#include <netinet/in.h> /* sockaddr_in */
#endif

#include <dht/dht.h>

#include <fmt/format.h>

#include "transmission.h"

#include "crypto-utils.h"
#include "file.h"
#include "log.h"
#include "net.h"
#include "peer-mgr.h"
#include "session.h"
#include "timer.h"
#include "torrent.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-strbuf.h"
#include "variant.h"
#include "utils.h" // tr_time(), _()

using namespace std::literals;

namespace
{
struct Impl
{
    std::unique_ptr<libtransmission::Timer> timer;
    std::array<unsigned char, 20> id = {};
    tr_socket_t udp4_socket = TR_BAD_SOCKET;
    tr_socket_t udp6_socket = TR_BAD_SOCKET;
    tr_session* session = nullptr;
};

Impl impl = {};
} // namespace

// mutex-locked wrapper around libdht's API
namespace locked_dht
{
namespace
{

[[nodiscard]] auto unique_lock()
{
    static std::recursive_mutex dht_mutex;
    return std::unique_lock(dht_mutex);
}

} // namespace

auto getNodes(struct sockaddr_in* sin, int* num, struct sockaddr_in6* sin6, int* num6)
{
    auto lock = unique_lock();
    return dht_get_nodes(sin, num, sin6, num6);
}

auto init(int s, int s6, unsigned char const* id, unsigned char const* v)
{
    auto lock = unique_lock();
    return dht_init(s, s6, id, v);
}

auto nodes(int af, int* good_return, int* dubious_return, int* cached_return, int* incoming_return)
{
    auto lock = unique_lock();
    return dht_nodes(af, good_return, dubious_return, cached_return, incoming_return);
}

auto periodic(
    void const* buf,
    size_t buflen,
    struct sockaddr const* from,
    int fromlen,
    time_t* tosleep,
    dht_callback_t* callback,
    void* closure)
{
    auto lock = unique_lock();
    return dht_periodic(buf, buflen, from, fromlen, tosleep, callback, closure);
}

auto ping_node(struct sockaddr const* sa, int salen)
{
    auto lock = unique_lock();
    return dht_ping_node(sa, salen);
}

auto search(unsigned char const* id, int port, int af, dht_callback_t* callback, void* closure)
{
    auto lock = unique_lock();
    return dht_search(id, port, af, callback, closure);
}

auto uninit()
{
    auto lock = unique_lock();
    return dht_uninit();
}

} // namespace locked_dht

enum class Status
{
    Stopped,
    Broken,
    Poor,
    Firewalled,
    Good
};

static constexpr std::string_view printableStatus(Status status)
{
    switch (status)
    {
    case Status::Stopped:
        return "stopped"sv;

    case Status::Broken:
        return "broken"sv;

    case Status::Poor:
        return "poor"sv;

    case Status::Firewalled:
        return "firewalled"sv;

    case Status::Good:
        return "good"sv;

    default:
        return "???"sv;
    }
}

bool tr_dhtEnabled()
{
    return impl.session != nullptr;
}

static constexpr auto getUdpSocket(int af)
{
    switch (af)
    {
    case AF_INET:
        return impl.udp4_socket;

    case AF_INET6:
        return impl.udp6_socket;

    default:
        return TR_BAD_SOCKET;
    }
}

static auto getStatus(int af, int* const setme_node_count = nullptr)
{
    if (getUdpSocket(af) == TR_BAD_SOCKET)
    {
        if (setme_node_count != nullptr)
        {
            *setme_node_count = 0;
        }

        return Status::Stopped;
    }

    int good = 0;
    int dubious = 0;
    int incoming = 0;
    locked_dht::nodes(af, &good, &dubious, nullptr, &incoming);

    if (setme_node_count != nullptr)
    {
        *setme_node_count = good + dubious;
    }

    if (good < 4 || good + dubious <= 8)
    {
        return Status::Broken;
    }

    if (good < 40)
    {
        return Status::Poor;
    }

    if (incoming < 8)
    {
        return Status::Firewalled;
    }

    return Status::Good;
}

static constexpr auto isReady(Status const status)
{
    return status >= Status::Firewalled;
}

static auto isReady(int af)
{
    return isReady(getStatus(af));
}

static bool isBootstrapDone(int af = 0)
{
    if (af == 0)
    {
        return isBootstrapDone(AF_INET) && isBootstrapDone(AF_INET6);
    }

    auto const status = getStatus(af, nullptr);
    return status == Status::Stopped || isReady(status);
}

static void nap(int roughly_sec)
{
    int const roughly_msec = roughly_sec * 1000;
    int const msec = roughly_msec / 2 + tr_rand_int_weak(roughly_msec);
    tr_wait_msec(msec);
}

static int getBootstrappedAF()
{
    if (isBootstrapDone(AF_INET6))
    {
        return AF_INET;
    }

    if (isBootstrapDone(AF_INET))
    {
        return AF_INET6;
    }

    return 0;
}

static void bootstrapFromName(char const* name, tr_port port, int af)
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
        locked_dht::ping_node(infop->ai_addr, infop->ai_addrlen);

        nap(15);

        if (isBootstrapDone(af))
        {
            break;
        }

        infop = infop->ai_next;
    }

    freeaddrinfo(info);
}

static void bootstrapFromFile(std::string_view config_dir)
{
    if (isBootstrapDone())
    {
        return;
    }

    // check for a manual bootstrap file.
    auto in = std::ifstream{ tr_pathbuf{ config_dir, "/dht.bootstrap"sv } };
    if (!in.is_open())
    {
        return;
    }

    // format is each line has address, a space char, and port number
    tr_logAddTrace("Attempting manual bootstrap");
    auto line = std::string{};
    while (!isBootstrapDone() && std::getline(in, line))
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
            bootstrapFromName(addrstr.c_str(), tr_port::fromHost(hport), getBootstrappedAF());
        }
    }
}

static void bootstrapStart(std::string_view config_dir, std::vector<std::byte> nodes4, std::vector<std::byte> nodes6)
{
    if (!tr_dhtEnabled())
    {
        return;
    }

    auto const num4 = std::size(nodes4) / 6;
    if (num4 > 0)
    {
        tr_logAddDebug(fmt::format("Bootstrapping from {} IPv4 nodes", num4));
    }

    auto const num6 = std::size(nodes6) / 18;
    if (num6 > 0)
    {
        tr_logAddDebug(fmt::format("Bootstrapping from {} IPv6 nodes", num6));
    }

    auto const* walk4 = std::data(nodes4);
    auto const* walk6 = std::data(nodes6);
    for (size_t i = 0; i < std::max(num4, num6); ++i)
    {
        if (i < num4 && !isBootstrapDone(AF_INET))
        {
            auto addr = tr_address{};
            auto port = tr_port{};
            std::tie(addr, walk4) = tr_address::fromCompact4(walk4);
            std::tie(port, walk4) = tr_port::fromCompact(walk4);
            tr_dhtAddNode(addr, port, true);
        }

        if (i < num6 && !isBootstrapDone(AF_INET6))
        {
            auto addr = tr_address{};
            auto port = tr_port{};
            std::tie(addr, walk6) = tr_address::fromCompact6(walk6);
            std::tie(port, walk6) = tr_port::fromCompact(walk6);
            tr_dhtAddNode(addr, port, true);
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

        if (isBootstrapDone())
        {
            break;
        }
    }

    if (!isBootstrapDone())
    {
        bootstrapFromFile(config_dir);
    }

    if (!isBootstrapDone())
    {
        for (int i = 0; i < 6; ++i)
        {
            /* We don't want to abuse our bootstrap nodes, so be very
               slow.  The initial wait is to give other nodes a chance
               to contact us before we attempt to contact a bootstrap
               node, for example because we've just been restarted. */
            nap(40);

            if (isBootstrapDone())
            {
                break;
            }

            if (i == 0)
            {
                tr_logAddDebug("Attempting bootstrap from dht.transmissionbt.com");
            }

            bootstrapFromName("dht.transmissionbt.com", tr_port::fromHost(6881), getBootstrappedAF());
        }
    }

    tr_logAddTrace("Finished bootstrapping");
}

int tr_dhtInit(tr_session* session, tr_socket_t udp4_socket, tr_socket_t udp6_socket)
{
    if (impl.session != nullptr) /* already initialized */
    {
        return -1;
    }

    tr_logAddInfo(_("Initializing DHT"));

    if (tr_env_key_exists("TR_DHT_VERBOSE"))
    {
        dht_debug = stderr;
    }

    auto benc = tr_variant{};
    auto const dat_file = tr_pathbuf{ session->configDir(), "/dht.dat"sv };
    auto const ok = tr_variantFromFile(&benc, TR_VARIANT_PARSE_BENC, dat_file.sv());

    bool have_id = false;
    auto nodes = std::vector<std::byte>{};
    auto nodes6 = std::vector<std::byte>{};

    if (ok)
    {
        auto sv = std::string_view{};
        have_id = tr_variantDictFindStrView(&benc, TR_KEY_id, &sv);
        if (have_id && std::size(sv) == std::size(impl.id))
        {
            std::copy(std::begin(sv), std::end(sv), std::data(impl.id));
        }

        size_t raw_len = 0U;
        std::byte const* raw = nullptr;

        if (tr_variantDictFindRaw(&benc, TR_KEY_nodes, &raw, &raw_len) && raw_len % 6 == 0)
        {
            nodes.assign(raw, raw + raw_len);
        }

        if (tr_variantDictFindRaw(&benc, TR_KEY_nodes6, &raw, &raw_len) && raw_len % 18 == 0)
        {
            nodes6.assign(raw, raw + raw_len);
        }

        tr_variantClear(&benc);
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
        tr_rand_buffer(std::data(impl.id), std::size(impl.id));
    }

    if (locked_dht::init(udp4_socket, udp6_socket, std::data(impl.id), nullptr) < 0)
    {
        auto const errcode = errno;
        tr_logAddDebug(fmt::format("DHT initialization failed: {} ({})", tr_strerror(errcode), errcode));
        impl = {};
        return -1;
    }

    impl.session = session;
    impl.udp4_socket = udp4_socket;
    impl.udp6_socket = udp4_socket;

    std::thread(bootstrapStart, std::string{ session->configDir() }, nodes, nodes6).detach();

    static auto constexpr MinInterval = 10ms;
    static auto constexpr MaxInterval = 1s;
    auto const random_percent = tr_rand_int_weak(1000) / 1000.0;
    auto interval = MinInterval + random_percent * (MaxInterval - MinInterval);
    impl.timer = session->timerMaker().create([]() { tr_dhtCallback(nullptr, 0, nullptr, 0); });
    impl.timer->startSingleShot(std::chrono::duration_cast<std::chrono::milliseconds>(interval));

    tr_logAddDebug("DHT initialized");

    return 1;
}

void tr_dhtUninit()
{
    TR_ASSERT(tr_dhtEnabled());

    tr_logAddTrace("Uninitializing DHT");

    impl.timer.reset();

    /* Since we only save known good nodes,
     * avoid erasing older data if we don't know enough nodes. */
    if (!isReady(AF_INET) && !isReady(AF_INET6))
    {
        tr_logAddTrace("Not saving nodes, DHT not ready");
    }
    else
    {
        auto constexpr MaxNodes = int{ 300 };
        auto constexpr PortLen = size_t{ 2 };
        auto constexpr CompactAddrLen = size_t{ 4 };
        auto constexpr CompactLen = size_t{ CompactAddrLen + PortLen };
        auto constexpr Compact6AddrLen = size_t{ 16 };
        auto constexpr Compact6Len = size_t{ Compact6AddrLen + PortLen };

        auto sins = std::array<struct sockaddr_in, MaxNodes>{};
        auto sins6 = std::array<struct sockaddr_in6, MaxNodes>{};
        int num = MaxNodes;
        int num6 = MaxNodes;
        int const n = locked_dht::getNodes(std::data(sins), &num, std::data(sins6), &num6);
        tr_logAddTrace(fmt::format("Saving {} ({} + {}) nodes", n, num, num6));

        tr_variant benc;
        tr_variantInitDict(&benc, 3);
        tr_variantDictAddRaw(&benc, TR_KEY_id, std::data(impl.id), std::size(impl.id));

        if (num > 0)
        {
            auto compact = std::array<char, MaxNodes * CompactLen>{};
            char* out = std::data(compact);
            for (auto const* in = std::data(sins), *end = in + num; in != end; ++in)
            {
                memcpy(out, &in->sin_addr, CompactAddrLen);
                out += CompactAddrLen;
                memcpy(out, &in->sin_port, PortLen);
                out += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes, std::data(compact), out - std::data(compact));
        }

        if (num6 > 0)
        {
            auto compact6 = std::array<char, MaxNodes * Compact6Len>{};
            char* out6 = std::data(compact6);
            for (auto const* in = std::data(sins6), *end = in + num6; in != end; ++in)
            {
                memcpy(out6, &in->sin6_addr, Compact6AddrLen);
                out6 += Compact6AddrLen;
                memcpy(out6, &in->sin6_port, PortLen);
                out6 += PortLen;
            }

            tr_variantDictAddRaw(&benc, TR_KEY_nodes6, std::data(compact6), out6 - std::data(compact6));
        }

        tr_variantToFile(&benc, TR_VARIANT_FMT_BENC, tr_pathbuf{ impl.session->configDir(), "/dht.dat"sv });
        tr_variantClear(&benc);
    }

    locked_dht::uninit();

    tr_logAddTrace("Done uninitializing DHT");

    impl = {};
}

bool tr_dhtAddNode(tr_address addr, tr_port port, bool bootstrap)
{
    if (!tr_dhtEnabled())
    {
        return false;
    }

    /* Since we don't want to abuse our bootstrap nodes,
     * we don't ping them if the DHT is in a good state. */

    if (bootstrap && isReady(addr.isIPv4() ? AF_INET : AF_INET6))
    {
        return false;
    }

    if (addr.isIPv4())
    {
        auto sin = sockaddr_in{};
        sin.sin_family = AF_INET;
        sin.sin_addr = addr.addr.addr4;
        sin.sin_port = port.network();
        locked_dht::ping_node((struct sockaddr*)&sin, sizeof(sin));
        return true;
    }

    if (addr.isIPv6())
    {
        auto sin6 = sockaddr_in6{};
        sin6.sin6_family = AF_INET6;
        sin6.sin6_addr = addr.addr.addr6;
        sin6.sin6_port = port.network();
        locked_dht::ping_node((struct sockaddr*)&sin6, sizeof(sin6));
        return true;
    }

    return false;
}

static void callback(void* vsession, int event, unsigned char const* info_hash, void const* data, size_t data_len)
{
    auto* const session = static_cast<tr_session*>(vsession);
    auto hash = tr_sha1_digest_t{};
    std::copy_n(reinterpret_cast<std::byte const*>(info_hash), std::size(hash), std::data(hash));
    auto const lock = session->unique_lock();
    auto* const tor = session->torrents().get(hash);

    if (event == DHT_EVENT_VALUES || event == DHT_EVENT_VALUES6)
    {
        if (tor != nullptr && tor->allowsDht())
        {
            auto const pex = event == DHT_EVENT_VALUES ? tr_pex::fromCompact4(data, data_len, nullptr, 0) :
                                                         tr_pex::fromCompact6(data, data_len, nullptr, 0);
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

static bool announceTorrent(tr_torrent const* const tor, int af, bool announce, tr_port incoming_peer_port)
{
    TR_ASSERT(tor->allowsDht());

    int numnodes = 0;
    auto const status = getStatus(af, &numnodes);
    if (status == Status::Stopped)
    {
        // let the caller believe everything is all right.
        return true;
    }

    if (status < Status::Poor)
    {
        tr_logAddTraceTor(
            tor,
            fmt::format(
                "{} DHT not ready ({}, {} nodes)",
                af == AF_INET6 ? "IPv6" : "IPv4",
                printableStatus(status),
                numnodes));
        return false;
    }

    auto const* dht_hash = reinterpret_cast<unsigned char const*>(std::data(tor->infoHash()));
    auto const hport = announce ? incoming_peer_port.host() : 0;
    int const rc = locked_dht::search(dht_hash, hport, af, callback, impl.session);
    if (rc < 0)
    {
        auto const error_code = errno;
        tr_logAddWarnTor(
            tor,
            fmt::format(
                _("Unable to announce torrent in DHT with {type}: {error} ({error_code}); state is {state}"),
                fmt::arg("type", af == AF_INET6 ? "IPv6" : "IPv4"),
                fmt::arg("state", printableStatus(status)),
                fmt::arg("error_code", error_code),
                fmt::arg("error", tr_strerror(error_code))));
        return false;
    }

    tr_logAddTraceTor(
        tor,
        fmt::format(
            "Starting {} DHT announce ({}, {} nodes)",
            af == AF_INET6 ? "IPv6" : "IPv4",
            printableStatus(status),
            numnodes));

    return true;
}

void tr_dhtUpkeep()
{
    TR_ASSERT(impl.session != nullptr);

    auto lock = impl.session->unique_lock();
    auto const now = tr_time();
    auto const incoming_peer_port = impl.session->advertisedPeerPort();

    for (auto* const tor : impl.session->torrents())
    {
        if (!tor->isRunning || !tor->allowsDht())
        {
            continue;
        }

        if (tor->dhtAnnounceAt <= now)
        {
            auto const ok = announceTorrent(tor, AF_INET, true, incoming_peer_port);
            auto const interval = ok ? 25 * 60 + tr_rand_int_weak(3 * 60) : 5 + tr_rand_int_weak(5);
            tor->dhtAnnounceAt = now + interval;
        }

        if (tor->dhtAnnounce6At <= now)
        {
            auto const ok = announceTorrent(tor, AF_INET6, true, incoming_peer_port);
            auto const interval = ok ? 25 * 60 + tr_rand_int_weak(3 * 60) : 5 + tr_rand_int_weak(5);
            tor->dhtAnnounce6At = now + interval;
        }
    }
}

void tr_dhtCallback(unsigned char* buf, int buflen, struct sockaddr* from, socklen_t fromlen)
{
    if (!tr_dhtEnabled())
    {
        return;
    }

    time_t tosleep = 0;
    int const rc = locked_dht::periodic(buf, buflen, from, fromlen, &tosleep, callback, impl.session);

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

    // Being slightly late is fine,
    // and has the added benefit of adding some jitter.
    auto const random_percent = tr_rand_int_weak(1000) / 1000.0;
    auto const min_interval = std::chrono::seconds{ tosleep };
    auto const max_interval = std::chrono::seconds{ tosleep + 1 };
    auto const interval = min_interval + random_percent * (max_interval - min_interval);
    impl.timer->startSingleShot(std::chrono::duration_cast<std::chrono::milliseconds>(interval));
}

extern "C"
{
    // This function should return true when a node is blacklisted.
    // We don't support using a blacklist with the DHT in Transmission,
    // since massive (ab)use of this feature could harm the DHT. However,
    // feel free to add support to your private copy as long as you don't
    // redistribute it.
    int dht_blacklisted(sockaddr const* /*sa*/, int /*salen*/)
    {
        return 0;
    }

    void dht_hash(
        void* hash_return,
        int hash_size,
        void const* v1,
        int len1,
        void const* v2,
        int len2,
        void const* v3,
        int len3)
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
        if (!tr_rand_buffer(buf, size))
        {
            return -1;
        }
        return size;
    }

    int dht_sendto(int sockfd, void const* buf, int len, int flags, struct sockaddr const* to, int tolen)
    {
        return sendto(sockfd, static_cast<char const*>(buf), len, flags, to, tolen);
    }

#if defined(_WIN32) && !defined(__MINGW32__)
    int dht_gettimeofday(struct timeval* tv, [[maybe_unused]] struct timezone* tz)
    {
        TR_ASSERT(tz == nullptr);

        auto const d = std::chrono::system_clock::now().time_since_epoch();
        auto const s = std::chrono::duration_cast<std::chrono::seconds>(d);
        tv->tv_sec = s.count();
        tv->tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(d - s).count();

        return 0;
    }
#endif

} // extern "C"
