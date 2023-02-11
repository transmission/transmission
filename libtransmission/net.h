// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm> // for std::copy_n
#include <cstddef> // size_t
#include <optional>
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

/****
*****
*****  tr_address
*****
****/

enum tr_address_type
{
    TR_AF_INET,
    TR_AF_INET6,
    NUM_TR_AF_INET_TYPES
};

struct tr_address;

[[nodiscard]] int tr_address_compare(tr_address const* a, tr_address const* b) noexcept;

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

struct tr_address
{
    [[nodiscard]] static std::optional<tr_address> fromString(std::string_view address_sv);
    [[nodiscard]] static std::optional<std::pair<tr_address, tr_port>> fromSockaddr(struct sockaddr const*);
    [[nodiscard]] static std::pair<tr_address, std::byte const*> fromCompact4(std::byte const* compact) noexcept;
    [[nodiscard]] static std::pair<tr_address, std::byte const*> fromCompact6(std::byte const* compact) noexcept;

    // human-readable formatting
    template<typename OutputIt>
    OutputIt readable(OutputIt out, tr_port port = {}) const;
    std::string_view readable(char* out, size_t outlen, tr_port port = {}) const;
    [[nodiscard]] std::string readable(tr_port port = {}) const;

    template<typename OutputIt>
    static OutputIt toCompact4(OutputIt out, in_addr const* addr4, tr_port port)
    {
        auto const nport = port.network();
        out = std::copy_n(reinterpret_cast<std::byte const*>(addr4), sizeof(*addr4), out);
        out = std::copy_n(reinterpret_cast<std::byte const*>(&nport), sizeof(nport), out);
        return out;
    }

    template<typename OutputIt>
    static OutputIt toCompact4(OutputIt out, sockaddr_in const* sa4)
    {
        return toCompact4(out, &sa4->sin_addr, tr_port::fromNetwork(sa4->sin_port));
    }

    template<typename OutputIt>
    static OutputIt toCompact6(OutputIt out, in6_addr const* addr6, tr_port port)
    {
        auto const nport = port.network();
        out = std::copy_n(reinterpret_cast<std::byte const*>(addr6), sizeof(*addr6), out);
        out = std::copy_n(reinterpret_cast<std::byte const*>(&nport), sizeof(nport), out);
        return out;
    }

    template<typename OutputIt>
    static OutputIt toCompact6(OutputIt out, sockaddr_in6 const* sa6)
    {
        return toCompact6(out, &sa6->sin6_addr, tr_port::fromNetwork(sa6->sin6_port));
    }

    template<typename OutputIt>
    OutputIt toCompact4(OutputIt out, tr_port port) const
    {
        return toCompact4(out, &this->addr.addr4, port);
    }

    template<typename OutputIt>
    OutputIt toCompact6(OutputIt out, tr_port port) const
    {
        return toCompact6(out, &this->addr.addr6, port);
    }

    template<typename OutputIt>
    static OutputIt toCompact(OutputIt out, sockaddr const* saddr)
    {
        return saddr->sa_family == AF_INET ? toCompact4(out, reinterpret_cast<sockaddr_in const*>(saddr)) :
                                             toCompact6(out, reinterpret_cast<sockaddr_in6 const*>(saddr));
    }

    template<typename OutputIt>
    static OutputIt toCompact(OutputIt out, struct sockaddr_storage* ss)
    {
        return toCompact(out, reinterpret_cast<struct sockaddr*>(ss));
    }

    [[nodiscard]] constexpr auto isIPv4() const noexcept
    {
        return type == TR_AF_INET;
    }

    [[nodiscard]] constexpr auto isIPv6() const noexcept
    {
        return type == TR_AF_INET6;
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

    [[nodiscard]] bool operator>(tr_address const& that) const noexcept
    {
        return this->compare(that) > 0;
    }

    //

    [[nodiscard]] std::pair<sockaddr_storage, socklen_t> toSockaddr(tr_port port) const noexcept;

    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr;
};

extern tr_address const tr_inaddr_any;
extern tr_address const tr_in6addr_any;

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port);

constexpr bool tr_address_is_valid(tr_address const* a)
{
    return a != nullptr && (a->type == TR_AF_INET || a->type == TR_AF_INET6);
}

/***********************************************************************
 * Sockets
 **********************************************************************/

struct tr_session;

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppress_msgs);

[[nodiscard]] std::optional<std::tuple<tr_address, tr_port, tr_socket_t>> tr_netAccept(
    tr_session* session,
    tr_socket_t listening_sockfd);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

void tr_netClose(tr_session* session, tr_socket_t s);

void tr_netCloseSocket(tr_socket_t fd);

bool tr_net_hasIPv6(tr_port);

/// TOS / DSCP

// get a string of one of <netinet/ip.h>'s IPTOS_ values, e.g. "cs0"
[[nodiscard]] std::string tr_netTosToName(int tos);

// get the number that corresponds to the specified IPTOS_ name, e.g. "cs0" returns 0x00
[[nodiscard]] std::optional<int> tr_netTosFromName(std::string_view name);

// set the IPTOS_ value for the specified socket
void tr_netSetTOS(tr_socket_t sock, int tos, tr_address_type type);

/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
[[nodiscard]] std::string tr_net_strerror(int err);

[[nodiscard]] std::optional<in6_addr> tr_globalIPv6(tr_session const* session);
