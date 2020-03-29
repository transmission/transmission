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

#include <errno.h>
#include <string.h>

#include <sys/types.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h> /* TCP_CONGESTION */
#endif

#include <event2/util.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "fdlimit.h" /* tr_fdSocketClose() */
#include "log.h"
#include "net.h"
#include "peer-io.h" /* tr_peerIoAddrStr() FIXME this should be moved to net.h */
#include "session.h" /* tr_sessionGetPublicAddress() */
#include "tr-assert.h"
#include "tr-utp.h" /* tr_utpSendTo() */
#include "utils.h" /* tr_time(), tr_logAddDebug() */

#ifndef IN_MULTICAST
#define IN_MULTICAST(a) (((a) & 0xf0000000) == 0xe0000000)
#endif

tr_address const tr_in6addr_any =
{
    .type = TR_AF_INET6,
    .addr.addr6 = IN6ADDR_ANY_INIT
};

tr_address const tr_inaddr_any =
{
    .type = TR_AF_INET,
    .addr.addr4.s_addr = INADDR_ANY
};

char* tr_net_strerror(char* buf, size_t buflen, int err)
{
    *buf = '\0';

#ifdef _WIN32

    DWORD len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, err, 0, buf, buflen, NULL);

    while (len > 0 && buf[len - 1] >= '\0' && buf[len - 1] <= ' ')
    {
        buf[--len] = '\0';
    }

#else

    tr_strlcpy(buf, tr_strerror(err), buflen);

#endif

    return buf;
}

char const* tr_address_to_string_with_buf(tr_address const* addr, char* buf, size_t buflen)
{
    TR_ASSERT(tr_address_is_valid(addr));

    if (addr->type == TR_AF_INET)
    {
        return evutil_inet_ntop(AF_INET, &addr->addr, buf, buflen);
    }
    else
    {
        return evutil_inet_ntop(AF_INET6, &addr->addr, buf, buflen);
    }
}

/*
 * Non-threadsafe version of tr_address_to_string_with_buf()
 * and uses a static memory area for a buffer.
 * This function is suitable to be called from libTransmission's networking code,
 * which is single-threaded.
 */
char const* tr_address_to_string(tr_address const* addr)
{
    static char buf[INET6_ADDRSTRLEN];
    return tr_address_to_string_with_buf(addr, buf, sizeof(buf));
}

bool tr_address_from_string(tr_address* dst, char const* src)
{
    bool ok;

    if ((ok = evutil_inet_pton(AF_INET, src, &dst->addr) == 1))
    {
        dst->type = TR_AF_INET;
    }

    if (!ok) /* try IPv6 */
    {
        if ((ok = evutil_inet_pton(AF_INET6, src, &dst->addr) == 1))
        {
            dst->type = TR_AF_INET6;
        }
    }

    return ok;
}

/*
 * Compare two tr_address structures.
 * Returns:
 * <0 if a < b
 * >0 if a > b
 * 0  if a == b
 */
int tr_address_compare(tr_address const* a, tr_address const* b)
{
    static int const sizes[2] = { sizeof(struct in_addr), sizeof(struct in6_addr) };

    /* IPv6 addresses are always "greater than" IPv4 */
    if (a->type != b->type)
    {
        return a->type == TR_AF_INET ? 1 : -1;
    }

    return memcmp(&a->addr, &b->addr, sizes[a->type]);
}

/***********************************************************************
 * TCP sockets
 **********************************************************************/

void tr_netSetTOS(tr_socket_t s, int tos, tr_address_type type)
{
    if (type == TR_AF_INET)
    {
#if defined(IP_TOS) && !defined(_WIN32)

        if (setsockopt(s, IPPROTO_IP, IP_TOS, (void const*)&tos, sizeof(tos)) == -1)
        {
            char err_buf[512];
            tr_net_strerror(err_buf, sizeof(err_buf), sockerrno);
            tr_logAddNamedInfo("Net", "Can't set TOS '%d': %s", tos, err_buf);
        }

#else

        (void)s;
        (void)tos;

#endif
    }
    else if (type == TR_AF_INET6)
    {
#if defined(IPV6_TCLASS) && !defined(_WIN32)
        if (setsockopt(s, IPPROTO_IPV6, IPV6_TCLASS, (void const*)&tos, sizeof(tos)) == -1)
        {
            char err_buf[512];
            tr_net_strerror(err_buf, sizeof(err_buf), sockerrno);
            tr_logAddNamedInfo("Net", "Can't set IPv6 QoS '%d': %s", tos, err_buf);
        }

#else

        (void)s;
        (void)tos;

#endif
    }
    else
    {
        /* program should never reach here! */
        tr_logAddNamedInfo("Net", "Something goes wrong while setting TOS/Traffic-Class");
    }
}

void tr_netSetCongestionControl(tr_socket_t s, char const* algorithm)
{
#ifdef TCP_CONGESTION

    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, (void const*)algorithm, strlen(algorithm) + 1) == -1)
    {
        char err_buf[512];
        tr_logAddNamedInfo("Net", "Can't set congestion control algorithm '%s': %s", algorithm, tr_net_strerror(err_buf,
            sizeof(err_buf), sockerrno));
    }

#else

    (void)s;
    (void)algorithm;

#endif
}

bool tr_address_from_sockaddr_storage(tr_address* setme_addr, tr_port* setme_port, struct sockaddr_storage const* from)
{
    if (from->ss_family == AF_INET)
    {
        struct sockaddr_in* sin = (struct sockaddr_in*)from;
        setme_addr->type = TR_AF_INET;
        setme_addr->addr.addr4.s_addr = sin->sin_addr.s_addr;
        *setme_port = sin->sin_port;
        return true;
    }

    if (from->ss_family == AF_INET6)
    {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)from;
        setme_addr->type = TR_AF_INET6;
        setme_addr->addr.addr6 = sin6->sin6_addr;
        *setme_port = sin6->sin6_port;
        return true;
    }

    return false;
}

static socklen_t setup_sockaddr(tr_address const* addr, tr_port port, struct sockaddr_storage* sockaddr)
{
    TR_ASSERT(tr_address_is_valid(addr));

    if (addr->type == TR_AF_INET)
    {
        struct sockaddr_in sock4;
        memset(&sock4, 0, sizeof(sock4));
        sock4.sin_family = AF_INET;
        sock4.sin_addr.s_addr = addr->addr.addr4.s_addr;
        sock4.sin_port = port;
        memcpy(sockaddr, &sock4, sizeof(sock4));
        return sizeof(struct sockaddr_in);
    }
    else
    {
        struct sockaddr_in6 sock6;
        memset(&sock6, 0, sizeof(sock6));
        sock6.sin6_family = AF_INET6;
        sock6.sin6_port = port;
        sock6.sin6_flowinfo = 0;
        sock6.sin6_addr = addr->addr.addr6;
        memcpy(sockaddr, &sock6, sizeof(sock6));
        return sizeof(struct sockaddr_in6);
    }
}

struct tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const* addr, tr_port port, bool clientIsSeed)
{
    TR_ASSERT(tr_address_is_valid(addr));

    struct tr_peer_socket ret = TR_PEER_SOCKET_INIT;

    static int const domains[NUM_TR_AF_INET_TYPES] = { AF_INET, AF_INET6 };
    tr_socket_t s;
    struct sockaddr_storage sock;
    socklen_t addrlen;
    tr_address const* source_addr;
    socklen_t sourcelen;
    struct sockaddr_storage source_sock;
    char err_buf[512];

    if (!tr_address_is_valid_for_peers(addr, port))
    {
        return ret;
    }

    s = tr_fdSocketCreate(session, domains[addr->type], SOCK_STREAM);

    if (s == TR_BAD_SOCKET)
    {
        return ret;
    }

    /* seeds don't need much of a read buffer... */
    if (clientIsSeed)
    {
        int n = 8192;

        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, (void const*)&n, sizeof(n)) == -1)
        {
            tr_logAddInfo("Unable to set SO_RCVBUF on socket %" PRIdMAX ": %s", (intmax_t)s,
                tr_net_strerror(err_buf, sizeof(err_buf), sockerrno));
        }
    }

    if (evutil_make_socket_nonblocking(s) == -1)
    {
        tr_netClose(session, s);
        return ret;
    }

    addrlen = setup_sockaddr(addr, port, &sock);

    /* set source address */
    source_addr = tr_sessionGetPublicAddress(session, addr->type, NULL);
    TR_ASSERT(source_addr != NULL);
    sourcelen = setup_sockaddr(source_addr, 0, &source_sock);

    if (bind(s, (struct sockaddr*)&source_sock, sourcelen) == -1)
    {
        tr_logAddError(_("Couldn't set source address %s on %" PRIdMAX ": %s"), tr_address_to_string(source_addr), (intmax_t)s,
            tr_net_strerror(err_buf, sizeof(err_buf), sockerrno));
        tr_netClose(session, s);
        return ret;
    }

    if (connect(s, (struct sockaddr*)&sock, addrlen) == -1 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        int const tmperrno = sockerrno;

        if ((tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr->type == TR_AF_INET)
        {
            tr_logAddError(_("Couldn't connect socket %" PRIdMAX " to %s, port %d (errno %d - %s)"), (intmax_t)s,
                tr_address_to_string(addr), (int)ntohs(port), tmperrno, tr_net_strerror(err_buf, sizeof(err_buf), tmperrno));
        }

        tr_netClose(session, s);
    }
    else
    {
        ret = tr_peer_socket_tcp_create(s);
    }

    tr_logAddDeep(__FILE__, __LINE__, NULL, "New OUTGOING connection %" PRIdMAX " (%s)", (intmax_t)s,
        tr_peerIoAddrStr(addr, port));

    return ret;
}

struct tr_peer_socket tr_netOpenPeerUTPSocket(tr_session* session, tr_address const* addr, tr_port port,
    bool clientIsSeed UNUSED)
{
    struct tr_peer_socket ret = TR_PEER_SOCKET_INIT;

    if (tr_address_is_valid_for_peers(addr, port))
    {
        struct sockaddr_storage ss;
        socklen_t const sslen = setup_sockaddr(addr, port, &ss);
        struct UTPSocket* const socket = UTP_Create(tr_utpSendTo, session, (struct sockaddr*)&ss, sslen);

        if (socket != NULL)
        {
            ret = tr_peer_socket_utp_create(socket);
        }
    }

    return ret;
}

static tr_socket_t tr_netBindTCPImpl(tr_address const* addr, tr_port port, bool suppressMsgs, int* errOut)
{
    TR_ASSERT(tr_address_is_valid(addr));

    static int const domains[NUM_TR_AF_INET_TYPES] = { AF_INET, AF_INET6 };
    struct sockaddr_storage sock;
    tr_socket_t fd;
    int addrlen;
    int optval;

    fd = socket(domains[addr->type], SOCK_STREAM, 0);

    if (fd == TR_BAD_SOCKET)
    {
        *errOut = sockerrno;
        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(fd) == -1)
    {
        *errOut = sockerrno;
        tr_netCloseSocket(fd);
        return TR_BAD_SOCKET;
    }

    optval = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void const*)&optval, sizeof(optval));
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void const*)&optval, sizeof(optval));

#ifdef IPV6_V6ONLY

    if (addr->type == TR_AF_INET6)
    {
        if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (void const*)&optval, sizeof(optval)) == -1)
        {
            if (sockerrno != ENOPROTOOPT) /* if the kernel doesn't support it, ignore it */
            {
                *errOut = sockerrno;
                tr_netCloseSocket(fd);
                return TR_BAD_SOCKET;
            }
        }
    }

#endif

    addrlen = setup_sockaddr(addr, htons(port), &sock);

    if (bind(fd, (struct sockaddr*)&sock, addrlen) == -1)
    {
        int const err = sockerrno;

        if (!suppressMsgs)
        {
            char const* fmt;
            char const* hint;
            char err_buf[512];

            if (err == EADDRINUSE)
            {
                hint = _("Is another copy of Transmission already running?");
            }
            else
            {
                hint = NULL;
            }

            if (hint == NULL)
            {
                fmt = _("Couldn't bind port %d on %s: %s");
            }
            else
            {
                fmt = _("Couldn't bind port %d on %s: %s (%s)");
            }

            tr_logAddError(fmt, port, tr_address_to_string(addr), tr_net_strerror(err_buf, sizeof(err_buf), err), hint);
        }

        tr_netCloseSocket(fd);
        *errOut = err;
        return TR_BAD_SOCKET;
    }

    if (!suppressMsgs)
    {
        tr_logAddDebug("Bound socket %" PRIdMAX " to port %d on %s", (intmax_t)fd, port, tr_address_to_string(addr));
    }

#ifdef TCP_FASTOPEN

#ifndef SOL_TCP
#define SOL_TCP IPPROTO_TCP
#endif

    optval = 5;
    setsockopt(fd, SOL_TCP, TCP_FASTOPEN, (void const*)&optval, sizeof(optval));

#endif

    if (listen(fd, 128) == -1)
    {
        *errOut = sockerrno;
        tr_netCloseSocket(fd);
        return TR_BAD_SOCKET;
    }

    return fd;
}

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppressMsgs)
{
    int unused;
    return tr_netBindTCPImpl(addr, port, suppressMsgs, &unused);
}

bool tr_net_hasIPv6(tr_port port)
{
    static bool result = false;
    static bool alreadyDone = false;

    if (!alreadyDone)
    {
        int err;
        tr_socket_t fd = tr_netBindTCPImpl(&tr_in6addr_any, port, true, &err);

        if (fd != TR_BAD_SOCKET || err != EAFNOSUPPORT) /* we support ipv6 */
        {
            result = true;
        }

        if (fd != TR_BAD_SOCKET)
        {
            tr_netCloseSocket(fd);
        }

        alreadyDone = true;
    }

    return result;
}

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t b, tr_address* addr, tr_port* port)
{
    tr_socket_t fd = tr_fdSocketAccept(session, b, addr, port);

    if (fd != TR_BAD_SOCKET && evutil_make_socket_nonblocking(fd) == -1)
    {
        tr_netClose(session, fd);
        fd = TR_BAD_SOCKET;
    }

    return fd;
}

void tr_netCloseSocket(tr_socket_t fd)
{
    evutil_closesocket(fd);
}

void tr_netClose(tr_session* session, tr_socket_t s)
{
    tr_fdSocketClose(session, s);
}

/*
   get_source_address() and global_unicast_address() were written by
   Juliusz Chroboczek, and are covered under the same license as dht.c.
   Please feel free to copy them into your software if it can help
   unbreaking the double-stack Internet. */

/* Get the source address used for a given destination address. Since
   there is no official interface to get this information, we create
   a connected UDP socket (connected UDP... hmm...) and check its source
   address. */
static int get_source_address(struct sockaddr const* dst, socklen_t dst_len, struct sockaddr* src, socklen_t* src_len)
{
    tr_socket_t s;
    int rc;
    int save;

    s = socket(dst->sa_family, SOCK_DGRAM, 0);

    if (s == TR_BAD_SOCKET)
    {
        goto fail;
    }

    /* Since it's a UDP socket, this doesn't actually send any packets. */
    rc = connect(s, dst, dst_len);

    if (rc == -1)
    {
        goto fail;
    }

    rc = getsockname(s, src, src_len);

    if (rc == -1)
    {
        goto fail;
    }

    evutil_closesocket(s);

    return rc;

fail:
    save = errno;
    evutil_closesocket(s);
    errno = save;
    return -1;
}

/* We all hate NATs. */
static int global_unicast_address(struct sockaddr_storage* ss)
{
    if (ss->ss_family == AF_INET)
    {
        unsigned char const* a = (unsigned char*)&((struct sockaddr_in*)ss)->sin_addr;

        if (a[0] == 0 || a[0] == 127 || a[0] >= 224 || a[0] == 10 || (a[0] == 172 && a[1] >= 16 && a[1] <= 31) ||
            (a[0] == 192 && a[1] == 168))
        {
            return 0;
        }

        return 1;
    }
    else if (ss->ss_family == AF_INET6)
    {
        unsigned char const* a = (unsigned char*)&((struct sockaddr_in6*)ss)->sin6_addr;
        /* 2000::/3 */
        return (a[0] & 0xE0) == 0x20 ? 1 : 0;
    }
    else
    {
        errno = EAFNOSUPPORT;
        return -1;
    }
}

static int tr_globalAddress(int af, void* addr, int* addr_len)
{
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    struct sockaddr* sa;
    socklen_t salen;
    int rc;

    switch (af)
    {
    case AF_INET:
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        evutil_inet_pton(AF_INET, "91.121.74.28", &sin.sin_addr);
        sin.sin_port = htons(6969);
        sa = (struct sockaddr*)&sin;
        salen = sizeof(sin);
        break;

    case AF_INET6:
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        /* In order for address selection to work right, this should be
           a native IPv6 address, not Teredo or 6to4. */
        evutil_inet_pton(AF_INET6, "2001:1890:1112:1::20", &sin6.sin6_addr);
        sin6.sin6_port = htons(6969);
        sa = (struct sockaddr*)&sin6;
        salen = sizeof(sin6);
        break;

    default:
        return -1;
    }

    rc = get_source_address(sa, salen, (struct sockaddr*)&ss, &sslen);

    if (rc < 0)
    {
        return -1;
    }

    if (global_unicast_address(&ss) == 0)
    {
        return -1;
    }

    switch (af)
    {
    case AF_INET:
        if (*addr_len < 4)
        {
            return -1;
        }

        memcpy(addr, &((struct sockaddr_in*)&ss)->sin_addr, 4);
        *addr_len = 4;
        return 1;

    case AF_INET6:
        if (*addr_len < 16)
        {
            return -1;
        }

        memcpy(addr, &((struct sockaddr_in6*)&ss)->sin6_addr, 16);
        *addr_len = 16;
        return 1;

    default:
        return -1;
    }
}

/* Return our global IPv6 address, with caching. */
unsigned char const* tr_globalIPv6(void)
{
    static unsigned char ipv6[16];
    static time_t last_time = 0;
    static bool have_ipv6 = false;
    time_t const now = tr_time();

    /* Re-check every half hour */
    if (last_time < now - 1800)
    {
        int addrlen = 16;
        int const rc = tr_globalAddress(AF_INET6, ipv6, &addrlen);
        have_ipv6 = rc >= 0 && addrlen == 16;
        last_time = now;
    }

    return have_ipv6 ? ipv6 : NULL;
}

/***
****
****
***/

static bool isIPv4MappedAddress(tr_address const* addr)
{
    return addr->type == TR_AF_INET6 && IN6_IS_ADDR_V4MAPPED(&addr->addr.addr6);
}

static bool isIPv6LinkLocalAddress(tr_address const* addr)
{
    return addr->type == TR_AF_INET6 && IN6_IS_ADDR_LINKLOCAL(&addr->addr.addr6);
}

/* isMartianAddr was written by Juliusz Chroboczek,
   and is covered under the same license as third-party/dht/dht.c. */
static bool isMartianAddr(struct tr_address const* a)
{
    TR_ASSERT(tr_address_is_valid(a));

    static unsigned char const zeroes[16] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

    switch (a->type)
    {
    case TR_AF_INET:
        {
            unsigned char const* address = (unsigned char const*)&a->addr.addr4;
            return address[0] == 0 || address[0] == 127 || (address[0] & 0xE0) == 0xE0;
        }

    case TR_AF_INET6:
        {
            unsigned char const* address = (unsigned char const*)&a->addr.addr6;
            return address[0] == 0xFF || (memcmp(address, zeroes, 15) == 0 && (address[15] == 0 || address[15] == 1));
        }

    default:
        return true;
    }
}

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port)
{
    return port != 0 && tr_address_is_valid(addr) && !isIPv6LinkLocalAddress(addr) && !isIPv4MappedAddress(addr) &&
        !isMartianAddr(addr);
}
