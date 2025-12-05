// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <fmt/format.h>

#include <libutp/utp.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-utp.h"
#include "libtransmission/timer.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-buffer.h"

#define tr_logAddErrorSock(sock, msg) tr_logAddError(msg, (sock)->display_name())
#define tr_logAddTraceSock(sock, msg) tr_logAddTrace(msg, (sock)->display_name())

namespace
{
#ifdef WITH_UTP
void get_bufsize(tr_socket_t const fd, int const optname, int& ret)
{
    auto tmp = int{};
    socklen_t len = sizeof(tmp);
    if (getsockopt(fd, SOL_SOCKET, optname, reinterpret_cast<char*>(&tmp), &len) == 0)
    {
        ret = std::max(tmp, ret);
    }
}

int default_udp_bufsize(int const optname)
{
    int ret = -1;

    if (auto const fd = socket(PF_INET, SOCK_DGRAM, 0); fd != TR_BAD_SOCKET)
    {
        get_bufsize(fd, optname, ret);
        tr_net_close_socket(fd);
    }

    if (auto const fd = socket(PF_INET6, SOCK_DGRAM, 0); fd != TR_BAD_SOCKET)
    {
        get_bufsize(fd, optname, ret);
        tr_net_close_socket(fd);
    }

    tr_logAddTrace(fmt::format("max UDP option {} was {}", optname, ret));
    return ret;
}

class tr_peer_socket_utp_impl final
    : public tr_peer_socket_utp
    , public std::enable_shared_from_this<tr_peer_socket_utp_impl>
{
public:
    tr_peer_socket_utp_impl(tr_socket_address const& socket_address, UTPSocket* sock, libtransmission::TimerMaker& timer_maker)
        : tr_peer_socket_utp{ socket_address }
        , sock_{ sock }
        , timer_{ timer_maker.create([this] { read_now(); }) }
    {
        TR_ASSERT(sock != nullptr);
        utp_set_userdata(sock_, this);
        tr_logAddTraceSock(this, fmt::format("socket (µTP) is {}", fmt::ptr(sock_)));
    }

    tr_peer_socket_utp_impl(tr_peer_socket_utp_impl const&) = delete;
    tr_peer_socket_utp_impl& operator=(tr_peer_socket_utp_impl const&) = delete;
    tr_peer_socket_utp_impl(tr_peer_socket_utp_impl&&) = delete;
    tr_peer_socket_utp_impl& operator=(tr_peer_socket_utp_impl&&) = delete;

    ~tr_peer_socket_utp_impl() override
    {
        utp_set_userdata(sock_, nullptr);
        utp_close(sock_);
    }

    [[nodiscard]] TR_CONSTEXPR20 Type type() const noexcept override
    {
        return Type::UTP;
    }

    void set_read_enabled(bool const enabled) override
    {
        is_read_enabled_ = enabled;
        maybe_read_soon();
    }

    void set_write_enabled(bool const enabled) override
    {
        is_write_enabled_ = enabled;
    }

    [[nodiscard]] bool is_read_enabled() const override
    {
        return is_read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const override
    {
        return is_write_enabled_;
    }

    [[nodiscard]] constexpr auto& read_buffer() noexcept
    {
        return inbuf_;
    }

    [[nodiscard]] auto read_buffer_size() const noexcept
    {
        return std::size(inbuf_);
    }

    void maybe_read_soon()
    {
        if (is_read_enabled() && !std::empty(inbuf_))
        {
            timer_->start_single_shot(std::chrono::milliseconds::zero());
        }
    }

    // --- libutp

    void on_utp_state_change(int const state) const
    {
        switch (state)
        {
        case UTP_STATE_CONNECT:
            tr_logAddTraceSock(this, "utp_on_state_change -- changed to connected");
            break;
        case UTP_STATE_WRITABLE:
            tr_logAddTraceSock(this, "utp_on_state_change -- changed to writable");
            if (is_write_enabled())
            {
                write_cb();
            }
            break;
        case UTP_STATE_EOF:
            {
                auto error = tr_error{};
                error.set_from_errno(ENOTCONN);
                error_cb(error);
            }
            break;
        case UTP_STATE_DESTROYING:
            tr_logAddErrorSock(this, "Impossible state UTP_STATE_DESTROYING");
            break;
        default:
            tr_logAddErrorSock(this, fmt::format(fmt::runtime(_("Unknown state: {state}")), fmt::arg("state", state)));
            break;
        }
    }

    void on_utp_error(int const errcode) const
    {
        tr_logAddTraceSock(this, fmt::format("on_utp_error -- {}", utp_error_code_names[errcode]));

        if (!error_cb_)
        {
            return;
        }

        auto error = tr_error{};
        switch (errcode)
        {
        case UTP_ECONNREFUSED:
            error.set_from_errno(ECONNREFUSED);
            break;
        case UTP_ECONNRESET:
            error.set_from_errno(ECONNRESET);
            break;
        case UTP_ETIMEDOUT:
            error.set_from_errno(ETIMEDOUT);
            break;
        default:
            error.set(errcode, utp_error_code_names[errcode]);
            break;
        }

        error_cb_(error);
    }

private:
    size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* /*error*/) override
    {
        n_bytes = std::min(n_bytes, read_buffer_size());
        buf.add(std::data(inbuf_), n_bytes);
        inbuf_.drain(n_bytes);
        return n_bytes;
    }

    size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* error) override
    {
        n_bytes = std::min(n_bytes, std::size(buf));
        if (n_bytes == 0U)
        {
            return {};
        }

        set_sockerrno(0);
        // NB: utp_write() does not modify its 2nd arg, but a wart in
        // libutp's public API requires it to be non-const anyway :shrug:
        if (auto const n_written = utp_write(sock_, const_cast<std::byte*>(std::data(buf)), n_bytes); n_written >= 0)
        {
            buf.drain(n_written);
            return static_cast<size_t>(n_written);
        }

        if (auto const error_code = sockerrno; error != nullptr && error_code != 0)
        {
            error->set(error_code, tr_net_strerror(error_code));
        }

        return {};
    }

    void read_now()
    {
        // The socket can destruct inside read_cb(), so keep it alive
        // for the duration of this code block. This can happen when
        // a BT handshake did not complete successfully for example.
        auto const keep_alive = shared_from_this();
        tr_logAddTraceSock(this, "this µTP socket is ready for reading");

        is_read_enabled_ = false;
        read_cb();

        // Continue processing any remaining data
        maybe_read_soon();
    }

    // ---

    UTPSocket* sock_;

    // This buffer acts in place of the OS's receive buffer.
    // Care should be taken to have it mimic that behaviour.
    PeerBuffer inbuf_;

    std::unique_ptr<libtransmission::Timer> timer_;

    bool is_read_enabled_ = false;
    bool is_write_enabled_ = false;
};
#endif
} // namespace

tr_peer_socket_utp::tr_peer_socket_utp(tr_socket_address const& socket_address)
    : tr_peer_socket{ socket_address }
{
}

std::shared_ptr<tr_peer_socket_utp> tr_peer_socket_utp::create(
    tr_socket_address const& socket_address,
    UTPSocket* sock,
    libtransmission::TimerMaker& timer_maker)
{
#ifdef WITH_UTP
    return std::make_unique<tr_peer_socket_utp_impl>(socket_address, sock, timer_maker);
#else
    return {};
#endif
}

std::shared_ptr<tr_peer_socket_utp> tr_peer_socket_utp::create(
    tr_socket_address const& socket_address,
    struct_utp_context* ctx,
    libtransmission::TimerMaker& timer_maker)
{
#ifdef WITH_UTP
    auto* const sock = utp_create_socket(ctx);
    auto const [ss, sslen] = socket_address.to_sockaddr();
    if (utp_connect(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) == 0)
    {
        return std::make_unique<tr_peer_socket_utp_impl>(socket_address, sock, timer_maker);
    }
#endif
    return {};
}

void tr_peer_socket_utp::init([[maybe_unused]] struct_utp_context* ctx)
{
#ifdef WITH_UTP
    // Mimic OS UDP socket buffer
    if (auto const rcvbuf = default_udp_bufsize(SO_RCVBUF); rcvbuf > 0)
    {
        utp_context_set_option(ctx, UTP_RCVBUF, rcvbuf);
    }
    if (auto const sndbuf = default_udp_bufsize(SO_SNDBUF); sndbuf > 0)
    {
        utp_context_set_option(ctx, UTP_SNDBUF, sndbuf);
    }

    // note: all the callback handlers here need to check `userdata` for nullptr
    // because libutp can fire callbacks on a socket after utp_close() is called

    utp_set_callback(
        ctx,
        UTP_ON_READ,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->read_buffer().add(args->buf, args->len);
                s->maybe_read_soon();

                // utp_read_drained() notifies libutp that we read a packet from them.
                // It opens up the congestion window by sending an ACK (soonish) if
                // one was not going to be sent.
                utp_read_drained(args->socket);
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_GET_READ_BUFFER_SIZE,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                return s->read_buffer_size();
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_ERROR,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->on_utp_error(args->error_code);
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_STATE_CHANGE,
        [](utp_callback_arguments* const args) -> uint64
        {
            if (auto const* const s = static_cast<tr_peer_socket_utp_impl*>(utp_get_userdata(args->socket)); s != nullptr)
            {
                s->on_utp_state_change(args->state);
            }
            return {};
        });
#endif
}
