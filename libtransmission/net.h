// This file Copyright © 2006-2023 Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::copy_n
#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstddef> // size_t
#include <optional>
#include <shared_mutex>
#include <mutex>
#include <string>
#include <string_view>
#include <utility> // std::pair

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
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
#else
/** @brief Platform-specific socket descriptor type. */
using tr_socket_t = int;
/** @brief Platform-specific invalid socket descriptor constant. */
#define TR_BAD_SOCKET (-1)

#define sockerrno errno
#endif

#include "timer.h"
#include "web.h"

using namespace std::literals;

/**
 * Literally just a port number.
 *
 * Exists so that you never have to wonder what byte order a port variable is in.
 */
class tr_port
{
public:
    tr_port() noexcept = default;

    [[nodiscard]] constexpr static tr_port fromHost(uint16_t hport) noexcept
    {
        return tr_port{ hport };
    }

    [[nodiscard]] static tr_port fromNetwork(uint16_t nport) noexcept
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

    constexpr void setHost(uint16_t hport) noexcept
    {
        hport_ = hport;
    }

    void setNetwork(uint16_t nport) noexcept
    {
        hport_ = ntohs(nport);
    }

    [[nodiscard]] static std::pair<tr_port, std::byte const*> fromCompact(std::byte const* compact) noexcept;

    [[nodiscard]] constexpr auto operator<(tr_port const& that) const noexcept
    {
        return hport_ < that.hport_;
    }

    [[nodiscard]] constexpr auto operator==(tr_port const& that) const noexcept
    {
        return hport_ == that.hport_;
    }

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

private:
    explicit constexpr tr_port(uint16_t hport) noexcept
        : hport_{ hport }
    {
    }

    uint16_t hport_ = 0;
};

enum tr_address_type
{
    TR_AF_INET,
    TR_AF_INET6,
    NUM_TR_AF_INET_TYPES
};

struct tr_address
{
    [[nodiscard]] static std::optional<tr_address> from_string(std::string_view address_sv);
    [[nodiscard]] static std::optional<std::pair<tr_address, tr_port>> from_sockaddr(struct sockaddr const*);
    [[nodiscard]] static std::pair<tr_address, std::byte const*> from_compact_ipv4(std::byte const* compact) noexcept;
    [[nodiscard]] static std::pair<tr_address, std::byte const*> from_compact_ipv6(std::byte const* compact) noexcept;

    // write the text form of the address, e.g. inet_ntop()
    template<typename OutputIt>
    OutputIt display_name(OutputIt out, tr_port port = {}) const;
    std::string_view display_name(char* out, size_t outlen, tr_port port = {}) const;
    [[nodiscard]] std::string display_name(tr_port port = {}) const;

    ///

    [[nodiscard]] constexpr auto is_ipv4() const noexcept
    {
        return type == TR_AF_INET;
    }

    [[nodiscard]] constexpr auto is_ipv6() const noexcept
    {
        return type == TR_AF_INET6;
    }

    /// bt protocol compact form

    // compact addr only -- used e.g. as `yourip` value in extension protocol handshake

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, in_addr const* addr4)
    {
        return std::copy_n(reinterpret_cast<std::byte const*>(addr4), sizeof(*addr4), out);
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, in6_addr const* addr6)
    {
        return std::copy_n(reinterpret_cast<std::byte const*>(addr6), sizeof(*addr6), out);
    }

    template<typename OutputIt>
    OutputIt to_compact(OutputIt out) const
    {
        return is_ipv4() ? to_compact_ipv4(out, &this->addr.addr4) : to_compact_ipv6(out, &this->addr.addr6);
    }

    // compact addr + port -- very common format used for peer exchange, dht, tracker announce responses

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, in_addr const* addr4, tr_port port)
    {
        out = tr_address::to_compact_ipv4(out, addr4);

        auto const nport = port.network();
        return std::copy_n(reinterpret_cast<std::byte const*>(&nport), sizeof(nport), out);
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, in6_addr const* addr6, tr_port port)
    {
        out = tr_address::to_compact_ipv6(out, addr6);

        auto const nport = port.network();
        return std::copy_n(reinterpret_cast<std::byte const*>(&nport), sizeof(nport), out);
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv4(OutputIt out, tr_port port) const
    {
        return to_compact_ipv4(out, &this->addr.addr4, port);
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv6(OutputIt out, tr_port port) const
    {
        return to_compact_ipv6(out, &this->addr.addr6, port);
    }

    template<typename OutputIt>
    OutputIt to_compact(OutputIt out, tr_port port)
    {
        return is_ipv4() ? to_compact_4(out, &this->addr.addr4, port) : to_compact_ipv6(out, &this->addr.addr6, port);
    }

    // compact sockaddr helpers

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, sockaddr_in const* sa4)
    {
        return to_compact_ipv4(out, &sa4->sin_addr, tr_port::fromNetwork(sa4->sin_port));
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, sockaddr_in6 const* sa6)
    {
        return to_compact_ipv6(out, &sa6->sin6_addr, tr_port::fromNetwork(sa6->sin6_port));
    }

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, sockaddr const* saddr)
    {
        return saddr->sa_family == AF_INET ? to_compact_ipv4(out, reinterpret_cast<sockaddr_in const*>(saddr)) :
                                             to_compact_ipv6(out, reinterpret_cast<sockaddr_in6 const*>(saddr));
    }

    template<typename OutputIt>
    static OutputIt to_compact(OutputIt out, struct sockaddr_storage* ss)
    {
        return to_compact(out, reinterpret_cast<struct sockaddr*>(ss));
    }

    // comparisons

    [[nodiscard]] int compare(tr_address const& that) const noexcept;

    [[nodiscard]] bool operator==(tr_address const& that) const noexcept
    {
        return this->compare(that) == 0;
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

    //

    [[nodiscard]] std::pair<sockaddr_storage, socklen_t> to_sockaddr(tr_port port) const noexcept;

    [[nodiscard]] bool is_global_unicast_address() const noexcept;

    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr;

    [[nodiscard]] static auto constexpr any_ipv4() noexcept
    {
        return tr_address{ TR_AF_INET, { { { { INADDR_ANY } } } } };
    }

    [[nodiscard]] static auto constexpr any_ipv6() noexcept
    {
        return tr_address{ TR_AF_INET6, { IN6ADDR_ANY_INIT } };
    }

    [[nodiscard]] constexpr auto is_valid() const noexcept
    {
        return type == TR_AF_INET || type == TR_AF_INET6;
    }

    [[nodiscard]] bool is_valid_for_peers(tr_port port) const noexcept;
};

// --- Sockets

struct tr_session;

tr_socket_t tr_netBindTCP(tr_address const& addr, tr_port port, bool suppress_msgs);

[[nodiscard]] std::optional<std::tuple<tr_address, tr_port, tr_socket_t>> tr_netAccept(
    tr_session* session,
    tr_socket_t listening_sockfd);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

void tr_net_close_socket(tr_socket_t fd);

[[nodiscard]] bool tr_net_hasIPv6(tr_port);

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

    [[nodiscard]] constexpr operator int() const noexcept
    {
        return value_;
    }

    [[nodiscard]] static std::optional<tr_tos_t> from_string(std::string_view);

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

/**
 * Cache global IP addresses.
 *
 * This class caches 3 useful info:
 * 1. Whether your machine supports the IP protocol
 * 2. Source address used for global connections
 * 3. Global address
 *
 * The idea is, if this class successfully cached a source address, that means
 * you have connectivity to the public internet. And if the global address is
 * the same with the source address, then you are not behind an NAT.
 *
 * Note: `tr_session.web_` depends on the source address cache, and the global address cache
 * depends on `tr_session.web_`. So we do the initial update of the source address in the
 * constructor, and do the initial update of the global address in `tr_session.initImpl()`.
 *
 * Note: This class isn't meant to be accessed by anyone other than tr_session,
 * so it has no public methods.
 */
class tr_global_ip_cache
{
public:
    explicit tr_global_ip_cache(tr_session* session_in);
    ~tr_global_ip_cache();
    tr_global_ip_cache(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache(tr_global_ip_cache&&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache const&) = delete;
    tr_global_ip_cache& operator=(tr_global_ip_cache&&) = delete;

private:
    [[nodiscard]] std::optional<tr_address> const& global_addr(tr_address_type type) noexcept;
    [[nodiscard]] std::optional<tr_address> const& global_source_addr(tr_address_type type) noexcept;

    [[nodiscard]] tr_address bind_addr(tr_address_type type) noexcept;

    void set_global_addr(tr_address const& addr) noexcept;
    void unset_global_addr(tr_address_type type) noexcept;
    void set_source_addr(tr_address const& addr) noexcept;
    void unset_addr(tr_address_type type) noexcept;

    void start_timer(tr_address_type type, std::chrono::milliseconds msec) noexcept;
    void stop_timer(tr_address_type type) noexcept;
    void set_is_updating(tr_address_type type) noexcept;
    void unset_is_updating(tr_address_type type) noexcept;

    void update_global_addr(tr_address_type type) noexcept;
    void update_source_addr(tr_address_type type) noexcept;

    // Only use as a callback for web_->fetch()
    void on_response_ip_query(tr_address_type type, tr_web::FetchResponse const& response) noexcept;

    [[nodiscard]] static std::optional<tr_address> get_global_source_address(int af, tr_address const& bind_addr);
    [[nodiscard]] static std::optional<tr_address> get_source_address(
        tr_address const& dst_addr,
        tr_port dst_port,
        tr_address const& bind_addr);

    tr_session* const session_;

    std::array<std::atomic_bool, NUM_TR_AF_INET_TYPES> is_updating_ = { false };
    std::array<std::mutex, NUM_TR_AF_INET_TYPES> is_updating_mutex_;
    std::array<std::condition_variable, NUM_TR_AF_INET_TYPES> is_updating_cv_;

    // Whether this machine supports this IP protocol
    std::array<std::atomic_bool, NUM_TR_AF_INET_TYPES> has_ip_protocol_ = { true, true };

    // Never directly read/write IP addresses for the sake of being thread safe
    // Use global_*_addr() for read, and set_*_addr()/unset_*_addr() for write instead
    std::array<std::shared_mutex, NUM_TR_AF_INET_TYPES> global_addr_mutex_, source_addr_mutex_;
    std::array<std::optional<tr_address>, NUM_TR_AF_INET_TYPES> global_addr_, source_addr_;

    // Keep the timer at the bottom of the class definition so that it will be destructed first
    // We don't want it to trigger after the IP addresses have been destroyed
    // (The destructor will acquire the IP address locks before proceeding, but still)
    std::array<std::unique_ptr<libtransmission::Timer>, NUM_TR_AF_INET_TYPES> upkeep_timer_;

    std::array<std::atomic_size_t, NUM_TR_AF_INET_TYPES> ix_service_ = { 0U };
    static auto constexpr IPQueryServices = std::array<std::string_view, 4>{ "https://icanhazip.com"sv,
                                                                             "https://api64.ipify.org"sv };

    friend struct tr_session;

public:
    static auto constexpr UpkeepInterval = 30min;
    static auto constexpr RetryUpkeepInterval = 30s;
};
