/******************************************************************************
 * $Id$
 *
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

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef _TR_NET_H_
#define _TR_NET_H_

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
 #define TR_PRI_SOCK "Id" /* intentionally signed to print -1 nicely. */

 #undef  EADDRINUSE
 #define EADDRINUSE              WSAEADDRINUSE
 #undef  ECONNREFUSED
 #define ECONNREFUSED            WSAECONNREFUSED
 #undef  ECONNRESET
 #define ECONNRESET              WSAECONNRESET
 #undef  EHOSTUNREACH
 #define EHOSTUNREACH            WSAEHOSTUNREACH
 #undef  EINPROGRESS
 #define EINPROGRESS             WSAEINPROGRESS
 #undef  ENOTCONN
 #define ENOTCONN                WSAENOTCONN
 #undef  EWOULDBLOCK
 #define EWOULDBLOCK             WSAEWOULDBLOCK
 #undef  EAFNOSUPPORT
 #define EAFNOSUPPORT            WSAEAFNOSUPPORT
 #undef  ENETUNREACH
 #define ENETUNREACH             WSAENETUNREACH

 #define sockerrno               WSAGetLastError ()
#else
 /** @brief Platform-specific socket descriptor type. */
 typedef int tr_socket_t;
 /** @brief Platform-specific invalid socket descriptor constant. */
 #define TR_BAD_SOCKET (-1)
 #define TR_PRI_SOCK "d"

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
    union {
        /* The order here is important for tr_in{,6}addr_any initialization,
         * since we can't use C99 designated initializers */
        struct in6_addr addr6;
        struct in_addr  addr4;
    } addr;
} tr_address;

extern const tr_address tr_inaddr_any;
extern const tr_address tr_in6addr_any;

const char* tr_address_to_string (const tr_address * addr);

const char* tr_address_to_string_with_buf (const tr_address  * addr,
                                           char              * buf,
                                           size_t              buflen);

bool tr_address_from_string (tr_address  * setme,
                              const char  * string);

bool tr_address_from_sockaddr_storage (tr_address                     * setme,
                                       tr_port                        * port,
                                       const struct sockaddr_storage  * src);

int tr_address_compare (const tr_address * a,
                        const tr_address * b);

bool tr_address_is_valid_for_peers (const tr_address  * addr,
                                    tr_port             port);

static inline bool
tr_address_is_valid (const tr_address * a)
{
    return (a != NULL) && (a->type==TR_AF_INET || a->type==TR_AF_INET6);
}

/***********************************************************************
 * Sockets
 **********************************************************************/

struct tr_session;

tr_socket_t tr_netOpenPeerSocket (tr_session       * session,
                                  const tr_address * addr,
                                  tr_port            port,
                                  bool               clientIsSeed);

struct UTPSocket *
tr_netOpenPeerUTPSocket (tr_session        * session,
                         const tr_address  * addr,
                         tr_port             port,
                         bool                clientIsSeed);

tr_socket_t tr_netBindTCP (const tr_address * addr,
                           tr_port            port,
                           bool               suppressMsgs);

tr_socket_t tr_netAccept (tr_session  * session,
                          tr_socket_t   bound,
                          tr_address  * setme_addr,
                          tr_port     * setme_port);

void tr_netSetTOS (tr_socket_t s,
                   int         tos);

void tr_netSetCongestionControl (tr_socket_t   s,
                                 const char  * algorithm);

void tr_netClose (tr_session  * session,
                  tr_socket_t   s);

void tr_netCloseSocket (tr_socket_t fd);

bool tr_net_hasIPv6 (tr_port);


/**
 * @brief get a human-representable string representing the network error.
 * @param err an errno on Unix/Linux and an WSAError on win32)
 */
char* tr_net_strerror (char * buf, size_t buflen, int err);

const unsigned char *tr_globalIPv6 (void);

#endif /* _TR_NET_H_ */
