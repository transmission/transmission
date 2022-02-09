// Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <climits>
#include <map>
#include <string_view>

#include "utils.h"

#ifdef _WIN32
#include <inttypes.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netinet/in.h>
#include <netinet/ip.h>
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

struct tr_address
{
    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    } addr;
};

extern tr_address const tr_inaddr_any;
extern tr_address const tr_in6addr_any;

char const* tr_address_to_string(tr_address const* addr);

char const* tr_address_to_string_with_buf(tr_address const* addr, char* buf, size_t buflen);

char const* tr_address_and_port_to_string(char* buf, size_t buflen, tr_address const* addr, tr_port port);

bool tr_address_from_string(tr_address* setme, char const* string);

bool tr_address_from_string(tr_address* dst, std::string_view src);

bool tr_address_from_sockaddr_storage(tr_address* setme, tr_port* port, struct sockaddr_storage const* src);

int tr_address_compare(tr_address const* a, tr_address const* b);

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port);

constexpr bool tr_address_is_valid(tr_address const* a)
{
    return a != nullptr && (a->type == TR_AF_INET || a->type == TR_AF_INET6);
}

/***********************************************************************
 * Sockets
 **********************************************************************/

/*
 * Definitions for DiffServ Codepoints as per RFCs 2474, 3246, 4594 & 8622.
 * Not all are guaranteed to be defined in <netinet/ip.h> so we keep a copy of the DSCP values.
 */
#ifndef IPTOS_DSCP_AF11
#define IPTOS_DSCP_AF11  0x28
#define IPTOS_DSCP_AF12  0x30
#define IPTOS_DSCP_AF13  0x38
#define IPTOS_DSCP_AF21  0x48
#define IPTOS_DSCP_AF22  0x50
#define IPTOS_DSCP_AF23  0x58
#define IPTOS_DSCP_AF31  0x68
#define IPTOS_DSCP_AF32  0x70
#define IPTOS_DSCP_AF33  0x78
#define IPTOS_DSCP_AF41  0x88
#define IPTOS_DSCP_AF42  0x90
#define IPTOS_DSCP_AF43  0x98
#define IPTOS_DSCP_EF  0xb8
#endif

#ifndef IPTOS_DSCP_CS0
#define IPTOS_DSCP_CS0  0x00
#define IPTOS_DSCP_CS1  0x20
#define IPTOS_DSCP_CS2  0x40
#define IPTOS_DSCP_CS3  0x60
#define IPTOS_DSCP_CS4  0x80
#define IPTOS_DSCP_CS5  0xa0
#define IPTOS_DSCP_CS6  0xc0
#define IPTOS_DSCP_CS7  0xe0
#endif

#ifndef IPTOS_DSCP_EF
#define IPTOS_DSCP_EF  0xb8
#endif

#ifndef IPTOS_DSCP_LE
#define IPTOS_DSCP_LE  0x04
#endif

const std::map<const std::string, int> DSCPvalues {
    { "none", INT_MAX },    // CS0 has value 0 but we want 'none' to mean OS default
    { "",     INT_MAX },    // an alias to 'none'
    { "af11", IPTOS_DSCP_AF11 },
    { "af12", IPTOS_DSCP_AF12 },
    { "af13", IPTOS_DSCP_AF13 },
    { "af21", IPTOS_DSCP_AF21 },
    { "af22", IPTOS_DSCP_AF22 },
    { "af23", IPTOS_DSCP_AF23 },
    { "af31", IPTOS_DSCP_AF31 },
    { "af32", IPTOS_DSCP_AF32 },
    { "af33", IPTOS_DSCP_AF33 },
    { "af41", IPTOS_DSCP_AF41 },
    { "af42", IPTOS_DSCP_AF42 },
    { "af43", IPTOS_DSCP_AF43 },
    { "cs0",  IPTOS_DSCP_CS0 },
    { "cs1",  IPTOS_DSCP_CS1 },
    { "cs2",  IPTOS_DSCP_CS2 },
    { "cs3",  IPTOS_DSCP_CS3 },
    { "cs4",  IPTOS_DSCP_CS4 },
    { "cs5",  IPTOS_DSCP_CS5 },
    { "cs6",  IPTOS_DSCP_CS6 },
    { "cs7",  IPTOS_DSCP_CS7 },
    { "ef",   IPTOS_DSCP_EF },
    { "le",   IPTOS_DSCP_LE }
};

struct tr_session;

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppressMsgs);

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t bound, tr_address* setme_addr, tr_port* setme_port);

void tr_netSetDSCP(tr_socket_t s, int dscp, tr_address_type type);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

void tr_netClose(tr_session* session, tr_socket_t s);

void tr_netCloseSocket(tr_socket_t fd);

bool tr_net_hasIPv6(tr_port);

/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
char* tr_net_strerror(char* buf, size_t buflen, int err);

unsigned char const* tr_globalIPv6(tr_session const* session);
