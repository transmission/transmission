// This file Copyright © 2010-2023 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator> // std::back_inserter
#include <string_view>
#include <utility> // std::pair

#include <sys/types.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h> /* TCP_CONGESTION */
#endif

#include <event2/util.h>

#include <fmt/core.h>

#include <libutp/utp.h>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "peer-socket.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "tr-utp.h"
#include "utils.h"
#include "variant.h"

using namespace std::literals;

#ifndef IN_MULTICAST
#define IN_MULTICAST(a) (((a)&0xf0000000) == 0xe0000000)
#endif

std::string tr_net_strerror(int err)
{
#ifdef _WIN32

    auto buf = std::array<char, 512>{};
    (void)FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, std::data(buf), std::size(buf), nullptr);
    return std::string{ tr_strvStrip(std::data(buf)) };

#else

    return std::string{ tr_strerror(err) };

#endif
}

// - TCP Sockets

[[nodiscard]] std::optional<tr_tos_t> tr_tos_t::from_string(std::string_view name)
{
    auto const needle = tr_strlower(tr_strvStrip(name));

    for (auto const& [value, key] : Names)
    {
        if (needle == key)
        {
            return tr_tos_t(value);
        }
    }

    if (auto value = tr_parseNum<int>(needle); value)
    {
        return tr_tos_t(*value);
    }

    return {};
}

std::string tr_tos_t::toString() const
{
    for (auto const& [value, key] : Names)
    {
        if (value_ == value)
        {
            return std::string{ key };
        }
    }

    return std::to_string(value_);
}

void tr_netSetTOS([[maybe_unused]] tr_socket_t s, [[maybe_unused]] int tos, tr_address_type type)
{
    if (s == TR_BAD_SOCKET)
    {
        return;
    }

    if (type == TR_AF_INET)
    {
#if defined(IP_TOS) && !defined(_WIN32)

        if (setsockopt(s, IPPROTO_IP, IP_TOS, (void const*)&tos, sizeof(tos)) == -1)
        {
            tr_logAddDebug(fmt::format("Can't set TOS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    }
    else if (type == TR_AF_INET6)
    {
#if defined(IPV6_TCLASS) && !defined(_WIN32)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, (void const*)&tos, sizeof(tos)) == -1)
        {
            tr_logAddDebug(fmt::format("Can't set IPv6 QoS '{}': {}", tos, tr_net_strerror(sockerrno)));
        }
#endif
    }
    else
    {
        /* program should never reach here! */
        tr_logAddDebug("Something goes wrong while setting TOS/Traffic-Class");
    }
}

void tr_netSetCongestionControl([[maybe_unused]] tr_socket_t s, [[maybe_unused]] char const* algorithm)
{
#ifdef TCP_CONGESTION

    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, (void const*)algorithm, strlen(algorithm) + 1) == -1)
    {
        tr_logAddDebug(fmt::format("Can't set congestion control algorithm '{}': {}", algorithm, tr_net_strerror(sockerrno)));
    }

#endif
}

static tr_socket_t createSocket(int domain, int type)
{
    auto const sockfd = socket(domain, type, 0);
    if (sockfd == TR_BAD_SOCKET)
    {
        if (sockerrno != EAFNOSUPPORT)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't create socket: {error} ({error_code})"),
                fmt::arg("error", tr_net_strerror(sockerrno)),
                fmt::arg("error_code", sockerrno)));
        }

        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(sockfd) == -1)
    {
        tr_net_close_socket(sockfd);
        return TR_BAD_SOCKET;
    }

    if (static bool buf_logged = false; !buf_logged)
    {
        int i = 0;
        socklen_t size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_SNDBUF size is {}", i));
        }

        i = 0;
        size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_RCVBUF size is {}", i));
        }

        buf_logged = true;
    }

    return sockfd;
}

tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const& addr, tr_port port, bool client_is_seed)
{
    TR_ASSERT(addr.is_valid());
    TR_ASSERT(!tr_peer_socket::limit_reached(session));

    if (tr_peer_socket::limit_reached(session) || !session->allowsTCP() || !addr.is_valid_for_peers(port))
    {
        return {};
    }

    static auto constexpr Domains = std::array<int, NUM_TR_AF_INET_TYPES>{ AF_INET, AF_INET6 };
    auto const s = createSocket(Domains[addr.type], SOCK_STREAM);
    if (s == TR_BAD_SOCKET)
    {
        return {};
    }

    // seeds don't need a big read buffer, so make it smaller
    if (client_is_seed)
    {
        int n = 8192;

        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&n), sizeof(n)) == -1)
        {
            tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF on socket {}: {}", s, tr_net_strerror(sockerrno)));
        }
    }

    auto const [sock, addrlen] = addr.to_sockaddr(port);

    // set source address
    auto const [source_addr, is_any] = session->publicAddress(addr.type);
    auto const [source_sock, sourcelen] = source_addr.to_sockaddr({});

    if (bind(s, reinterpret_cast<sockaddr const*>(&source_sock), sourcelen) == -1)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't set source address {address} on {socket}: {error} ({error_code})"),
            fmt::arg("address", source_addr.display_name()),
            fmt::arg("socket", s),
            fmt::arg("error", tr_net_strerror(sockerrno)),
            fmt::arg("error_code", sockerrno)));
        tr_net_close_socket(s);
        return {};
    }

    auto ret = tr_peer_socket{};
    if (connect(s, reinterpret_cast<sockaddr const*>(&sock), addrlen) == -1 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        if (auto const tmperrno = sockerrno;
            (tmperrno != ECONNREFUSED && tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr.is_ipv4())
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't connect socket {socket} to {address}:{port}: {error} ({error_code})"),
                fmt::arg("socket", s),
                fmt::arg("address", addr.display_name()),
                fmt::arg("port", port.host()),
                fmt::arg("error", tr_net_strerror(tmperrno)),
                fmt::arg("error_code", tmperrno)));
        }

        tr_net_close_socket(s);
    }
    else
    {
        ret = tr_peer_socket{ session, addr, port, s };
    }

    tr_logAddTrace(fmt::format("New OUTGOING connection {} ({})", s, addr.display_name(port)));

    return ret;
}

static tr_socket_t tr_netBindTCPImpl(tr_address const& addr, tr_port port, bool suppress_msgs, int* err_out)
{
    TR_ASSERT(addr.is_valid());

    static auto constexpr Domains = std::array<int, NUM_TR_AF_INET_TYPES>{ AF_INET, AF_INET6 };

    auto const fd = socket(Domains[addr.type], SOCK_STREAM, 0);
    if (fd == TR_BAD_SOCKET)
    {
        *err_out = sockerrno;
        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(fd) == -1)
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    int optval = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char const*>(&optval), sizeof(optval));
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));

#ifdef IPV6_V6ONLY

    if (addr.is_ipv6() &&
        (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&optval), sizeof(optval)) == -1) &&
        (sockerrno != ENOPROTOOPT)) // if the kernel doesn't support it, ignore it
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

#endif

    auto const [sock, addrlen] = addr.to_sockaddr(port);

    if (bind(fd, reinterpret_cast<sockaddr const*>(&sock), addrlen) == -1)
    {
        int const err = sockerrno;

        if (!suppress_msgs)
        {
            tr_logAddError(fmt::format(
                err == EADDRINUSE ?
                    _("Couldn't bind port {port} on {address}: {error} ({error_code}) -- Is another copy of Transmission already running?") :
                    _("Couldn't bind port {port} on {address}: {error} ({error_code})"),
                fmt::arg("address", addr.display_name()),
                fmt::arg("port", port.host()),
                fmt::arg("error", tr_net_strerror(err)),
                fmt::arg("error_code", err)));
        }

        tr_net_close_socket(fd);
        *err_out = err;
        return TR_BAD_SOCKET;
    }

    if (!suppress_msgs)
    {
        tr_logAddDebug(fmt::format(FMT_STRING("Bound socket {:d} to port {:d} on {:s}"), fd, port.host(), addr.display_name()));
    }

#ifdef TCP_FASTOPEN

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

    optval = 5;
    (void)setsockopt(fd, SOL_TCP, TCP_FASTOPEN, reinterpret_cast<char const*>(&optval), sizeof(optval));

#endif

#ifdef _WIN32
    if (listen(fd, SOMAXCONN) == -1)
#else /* _WIN32 */
    /* Listen queue backlog will be capped to the operating system's limit. */
    if (listen(fd, INT_MAX) == -1)
#endif /* _WIN32 */
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    return fd;
}

tr_socket_t tr_netBindTCP(tr_address const& addr, tr_port port, bool suppress_msgs)
{
    int unused = 0;
    return tr_netBindTCPImpl(addr, port, suppress_msgs, &unused);
}

bool tr_net_hasIPv6(tr_port port)
{
    static bool result = false;
    static bool already_done = false;

    if (!already_done)
    {
        int err = 0;
        auto const fd = tr_netBindTCPImpl(tr_address::any_ipv6(), port, true, &err);

        if (fd != TR_BAD_SOCKET || err != EAFNOSUPPORT) /* we support ipv6 */
        {
            result = true;
        }

        if (fd != TR_BAD_SOCKET)
        {
            tr_net_close_socket(fd);
        }

        already_done = true;
    }

    return result;
}

std::optional<std::tuple<tr_address, tr_port, tr_socket_t>> tr_netAccept(tr_session* session, tr_socket_t listening_sockfd)
{
    TR_ASSERT(session != nullptr);

    // accept the incoming connection
    auto sock = sockaddr_storage{};
    socklen_t len = sizeof(struct sockaddr_storage);
    auto const sockfd = accept(listening_sockfd, reinterpret_cast<sockaddr*>(&sock), &len);
    if (sockfd == TR_BAD_SOCKET)
    {
        return {};
    }

    // get the address and port,
    // make the socket unblocking,
    // and confirm we don't have too many peers
    auto const addrport = tr_address::from_sockaddr(reinterpret_cast<struct sockaddr*>(&sock));
    if (!addrport || evutil_make_socket_nonblocking(sockfd) == -1 || tr_peer_socket::limit_reached(session))
    {
        tr_net_close_socket(sockfd);
        return {};
    }

    return std::make_tuple(addrport->first, addrport->second, sockfd);
}

void tr_net_close_socket(tr_socket_t sockfd)
{
    evutil_closesocket(sockfd);
}

// ---

namespace
{
namespace is_valid_for_peers_helpers
{

[[nodiscard]] constexpr auto is_ipv4_mapped_address(tr_address const* addr)
{
    return addr->is_ipv6() && IN6_IS_ADDR_V4MAPPED(&addr->addr.addr6);
}

[[nodiscard]] constexpr auto is_ipv6_link_local_address(tr_address const* addr)
{
    return addr->is_ipv6() && IN6_IS_ADDR_LINKLOCAL(&addr->addr.addr6);
}

/* isMartianAddr was written by Juliusz Chroboczek,
   and is covered under the same license as third-party/dht/dht.c. */
[[nodiscard]] auto is_martian_addr(tr_address const& addr)
{
    static auto constexpr Zeroes = std::array<unsigned char, 16>{};

    switch (addr.type)
    {
    case TR_AF_INET:
        {
            auto const* const address = reinterpret_cast<unsigned char const*>(&addr.addr.addr4);
            return address[0] == 0 || address[0] == 127 || (address[0] & 0xE0) == 0xE0;
        }

    case TR_AF_INET6:
        {
            auto const* const address = reinterpret_cast<unsigned char const*>(&addr.addr.addr6);
            return address[0] == 0xFF ||
                (memcmp(address, std::data(Zeroes), 15) == 0 && (address[15] == 0 || address[15] == 1));
        }

    default:
        return true;
    }
}

} // namespace is_valid_for_peers_helpers
} // namespace

bool tr_address::is_valid_for_peers(tr_port port) const noexcept
{
    using namespace is_valid_for_peers_helpers;

    return is_valid() && !std::empty(port) && !is_ipv6_link_local_address(this) && !is_ipv4_mapped_address(this) &&
        !is_martian_addr(*this);
}

// --- tr_port

std::pair<tr_port, std::byte const*> tr_port::fromCompact(std::byte const* compact) noexcept
{
    static auto constexpr PortLen = size_t{ 2 };

    static_assert(PortLen == sizeof(uint16_t));
    auto nport = uint16_t{};
    std::copy_n(compact, PortLen, reinterpret_cast<std::byte*>(&nport));
    compact += PortLen;

    return std::make_pair(tr_port::fromNetwork(nport), compact);
}

// --- tr_address

std::optional<tr_address> tr_address::from_string(std::string_view address_sv)
{
    auto const address_sz = tr_strbuf<char, TR_ADDRSTRLEN>{ address_sv };

    auto addr = tr_address{};

    addr.addr.addr4 = {};
    if (evutil_inet_pton(AF_INET, address_sz, &addr.addr.addr4) == 1)
    {
        addr.type = TR_AF_INET;
        return addr;
    }

    addr.addr.addr6 = {};
    if (evutil_inet_pton(AF_INET6, address_sz, &addr.addr.addr6) == 1)
    {
        addr.type = TR_AF_INET6;
        return addr;
    }

    return {};
}

std::string_view tr_address::display_name(char* out, size_t outlen, tr_port port) const
{
    if (std::empty(port))
    {
        return is_ipv4() ? evutil_inet_ntop(AF_INET, &addr, out, outlen) : evutil_inet_ntop(AF_INET6, &addr, out, outlen);
    }

    auto buf = std::array<char, INET6_ADDRSTRLEN>{};
    auto const addr_sv = display_name(std::data(buf), std::size(buf));
    auto const [end, size] = fmt::format_to_n(out, outlen - 1, FMT_STRING("[{:s}]:{:d}"), addr_sv, port.host());
    return { out, size };
}

template<typename OutputIt>
OutputIt tr_address::display_name(OutputIt out, tr_port port) const
{
    auto addrbuf = std::array<char, TR_ADDRSTRLEN + 16>{};
    auto const addr_sv = display_name(std::data(addrbuf), std::size(addrbuf), port);
    return std::copy(std::begin(addr_sv), std::end(addr_sv), out);
}

template char* tr_address::display_name<char*>(char*, tr_port) const;

[[nodiscard]] std::string tr_address::display_name(tr_port port) const
{
    auto buf = std::string{};
    buf.reserve(INET6_ADDRSTRLEN + 16);
    this->display_name(std::back_inserter(buf), port);
    return buf;
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv4(std::byte const* compact) noexcept
{
    static auto constexpr Addr4Len = size_t{ 4 };

    auto address = tr_address{};
    static_assert(sizeof(address.addr.addr4) == Addr4Len);
    address.type = TR_AF_INET;
    std::copy_n(compact, Addr4Len, reinterpret_cast<std::byte*>(&address.addr));
    compact += Addr4Len;

    return std::make_pair(address, compact);
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv6(std::byte const* compact) noexcept
{
    static auto constexpr Addr6Len = size_t{ 16 };

    auto address = tr_address{};
    address.type = TR_AF_INET6;
    std::copy_n(compact, Addr6Len, reinterpret_cast<std::byte*>(&address.addr.addr6.s6_addr));
    compact += Addr6Len;

    return std::make_pair(address, compact);
}

std::optional<std::pair<tr_address, tr_port>> tr_address::from_sockaddr(struct sockaddr const* from)
{
    if (from == nullptr)
    {
        return {};
    }

    if (from->sa_family == AF_INET)
    {
        auto const* const sin = reinterpret_cast<struct sockaddr_in const*>(from);
        auto addr = tr_address{};
        addr.type = TR_AF_INET;
        addr.addr.addr4 = sin->sin_addr;
        return std::make_pair(addr, tr_port::fromNetwork(sin->sin_port));
    }

    if (from->sa_family == AF_INET6)
    {
        auto const* const sin6 = reinterpret_cast<struct sockaddr_in6 const*>(from);
        auto addr = tr_address{};
        addr.type = TR_AF_INET6;
        addr.addr.addr6 = sin6->sin6_addr;
        return std::make_pair(addr, tr_port::fromNetwork(sin6->sin6_port));
    }

    return {};
}

std::pair<sockaddr_storage, socklen_t> tr_address::to_sockaddr(tr_port port) const noexcept
{
    auto ss = sockaddr_storage{};

    if (is_ipv4())
    {
        auto* const ss4 = reinterpret_cast<sockaddr_in*>(&ss);
        ss4->sin_addr = addr.addr4;
        ss4->sin_family = AF_INET;
        ss4->sin_port = port.network();
        return { ss, sizeof(sockaddr_in) };
    }

    auto* const ss6 = reinterpret_cast<sockaddr_in6*>(&ss);
    ss6->sin6_addr = addr.addr6;
    ss6->sin6_family = AF_INET6;
    ss6->sin6_flowinfo = 0;
    ss6->sin6_port = port.network();
    return { ss, sizeof(sockaddr_in6) };
}

int tr_address::compare(tr_address const& that) const noexcept // <=>
{
    // IPv6 addresses are always "greater than" IPv4
    if (this->type != that.type)
    {
        return this->is_ipv4() ? 1 : -1;
    }

    return this->is_ipv4() ? memcmp(&this->addr.addr4, &that.addr.addr4, sizeof(this->addr.addr4)) :
                             memcmp(&this->addr.addr6.s6_addr, &that.addr.addr6.s6_addr, sizeof(this->addr.addr6.s6_addr));
}

// https://en.wikipedia.org/wiki/Reserved_IP_addresses
[[nodiscard]] bool tr_address::is_global_unicast_address() const noexcept
{
    if (is_ipv4())
    {
        auto const* const a = reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr);

        // [0.0.0.0–0.255.255.255]
        // Current network.
        if (a[0] == 0)
        {
            return false;
        }

        // [10.0.0.0 – 10.255.255.255]
        // Used for local communications within a private network.
        if (a[0] == 10)
        {
            return false;
        }

        // [100.64.0.0–100.127.255.255]
        // Shared address space for communications between a service provider
        // and its subscribers when using a carrier-grade NAT.
        if ((a[0] == 100) && (64 <= a[1] && a[1] <= 127))
        {
            return false;
        }

        // [169.254.0.0–169.254.255.255]
        // Used for link-local addresses[5] between two hosts on a single link
        // when no IP address is otherwise specified, such as would have
        // normally been retrieved from a DHCP server.
        if (a[0] == 169 && a[1] == 254)
        {
            return false;
        }

        // [172.16.0.0–172.31.255.255]
        // Used for local communications within a private network.
        if ((a[0] == 172) && (16 <= a[1] && a[1] <= 31))
        {
            return false;
        }

        // [192.0.0.0–192.0.0.255]
        // IETF Protocol Assignments.
        if (a[0] == 192 && a[1] == 0 && a[2] == 0)
        {
            return false;
        }

        // [192.0.2.0–192.0.2.255]
        // Assigned as TEST-NET-1, documentation and examples.
        if (a[0] == 192 && a[1] == 0 && a[2] == 2)
        {
            return false;
        }

        // [192.88.99.0–192.88.99.255]
        // Reserved. Formerly used for IPv6 to IPv4 relay.
        if (a[0] == 192 && a[1] == 88 && a[2] == 99)
        {
            return false;
        }

        // [192.168.0.0–192.168.255.255]
        // Used for local communications within a private network.
        if (a[0] == 192 && a[1] == 168)
        {
            return false;
        }

        // [198.18.0.0–198.19.255.255]
        // Used for benchmark testing of inter-network communications
        // between two separate subnets.
        if (a[0] == 198 && (18 <= a[1] && a[1] <= 19))
        {
            return false;
        }

        // [198.51.100.0–198.51.100.255]
        // Assigned as TEST-NET-2, documentation and examples.
        if (a[0] == 198 && a[1] == 51 && a[2] == 100)
        {
            return false;
        }

        // [203.0.113.0–203.0.113.255]
        // Assigned as TEST-NET-3, documentation and examples.
        if (a[0] == 203 && a[1] == 0 && a[2] == 113)
        {
            return false;
        }

        // [224.0.0.0–239.255.255.255]
        // In use for IP multicast. (Former Class D network.)
        if (224 <= a[0] && a[0] <= 230)
        {
            return false;
        }

        // [233.252.0.0-233.252.0.255]
        // Assigned as MCAST-TEST-NET, documentation and examples.
        if (a[0] == 233 && a[1] == 252 && a[2] == 0)
        {
            return false;
        }

        // [240.0.0.0–255.255.255.254]
        // Reserved for future use. (Former Class E network.)
        // [255.255.255.255]
        // Reserved for the "limited broadcast" destination address.
        if (240 <= a[0])
        {
            return false;
        }

        return true;
    }

    if (is_ipv6())
    {
        auto const* const a = addr.addr6.s6_addr;

        // TODO: 2000::/3 is commonly used for global unicast but technically
        // other spaces would be allowable too, so we should test those here.
        // See RFC 4291 in the Section 2.4 lising global unicast as everything
        // that's not link-local, multicast, loopback, or unspecified.
        return (a[0] & 0xE0) == 0x20;
    }

    return false;
}

tr_global_ip_cache::tr_global_ip_cache(tr_session* const session_in, tr_address_type type_in)
    : session_{ session_in }
    , is_updating_{ false }
    , type{ type_in }
    , upkeep_timer_{ session_in->timerMaker().create() }
{
}

tr_global_ip_cache::~tr_global_ip_cache()
{
    // Wait until all updates are done
    std::unique_lock lock{ is_updating_mutex_ };
    is_updating_cv_.wait_for(lock, 5s, [this]() { return !is_updating_; });
    TR_ASSERT(!is_updating_);

    // Destroying std::shared_mutex while someone owns it is undefined behaviour
    std::lock(global_addr_mutex_, source_addr_mutex_);
    std::unique_lock const ip_lock1{ global_addr_mutex_, std::adopt_lock };
    std::unique_lock const ip_lock2{ source_addr_mutex_, std::adopt_lock };
}

std::optional<tr_address> const& tr_global_ip_cache::global_addr() noexcept
{
    std::shared_lock const lock{ global_addr_mutex_ };
    return global_addr_;
}

std::optional<tr_address> const& tr_global_ip_cache::global_source_addr() noexcept
{
    std::shared_lock const lock{ source_addr_mutex_ };
    return source_addr_;
}

void tr_global_ip_cache::start_timer(std::chrono::milliseconds msec) noexcept
{
    upkeep_timer_->startRepeating(msec);
}

void tr_global_ip_cache::stop_timer() noexcept
{
    upkeep_timer_->stop();
}

void tr_global_ip_cache::set_is_updating() noexcept
{
    std::unique_lock lock{ is_updating_mutex_ };
    is_updating_ = true;
    lock.unlock();
    is_updating_cv_.notify_one();
}

void tr_global_ip_cache::unset_is_updating() noexcept
{
    std::unique_lock lock{ is_updating_mutex_ };
    is_updating_ = false;
    lock.unlock();
    is_updating_cv_.notify_one();
}

void tr_global_ip_cache::set_global_addr(std::optional<tr_address> const& addr) noexcept
{
    TR_ASSERT(!addr || addr->type == type);
    std::lock_guard const lock{ global_addr_mutex_ };
    if (addr)
    {
        tr_logAddTrace(fmt::format("Cached global address {}", addr->display_name()));
    }
    else
    {
        tr_logAddTrace(fmt::format("Unset {} global address cache", type == TR_AF_INET ? "IPv4" : "IPv6"));
    }
}

void tr_global_ip_cache::unset_global_addr() noexcept
{
    std::lock_guard const lock{ global_addr_mutex_ };
    global_addr_.reset();
    tr_logAddTrace(fmt::format("Unset {} global address cache", type == TR_AF_INET ? "IPv4" : "IPv6"));
}

void tr_global_ip_cache::set_source_addr(std::optional<tr_address> const& addr) noexcept
{
    TR_ASSERT(!addr || addr->type == type);
    std::lock_guard const lock{ source_addr_mutex_ };
    source_addr_ = addr;
    if (addr)
    {
        tr_logAddTrace(fmt::format("Cached source address {}", addr->display_name()));
    }
    else
    {
        tr_logAddTrace(fmt::format("Unset {} source address cache", type == TR_AF_INET ? "IPv4" : "IPv6"));
    }
}

void tr_global_ip_cache::unset_source_addr() noexcept
{
    std::lock_guard const lock{ source_addr_mutex_ };
    source_addr_.reset();
    tr_logAddTrace(fmt::format("Unset {} source address cache", type == TR_AF_INET ? "IPv4" : "IPv6"));
}

// tr_global_ip_cache::get_global_source_address() and tr_global_ipv6_cache::get_source_address
// are modified from code by Juliusz Chroboczek and is covered under the same license as dht.cc.
// Please feel free to copy them into your software if it can help
// unbreaking the double-stack Internet.
std::optional<tr_address> tr_global_ip_cache::get_global_source_address(int af)
{
    TR_ASSERT(af == AF_INET || af == AF_INET6);

    // Pick some destination address to pretend to send a packet to
    static auto constexpr DstIPv4 = "91.121.74.28"sv;
    static auto constexpr DstIPv6 = "2001:1890:1112:1::20"sv;
    auto const dst_addr = tr_address::from_string(af == AF_INET ? DstIPv4 : DstIPv6);
    auto const dst_port = tr_port::fromHost(6969);

    // In order for address selection to work right,
    // this should be a global unicast address, not Teredo or 6to4
    TR_ASSERT(dst_addr.has_value() && dst_addr->is_global_unicast_address());

    if (dst_addr)
    {
        return get_source_address(*dst_addr, dst_port);
    }

    return {};
}

// Get the source address used for a given destination address.
// Since there is no official interface to get this information,
// we create a connected UDP socket (connected UDP... hmm...)
// and check its source address.
//
// Since it's a UDP socket, this doesn't actually send any packets
std::optional<tr_address> tr_global_ip_cache::get_source_address(tr_address const& dst_addr, tr_port dst_port)
{
    auto const save = errno;

    auto const [dst_ss, dst_sslen] = dst_addr.to_sockaddr(dst_port);
    if (auto const sock = socket(dst_ss.ss_family, SOCK_DGRAM, 0); sock != TR_BAD_SOCKET)
    {
        if (connect(sock, reinterpret_cast<sockaddr const*>(&dst_ss), dst_sslen) == 0)
        {
            auto src_ss = sockaddr_storage{};
            auto src_sslen = socklen_t{ sizeof(src_ss) };
            if (getsockname(sock, reinterpret_cast<sockaddr*>(&src_ss), &src_sslen) == 0)
            {
                if (auto const addrport = tr_address::from_sockaddr(reinterpret_cast<sockaddr*>(&src_ss)); addrport)
                {
                    evutil_closesocket(sock);
                    errno = save;
                    return addrport->first;
                }
            }
        }

        tr_net_close_socket(sock);
    }

    errno = save;
    return {};
}

tr_global_ipv4_cache::tr_global_ipv4_cache(tr_session* const session_in)
    : tr_global_ip_cache{ session_in, TR_AF_INET }
    , ix_service_{ 0 }
{
    auto const cb = [this]()
    {
        this->update_ipv4_addr();
    };
    upkeep_timer_->setCallback(cb);
    start_timer(UpkeepInterval);
    session_->runInSessionThread(cb);
}

void tr_global_ipv4_cache::update_ipv4_addr() noexcept
{
    TR_ASSERT(session_->amInSessionThread());
    TR_ASSERT(ix_service_ < std::size(IPv4QueryServices));

    if (ix_service_ == 0U)
    {
        TR_ASSERT(!is_updating_);
        set_is_updating();

        // Update the source address for global connections
        set_source_addr(get_global_source_address(AF_INET));
    }

    // Update global address
    auto options = tr_web::FetchOptions{ IPv4QueryServices[ix_service_],
                                         [this](tr_web::FetchResponse const& response) { this->onIPv4Response(response); },
                                         nullptr };
    options.ip_proto = tr_web::FetchOptions::IPProtocol::V4;
    options.sndbuf = 4096;
    options.rcvbuf = 4096;
    session_->fetch(std::move(options));
}

void tr_global_ipv4_cache::onIPv4Response(tr_web::FetchResponse const& response)
{
    TR_ASSERT(session_->amInSessionThread());
    TR_ASSERT(is_updating_);
    TR_ASSERT(ix_service_ < std::size(IPv4QueryServices));

    auto success = bool{ false };

    if (response.status == 200 /* HTTP_OK */)
    {
        std::string ip{ response.body };

        // Trim IP string
        auto constexpr Ws = [](unsigned char c)
        {
            return std::isspace(c);
        };
        ip.erase(ip.begin(), std::find_if_not(ip.begin(), ip.end(), Ws));
        ip.erase(std::find_if_not(ip.rbegin(), ip.rend(), Ws).base(), ip.end());

        // Update member
        if (auto addr = tr_address::from_string(ip); addr && addr->is_global_unicast_address() && addr->is_ipv4())
        {
            set_global_addr(addr);

            success = true;
            upkeep_timer_->setInterval(UpkeepInterval);

            tr_logAddInfo(fmt::format(
                _("Successfully updated global IPv4 address from {url} (HTTP {status})"),
                fmt::arg("url", IPv4QueryServices[ix_service_]),
                fmt::arg("status", response.status)));
        }
    }

    // Try next IP query URL
    if (!success && ++ix_service_ < std::size(IPv4QueryServices))
    {
        update_ipv4_addr();
        return;
    }

    if (!success)
    {
        tr_logAddDebug("Couldn't obtain global IPv4 address");
        unset_global_addr();
        upkeep_timer_->setInterval(RetryUpkeepInterval);
    }

    ix_service_ = 0;
    unset_is_updating();
}

tr_global_ipv6_cache::tr_global_ipv6_cache(tr_session* const session_in)
    : tr_global_ip_cache{ session_in, TR_AF_INET6 }
{
    auto const cb = [this]()
    {
        this->update_ipv6_addr();
    };
    upkeep_timer_->setCallback(cb);
    start_timer(UpkeepInterval);
    session_->runInSessionThread(cb);
}

void tr_global_ipv6_cache::update_ipv6_addr() noexcept
{
    TR_ASSERT(session_->amInSessionThread());
    TR_ASSERT(!is_updating_);

    set_is_updating();

    // Get the IPv6 source address used for global connections
    auto const& source_addr = get_global_source_address(AF_INET6);

    // Update the source address for global connections
    set_source_addr(source_addr);

    // Update global address
    if (source_addr && source_addr->is_global_unicast_address())
    {
        set_global_addr(source_addr);
    }
    else
    {
        unset_global_addr();
    }

    if (source_addr)
    {
        upkeep_timer_->setInterval(UpkeepInterval);
        tr_logAddInfo(_("Successfully updated source IPv6 address"));
    }
    else
    {
        upkeep_timer_->setInterval(RetryUpkeepInterval);
        tr_logAddDebug("Couldn't obtain source IPv6 address");
    }

    if (global_addr_)
    {
        tr_logAddInfo(_("Successfully updated global IPv6 address"));
    }
    else
    {
        tr_logAddDebug("Couldn't obtain global IPv6 address");
    }

    unset_is_updating();
}
