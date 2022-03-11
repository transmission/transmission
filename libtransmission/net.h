// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <inttypes.h>
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

int tr_address_compare(tr_address const* a, tr_address const* b);

struct tr_address
{
    static tr_address from_4byte_ipv4(std::string_view in);

    static std::optional<tr_address> from_string(std::string_view str);

    std::string to_string() const;
    std::string to_string(tr_port port) const;

    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr;

    [[nodiscard]] int compare(tr_address const& that) const
    {
        return tr_address_compare(this, &that);
    }

    [[nodiscard]] bool operator==(tr_address const& that) const
    {
        return compare(that) == 0;
    }

    [[nodiscard]] bool operator<(tr_address const& that) const
    {
        return compare(that) < 0;
    }

    [[nodiscard]] bool operator>(tr_address const& that) const
    {
        return compare(that) > 0;
    }
};

extern tr_address const tr_inaddr_any;
extern tr_address const tr_in6addr_any;

char const* tr_address_to_string(tr_address const* addr);

char const* tr_address_to_string_with_buf(tr_address const* addr, char* buf, size_t buflen);

char const* tr_address_and_port_to_string(char* buf, size_t buflen, tr_address const* addr, tr_port port);

bool tr_address_from_string(tr_address* setme, char const* string);

bool tr_address_from_string(tr_address* dst, std::string_view src);

bool tr_address_from_sockaddr_storage(tr_address* setme, tr_port* port, struct sockaddr_storage const* src);

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

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t bound, tr_address* setme_addr, tr_port* setme_port);

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
