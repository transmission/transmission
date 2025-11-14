// This file Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::copy_n
#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint16_t, uint32_t, uint8_t
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::pair

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#ifdef _WIN32
using tr_socket_t = SOCKET;
#define TR_BAD_SOCKET INVALID_SOCKET

#undef EADDRINUSE
#define EADDRINUSE WSAEADDRINUSE
#undef ECONNREFUSED
#define ECONNREFUSED WSAECONNREFUSED
#undef ECONNRESET
#define ECONNRESET WSAECONNRESET
#undef EHOSTUNREACH
#define EHOSTUNREACH WSAEHOSTUNREACH
#undef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#undef ENOTCONN
#define ENOTCONN WSAENOTCONN
#undef EWOULDBLOCK
#define EWOULDBLOCK WSAEWOULDBLOCK
#undef EAFNOSUPPORT
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#undef ENETUNREACH
#define ENETUNREACH WSAENETUNREACH

#define sockerrno WSAGetLastError()
#define set_sockerrno(save)
#else
/** @brief Platform-specific socket descriptor type. */
using tr_socket_t = int;
/** @brief Platform-specific invalid socket descriptor constant. */
#define TR_BAD_SOCKET (-1)

#define sockerrno errno
#define set_sockerrno(save) (sockerrno) = (save)
#endif

#include "libtransmission/transmission.h" // tr_peer_from

#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h" // for tr_compare_3way()

/**
 * Literally just a port number.
 *
 * Exists so that you never have to wonder what byte order a port variable is in.
 */
class tr_port
{
public:
    tr_port() noexcept = default;

    [[nodiscard]] constexpr static tr_port from_host(uint16_t hport) noexcept
    {
        return tr_port{ hport };
    }

    [[nodiscard]] static tr_port from_network(uint16_t nport) noexcept
    {
        return tr_port{ ntohs(nport) };
    }

    [[nodiscard]] constexpr uint16_t host() const noexcept
    {
        return hport_;
    }

    [[nodiscard]] uint16_t network() const noexcept
    {
        return htons(hport_);
    }

    constexpr void set_host(uint16_t hport) noexcept
    {
        hport_ = hport;
    }

    void set_network(uint16_t nport) noexcept
    {
        hport_ = ntohs(nport);
    }

    [[nodiscard]] static std::pair<tr_port, std::byte const*> from_compact(std::byte const* compact) noexcept;

    [[nodiscard]] constexpr auto operator<(tr_port const& that) const noexcept
    {
        return hport_ < that.hport_;
    }

    [[nodiscard]] constexpr auto operator==(tr_port const& that) const noexcept
    {
        return hport_ == that.hport_;
    }

    // Can be removed once we use C++20
    [[nodiscard]] constexpr auto operator!=(tr_port const& that) const noexcept
    {
        return hport_ != that.hport_;
    }

    [[nodiscard]] constexpr auto empty() const noexcept
    {
        return hport_ == 0;
    }

    constexpr void clear() noexcept
    {
        hport_ = 0;
    }

    static auto constexpr CompactPortBytes = 2U;

private:
    explicit constexpr tr_port(uint16_t hport) noexcept
        : hport_{ hport }
    {
    }

    uint16_t hport_ = 0;
};

enum tr_address_type : uint8_t
{
    TR_AF_INET = 0,
    TR_AF_INET6,
    NUM_TR_AF_INET_TYPES,
    TR_AF_UNSPEC = NUM_TR_AF_INET_TYPES
};

std::string_view tr_ip_protocol_to_sv(tr_address_type type);
int tr_ip_protocol_to_af(tr_address_type type);
tr_address_type tr_af_to_ip_protocol(int af);

struct tr_address
{
    [[nodiscard]] static std::optional<tr_address> from_string(std::string_view address_sv);
    [[nodiscard]] static std::pair<tr_address, std::byte const*> from_compact_ipv4(std::byte const* compact) noexcept;
    [[nodiscard]] static std::pair<tr_address, std::byte const*> from_compact_ipv6(std::byte const* compact) noexcept;

    // --- write the text form of the address, e.g. inet_ntop()
    std::string_view display_name(char* out, size_t outlen) const;
    [[nodiscard]] std::string display_name() const;

    // ---

    [[nodiscard]] constexpr auto is_ipv4() const noexcept
    {
        return type == TR_AF_INET;
    }

    [[nodiscard]] constexpr auto is_ipv6() const noexcept
    {
        return type == TR_AF_INET6;
    }

    // --- bt protocol compact form

    // compact addr only -- used e.g. as `yourip` value in extension protocol handshake

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, in_addr const& addr4)
    {
        return std::copy_n(reinterpret_cast<std::byte const*>(&addr4.s_addr), sizeof(addr4.s_addr), out);
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, in6_addr const& addr6)
    {
        return std::copy_n(reinterpret_cast<std::byte const*>(&addr6.s6_addr), sizeof(addr6.s6_addr), out);
    }

    template<typename OutputIt>
    OutputIt to_compact(OutputIt out) const // NOLINT(modernize-use-nodiscard)
    {
        switch (type)
        {
        case TR_AF_INET:
            return to_compact_ipv4(out, addr.addr4);
        case TR_AF_INET6:
            return to_compact_ipv6(out, addr.addr6);
        default:
            TR_ASSERT_MSG(false, "invalid address type");
            return out;
        }
    }

    // ---

    [[nodiscard]] std::optional<unsigned> to_interface_index() const noexcept;

    // --- comparisons

    [[nodiscard]] int compare(tr_address const& that) const noexcept;

    [[nodiscard]] bool operator==(tr_address const& that) const noexcept
    {
        return this->compare(that) == 0;
    }

    [[nodiscard]] bool operator!=(tr_address const& that) const noexcept
    {
        return !(*this == that);
    }

    [[nodiscard]] bool operator<(tr_address const& that) const noexcept
    {
        return this->compare(that) < 0;
    }

    [[nodiscard]] bool operator<=(tr_address const& that) const noexcept
    {
        return this->compare(that) <= 0;
    }

    [[nodiscard]] bool operator>(tr_address const& that) const noexcept
    {
        return this->compare(that) > 0;
    }

    // ---

    [[nodiscard]] bool is_global_unicast() const noexcept;

    // 0.0.0.0/8
    [[nodiscard]] constexpr bool is_ipv4_current_network() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 0U;
    }

    // 10.0.0.0/8
    [[nodiscard]] constexpr bool is_ipv4_10_private() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 10U;
    }

    // 100.64.0.0/10
    [[nodiscard]] constexpr bool is_ipv4_carrier_grade_nat() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 100U &&
            (reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] & 0xC0U) == 64U;
    }

    // 127.0.0.0/8
    [[nodiscard]] constexpr bool is_ipv4_loopback() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 127U;
    }

    // 169.254.0.0/16
    [[nodiscard]] constexpr bool is_ipv4_link_local() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 169U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 254U;
    }

    // 172.16.0.0/12
    [[nodiscard]] constexpr bool is_ipv4_172_private() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 172U &&
            (reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] & 0xF0U) == 16U;
    }

    // 192.0.0.0/24
    [[nodiscard]] constexpr bool is_ipv4_ietf_protocol_assignment() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 192U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 0U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 0U;
    }

    // 192.0.2.0/24
    [[nodiscard]] constexpr bool is_ipv4_test_net_1() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 192U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 0U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 2U;
    }

    // 192.88.99.0/24
    [[nodiscard]] constexpr bool is_ipv4_6to4_relay() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 192U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 88U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 99U;
    }

    // 192.168.0.0/16
    [[nodiscard]] constexpr bool is_ipv4_192_168_private() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 192U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 168U;
    }

    // 198.18.0.0/15
    [[nodiscard]] constexpr bool is_ipv4_benchmark() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 198U &&
            (reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] & 0xFEU) == 18U;
    }

    // 198.51.100.0/24
    [[nodiscard]] constexpr bool is_ipv4_test_net_2() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 198U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 51U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 100U;
    }

    // 203.0.113.0/24
    [[nodiscard]] constexpr bool is_ipv4_test_net_3() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 203U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 0U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 113U;
    }

    // 224.0.0.0/4
    [[nodiscard]] constexpr bool is_ipv4_multicast() const noexcept
    {
        return is_ipv4() && (reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] & 0xF0U) == 224U;
    }

    // 233.252.0.0/24
    [[nodiscard]] constexpr bool is_ipv4_mcast_test_net() const noexcept
    {
        return is_ipv4() && reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] == 233U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[1] == 252U &&
            reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[2] == 0U;
    }

    // 240.0.0.0/4 -255.255.255.255/32
    [[nodiscard]] constexpr bool is_ipv4_reserved_class_e() const noexcept
    {
        return is_ipv4() && !is_ipv4_limited_broadcast() &&
            (reinterpret_cast<uint8_t const*>(&addr.addr4.s_addr)[0] & 0xF0U) == 240U;
    }

    // 255.255.255.255/32
    [[nodiscard]] constexpr bool is_ipv4_limited_broadcast() const noexcept
    {
        return is_ipv4() && addr.addr4.s_addr == 0xFFFFFFFFU;
    }

    // ::/128
    [[nodiscard]] constexpr bool is_ipv6_unspecified() const noexcept
    {
        return is_ipv6() && IN6_IS_ADDR_UNSPECIFIED(&addr.addr6);
    }

    // ::1/128
    [[nodiscard]] constexpr bool is_ipv6_loopback() const noexcept
    {
        return is_ipv6() && IN6_IS_ADDR_LOOPBACK(&addr.addr6);
    }

    // ::ffff:0:0/96
    [[nodiscard]] constexpr bool is_ipv6_ipv4_mapped() const noexcept
    {
        return is_ipv6() && IN6_IS_ADDR_V4MAPPED(&addr.addr6);
    }

    // fe80::/64 from fe80::/10
    [[nodiscard]] constexpr bool is_ipv6_link_local() const noexcept
    {
        return is_ipv6() && IN6_IS_ADDR_LINKLOCAL(&addr.addr6);
    }

    // ff00::/8
    [[nodiscard]] constexpr bool is_ipv6_multicast() const noexcept
    {
        return is_ipv6() && IN6_IS_ADDR_MULTICAST(&addr.addr6);
    }

    [[nodiscard]] std::optional<tr_address> from_ipv4_mapped() const noexcept;

    tr_address_type type = NUM_TR_AF_INET_TYPES;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr = {};

    static auto constexpr CompactAddrBytes = std::array{ 4U, 16U };
    static auto constexpr CompactAddrMaxBytes = *std::max_element(std::begin(CompactAddrBytes), std::end(CompactAddrBytes));
    static_assert(std::size(CompactAddrBytes) == NUM_TR_AF_INET_TYPES);

    [[nodiscard]] static auto any(tr_address_type type) noexcept
    {
        switch (type)
        {
        case TR_AF_INET:
            return tr_address{ TR_AF_INET, { { { { INADDR_ANY } } } } };
        case TR_AF_INET6:
            return tr_address{ TR_AF_INET6, { IN6ADDR_ANY_INIT } };
        default:
            TR_ASSERT_MSG(false, "invalid type");
            return tr_address{};
        }
    }

    [[nodiscard]] static constexpr auto is_valid(tr_address_type type) noexcept
    {
        return type == TR_AF_INET || type == TR_AF_INET6;
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept
    {
        return is_valid(type);
    }

    [[nodiscard]] auto is_any() const noexcept
    {
        return is_valid() && *this == any(type);
    }
};

struct tr_socket_address
{
    tr_socket_address() = default;

    tr_socket_address(tr_address const& address, tr_port port)
        : address_{ address }
        , port_{ port }
    {
    }

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return address_;
    }

    [[nodiscard]] constexpr auto port() const noexcept
    {
        return port_;
    }

    [[nodiscard]] static std::string display_name(tr_address const& address, tr_port port) noexcept;
    [[nodiscard]] auto display_name() const noexcept
    {
        return display_name(address_, port_);
    }

    [[nodiscard]] auto is_valid() const noexcept
    {
        return address_.is_valid();
    }

    [[nodiscard]] bool is_valid_for_peers(tr_peer_from from) const noexcept;

    [[nodiscard]] int compare(tr_socket_address const& that) const noexcept
    {
        if (auto const val = tr_compare_3way(address_, that.address_); val != 0)
        {
            return val;
        }

        return tr_compare_3way(port_, that.port_);
    }

    // --- compact addr + port -- very common format used for peer exchange, dht, tracker announce responses

    [[nodiscard]] static std::pair<tr_socket_address, std::byte const*> from_compact_ipv4(std::byte const* compact) noexcept
    {
        auto socket_address = tr_socket_address{};
        std::tie(socket_address.address_, compact) = tr_address::from_compact_ipv4(compact);
        std::tie(socket_address.port_, compact) = tr_port::from_compact(compact);
        return { socket_address, compact };
    }

    [[nodiscard]] static std::pair<tr_socket_address, std::byte const*> from_compact_ipv6(std::byte const* compact) noexcept
    {
        auto socket_address = tr_socket_address{};
        std::tie(socket_address.address_, compact) = tr_address::from_compact_ipv6(compact);
        std::tie(socket_address.port_, compact) = tr_port::from_compact(compact);
        return { socket_address, compact };
    }

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, tr_address const& addr, tr_port const port)
    {
        out = addr.to_compact(out);

        auto const nport = port.network();
        return std::copy_n(reinterpret_cast<std::byte const*>(&nport), sizeof(nport), out);
    }

    template<typename OutputIt>
    OutputIt to_compact(OutputIt out) const // NOLINT(modernize-use-nodiscard)
    {
        return to_compact(out, address_, port_);
    }

    // --- compact sockaddr helpers

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, sockaddr const* saddr)
    {
        if (auto socket_address = from_sockaddr(saddr); socket_address)
        {
            return socket_address->to_compact(out);
        }

        return out;
    }

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, sockaddr_storage const* ss)
    {
        return to_compact(out, reinterpret_cast<sockaddr const*>(ss));
    }

    // --- sockaddr helpers

    [[nodiscard]] static std::optional<tr_socket_address> from_string(std::string_view sockaddr_sv);
    [[nodiscard]] static std::optional<tr_socket_address> from_sockaddr(sockaddr const* from);
    [[nodiscard]] static std::pair<sockaddr_storage, socklen_t> to_sockaddr(tr_address const& addr, tr_port port) noexcept;

    [[nodiscard]] std::pair<sockaddr_storage, socklen_t> to_sockaddr() const noexcept
    {
        return to_sockaddr(address_, port_);
    }

    // --- Comparisons

    [[nodiscard]] auto operator<(tr_socket_address const& that) const noexcept
    {
        return compare(that) < 0;
    }

    [[nodiscard]] auto operator==(tr_socket_address const& that) const noexcept
    {
        return compare(that) == 0;
    }

    tr_address address_;
    tr_port port_;

    static auto constexpr CompactSockAddrBytes = std::array{ tr_address::CompactAddrBytes[0] + tr_port::CompactPortBytes,
                                                             tr_address::CompactAddrBytes[1] + tr_port::CompactPortBytes };
    static auto constexpr CompactSockAddrMaxBytes = tr_address::CompactAddrMaxBytes + tr_port::CompactPortBytes;
    static_assert(std::size(CompactSockAddrBytes) == NUM_TR_AF_INET_TYPES);
};

template<>
struct std::hash<tr_socket_address>
{
public:
    std::size_t operator()(tr_socket_address const& socket_address) const noexcept
    {
        auto const& [addr, port] = socket_address;
        return hash_combine(ip_hash(addr), PortHasher(port.host()));
    }

private:
    // https://stackoverflow.com/a/27952689/11390656
    [[nodiscard]] static constexpr std::size_t hash_combine(std::size_t const a, std::size_t const b)
    {
        return a ^ (b + 0x9e3779b9U + (a << 6U) + (a >> 2U));
    }

    [[nodiscard]] static std::size_t ip_hash(tr_address const& addr) noexcept
    {
        switch (addr.type)
        {
        case TR_AF_INET:
            return IPv4Hasher(addr.addr.addr4.s_addr);
        case TR_AF_INET6:
            return IPv6Hasher({ reinterpret_cast<char const*>(addr.addr.addr6.s6_addr), sizeof(addr.addr.addr6.s6_addr) });
        default:
            TR_ASSERT_MSG(false, "Invalid type");
            return {};
        }
    }

    constexpr static std::hash<uint32_t> IPv4Hasher{};
    constexpr static std::hash<std::string_view> IPv6Hasher{};
    constexpr static std::hash<uint16_t> PortHasher{};
};

// --- Sockets

struct tr_session;

tr_socket_t tr_netBindTCP(tr_address const& addr, tr_port port, bool suppress_msgs);

[[nodiscard]] std::optional<std::pair<tr_socket_address, tr_socket_t>> tr_netAccept(
    tr_session* session,
    tr_socket_t listening_sockfd);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

[[nodiscard]] tr_socket_t tr_net_open_peer_socket(
    tr_session* session,
    tr_socket_address const& socket_address,
    bool client_is_seed);

void tr_net_close_socket(tr_socket_t fd);

// --- TOS / DSCP

/**
 * A `toString()` / `from_string()` convenience wrapper around the TOS int value
 */
class tr_tos_t
{
public:
    constexpr tr_tos_t() = default;

    constexpr explicit tr_tos_t(int value)
        : value_{ value }
    {
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    [[nodiscard]] constexpr operator int() const noexcept
    {
        return value_;
    }

    [[nodiscard]] static std::optional<tr_tos_t> from_string(std::string_view name);

    [[nodiscard]] std::string toString() const;

private:
    int value_ = 0x04;

    // RFCs 2474, 3246, 4594 & 8622
    // Service class names are defined in RFC 4594, RFC 5865, and RFC 8622.
    // Not all platforms have these IPTOS_ definitions, so hardcode them here
    static auto constexpr Names = std::array<std::pair<int, std::string_view>, 28>{ {
        { 0x00, "cs0" }, // IPTOS_CLASS_CS0
        { 0x04, "le" },
        { 0x20, "cs1" }, // IPTOS_CLASS_CS1
        { 0x28, "af11" }, // IPTOS_DSCP_AF11
        { 0x30, "af12" }, // IPTOS_DSCP_AF12
        { 0x38, "af13" }, // IPTOS_DSCP_AF13
        { 0x40, "cs2" }, // IPTOS_CLASS_CS2
        { 0x48, "af21" }, // IPTOS_DSCP_AF21
        { 0x50, "af22" }, // IPTOS_DSCP_AF22
        { 0x58, "af23" }, // IPTOS_DSCP_AF23
        { 0x60, "cs3" }, // IPTOS_CLASS_CS3
        { 0x68, "af31" }, // IPTOS_DSCP_AF31
        { 0x70, "af32" }, // IPTOS_DSCP_AF32
        { 0x78, "af33" }, // IPTOS_DSCP_AF33
        { 0x80, "cs4" }, // IPTOS_CLASS_CS4
        { 0x88, "af41" }, // IPTOS_DSCP_AF41
        { 0x90, "af42" }, // IPTOS_DSCP_AF42
        { 0x98, "af43" }, // IPTOS_DSCP_AF43
        { 0xa0, "cs5" }, // IPTOS_CLASS_CS5
        { 0xb8, "ef" }, // IPTOS_DSCP_EF
        { 0xc0, "cs6" }, // IPTOS_CLASS_CS6
        { 0xe0, "cs7" }, // IPTOS_CLASS_CS7

        // <netinet/ip.h> lists these TOS names as deprecated,
        // but keep them defined here for backward compatibility
        { 0x00, "routine" }, // IPTOS_PREC_ROUTINE
        { 0x02, "lowcost" }, // IPTOS_LOWCOST
        { 0x02, "mincost" }, // IPTOS_MINCOST
        { 0x04, "reliable" }, // IPTOS_RELIABILITY
        { 0x08, "throughput" }, // IPTOS_THROUGHPUT
        { 0x10, "lowdelay" }, // IPTOS_LOWDELAY
    } };
};

// set the IPTOS_ value for the specified socket
void tr_netSetTOS(tr_socket_t sock, int tos, tr_address_type type);

/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
[[nodiscard]] std::string tr_net_strerror(int err);

[[nodiscard]] int tr_make_listen_socket_ipv6only(tr_socket_t sock);
