// This file Copyright © 2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cstring> /* memcmp(), memcpy(), memset() */
#include <cstdlib> /* malloc(), free() */

#ifdef _WIN32
#include <io.h> /* dup2() */
#else
#include <unistd.h> /* dup2() */
#endif

#include <fmt/core.h>

#include <event2/event.h>

#include <cstdint>

#include "transmission.h"

#include "log.h"
#include "net.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-udp.h"
#include "tr-utp.h"
#include "utils.h"

/* Since we use a single UDP socket in order to implement multiple
   uTP sockets, try to set up huge buffers. */

#define RECV_BUFFER_SIZE (4 * 1024 * 1024)
#define SEND_BUFFER_SIZE (1 * 1024 * 1024)
#define SMALL_BUFFER_SIZE (32 * 1024)

static char constexpr ModuleName[] = "UDP";

#define logerr(sv) \
    do \
    { \
        if (tr_log::error::enabled()) \
        { \
            tr_log::error::add(TR_LOC, sv, ModuleName); \
        } \
    } while (0)

#define logwarn(sv) \
    do \
    { \
        if (tr_log::warn::enabled()) \
        { \
            tr_log::warn::add(TR_LOC, sv, ModuleName); \
        } \
    } while (0)

#define loginfo(sv) \
    do \
    { \
        if (tr_log::info::enabled()) \
        { \
            tr_log::info::add(TR_LOC, sv, ModuleName); \
        } \
    } while (0)

#define logdbg(sv) \
    do \
    { \
        if (tr_log::debug::enabled()) \
        { \
            tr_log::debug::add(TR_LOC, sv, ModuleName); \
        } \
    } while (0)

static void set_socket_buffers(tr_socket_t fd, bool large)
{
    int rbuf = 0;
    int sbuf = 0;
    socklen_t rbuf_len = sizeof(rbuf);
    socklen_t sbuf_len = sizeof(sbuf);

    int size = large ? RECV_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    int rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&size), sizeof(size));

    if (rc < 0)
    {
        auto const errcode = sockerrno;
        logwarn(fmt::format(
            _("Failed to set receive buffer: {errmsg} ({errcode})"),
            fmt::arg("errmsg", tr_net_strerror(errcode)),
            fmt::arg("errcode", errcode)));
    }

    size = large ? SEND_BUFFER_SIZE : SMALL_BUFFER_SIZE;
    rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&size), sizeof(size));

    if (rc < 0)
    {
        auto const errcode = sockerrno;
        logwarn(fmt::format(
            _("Failed to set send buffer: {errmsg} ({errcode})"),
            fmt::arg("errmsg", tr_net_strerror(errcode)),
            fmt::arg("errcode", errcode)));
    }

    if (large)
    {
        rc = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rbuf), &rbuf_len);

        if (rc < 0)
        {
            rbuf = 0;
        }

        rc = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sbuf), &sbuf_len);

        if (rc < 0)
        {
            sbuf = 0;
        }

        if (rbuf < RECV_BUFFER_SIZE)
        {
            logerr(fmt::format(
                _("Failed to set receive buffer: requested {req}, got {qty}"),
                fmt::arg("req", RECV_BUFFER_SIZE),
                fmt::arg("qty", rbuf)));
#ifdef __linux__
            loginfo(fmt::format(
                _("Please add the line `net.core.rmem_max = {qty}` to /etc/sysctl.conf"),
                fmt::arg("qty", RECV_BUFFER_SIZE)));
#endif
        }

        if (sbuf < SEND_BUFFER_SIZE)
        {
            logerr(fmt::format(
                _("Failed to set send buffer: requested {req}, got {qty}"),
                fmt::arg("req", SEND_BUFFER_SIZE),
                fmt::arg("qty", sbuf)));
#ifdef __linux__
            loginfo(fmt::format(
                _("Please add the line `net.core.wmem_max = {qty}` to /etc/sysctl.conf"),
                fmt::arg("qty", SEND_BUFFER_SIZE)));
#endif
        }
    }
}

void tr_udpSetSocketBuffers(tr_session* session)
{
    bool utp = tr_sessionIsUTPEnabled(session);

    if (session->udp_socket != TR_BAD_SOCKET)
    {
        set_socket_buffers(session->udp_socket, utp);
    }

    if (session->udp6_socket != TR_BAD_SOCKET)
    {
        set_socket_buffers(session->udp6_socket, utp);
    }
}

void tr_udpSetSocketTOS(tr_session* session)
{
    session->setSocketTOS(session->udp_socket, TR_AF_INET);
    session->setSocketTOS(session->udp6_socket, TR_AF_INET6);
}

/* BEP-32 has a rather nice explanation of why we need to bind to one
   IPv6 address, if I may say so myself. */
// TODO: remove goto, it prevents reducing scope of local variables
static void rebind_ipv6(tr_session* ss, bool force)
{
    struct sockaddr_in6 sin6;
    unsigned char const* ipv6 = tr_globalIPv6(ss);
    tr_socket_t s = TR_BAD_SOCKET;
    int rc = -1;
    int one = 1;

    /* We currently have no way to enable or disable IPv6 after initialisation.
       No way to fix that without some surgery to the DHT code itself. */
    if (ipv6 == nullptr || (!force && ss->udp6_socket == TR_BAD_SOCKET))
    {
        if (ss->udp6_bound != nullptr)
        {
            free(ss->udp6_bound);
            ss->udp6_bound = nullptr;
        }

        return;
    }

    if (ss->udp6_bound != nullptr && memcmp(ipv6, ss->udp6_bound, 16) == 0)
    {
        return;
    }

    s = socket(PF_INET6, SOCK_DGRAM, 0);

    if (s == TR_BAD_SOCKET)
    {
        goto FAIL;
    }

#ifdef IPV6_V6ONLY
    /* Since we always open an IPv4 socket on the same port, this
       shouldn't matter.  But I'm superstitious. */
    (void)setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&one), sizeof(one));
#endif

    memset(&sin6, 0, sizeof(sin6));
    sin6.sin6_family = AF_INET6;

    if (ipv6 != nullptr)
    {
        memcpy(&sin6.sin6_addr, ipv6, 16);
    }

    sin6.sin6_port = htons(ss->udp_port);

    rc = bind(s, (struct sockaddr*)&sin6, sizeof(sin6));

    if (rc == -1)
    {
        goto FAIL;
    }

    if (ss->udp6_socket == TR_BAD_SOCKET)
    {
        ss->udp6_socket = s;
    }
    else
    {
        /* FIXME: dup2 doesn't work for sockets on Windows */
        rc = dup2(s, ss->udp6_socket);

        if (rc == -1)
        {
            goto FAIL;
        }

        tr_netCloseSocket(s);
    }

    if (ss->udp6_bound == nullptr)
    {
        ss->udp6_bound = static_cast<unsigned char*>(malloc(16));
    }

    if (ss->udp6_bound != nullptr)
    {
        memcpy(ss->udp6_bound, ipv6, 16);
    }

    return;

FAIL:
    /* Something went wrong.  It's difficult to recover, so let's simply
       set things up so that we try again next time. */
    logwarn(_("Couldn't rebind IPv6 socket"));

    if (s != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(s);
    }

    if (ss->udp6_bound != nullptr)
    {
        free(ss->udp6_bound);
        ss->udp6_bound = nullptr;
    }
}

static void event_callback(evutil_socket_t s, [[maybe_unused]] short type, void* vsession)
{
    TR_ASSERT(tr_isSession(static_cast<tr_session*>(vsession)));
    TR_ASSERT(type == EV_READ);

    unsigned char buf[4096];
    struct sockaddr_storage from;
    auto* session = static_cast<tr_session*>(vsession);

    socklen_t fromlen = sizeof(from);
    int rc = recvfrom(s, reinterpret_cast<char*>(buf), 4096 - 1, 0, (struct sockaddr*)&from, &fromlen);

    /* Since most packets we receive here are ÂµTP, make quick inline
       checks for the other protocols.  The logic is as follows:
       - all DHT packets start with 'd'
       - all UDP tracker packets start with a 32-bit (!) "action", which
         is between 0 and 3
       - the above cannot be ÂµTP packets, since these start with a 4-bit
         version number (1). */
    if (rc > 0)
    {
        if (buf[0] == 'd')
        {
            if (tr_sessionAllowsDHT(session))
            {
                buf[rc] = '\0'; /* required by the DHT code */
                tr_dhtCallback(buf, rc, (struct sockaddr*)&from, fromlen, vsession);
            }
        }
        else if (rc >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3)
        {
            if (!tau_handle_message(session, buf, rc))
            {
                logdbg("Couldn't parse UDP tracker packet.");
            }
        }
        else
        {
            if (tr_sessionIsUTPEnabled(session))
            {
                if (!tr_utpPacket(buf, rc, (struct sockaddr*)&from, fromlen, session))
                {
                    logdbg("Unexpected UDP packet");
                }
            }
        }
    }
}

void tr_udpInit(tr_session* ss)
{
    TR_ASSERT(ss->udp_socket == TR_BAD_SOCKET);
    TR_ASSERT(ss->udp6_socket == TR_BAD_SOCKET);

    ss->udp_port = tr_sessionGetPeerPort(ss);

    if (ss->udp_port <= 0)
    {
        return;
    }

    ss->udp_socket = socket(PF_INET, SOCK_DGRAM, 0);

    if (ss->udp_socket == TR_BAD_SOCKET)
    {
        logwarn(_("Couldn't create IPv4 socket"));
    }
    else
    {
        auto is_default = bool{};
        tr_address const* public_addr = tr_sessionGetPublicAddress(ss, TR_AF_INET, &is_default);

        auto sin = sockaddr_in{};
        sin.sin_family = AF_INET;
        if (public_addr != nullptr && !is_default)
        {
            memcpy(&sin.sin_addr, &public_addr->addr.addr4, sizeof(struct in_addr));
        }

        sin.sin_port = htons(ss->udp_port);
        int const rc = bind(ss->udp_socket, (struct sockaddr*)&sin, sizeof(sin));

        if (rc == -1)
        {
            logwarn(_("Couldn't bind IPv4 socket"));
            tr_netCloseSocket(ss->udp_socket);
            ss->udp_socket = TR_BAD_SOCKET;
        }
        else
        {
            ss->udp_event = event_new(ss->event_base, ss->udp_socket, EV_READ | EV_PERSIST, event_callback, ss);

            if (ss->udp_event == nullptr)
            {
                logwarn(_("Couldn't allocate IPv4 event"));
            }
        }
    }

    // IPV6

    if (tr_globalIPv6(nullptr) != nullptr)
    {
        rebind_ipv6(ss, true);
    }

    if (ss->udp6_socket != TR_BAD_SOCKET)
    {
        ss->udp6_event = event_new(ss->event_base, ss->udp6_socket, EV_READ | EV_PERSIST, event_callback, ss);

        if (ss->udp6_event == nullptr)
        {
            logwarn(_("Couldn't allocate IPv6 event"));
        }
    }

    tr_udpSetSocketBuffers(ss);

    tr_udpSetSocketTOS(ss);

    if (ss->isDHTEnabled)
    {
        tr_dhtInit(ss);
    }

    if (ss->udp_event != nullptr)
    {
        event_add(ss->udp_event, nullptr);
    }

    if (ss->udp6_event != nullptr)
    {
        event_add(ss->udp6_event, nullptr);
    }
}

void tr_udpUninit(tr_session* ss)
{
    tr_dhtUninit(ss);

    if (ss->udp_socket != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(ss->udp_socket);
        ss->udp_socket = TR_BAD_SOCKET;
    }

    if (ss->udp_event != nullptr)
    {
        event_free(ss->udp_event);
        ss->udp_event = nullptr;
    }

    if (ss->udp6_socket != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(ss->udp6_socket);
        ss->udp6_socket = TR_BAD_SOCKET;
    }

    if (ss->udp6_event != nullptr)
    {
        event_free(ss->udp6_event);
        ss->udp6_event = nullptr;
    }

    if (ss->udp6_bound != nullptr)
    {
        free(ss->udp6_bound);
        ss->udp6_bound = nullptr;
    }
}
