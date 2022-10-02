// This file Copyright © 2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring> /* memcmp(), memset() */

#ifdef _WIN32
#include <io.h> /* dup2() */
#else
#include <unistd.h> /* dup2() */
#endif

#include <event2/event.h>

#include <fmt/core.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-dht.h"
#include "tr-utp.h"
#include "utils.h"

/* Since we use a single UDP socket in order to implement multiple
   uTP sockets, try to set up huge buffers. */

static auto constexpr RecvBufferSize = 4 * 1024 * 1024;
static auto constexpr SendBufferSize = 1 * 1024 * 1024;
static auto constexpr SmallBufferSize = 32 * 1024;

static void set_socket_buffers(tr_socket_t fd, bool large)
{
    int rbuf = 0;
    int sbuf = 0;
    socklen_t rbuf_len = sizeof(rbuf);
    socklen_t sbuf_len = sizeof(sbuf);

    int size = large ? RecvBufferSize : SmallBufferSize;
    int rc = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&size), sizeof(size));

    if (rc < 0)
    {
        tr_logAddDebug(fmt::format("Couldn't set receive buffer: {}", tr_net_strerror(sockerrno)));
    }

    size = large ? SendBufferSize : SmallBufferSize;
    rc = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&size), sizeof(size));

    if (rc < 0)
    {
        tr_logAddDebug(fmt::format("Couldn't set send buffer: {}", tr_net_strerror(sockerrno)));
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

        if (rbuf < RecvBufferSize)
        {
            tr_logAddDebug(fmt::format("Couldn't set receive buffer: requested {}, got {}", RecvBufferSize, rbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.rmem_max = {}' to /etc/sysctl.conf", RecvBufferSize));
#endif
        }

        if (sbuf < SendBufferSize)
        {
            tr_logAddDebug(fmt::format("Couldn't set send buffer: requested {}, got {}", SendBufferSize, sbuf));
#ifdef __linux__
            tr_logAddDebug(fmt::format("Please add the line 'net.core.wmem_max = {}' to /etc/sysctl.conf", SendBufferSize));
#endif
        }
    }
}

void tr_session::tr_udp_core::set_socket_buffers()
{
    bool const utp = session_.allowsUTP();

    if (udp_socket_ != TR_BAD_SOCKET)
    {
        ::set_socket_buffers(udp_socket_, utp);
    }
    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        ::set_socket_buffers(udp6_socket_, utp);
    }
}

static tr_socket_t rebind_ipv6_impl(in6_addr sin6_addr, tr_port port)
{
    auto const sock = socket(PF_INET6, SOCK_DGRAM, 0);
    if (sock == TR_BAD_SOCKET)
    {
        return TR_BAD_SOCKET;
    }

#ifdef IPV6_V6ONLY
    /* Since we always open an IPv4 socket on the same port,
     * this shouldn't matter.  But I'm superstitious. */
    int one = 1;
    (void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&one), sizeof(one));
#endif

    auto sin6 = sockaddr_in6{};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = sin6_addr;
    sin6.sin6_port = port.network();
    if (::bind(sock, reinterpret_cast<struct sockaddr*>(&sin6), sizeof(sin6)) == -1)
    {
        tr_netCloseSocket(sock);
        return TR_BAD_SOCKET;
    }

    return sock;
}

/* BEP-32 has a rather nice explanation of why we need to bind to one
   IPv6 address, if I may say so myself. */
void tr_session::tr_udp_core::rebind_ipv6(bool force)
{
    auto const ipv6 = tr_globalIPv6(&session_);

    /* We currently have no way to enable or disable IPv6 after initialisation.
       No way to fix that without some surgery to the DHT code itself. */
    if (!ipv6 || (!force && udp6_socket_ == TR_BAD_SOCKET))
    {
        udp6_bound_.reset();
        return;
    }

    if (udp6_bound_ && memcmp(&*udp6_bound_, &*ipv6, sizeof(*ipv6)) == 0)
    {
        return;
    }

    auto const sock = rebind_ipv6_impl(*ipv6, udp_port_);
    if (sock == TR_BAD_SOCKET)
    {
        /* Something went wrong.  It's difficult to recover, so let's
         * simply set things up so that we try again next time. */
        auto const error_code = errno;
        auto ipv6_readable = std::array<char, INET6_ADDRSTRLEN>{};
        evutil_inet_ntop(AF_INET6, &*ipv6, std::data(ipv6_readable), std::size(ipv6_readable));
        tr_logAddWarn(fmt::format(
            _("Couldn't rebind IPv6 socket {address}: {error} ({error_code})"),
            fmt::arg("address", std::data(ipv6_readable)),
            fmt::arg("error", tr_strerror(error_code)),
            fmt::arg("error_code", error_code)));

        udp6_bound_.reset();
        return;
    }

    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(udp6_socket_);
    }

    udp6_socket_ = sock;
    udp6_bound_ = ipv6;
}

static void event_callback(evutil_socket_t s, [[maybe_unused]] short type, void* vsession)
{
    TR_ASSERT(vsession != nullptr);
    TR_ASSERT(type == EV_READ);

    auto buf = std::array<unsigned char, 4096>{};
    auto from = sockaddr_storage{};
    auto* session = static_cast<tr_session*>(vsession);

    socklen_t fromlen = sizeof(from);
    int const
        rc = recvfrom(s, reinterpret_cast<char*>(std::data(buf)), std::size(buf) - 1, 0, (struct sockaddr*)&from, &fromlen);

    /* Since most packets we receive here are ÂµTP, make quick inline
       checks for the other protocols. The logic is as follows:
       - all DHT packets start with 'd'
       - all UDP tracker packets start with a 32-bit (!) "action", which
         is between 0 and 3
       - the above cannot be ÂµTP packets, since these start with a 4-bit
         version number (1). */
    if (rc > 0)
    {
        if (buf[0] == 'd')
        {
            if (session->allowsDHT())
            {
                buf[rc] = '\0'; /* required by the DHT code */
                tr_dhtCallback(std::data(buf), rc, (struct sockaddr*)&from, fromlen);
            }
        }
        else if (rc >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3)
        {
            if (!session->tau_handle_message(std::data(buf), rc))
            {
                tr_logAddTrace("Couldn't parse UDP tracker packet.");
            }
        }
        else
        {
            if (session->allowsUTP())
            {
                if (!tr_utpPacket(std::data(buf), rc, (struct sockaddr*)&from, fromlen, session))
                {
                    tr_logAddTrace("Unexpected UDP packet");
                }
            }
        }
    }
}

tr_session::tr_udp_core::tr_udp_core(tr_session& session)
    : session_{ session }
{
    udp_port_ = session_.peerPort();
    if (std::empty(udp_port_))
    {
        return;
    }

    udp_socket_ = socket(PF_INET, SOCK_DGRAM, 0);

    if (udp_socket_ == TR_BAD_SOCKET)
    {
        tr_logAddWarn(_("Couldn't create IPv4 socket"));
    }
    else
    {
        auto const [public_addr, is_default] = session_.publicAddress(TR_AF_INET);

        auto sin = sockaddr_in{};
        sin.sin_family = AF_INET;
        if (!is_default)
        {
            sin.sin_addr = public_addr.addr.addr4;
        }

        sin.sin_port = udp_port_.network();
        int const rc = bind(udp_socket_, (struct sockaddr*)&sin, sizeof(sin));

        if (rc == -1)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                _("Couldn't bind IPv4 socket {address}: {error} ({error_code})"),
                fmt::arg("address", public_addr.readable(udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));
            tr_netCloseSocket(udp_socket_);
            udp_socket_ = TR_BAD_SOCKET;
        }
        else
        {
            udp_event_ = event_new(session_.eventBase(), udp_socket_, EV_READ | EV_PERSIST, event_callback, &session_);

            if (udp_event_ == nullptr)
            {
                tr_logAddWarn(_("Couldn't allocate IPv4 event"));
            }
        }
    }

    // IPV6

    if (tr_globalIPv6(nullptr).has_value())
    {
        rebind_ipv6(true);
    }

    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        udp6_event_ = event_new(session_.eventBase(), udp6_socket_, EV_READ | EV_PERSIST, event_callback, &session_);

        if (udp6_event_ == nullptr)
        {
            tr_logAddWarn(_("Couldn't allocate IPv6 event"));
        }
    }

    set_socket_buffers();
    set_socket_tos();

    if (session_.allowsDHT())
    {
        tr_dhtInit(&session_, udp_socket_, udp6_socket_);
    }

    if (udp_event_ != nullptr)
    {
        event_add(udp_event_, nullptr);
    }
    if (udp6_event_ != nullptr)
    {
        event_add(udp6_event_, nullptr);
    }
}

void tr_session::tr_udp_core::dhtUpkeep()
{
    if (tr_dhtEnabled())
    {
        tr_dhtUpkeep();
    }
}

void tr_session::tr_udp_core::dhtUninit()
{
    if (tr_dhtEnabled())
    {
        tr_dhtUninit();
    }
}

tr_session::tr_udp_core::~tr_udp_core()
{
    dhtUninit();

    if (udp_socket_ != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(udp_socket_);
        udp_socket_ = TR_BAD_SOCKET;
    }

    if (udp_event_ != nullptr)
    {
        event_free(udp_event_);
        udp_event_ = nullptr;
    }

    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        tr_netCloseSocket(udp6_socket_);
        udp6_socket_ = TR_BAD_SOCKET;
    }

    if (udp6_event_ != nullptr)
    {
        event_free(udp6_event_);
        udp6_event_ = nullptr;
    }

    udp6_bound_.reset();
}

void tr_session::tr_udp_core::sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen) const
{
    int error = 0;
    std::array<char, std::max(INET_ADDRSTRLEN, INET6_ADDRSTRLEN) + 1> peer = {};

    if (to->sa_family == AF_INET)
    {
        if (udp_socket_ != TR_BAD_SOCKET)
        {
            if (::sendto(udp_socket_, static_cast<char const*>(buf), buflen, 0, to, tolen) == -1)
            {
                error = -1;
            }
        }
        else
        {
            error = -1;
            errno = EBADF;
        }
        if (error == -1)
        {
            evutil_inet_ntop(
                AF_INET,
                &((reinterpret_cast<struct sockaddr_in const*>(to))->sin_addr),
                std::data(peer),
                std::size(peer));
        }
    }
    else if (to->sa_family == AF_INET6)
    {
        if (udp6_socket_ != TR_BAD_SOCKET)
        {
            if (::sendto(udp6_socket_, static_cast<char const*>(buf), buflen, 0, to, tolen) == -1)
            {
                error = -1;
            }
        }
        else
        {
            error = -1;
            errno = EBADF;
        }
        if (error == -1)
        {
            evutil_inet_ntop(
                AF_INET6,
                &((reinterpret_cast<struct sockaddr_in6 const*>(to))->sin6_addr),
                std::data(peer),
                std::size(peer));
        }
    }
    else
    {
        error = -1;
        errno = EAFNOSUPPORT;
    }

    if (error == -1)
    {
        tr_logAddWarn(fmt::format(
            "Couldn't send to {address}: {errno} ({error})",
            fmt::arg("address", std::data(peer)),
            fmt::arg("errno", errno),
            fmt::arg("error", tr_strerror(errno))));
    }
}
