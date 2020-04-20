/******************************************************************************
 * Copyright (c) Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifdef _WIN32
#include <inttypes.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#ifdef _WIN32
typedef SOCKET tr_socket_t;
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
typedef int tr_socket_t;
/** @brief Platform-specific invalid socket descriptor constant. */
#define TR_BAD_SOCKET (-1)

#define sockerrno errno
#endif

/****
*****
*****  tr_address
*****
****/

typedef enum tr_address_type
{
    TR_AF_INET,
    TR_AF_INET6,
    NUM_TR_AF_INET_TYPES
}
tr_address_type;

typedef struct tr_address
{
    tr_address_type type;
    union
    {
        struct in6_addr addr6;
        struct in_addr addr4;
    }
    addr;
}
tr_address;

extern tr_address const tr_inaddr_any;
extern tr_address const tr_in6addr_any;

char const* tr_address_to_string(tr_address const* addr);

char const* tr_address_to_string_with_buf(tr_address const* addr, char* buf, size_t buflen);

bool tr_address_from_string(tr_address* setme, char const* string);

bool tr_address_from_sockaddr_storage(tr_address* setme, tr_port* port, struct sockaddr_storage const* src);

int tr_address_compare(tr_address const* a, tr_address const* b);

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port);

static inline bool tr_address_is_valid(tr_address const* a)
{
    return a != NULL && (a->type == TR_AF_INET || a->type == TR_AF_INET6);
}

/***********************************************************************
 * Sockets
 **********************************************************************/

/* https://en.wikipedia.org/wiki/Differentiated_services#Class_Selector */
enum
{
    TR_IPTOS_LOWCOST = 0x38, /* AF13: low prio, high drop */
    TR_IPTOS_LOWDELAY = 0x70, /* AF32: high prio, mid drop */
    TR_IPTOS_THRUPUT = 0x20, /* CS1: low prio, undef drop */
    TR_IPTOS_RELIABLE = 0x28 /* AF11: low prio, low drop */
};

struct tr_session;

struct tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const* addr, tr_port port, bool clientIsSeed);

struct tr_peer_socket tr_netOpenPeerUTPSocket(tr_session* session, tr_address const* addr, tr_port port, bool clientIsSeed);

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppressMsgs);

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t bound, tr_address* setme_addr, tr_port* setme_port);

void tr_netSetTOS(tr_socket_t s, int tos, tr_address_type type);

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm);

void tr_netClose(tr_session* session, tr_socket_t s);

void tr_netCloseSocket(tr_socket_t fd);

bool tr_net_hasIPv6(tr_port);

/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
char* tr_net_strerror(char* buf, size_t buflen, int err);

unsigned char const* tr_globalIPv6(void);
