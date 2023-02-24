// This file Copyright © 2010-2023 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring> /* memcmp(), memset() */

#include <event2/event.h>

#include <fmt/core.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "tr-assert.h"
#include "tr-utp.h"
#include "utils.h"

namespace
{
/* Since we use a single UDP socket in order to implement multiple
   µTP sockets, try to set up huge buffers. */
void set_socket_buffers(tr_socket_t fd, bool large)
{
    static auto constexpr RecvBufferSize = 4 * 1024 * 1024;
    static auto constexpr SendBufferSize = 1 * 1024 * 1024;
    static auto constexpr SmallBufferSize = 32 * 1024;

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

void event_callback(evutil_socket_t s, [[maybe_unused]] short type, void* vsession)
{
    TR_ASSERT(vsession != nullptr);
    TR_ASSERT(type == EV_READ);

    auto buf = std::array<unsigned char, 8192>{};
    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{ sizeof(from) };
    auto* const from_sa = reinterpret_cast<sockaddr*>(&from);

    auto const rc = recvfrom(s, reinterpret_cast<char*>(std::data(buf)), std::size(buf) - 1, 0, from_sa, &fromlen);
    if (rc <= 0)
    {
        return;
    }

    /* Since most packets we receive here are µTP, make quick inline
       checks for the other protocols. The logic is as follows:
       - all DHT packets start with 'd'
       - all UDP tracker packets start with a 32-bit (!) "action", which
         is between 0 and 3
       - the above cannot be µTP packets, since these start with a 4-bit
         version number (1). */
    auto* const session = static_cast<tr_session*>(vsession);
    if (buf[0] == 'd')
    {
        if (session->dht_)
        {
            buf[rc] = '\0'; // libdht requires zero-terminated messages
            session->dht_->handleMessage(std::data(buf), rc, from_sa, fromlen);
        }
    }
    else if (rc >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3)
    {
        if (!session->announcer_udp_->handleMessage(std::data(buf), rc))
        {
            tr_logAddTrace("Couldn't parse UDP tracker packet.");
        }
    }
    else if (session->allowsUTP() && (session->utp_context != nullptr))
    {
        if (!tr_utpPacket(std::data(buf), rc, from_sa, fromlen, session))
        {
            tr_logAddTrace("Unexpected UDP packet");
        }
    }
}
} // namespace

// BEP-32 explains why we need to bind to one IPv6 address

tr_session::tr_udp_core::tr_udp_core(tr_session& session, tr_port udp_port)
    : udp_port_{ udp_port }
    , session_{ session }
{
    if (std::empty(udp_port_))
    {
        return;
    }

    if (auto sock = socket(PF_INET, SOCK_DGRAM, 0); sock != TR_BAD_SOCKET)
    {
        auto optval = int{ 1 };
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));

        auto const [addr, is_any] = session_.publicAddress(TR_AF_INET);
        auto const [ss, sslen] = addr.to_sockaddr(udp_port_);

        if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                _("Couldn't bind IPv4 socket {address}: {error} ({error_code})"),
                fmt::arg("address", addr.display_name(udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else
        {
            tr_logAddInfo(fmt::format("Bound UDP IPv4 address {:s}", addr.display_name(udp_port_)));
            session_.setSocketTOS(sock, TR_AF_INET);
            set_socket_buffers(sock, session_.allowsUTP());
            udp4_socket_ = sock;
            udp4_event_.reset(event_new(session_.eventBase(), udp4_socket_, EV_READ | EV_PERSIST, event_callback, &session_));
            event_add(udp4_event_.get(), nullptr);
        }
    }

    if (!tr_net_hasIPv6(udp_port_))
    {
        // no IPv6; do nothing
    }
    else if (auto sock = socket(PF_INET6, SOCK_DGRAM, 0); sock != TR_BAD_SOCKET)
    {
        auto optval = int{ 1 };
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&optval), sizeof(optval));

        auto const [addr, is_any] = session_.publicAddress(TR_AF_INET6);
        auto const [ss, sslen] = addr.to_sockaddr(udp_port_);

        if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                _("Couldn't bind IPv6 socket {address}: {error} ({error_code})"),
                fmt::arg("address", addr.display_name(udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else
        {
            tr_logAddInfo(fmt::format("Bound UDP IPv6 address {:s}", addr.display_name(udp_port_)));
            session_.setSocketTOS(sock, TR_AF_INET6);
            set_socket_buffers(sock, session_.allowsUTP());
            udp6_socket_ = sock;
            udp6_event_.reset(event_new(session_.eventBase(), udp6_socket_, EV_READ | EV_PERSIST, event_callback, &session_));
            event_add(udp6_event_.get(), nullptr);

#ifdef IPV6_V6ONLY
            // Since we always open an IPv4 socket on the same port,
            // this shouldn't matter.  But I'm superstitious.
            int one = 1;
            (void)setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char const*>(&one), sizeof(one));
#endif
        }
    }
}

tr_session::tr_udp_core::~tr_udp_core()
{
    udp6_event_.reset();

    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp6_socket_);
        udp6_socket_ = TR_BAD_SOCKET;
    }

    udp4_event_.reset();

    if (udp4_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp4_socket_);
        udp4_socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::tr_udp_core::sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen) const
{
    if (to->sa_family != AF_INET && to->sa_family != AF_INET6)
    {
        errno = EAFNOSUPPORT;
    }
    else if (auto const sock = to->sa_family == AF_INET ? udp4_socket_ : udp6_socket_; sock == TR_BAD_SOCKET)
    {
        // don't warn on bad sockets; the system may not support IPv6
        return;
    }
    else if (::sendto(sock, static_cast<char const*>(buf), buflen, 0, to, tolen) != -1)
    {
        return;
    }

    auto display_name = std::string{};
    if (auto const addrport = tr_address::from_sockaddr(to); addrport)
    {
        auto const& [addr, port] = *addrport;
        display_name = addr.display_name(port);
    }

    tr_logAddWarn(fmt::format(
        "Couldn't send to {address}: {errno} ({error})",
        fmt::arg("address", display_name),
        fmt::arg("errno", errno),
        fmt::arg("error", tr_strerror(errno))));
}
