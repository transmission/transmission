// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef _WIN32
#include <netinet/tcp.h> // TCP_CONGESTION
#endif

#include <event2/event.h>

#include <fmt/format.h>

#include "libtransmission/log.h"
#include "libtransmission/peer-socket-tcp.h"
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils-ev.h"

#define tr_logAddTraceSock(sock, msg) tr_logAddTrace(msg, (sock)->display_name())

namespace
{
tr_socket_t create_socket(int const domain)
{
    auto const sockfd = socket(domain, SOCK_STREAM, 0);
    if (sockfd == TR_BAD_SOCKET)
    {
        if (sockerrno != EAFNOSUPPORT)
        {
            tr_logAddWarn(
                fmt::format(
                    fmt::runtime(_("Couldn't create socket: {error} ({error_code})")),
                    fmt::arg("error", tr_net_strerror(sockerrno)),
                    fmt::arg("error_code", sockerrno)));
        }
        return TR_BAD_SOCKET;
    }

    if (evutil_make_socket_nonblocking(sockfd) == -1)
    {
        tr_net_close_socket(sockfd);
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

tr_socket_t open_peer_socket(tr_session const& session, tr_socket_address const& socket_address, bool const client_is_seed)
{
    auto const& [addr, port] = socket_address;

    TR_ASSERT(addr.is_valid());

    if (!session.allowsTCP() || !socket_address.is_valid())
    {
        return TR_BAD_SOCKET;
    }

    auto const s = create_socket(tr_ip_protocol_to_af(addr.type));
    if (s == TR_BAD_SOCKET)
    {
        return TR_BAD_SOCKET;
    }

    // seeds don't need a big read buffer, so make it smaller
    if (client_is_seed)
    {
        constexpr int N = 8192;
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const*>(&N), sizeof(N)) == -1)
        {
            tr_logAddDebug(fmt::format("Unable to set SO_RCVBUF on socket {}: {}", s, tr_net_strerror(sockerrno)));
        }
    }

    auto const [sock, addrlen] = socket_address.to_sockaddr();

    // set source address
    auto const source_addr = session.bind_address(addr.type);
    auto const [source_sock, sourcelen] = tr_socket_address::to_sockaddr(source_addr, {});

    if (bind(s, reinterpret_cast<sockaddr const*>(&source_sock), sourcelen) == -1)
    {
        tr_logAddWarn(
            fmt::format(
                fmt::runtime(_("Couldn't set source address {address} on {socket}: {error} ({error_code})")),
                fmt::arg("address", source_addr.display_name()),
                fmt::arg("socket", s),
                fmt::arg("error", tr_net_strerror(sockerrno)),
                fmt::arg("error_code", sockerrno)));
        tr_net_close_socket(s);
        return TR_BAD_SOCKET;
    }

    if (connect(s, reinterpret_cast<sockaddr const*>(&sock), addrlen) == -1 &&
#ifdef _WIN32
        sockerrno != WSAEWOULDBLOCK &&
#endif
        sockerrno != EINPROGRESS)
    {
        if (auto const tmperrno = sockerrno;
            (tmperrno != ECONNREFUSED && tmperrno != ENETUNREACH && tmperrno != EHOSTUNREACH) || addr.is_ipv4())
        {
            tr_logAddWarn(
                fmt::format(
                    fmt::runtime(_("Couldn't connect socket {socket} to {address}:{port}: {error} ({error_code})")),
                    fmt::arg("socket", s),
                    fmt::arg("address", addr.display_name()),
                    fmt::arg("port", port.host()),
                    fmt::arg("error", tr_net_strerror(tmperrno)),
                    fmt::arg("error_code", tmperrno)));
        }

        tr_net_close_socket(s);
        return TR_BAD_SOCKET;
    }

    tr_logAddTrace(fmt::format("New OUTGOING connection {} ({})", s, socket_address.display_name()));

    return s;
}

void set_congestion_control([[maybe_unused]] tr_socket_t const s, [[maybe_unused]] char const* const algorithm)
{
#ifdef TCP_CONGESTION
    if (setsockopt(s, IPPROTO_TCP, TCP_CONGESTION, algorithm, strlen(algorithm) + 1) == -1)
    {
        tr_logAddDebug(fmt::format("Can't set congestion control algorithm '{}': {}", algorithm, tr_net_strerror(sockerrno)));
    }
#endif
}

class tr_peer_socket_tcp_impl final : public tr_peer_socket_tcp
{
public:
    tr_peer_socket_tcp_impl(tr_session& session, tr_socket_address const& socket_address, tr_socket_t sock)
        : tr_peer_socket_tcp{ socket_address }
        , sock_{ sock }
    {
        TR_ASSERT(sock_ != TR_BAD_SOCKET);

        session.setSocketDiffServ(sock, address().type);

        if (auto const& algo = session.peerCongestionAlgorithm(); !std::empty(algo))
        {
            set_congestion_control(sock, algo.c_str());
        }

        event_read_.reset(event_new(session.event_base(), sock_, EV_READ, event_read_cb, this));
        event_write_.reset(event_new(session.event_base(), sock_, EV_WRITE, event_write_cb, this));

        tr_logAddTraceSock(this, fmt::format("socket (tcp) is {}", sock_));
    }

    tr_peer_socket_tcp_impl(tr_peer_socket_tcp_impl const&) = delete;
    tr_peer_socket_tcp_impl& operator=(tr_peer_socket_tcp_impl const&) = delete;
    tr_peer_socket_tcp_impl(tr_peer_socket_tcp_impl&&) = delete;
    tr_peer_socket_tcp_impl& operator=(tr_peer_socket_tcp_impl&&) = delete;

    ~tr_peer_socket_tcp_impl() override
    {
        event_read_.reset();
        event_write_.reset();
        tr_net_close_socket(sock_);
    }

    [[nodiscard]] TR_CONSTEXPR20 Type type() const noexcept override
    {
        return Type::TCP;
    }

    void set_read_enabled(bool const enabled) override
    {
        if (is_read_enabled_ == enabled)
        {
            return;
        }

        is_read_enabled_ = enabled;
        if (enabled)
        {
            tr_logAddTraceSock(this, "enabling ready-to-read polling");
            event_add(event_read_.get(), nullptr);
        }
        else
        {
            tr_logAddTraceSock(this, "disabling ready-to-read polling");
            event_del(event_read_.get());
        }
    }

    void set_write_enabled(bool const enabled) override
    {
        if (is_write_enabled_ == enabled)
        {
            return;
        }

        is_write_enabled_ = enabled;
        if (enabled)
        {
            tr_logAddTraceSock(this, "enabling ready-to-write polling");
            event_add(event_write_.get(), nullptr);
        }
        else
        {
            tr_logAddTraceSock(this, "disabling ready-to-write polling");
            event_del(event_write_.get());
        }
    }

    [[nodiscard]] bool is_read_enabled() const override
    {
        return is_read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const override
    {
        return is_write_enabled_;
    }

private:
    size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* error) override
    {
        auto const [bufptr, buflen] = buf.reserve_space(n_bytes);
        n_bytes = std::min(n_bytes, buflen);
        TR_ASSERT(n_bytes > 0U);
        auto const n_read = recv(sock_, reinterpret_cast<char*>(bufptr), n_bytes, 0);
        auto const err = sockerrno;

        if (n_read > 0)
        {
            buf.commit_space(n_read);
            return static_cast<size_t>(n_read);
        }

        // When a stream socket peer has performed an orderly shutdown,
        // the return value will be 0 (the traditional "end-of-file" return).
        if (error != nullptr)
        {
            if (n_read == 0)
            {
                error->set_from_errno(ENOTCONN);
            }
            else
            {
                error->set(err, tr_net_strerror(err));
            }
        }

        return {};
    }

    size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* error) override
    {
        n_bytes = std::min(n_bytes, std::size(buf));
        if (n_bytes == 0U)
        {
            return {};
        }

        if (auto const n_sent = send(sock_, reinterpret_cast<char const*>(std::data(buf)), n_bytes, 0); n_sent >= 0U)
        {
            buf.drain(n_sent);
            return n_sent;
        }

        if (error != nullptr)
        {
            auto const err = sockerrno;
            error->set(err, tr_net_strerror(err));
        }

        return {};
    }

    static void event_read_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vs)
    {
        auto* const s = static_cast<tr_peer_socket_tcp_impl*>(vs);
        tr_logAddTraceSock(s, "libevent says this peer socket is ready for reading");

        TR_ASSERT(s->sock_ == fd);

        s->is_read_enabled_ = false;
        s->read_cb();
    }

    static void event_write_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vs)
    {
        auto* const s = static_cast<tr_peer_socket_tcp_impl*>(vs);
        tr_logAddTraceSock(s, "libevent says this peer socket is ready for writing");

        TR_ASSERT(s->sock_ == fd);

        s->is_write_enabled_ = false;
        s->write_cb();
    }

    tr_socket_t sock_;

    libtransmission::evhelpers::event_unique_ptr event_read_;
    libtransmission::evhelpers::event_unique_ptr event_write_;

    bool is_read_enabled_ = false;
    bool is_write_enabled_ = false;
};
} // namespace

tr_peer_socket_tcp::tr_peer_socket_tcp(tr_socket_address const& socket_address)
    : tr_peer_socket{ socket_address }
{
}

std::unique_ptr<tr_peer_socket_tcp> tr_peer_socket_tcp::create(
    tr_session& session,
    tr_socket_address const& socket_address,
    bool const client_is_seed)
{
    if (auto const sock = open_peer_socket(session, socket_address, client_is_seed); sock != TR_BAD_SOCKET)
    {
        return create(session, socket_address, sock);
    }
    return {};
}

std::unique_ptr<tr_peer_socket_tcp> tr_peer_socket_tcp::create(
    tr_session& session,
    tr_socket_address const& socket_address,
    tr_socket_t sock)
{
    return std::make_unique<tr_peer_socket_tcp_impl>(session, socket_address, sock);
}
