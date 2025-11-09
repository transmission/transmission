// This file Copyright © Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstddef>
#include <memory>
#include <string>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h> // IPV6_V6ONLY, IPPROTO_IPV6
#include <sys/socket.h> // setsockopt, SOL_SOCKET, bind
#endif

#include <event2/util.h>

#include <fmt/format.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-utp.h"
#include "libtransmission/utils.h"

namespace
{

// Since we use a single UDP socket in order to implement multiple
// µTP sockets, try to set up huge buffers.
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
    if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0)
    {
        tr_logAddDebug(fmt::format("Couldn't set receive buffer: {}", tr_net_strerror(sockerrno)));
    }

    size = large ? SendBufferSize : SmallBufferSize;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const*>(&size), sizeof(size)) < 0)
    {
        tr_logAddDebug(fmt::format("Couldn't set send buffer: {}", tr_net_strerror(sockerrno)));
    }

    if (large)
    {
        if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&rbuf), &rbuf_len) < 0)
        {
            rbuf = 0;
        }

        if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&sbuf), &sbuf_len) < 0)
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

} // namespace

void tr_session::tr_udp_core::on_read_event(tr_socket_t socket)
{
    tr_socket_t s = socket;
    auto* const session = &session_;

    auto buf = std::array<unsigned char, 8192>{};
    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{ sizeof(from) };
    auto* const from_sa = reinterpret_cast<sockaddr*>(&from);
    auto got_utp_packet = false;

    auto const from_str = [from_sa]
    {
        return tr_socket_address::from_sockaddr(from_sa).value_or(tr_socket_address{}).display_name();
    };

    for (;;)
    {
        auto const n_read = recvfrom(s, reinterpret_cast<char*>(std::data(buf)), std::size(buf) - 1, 0, from_sa, &fromlen);
        if (n_read <= 0)
        {
            if (got_utp_packet)
            {
                // To reduce protocol overhead, we wait until we've read all UDP packets
                // we can, then send one ACK for each µTP socket that received packet(s).
                tr_utp_issue_deferred_acks(session);
            }
            return;
        }

        // Since most packets we receive here are µTP, make quick inline
        // checks for the other protocols. The logic is as follows:
        // - all DHT packets start with 'd' (100)
        // - all UDP tracker packets start with a 32-bit (!) "action", which
        //   is between 0 and 3
        // - the above cannot be µTP packets, since these start with a 4-bit
        //   "type" between 0 and 4, followed by a 4-bit version number (1)
        if (buf[0] == 'd')
        {
            if (session->dht_)
            {
                buf[n_read] = '\0'; // libdht requires zero-terminated messages
                session->dht_->handle_message(std::data(buf), n_read, from_sa, fromlen);
            }
        }
        else if (n_read >= 8 && buf[0] == 0 && buf[1] == 0 && buf[2] == 0 && buf[3] <= 3)
        {
            if (!session->announcer_udp_->handle_message(std::data(buf), n_read, from_sa, fromlen))
            {
                tr_logAddTrace(fmt::format("{} Couldn't parse UDP tracker packet.", from_str()));
            }
        }
        else if (session->allowsUTP() && session->utp_context != nullptr)
        {
            if (tr_utp_packet(std::data(buf), n_read, from_sa, fromlen, session))
            {
                got_utp_packet = true;
            }
            else
            {
                tr_logAddTrace(fmt::format(
                    "{} Unexpected UDP packet... len {} [{}]",
                    from_str(),
                    n_read,
                    tr_base64_encode({ reinterpret_cast<char const*>(std::data(buf)), static_cast<size_t>(n_read) })));
            }
        }
    }
}

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
        (void)evutil_make_listen_socket_reuseable(sock);

        auto const addr = session_.bind_address(TR_AF_INET);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(sock) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                fmt::runtime(_("Couldn't make IPv4 socket non-blocking {address}: {error} ({error_code})")),
                fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                fmt::runtime(_("Couldn't bind IPv4 socket {address}: {error} ({error_code})")),
                fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else
        {
            tr_logAddInfo(fmt::format("Bound UDP IPv4 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketTOS(sock, TR_AF_INET);
            set_socket_buffers(sock, session_.allowsUTP());
            udp4_socket_ = sock;
            udp4_event_handler_ = std::make_unique<tr_session::tr_udp_core_libuv_handler>(
                session,
                udp4_socket_,
                [this](tr_socket_t socket) { on_read_event(socket); });
            udp4_event_handler_->start();
        }
    }

    if (!session.has_ip_protocol(TR_AF_INET6))
    {
        // no IPv6; do nothing
    }
    else if (auto sock = socket(PF_INET6, SOCK_DGRAM, 0); sock != TR_BAD_SOCKET)
    {
        (void)evutil_make_listen_socket_reuseable(sock);
        (void)tr_make_listen_socket_ipv6only(sock);

        auto const addr = session_.bind_address(TR_AF_INET6);
        auto const [ss, sslen] = tr_socket_address::to_sockaddr(addr, udp_port_);

        if (evutil_make_socket_nonblocking(sock) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                fmt::runtime(_("Couldn't make IPv6 socket non-blocking {address}: {error} ({error_code})")),
                fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else if (bind(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) != 0)
        {
            auto const error_code = errno;
            tr_logAddWarn(fmt::format(
                fmt::runtime(_("Couldn't bind IPv6 socket {address}: {error} ({error_code})")),
                fmt::arg("address", tr_socket_address::display_name(addr, udp_port_)),
                fmt::arg("error", tr_strerror(error_code)),
                fmt::arg("error_code", error_code)));

            tr_net_close_socket(sock);
        }
        else
        {
            tr_logAddInfo(fmt::format("Bound UDP IPv6 address {:s}", tr_socket_address::display_name(addr, udp_port_)));
            session_.setSocketTOS(sock, TR_AF_INET6);
            set_socket_buffers(sock, session_.allowsUTP());
            udp6_socket_ = sock;
            udp6_event_handler_ = std::make_unique<tr_session::tr_udp_core_libuv_handler>(
                session,
                udp6_socket_,
                [this](tr_socket_t socket) { on_read_event(socket); });
            udp6_event_handler_->start();
        }
    }
}

tr_session::tr_udp_core::~tr_udp_core()
{
    udp6_event_handler_.reset();

    if (udp6_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp6_socket_);
        udp6_socket_ = TR_BAD_SOCKET;
    }

    udp4_event_handler_.reset();

    if (udp4_socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(udp4_socket_);
        udp4_socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::tr_udp_core::sendto(void const* buf, size_t buflen, struct sockaddr const* to, socklen_t const tolen) const
{
    auto const addrport = tr_socket_address::from_sockaddr(to);
    if (to->sa_family != AF_INET && to->sa_family != AF_INET6)
    {
        errno = EAFNOSUPPORT;
    }
    else if (auto const sock = to->sa_family == AF_INET ? udp4_socket_ : udp6_socket_; sock == TR_BAD_SOCKET)
    {
        // don't warn on bad sockets; the system may not support IPv6
        return;
    }
    else if (
        addrport && !addrport->address().is_ipv4_loopback_address() && !addrport->address().is_ipv6_loopback_address() &&
        !session_.source_address(tr_af_to_ip_protocol(to->sa_family)))
    {
        // don't try to send if we don't have a route in this IP protocol
        return;
    }
    else if (::sendto(sock, static_cast<char const*>(buf), buflen, 0, to, tolen) != -1)
    {
        return;
    }

    auto display_name = std::string{};
    if (addrport)
    {
        display_name = addrport->display_name();
    }

    tr_logAddWarn(fmt::format(
        "Couldn't send to {address}: {errno} ({error})",
        fmt::arg("address", display_name),
        fmt::arg("errno", errno),
        fmt::arg("error", tr_strerror(errno))));
}
