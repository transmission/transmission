// This file Copyright © 2010-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iterator> // std::back_inserter
#include <string_view>
#include <utility> // std::pair

#include <sys/types.h>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/tcp.h> /* TCP_CONGESTION */
#endif

#include <event2/util.h>

#include <fmt/core.h>

#include <libutp/utp.h>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "peer-socket.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-macros.h"
#include "tr-utp.h"
#include "utils.h"

#ifndef IN_MULTICAST
#define IN_MULTICAST(a) (((a)&0xf0000000) == 0xe0000000)
#endif

tr_address const tr_in6addr_any = { TR_AF_INET6, { IN6ADDR_ANY_INIT } };

tr_address const tr_inaddr_any = { TR_AF_INET, { { { { INADDR_ANY } } } } };

std::string tr_net_strerror(int err)
{
#ifdef _WIN32

    auto buf = std::array<char, 512>{};
    auto const len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, err, 0, std::data(buf), std::size(buf), nullptr);
    return std::string{ tr_strvStrip(std::data(buf)) };

#else

    return std::string{ tr_strerror(err) };

#endif
}

char const* tr_address_and_port_to_string(char* buf, size_t buflen, tr_address const* addr, tr_port port)
{
    char addr_buf[INET6_ADDRSTRLEN];
    tr_address_to_string_with_buf(addr, addr_buf, sizeof(addr_buf));
    *fmt::format_to_n(buf, buflen - 1, FMT_STRING("[{:s}]:{:d}"), addr_buf, port.host()).out = '\0';
    return buf;
}

char const* tr_address_to_string_with_buf(tr_address const* addr, char* buf, size_t buflen)
{
    TR_ASSERT(tr_address_is_valid(addr));

    return addr->type == TR_AF_INET ? evutil_inet_ntop(AF_INET, &addr->addr, buf, buflen) :
                                      evutil_inet_ntop(AF_INET6, &addr->addr, buf, buflen);
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
    if (evutil_inet_pton(AF_INET, src, &dst->addr) == 1)
    {
        dst->type = TR_AF_INET;
        return true;
    }

    if (evutil_inet_pton(AF_INET6, src, &dst->addr) == 1)
    {
        dst->type = TR_AF_INET6;
        return true;
    }

    return false;
}

bool tr_address_from_string(tr_address* dst, std::string_view src)
{
    // inet_pton() requires zero-terminated strings,
    // so make a zero-terminated copy here on the stack.
    auto buf = std::array<char, TR_ADDRSTRLEN>{};
    if (std::size(src) >= std::size(buf))
    {
        // shouldn't ever be that large; malformed address
        return false;
    }

    *std::copy(std::begin(src), std::end(src), std::begin(buf)) = '\0';

    return tr_address_from_string(dst, std::data(buf));
}

/*
 * Compare two tr_address structures.
 * Returns:
 * <0 if a < b
 * >0 if a > b
 * 0  if a == b
 */
int tr_address_compare(tr_address const* a, tr_address const* b) noexcept
{
    // IPv6 addresses are always "greater than" IPv4
    if (a->type != b->type)
    {
        return a->type == TR_AF_INET ? 1 : -1;
    }

    return a->type == TR_AF_INET ? memcmp(&a->addr.addr4, &b->addr.addr4, sizeof(a->addr.addr4)) :
                                   memcmp(&a->addr.addr6.s6_addr, &b->addr.addr6.s6_addr, sizeof(a->addr.addr6.s6_addr));
}

/***********************************************************************
 * TCP sockets
 **********************************************************************/

// RFCs 2474, 3246, 4594 & 8622
// Service class names are defined in RFC 4594, RFC 5865, and RFC 8622.
// Not all platforms have these IPTOS_ definitions, so hardcode them here
static auto constexpr IpTosNames = std::array<std::pair<int, std::string_view>, 28>{ {
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

std::string tr_netTosToName(int tos)
{
    auto const test = [tos](auto const& pair)
    {
        return pair.first == tos;
    };
    auto const it = std::find_if(std::begin(IpTosNames), std::end(IpTosNames), test);
    return it == std::end(IpTosNames) ? std::to_string(tos) : std::string{ it->second };
}

std::optional<int> tr_netTosFromName(std::string_view name)
{
    auto const test = [&name](auto const& pair)
    {
        return pair.second == name;
    };
    auto const it = std::find_if(std::begin(IpTosNames), std::end(IpTosNames), test);
    return it != std::end(IpTosNames) ? it->first : tr_parseNum<int>(name);
}

void tr_netSetTOS([[maybe_unused]] tr_socket_t s, [[maybe_unused]] int tos, tr_address_type type)
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

void tr_netSetCongestionControl([[maybe_unused]] tr_socket_t s, [[maybe_unused]] char const* algorithm)
{
#ifdef TCP_CONGESTION

    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, (void const*)algorithm, strlen(algorithm) + 1) == -1)
    {
        tr_logAddDebug(fmt::format("Can't set congestion control algorithm '{}': {}", algorithm, tr_net_strerror(sockerrno)));
    }

#endif
}

bool tr_address_from_sockaddr_storage(tr_address* setme_addr, tr_port* setme_port, struct sockaddr_storage const* from)
{
    if (from->ss_family == AF_INET)
    {
        auto const* const sin = (struct sockaddr_in const*)from;
        setme_addr->type = TR_AF_INET;
        setme_addr->addr.addr4.s_addr = sin->sin_addr.s_addr;
        *setme_port = tr_port::fromNetwork(sin->sin_port);
        return true;
    }

    if (from->ss_family == AF_INET6)
    {
        auto const* const sin6 = (struct sockaddr_in6 const*)from;
        setme_addr->type = TR_AF_INET6;
        setme_addr->addr.addr6 = sin6->sin6_addr;
        *setme_port = tr_port::fromNetwork(sin6->sin6_port);
        return true;
    }

    return false;
}

static socklen_t setup_sockaddr(tr_address const* addr, tr_port port, struct sockaddr_storage* sockaddr)
{
    TR_ASSERT(tr_address_is_valid(addr));

    if (addr->type == TR_AF_INET)
    {
        sockaddr_in sock4 = {};
        sock4.sin_family = AF_INET;
        sock4.sin_addr.s_addr = addr->addr.addr4.s_addr;
        sock4.sin_port = port.network();
        memcpy(sockaddr, &sock4, sizeof(sock4));
        return sizeof(struct sockaddr_in);
    }

    sockaddr_in6 sock6 = {};
    sock6.sin6_family = AF_INET6;
    sock6.sin6_port = port.network();
    sock6.sin6_flowinfo = 0;
    sock6.sin6_addr = addr->addr.addr6;
    memcpy(sockaddr, &sock6, sizeof(sock6));
    return sizeof(struct sockaddr_in6);
}

static tr_socket_t createSocket(tr_session* session, int domain, int type)
{
    TR_ASSERT(tr_isSession(session));

    auto const sockfd = socket(domain, type, 0);
    if (sockfd == TR_BAD_SOCKET)
    {
        if (sockerrno != EAFNOSUPPORT)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't create socket: {error} ({error_code})"),
                fmt::arg("error", tr_net_strerror(sockerrno)),
                fmt::arg("error_code", sockerrno)));
        }

        return TR_BAD_SOCKET;
    }

    if ((evutil_make_socket_nonblocking(sockfd) == -1) || !session->incPeerCount())
    {
        tr_netClose(session, sockfd);
        return TR_BAD_SOCKET;
    }

    if (static bool buf_logged = false; !buf_logged)
    {
        int i = 0;
        socklen_t size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_SNDBUF size is {}", i));
        }

        i = 0;
        size = sizeof(i);

        if (getsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&i), &size) != -1)
        {
            tr_logAddTrace(fmt::format("SO_RCVBUF size is {}", i));
        }

        buf_logged = true;
    }

    return sockfd;
}

struct tr_peer_socket tr_netOpenPeerSocket(tr_session* session, tr_address const* addr, tr_port port, bool client_is_seed)
{
    TR_ASSERT(tr_address_is_valid(addr));

    if (!tr_address_is_valid_for_peers(addr, port))
    {
        return {};
    }

    static auto constexpr Domains = std::array<int, NUM_TR_AF_INET_TYPES>{ AF_INET, AF_INET6 };
    auto const s = createSocket(session, Domains[addr->type], SOCK_STREAM);
    if (s == TR_BAD_SOCKET)
    {
        return {};
    }

    // seeds don't need a big read buffer, so make it smaller
    if (client_is_seed)
    {
        int n = 8192;

        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&n), sizeof(n)) == -1)
        {
            tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF on socket {}: {}", s, tr_net_strerror(sockerrno)));
        }
    }

    struct sockaddr_storage sock;
    socklen_t const addrlen = setup_sockaddr(addr, port, &sock);

    // set source address
    tr_address const* const source_addr = tr_sessionGetPublicAddress(session, addr->type, nullptr);
    TR_ASSERT(source_addr != nullptr);
    struct sockaddr_storage source_sock;
    socklen_t const sourcelen = setup_sockaddr(source_addr, {}, &source_sock);

    if (bind(s, (struct sockaddr*)&source_sock, sourcelen) == -1)
    {
        tr_logAddWarn(fmt::format(
            _("Couldn't set source address {address} on {socket}: {error} ({error_code})"),
            fmt::arg("address", source_addr->readable()),
            fmt::arg("socket", s),
            fmt::arg("error", tr_net_strerror(sockerrno)),
            fmt::arg("error_code", sockerrno)));
        tr_netClose(session, s);
        return {};
    }

    auto ret = tr_peer_socket{};
    if (connect(s, (struct sockaddr*)&sock, addrlen) == -1 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        int const tmperrno = sockerrno;

        if ((tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr->type == TR_AF_INET)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't connect socket {socket} to {address}:{port}: {error} ({error_code})"),
                fmt::arg("socket", s),
                fmt::arg("address", addr->readable()),
                fmt::arg("port", port.host()),
                fmt::arg("error", tr_net_strerror(tmperrno)),
                fmt::arg("error_code", tmperrno)));
        }

        tr_netClose(session, s);
    }
    else
    {
        ret = tr_peer_socket_tcp_create(s);
    }

    char addrstr[TR_ADDRSTRLEN];
    tr_address_and_port_to_string(addrstr, sizeof(addrstr), addr, port);
    tr_logAddTrace(fmt::format("New OUTGOING connection {} ({})", s, addrstr));

    return ret;
}

struct tr_peer_socket tr_netOpenPeerUTPSocket(
    tr_session* session,
    tr_address const* addr,
    tr_port port,
    bool /*client_is_seed*/)
{
    auto ret = tr_peer_socket{};

    if (session->utp_context != nullptr && tr_address_is_valid_for_peers(addr, port))
    {
        struct sockaddr_storage ss;
        socklen_t const sslen = setup_sockaddr(addr, port, &ss);
        auto* const socket = utp_create_socket(session->utp_context);

        if (socket != nullptr)
        {
            if (utp_connect(socket, reinterpret_cast<sockaddr*>(&ss), sslen) != -1)
            {
                ret = tr_peer_socket_utp_create(socket);
            }
            else
            {
                utp_close(socket);
            }
        }
    }

    return ret;
}

void tr_netClosePeerSocket(tr_session* session, tr_peer_socket socket)
{
    switch (socket.type)
    {
    case TR_PEER_SOCKET_TYPE_NONE:
        break;

    case TR_PEER_SOCKET_TYPE_TCP:
        tr_netClose(session, socket.handle.tcp);
        break;

#ifdef WITH_UTP
    case TR_PEER_SOCKET_TYPE_UTP:
        utp_set_userdata(socket.handle.utp, nullptr);
        utp_close(socket.handle.utp);
        break;
#endif

    default:
        TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unsupported peer socket type {:d}"), socket.type));
    }
}

static tr_socket_t tr_netBindTCPImpl(tr_address const* addr, tr_port port, bool suppressMsgs, int* errOut)
{
    TR_ASSERT(tr_address_is_valid(addr));

    static int const domains[NUM_TR_AF_INET_TYPES] = { AF_INET, AF_INET6 };
    struct sockaddr_storage sock;

    auto const fd = socket(domains[addr->type], SOCK_STREAM, 0);
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

    int optval = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<char const*>(&optval), sizeof(optval));
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));

#ifdef IPV6_V6ONLY

    if ((addr->type == TR_AF_INET6) &&
        (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&optval), sizeof(optval)) == -1) &&
        (sockerrno != ENOPROTOOPT)) // if the kernel doesn't support it, ignore it
    {
        *errOut = sockerrno;
        tr_netCloseSocket(fd);
        return TR_BAD_SOCKET;
    }

#endif

    int const addrlen = setup_sockaddr(addr, port, &sock);

    if (bind(fd, (struct sockaddr*)&sock, addrlen) == -1)
    {
        int const err = sockerrno;

        if (!suppressMsgs)
        {
            tr_logAddError(fmt::format(
                err == EADDRINUSE ?
                    _("Couldn't bind port {port} on {address}: {error} ({error_code}) -- Is another copy of Transmission already running?") :
                    _("Couldn't bind port {port} on {address}: {error} ({error_code})"),
                fmt::arg("address", addr->readable()),
                fmt::arg("port", port.host()),
                fmt::arg("error", tr_net_strerror(err)),
                fmt::arg("error_code", err)));
        }

        tr_netCloseSocket(fd);
        *errOut = err;
        return TR_BAD_SOCKET;
    }

    if (!suppressMsgs)
    {
        tr_logAddDebug(fmt::format(FMT_STRING("Bound socket {:d} to port {:d} on {:s}"), fd, port.host(), addr->readable()));
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
        *errOut = sockerrno;
        tr_netCloseSocket(fd);
        return TR_BAD_SOCKET;
    }

    return fd;
}

tr_socket_t tr_netBindTCP(tr_address const* addr, tr_port port, bool suppressMsgs)
{
    int unused = 0;
    return tr_netBindTCPImpl(addr, port, suppressMsgs, &unused);
}

bool tr_net_hasIPv6(tr_port port)
{
    static bool result = false;
    static bool alreadyDone = false;

    if (!alreadyDone)
    {
        int err = 0;
        auto const fd = tr_netBindTCPImpl(&tr_in6addr_any, port, true, &err);

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

tr_socket_t tr_netAccept(tr_session* session, tr_socket_t listening_sockfd, tr_address* addr, tr_port* port)
{
    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(addr != nullptr);
    TR_ASSERT(port != nullptr);

    // accept the incoming connection
    struct sockaddr_storage sock;
    socklen_t len = sizeof(struct sockaddr_storage);
    auto const sockfd = accept(listening_sockfd, (struct sockaddr*)&sock, &len);
    if (sockfd == TR_BAD_SOCKET)
    {
        return TR_BAD_SOCKET;
    }

    // get the address and port,
    // make the socket unblocking,
    // and confirm we don't have too many peers
    if (!tr_address_from_sockaddr_storage(addr, port, &sock) || evutil_make_socket_nonblocking(sockfd) == -1 ||
        !session->incPeerCount())
    {
        tr_netCloseSocket(sockfd);
        return TR_BAD_SOCKET;
    }

    return sockfd;
}

void tr_netCloseSocket(tr_socket_t sockfd)
{
    evutil_closesocket(sockfd);
}

void tr_netClose(tr_session* session, tr_socket_t sockfd)
{
    tr_netCloseSocket(sockfd);
    session->decPeerCount();
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
    tr_socket_t const s = socket(dst->sa_family, SOCK_DGRAM, 0);
    if (s == TR_BAD_SOCKET)
    {
        return -1;
    }

    // since it's a UDP socket, this doesn't actually send any packets
    if (connect(s, dst, dst_len) == 0 && getsockname(s, src, src_len) == 0)
    {
        evutil_closesocket(s);
        return 0;
    }

    auto const save = errno;
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

    if (ss->ss_family == AF_INET6)
    {
        unsigned char const* a = (unsigned char*)&((struct sockaddr_in6*)ss)->sin6_addr;
        /* 2000::/3 */
        return (a[0] & 0xE0) == 0x20 ? 1 : 0;
    }

    errno = EAFNOSUPPORT;
    return -1;
}

static int tr_globalAddress(int af, void* addr, int* addr_len)
{
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
    struct sockaddr const* sa = nullptr;
    socklen_t salen = 0;

    switch (af)
    {
    case AF_INET:
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        evutil_inet_pton(AF_INET, "91.121.74.28", &sin.sin_addr);
        sin.sin_port = htons(6969);
        sa = (struct sockaddr const*)&sin;
        salen = sizeof(sin);
        break;

    case AF_INET6:
        memset(&sin6, 0, sizeof(sin6));
        sin6.sin6_family = AF_INET6;
        /* In order for address selection to work right, this should be
           a native IPv6 address, not Teredo or 6to4. */
        evutil_inet_pton(AF_INET6, "2001:1890:1112:1::20", &sin6.sin6_addr);
        sin6.sin6_port = htons(6969);
        sa = (struct sockaddr const*)&sin6;
        salen = sizeof(sin6);
        break;

    default:
        return -1;
    }

    if (int const rc = get_source_address(sa, salen, (struct sockaddr*)&ss, &sslen); rc < 0)
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
unsigned char const* tr_globalIPv6(tr_session const* session)
{
    static unsigned char ipv6[16];
    static time_t last_time = 0;
    static bool have_ipv6 = false;

    /* Re-check every half hour */
    if (auto const now = tr_time(); last_time < now - 1800)
    {
        int addrlen = 16;
        int const rc = tr_globalAddress(AF_INET6, ipv6, &addrlen);
        have_ipv6 = rc >= 0 && addrlen == 16;
        last_time = now;
    }

    if (!have_ipv6)
    {
        return nullptr; /* No IPv6 address at all. */
    }

    /* Return the default address. This is useful for checking
       for connectivity in general. */
    if (session == nullptr)
    {
        return ipv6;
    }

    /* We have some sort of address, now make sure that we return
       our bound address if non-default. */

    bool is_default = false;
    auto const* ipv6_bindaddr = tr_sessionGetPublicAddress(session, TR_AF_INET6, &is_default);
    if (ipv6_bindaddr != nullptr && !is_default)
    {
        /* Explicitly bound. Return that address. */
        memcpy(ipv6, ipv6_bindaddr->addr.addr6.s6_addr, 16);
    }

    return ipv6;
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

    static unsigned char const zeroes[16] = {};

    switch (a->type)
    {
    case TR_AF_INET:
        {
            auto const* const address = (unsigned char const*)&a->addr.addr4;
            return address[0] == 0 || address[0] == 127 || (address[0] & 0xE0) == 0xE0;
        }

    case TR_AF_INET6:
        {
            auto const* const address = (unsigned char const*)&a->addr.addr6;
            return address[0] == 0xFF || (memcmp(address, zeroes, 15) == 0 && (address[15] == 0 || address[15] == 1));
        }

    default:
        return true;
    }
}

bool tr_address_is_valid_for_peers(tr_address const* addr, tr_port port)
{
    return !std::empty(port) && tr_address_is_valid(addr) && !isIPv6LinkLocalAddress(addr) && !isIPv4MappedAddress(addr) &&
        !isMartianAddr(addr);
}

struct tr_peer_socket tr_peer_socket_tcp_create(tr_socket_t const handle)
{
    TR_ASSERT(handle != TR_BAD_SOCKET);

    return { TR_PEER_SOCKET_TYPE_TCP, { handle } };
}

struct tr_peer_socket tr_peer_socket_utp_create(struct UTPSocket* const handle)
{
    TR_ASSERT(handle != nullptr);

    auto ret = tr_peer_socket{ TR_PEER_SOCKET_TYPE_UTP, {} };
    ret.handle.utp = handle;
    return ret;
}

/// tr_port

std::pair<tr_port, uint8_t const*> tr_port::fromCompact(uint8_t const* compact) noexcept
{
    static auto constexpr PortLen = size_t{ 2 };

    static_assert(PortLen == sizeof(uint16_t));
    auto nport = uint16_t{};
    std::copy_n(compact, PortLen, reinterpret_cast<uint8_t*>(&nport));
    compact += PortLen;

    return std::make_pair(tr_port::fromNetwork(nport), compact);
}

/// tr_address

std::optional<tr_address> tr_address::fromString(std::string_view address_str)
{
    auto addr = tr_address{};

    if (!tr_address_from_string(&addr, address_str))
    {
        return {};
    }

    return addr;
}

template<typename OutputIt>
OutputIt tr_address::readable(OutputIt out) const
{
    auto buf = std::array<char, INET6_ADDRSTRLEN>{};
    tr_address_to_string_with_buf(this, std::data(buf), std::size(buf));
    return fmt::format_to(out, FMT_STRING("{:s}"), std::data(buf));
}

template char* tr_address::readable<char*>(char*) const;

std::string tr_address::readable() const
{
    auto buf = std::string{};
    buf.reserve(INET6_ADDRSTRLEN);
    this->readable(std::back_inserter(buf));
    return buf;
}

template<typename OutputIt>
OutputIt tr_address::readable(OutputIt out, tr_port port) const
{
    auto buf = std::array<char, INET6_ADDRSTRLEN>{};
    tr_address_to_string_with_buf(this, std::data(buf), std::size(buf));
    return fmt::format_to(out, FMT_STRING("[{:s}]:{:d}"), std::data(buf), port.host());
}

template char* tr_address::readable<char*>(char*, tr_port) const;

std::string tr_address::readable(tr_port port) const
{
    auto buf = std::string{};
    buf.reserve(INET6_ADDRSTRLEN + 9);
    this->readable(std::back_inserter(buf), port);
    return buf;
}

std::pair<tr_address, uint8_t const*> tr_address::fromCompact4(uint8_t const* compact) noexcept
{
    static auto constexpr Addr4Len = size_t{ 4 };

    auto address = tr_address{};
    static_assert(sizeof(address.addr.addr4) == Addr4Len);
    address.type = TR_AF_INET;
    std::copy_n(compact, Addr4Len, reinterpret_cast<uint8_t*>(&address.addr));
    compact += Addr4Len;

    return std::make_pair(address, compact);
}

std::pair<tr_address, uint8_t const*> tr_address::fromCompact6(uint8_t const* compact) noexcept
{
    static auto constexpr Addr6Len = size_t{ 16 };

    auto address = tr_address{};
    address.type = TR_AF_INET6;
    std::copy_n(compact, Addr6Len, reinterpret_cast<uint8_t*>(&address.addr.addr6.s6_addr));
    compact += Addr6Len;

    return std::make_pair(address, compact);
}

int tr_address::compare(tr_address const& that) const noexcept // <=>
{
    return tr_address_compare(this, &that);
}
