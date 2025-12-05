// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator> // std::back_inserter
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::pair

#ifdef _WIN32
#include <winsock2.h> // must come before iphlpapi.h
#include <iphlpapi.h>
#include <ws2tcpip.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#endif

#include <event2/util.h>

#include <fmt/format.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket.h"
#include "libtransmission/session.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-strbuf.h"
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

using namespace std::literals;

std::string tr_net_strerror(int err)
{
#ifdef _WIN32

    auto buf = std::array<char, 512>{};
    (void)FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, std::data(buf), std::size(buf), nullptr);
    return std::string{ tr_strv_strip(std::data(buf)) };

#else

    return std::string{ tr_strerror(err) };

#endif
}

std::string_view tr_ip_protocol_to_sv(tr_address_type type)
{
    using namespace std::literals;

    switch (type)
    {
    case TR_AF_INET:
        return "IPv4"sv;
    case TR_AF_INET6:
        return "IPv6"sv;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return {};
    }
}

int tr_ip_protocol_to_af(tr_address_type type)
{
    switch (type)
    {
    case TR_AF_INET:
        return AF_INET;
    case TR_AF_INET6:
        return AF_INET6;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return {};
    }
}

tr_address_type tr_af_to_ip_protocol(int af)
{
    switch (af)
    {
    case AF_INET:
        return TR_AF_INET;
    case AF_INET6:
        return TR_AF_INET6;
    default:
        TR_ASSERT_MSG(false, "invalid address family");
        return NUM_TR_AF_INET_TYPES;
    }
}

int tr_make_listen_socket_ipv6only(tr_socket_t const sock)
{
#if defined(IPV6_V6ONLY)
    int optval = 1;
    return setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&optval), sizeof(optval));
#else
    return 0;
#endif
}

// - TCP Sockets

void tr_netSetDiffServ([[maybe_unused]] tr_socket_t s, [[maybe_unused]] int tos, tr_address_type type)
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

namespace
{
tr_socket_t tr_netBindTCPImpl(tr_address const& addr, tr_port port, bool suppress_msgs, int* err_out)
{
    TR_ASSERT(addr.is_valid());

    auto const fd = socket(tr_ip_protocol_to_af(addr.type), SOCK_STREAM, 0);
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
    (void)evutil_make_listen_socket_reuseable(fd);

    if (addr.is_ipv6() && tr_make_listen_socket_ipv6only(fd) == -1 &&
        sockerrno != ENOPROTOOPT) // if the kernel doesn't support it, ignore it
    {
        *err_out = sockerrno;
        tr_net_close_socket(fd);
        return TR_BAD_SOCKET;
    }

    auto const [sock, addrlen] = tr_socket_address::to_sockaddr(addr, port);

    if (bind(fd, reinterpret_cast<sockaddr const*>(&sock), addrlen) == -1)
    {
        int const err = sockerrno;

        if (!suppress_msgs)
        {
            tr_logAddError(
                fmt::format(
                    fmt::runtime(
                        err == EADDRINUSE ?
                            _("Couldn't bind port {port} on {address}: {error} ({error_code}) -- Is another copy of Transmission already running?") :
                            _("Couldn't bind port {port} on {address}: {error} ({error_code})")),
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
        tr_logAddDebug(fmt::format("Bound socket {:d} to port {:d} on {:s}", fd, port.host(), addr.display_name()));
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
} // namespace

tr_socket_t tr_netBindTCP(tr_address const& addr, tr_port port, bool suppress_msgs)
{
    int unused = 0;
    return tr_netBindTCPImpl(addr, port, suppress_msgs, &unused);
}

std::optional<std::pair<tr_socket_address, tr_socket_t>> tr_netAccept(tr_session* session, tr_socket_t listening_sockfd)
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
    auto const addrport = tr_socket_address::from_sockaddr(reinterpret_cast<struct sockaddr*>(&sock));
    if (!addrport || evutil_make_socket_nonblocking(sockfd) == -1 || tr_peer_socket::limit_reached(session))
    {
        tr_net_close_socket(sockfd);
        return {};
    }

    return std::pair{ *addrport, sockfd };
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

/* isMartianAddr was written by Juliusz Chroboczek,
   and is covered under the same license as third-party/dht/dht.c. */
[[nodiscard]] auto is_martian_addr(tr_address const& addr, tr_peer_from from)
{
    auto const loopback_allowed = from == TR_PEER_FROM_INCOMING || from == TR_PEER_FROM_LPD || from == TR_PEER_FROM_RESUME;
    return addr.is_ipv4_current_network() || addr.is_ipv6_unspecified() ||
        (!loopback_allowed && (addr.is_ipv4_loopback() || addr.is_ipv6_loopback())) || addr.is_ipv4_multicast() ||
        addr.is_ipv6_multicast();
}

} // namespace is_valid_for_peers_helpers
} // namespace

// --- tr_port

std::pair<tr_port, std::byte const*> tr_port::from_compact(std::byte const* compact) noexcept
{
    static auto constexpr PortLen = size_t{ 2 };

    static_assert(PortLen == sizeof(uint16_t));
    auto nport = uint16_t{};
    std::copy_n(compact, PortLen, reinterpret_cast<std::byte*>(&nport));
    compact += PortLen;

    return std::make_pair(tr_port::from_network(nport), compact);
}

// --- tr_address

std::optional<tr_address> tr_address::from_string(std::string_view address_sv)
{
    auto const address_sz = tr_strbuf<char, TrAddrStrlen>{ address_sv };

    auto ss = sockaddr_storage{};
    auto sslen = int{ sizeof(ss) };
    if (evutil_parse_sockaddr_port(address_sz, reinterpret_cast<sockaddr*>(&ss), &sslen) != 0)
    {
        return {};
    }

    auto addr = tr_address{};
    switch (ss.ss_family)
    {
    case AF_INET:
        addr.addr.addr4 = reinterpret_cast<sockaddr_in*>(&ss)->sin_addr;
        addr.type = TR_AF_INET;
        return addr;

    case AF_INET6:
        addr.addr.addr6 = reinterpret_cast<sockaddr_in6*>(&ss)->sin6_addr;
        addr.type = TR_AF_INET6;
        return addr;

    default:
        return {};
    }
}

[[nodiscard]] std::string tr_address::display_name() const
{
    auto buf = std::array<char, std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN)>{};
    TR_ASSERT(is_valid());
    if (auto* name = evutil_inet_ntop(tr_ip_protocol_to_af(type), &addr, std::data(buf), std::size(buf)))
    {
        return std::string{ name };
    }
    return std::string{ "Invalid address" };
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv4(std::byte const* compact) noexcept
{
    static auto constexpr Addr4Len = tr_address::CompactAddrBytes[TR_AF_INET];

    auto address = tr_address{};
    static_assert(sizeof(address.addr.addr4) == Addr4Len);
    address.type = TR_AF_INET;
    std::copy_n(compact, Addr4Len, reinterpret_cast<std::byte*>(&address.addr));
    compact += Addr4Len;

    return { address, compact };
}

std::pair<tr_address, std::byte const*> tr_address::from_compact_ipv6(std::byte const* compact) noexcept
{
    static auto constexpr Addr6Len = tr_address::CompactAddrBytes[TR_AF_INET6];

    auto address = tr_address{};
    address.type = TR_AF_INET6;
    std::copy_n(compact, Addr6Len, reinterpret_cast<std::byte*>(&address.addr.addr6.s6_addr));
    compact += Addr6Len;

    return { address, compact };
}

std::optional<unsigned> tr_address::to_interface_index() const noexcept
{
    if (!is_valid())
    {
        tr_logAddDebug("Invalid target address to find interface index");
        return {};
    }

    tr_logAddDebug(fmt::format("Find interface index for {}", display_name()));

#ifdef _WIN32
    auto p_addresses = std::unique_ptr<void, void (*)(void*)>{ nullptr, operator delete };

    // The recommended method of calling the GetAdaptersAddresses function is to
    // pre-allocate a 15KB working buffer pointed to by the AdapterAddresses parameter.
    // On typical computers, this dramatically reduces the chances that the
    // GetAdaptersAddresses function returns ERROR_BUFFER_OVERFLOW, which would require
    // calling GetAdaptersAddresses function multiple times.
    // https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
    for (auto p_addresses_size = ULONG{ 15000 } /* 15KB */;;)
    {
        p_addresses.reset(operator new(p_addresses_size, std::nothrow));
        if (!p_addresses)
        {
            tr_logAddDebug("Could not allocate memory for interface list");
            return {};
        }

        if (auto ret = GetAdaptersAddresses(
                AF_UNSPEC,
                GAA_FLAG_SKIP_FRIENDLY_NAME,
                nullptr,
                reinterpret_cast<PIP_ADAPTER_ADDRESSES>(p_addresses.get()),
                &p_addresses_size);
            ret != ERROR_BUFFER_OVERFLOW)
        {
            if (ret != ERROR_SUCCESS)
            {
                tr_logAddDebug(fmt::format("Failed to retrieve interface list: {} ({})", ret, tr_win32_format_message(ret)));
                return {};
            }
            break;
        }
    }

    for (auto const* cur = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(p_addresses.get()); cur != nullptr; cur = cur->Next)
    {
        if (cur->OperStatus != IfOperStatusUp)
        {
            continue;
        }

        for (auto const* sa_p = cur->FirstUnicastAddress; sa_p != nullptr; sa_p = sa_p->Next)
        {
            if (auto if_addr = tr_socket_address::from_sockaddr(sa_p->Address.lpSockaddr);
                if_addr && if_addr->address() == *this)
            {
                auto const ret = type == TR_AF_INET ? cur->IfIndex : cur->Ipv6IfIndex;
                tr_logAddDebug(fmt::format("Found interface index for {}: {}", display_name(), ret));
                return ret;
            }
        }
    }
#else
    struct ifaddrs* ifa = nullptr;
    if (getifaddrs(&ifa) != 0)
    {
        auto err = errno;
        tr_logAddDebug(fmt::format("Failed to retrieve interface list: {} ({})", err, tr_strerror(err)));
        return {};
    }
    auto const ifa_uniq = std::unique_ptr<ifaddrs, void (*)(struct ifaddrs*)>{ ifa, freeifaddrs };

    for (; ifa != nullptr; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == nullptr || (ifa->ifa_flags & IFF_UP) == 0U)
        {
            continue;
        }

        if (auto if_addr = tr_socket_address::from_sockaddr(ifa->ifa_addr); if_addr && if_addr->address() == *this)
        {
            auto const ret = if_nametoindex(ifa->ifa_name);
            tr_logAddDebug(fmt::format("Found interface index for {}: {}", display_name(), ret));
            return ret;
        }
    }
#endif

    tr_logAddDebug(fmt::format("Could not find interface index for {}", display_name()));
    return {};
}

std::strong_ordering tr_address::operator<=>(tr_address const& that) const noexcept
{
    // IPv6 addresses are always "greater than" IPv4
    if (auto const val = this->type <=> that.type; val != 0)
    {
        return val;
    }

    auto const val = this->is_ipv4() ?
        memcmp(&this->addr.addr4, &that.addr.addr4, sizeof(this->addr.addr4)) :
        memcmp(&this->addr.addr6.s6_addr, &that.addr.addr6.s6_addr, sizeof(this->addr.addr6.s6_addr));
    if (val < 0)
    {
        return std::strong_ordering::less;
    }
    if (val > 0)
    {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

// https://en.wikipedia.org/wiki/Reserved_IP_addresses
//
// https://www.rfc-editor.org/rfc/rfc4291.html#section-2.4
// address type         Binary prefix        IPv6 notation   Section
// ------------         -------------        -------------   -------
// Unspecified          00...0  (128 bits)   ::/128          2.5.2
// Loopback             00...1  (128 bits)   ::1/128         2.5.3
// Multicast            11111111             FF00::/8        2.7
// Link-Local unicast   1111111010           FE80::/10       2.5.6
// Global Unicast       (everything else)
[[nodiscard]] bool tr_address::is_global_unicast() const noexcept
{
    return !is_ipv4_current_network() && //
        !is_ipv4_10_private() && //
        !is_ipv4_carrier_grade_nat() && //
        !is_ipv4_loopback() && //
        !is_ipv4_link_local() && //
        !is_ipv4_172_private() && //
        !is_ipv4_ietf_protocol_assignment() && //
        !is_ipv4_test_net_1() && //
        !is_ipv4_6to4_relay() && //
        !is_ipv4_192_168_private() && //
        !is_ipv4_benchmark() && //
        !is_ipv4_test_net_2() && //
        !is_ipv4_test_net_3() && //
        !is_ipv4_multicast() && //
        !is_ipv4_mcast_test_net() && //
        !is_ipv4_reserved_class_e() && //
        !is_ipv4_limited_broadcast() && //
        !is_ipv6_unspecified() && //
        !is_ipv6_loopback() && //
        !is_ipv6_multicast() && //
        !is_ipv6_link_local();
}

std::optional<tr_address> tr_address::from_ipv4_mapped() const noexcept
{
    if (!is_ipv6_ipv4_mapped())
    {
        return {};
    }

    return from_compact_ipv4(reinterpret_cast<std::byte const*>(&addr.addr6.s6_addr) + 12).first;
}

// --- tr_socket_addrses

std::string tr_socket_address::display_name(tr_address const& address, tr_port port)
{
    return fmt::format(fmt::runtime(address.is_ipv6() ? "[{:s}]:{:d}" : "{:s}:{:d}"), address.display_name(), port.host());
}

bool tr_socket_address::is_valid_for_peers(tr_peer_from from) const noexcept
{
    using namespace is_valid_for_peers_helpers;

    return is_valid() && !std::empty(port_) && !address_.is_ipv6_link_local() && !address_.is_ipv6_ipv4_mapped() &&
        !is_martian_addr(address_, from);
}

std::optional<tr_socket_address> tr_socket_address::from_string(std::string_view sockaddr_sv)
{
    auto ss = sockaddr_storage{};
    auto sslen = int{ sizeof(ss) };
    if (evutil_parse_sockaddr_port(tr_strbuf<char, TrAddrStrlen>{ sockaddr_sv }, reinterpret_cast<sockaddr*>(&ss), &sslen) != 0)
    {
        return {};
    }

    return from_sockaddr(reinterpret_cast<struct sockaddr const*>(&ss));
}

std::optional<tr_socket_address> tr_socket_address::from_sockaddr(struct sockaddr const* from)
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
        return tr_socket_address{ addr, tr_port::from_network(sin->sin_port) };
    }

    if (from->sa_family == AF_INET6)
    {
        auto const* const sin6 = reinterpret_cast<struct sockaddr_in6 const*>(from);
        auto addr = tr_address{};
        addr.type = TR_AF_INET6;
        addr.addr.addr6 = sin6->sin6_addr;
        return tr_socket_address{ addr, tr_port::from_network(sin6->sin6_port) };
    }

    tr_logAddDebug(fmt::format("Unsupported address family {:d}", from->sa_family));
    return {};
}

std::pair<sockaddr_storage, socklen_t> tr_socket_address::to_sockaddr(tr_address const& addr, tr_port port) noexcept
{
    auto ss = sockaddr_storage{};

    if (addr.is_ipv4())
    {
        auto* const ss4 = reinterpret_cast<sockaddr_in*>(&ss);
        ss4->sin_addr = addr.addr.addr4;
        ss4->sin_family = AF_INET;
        ss4->sin_port = port.network();
        return { ss, sizeof(sockaddr_in) };
    }

    auto* const ss6 = reinterpret_cast<sockaddr_in6*>(&ss);
    ss6->sin6_addr = addr.addr.addr6;
    ss6->sin6_family = AF_INET6;
    ss6->sin6_flowinfo = 0;
    ss6->sin6_port = port.network();
    return { ss, sizeof(sockaddr_in6) };
}
