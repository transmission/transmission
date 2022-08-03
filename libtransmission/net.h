// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <optional>
#include <string>
#include <string_view>
#include <utility> // std::pair

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <errno.h>
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

    [[nodiscard]] static std::pair<tr_port, uint8_t const*> fromCompact(uint8_t const* compact) noexcept;

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
    constexpr tr_port(uint16_t hport) noexcept
        : hport_{ hport }
    {
    }

    uint16_t hport_ = 0;
};

struct tr_address
{
    [[nodiscard]] static std::optional<tr_address> fromString(std::string_view address_sv);
    [[nodiscard]] static std::pair<tr_address, uint8_t const*> fromCompact4(uint8_t const* compact) noexcept;
    [[nodiscard]] static std::pair<tr_address, uint8_t const*> fromCompact6(uint8_t const* compact) noexcept;

    // human-readable formatting
    template<typename OutputIt>
    OutputIt readable(OutputIt out, tr_port port = {}) const;
    std::string_view readable(char* out, size_t outlen, tr_port port = {}) const;
    [[nodiscard]] std::string readable(tr_port port = {}) const;

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

    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr;
};

extern tr_address const tr_inaddr_any;
extern tr_address const tr_in6addr_any;

bool tr_address_from_sockaddr_storage(tr_address* setme, tr_port* port, struct sockaddr_storage const* from);

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port);

constexpr bool tr_address_is_valid(tr_address const* a)
{
    return a != nullptr && (a->type == TR_AF_INET || a->type == TR_AF_INET6);
}

/***********************************************************************
 * Sockets
 **********************************************************************/

struct tr_session;

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppressMsgs);

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t listening_sockfd, tr_address* setme_addr, tr_port* setme_port);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

void tr_netClose(tr_session* session, tr_socket_t s);

void tr_netCloseSocket(tr_socket_t fd);

bool tr_net_hasIPv6(tr_port);

/// TOS / DSCP

// get a string of one of <netinet/ip.h>'s IPTOS_ values, e.g. "cs0"
std::string tr_netTosToName(int tos);

// get the number that corresponds to the specified IPTOS_ name, e.g. "cs0" returns 0x00
std::optional<int> tr_netTosFromName(std::string_view name);

// set the IPTOS_ value for the specified socket
void tr_netSetTOS(tr_socket_t sock, int tos, tr_address_type type);

/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
std::string tr_net_strerror(int err);

unsigned char const* tr_globalIPv6(tr_session const* session);
