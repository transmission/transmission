// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

#include <event2/event.h>
#include <event2/bufferevent.h>

#include <libutp/utp.h>

#include <fmt/format.h>

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "tr-assert.h"
#include "tr-utp.h"
#include "utils.h" // for _()

#ifdef _WIN32
#undef EAGAIN
#define EAGAIN WSAEWOULDBLOCK
#undef EINTR
#define EINTR WSAEINTR
#undef EINPROGRESS
#define EINPROGRESS WSAEINPROGRESS
#undef EPIPE
#define EPIPE WSAECONNRESET
#endif

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

namespace
{
// Helps us to ignore errors that say "try again later"
// since that's what peer-io does by default anyway.
[[nodiscard]] auto constexpr canRetryFromError(int error_code) noexcept
{
    return error_code == 0 || error_code == EAGAIN || error_code == EINTR || error_code == EINPROGRESS;
}

size_t get_desired_output_buffer_size(tr_peerIo const* io, uint64_t now)
{
    // this is all kind of arbitrary, but what seems to work well is
    // being large enough to hold the next 20 seconds' worth of input,
    // or a few blocks, whichever is bigger. OK to tweak this as needed.
    static auto constexpr PeriodSecs = 15U;

    // the 3 is an arbitrary number of blocks;
    // the .5 is to leave room for protocol messages
    static auto constexpr Floor = static_cast<size_t>(tr_block_info::BlockSize * 3.5);

    auto const current_speed_bytes_per_second = io->get_piece_speed_bytes_per_second(now, TR_UP);
    return std::max(Floor, current_speed_bytes_per_second * PeriodSecs);
}
} // namespace

// ---

tr_peerIo::tr_peerIo(
    tr_session* session,
    tr_sha1_digest_t const* info_hash,
    bool is_incoming,
    bool is_seed,
    tr_bandwidth* parent_bandwidth)
    : bandwidth_{ parent_bandwidth }
    , info_hash_{ info_hash != nullptr ? *info_hash : tr_sha1_digest_t{} }
    , session_{ session }
    , is_seed_{ is_seed }
    , is_incoming_{ is_incoming }
{
}

std::shared_ptr<tr_peerIo> tr_peerIo::create(
    tr_session* session,
    tr_bandwidth* parent,
    tr_sha1_digest_t const* info_hash,
    bool is_incoming,
    bool is_seed)
{
    TR_ASSERT(session != nullptr);
    auto lock = session->unique_lock();

    auto io = std::make_shared<tr_peerIo>(session, info_hash, is_incoming, is_seed, parent);
    io->bandwidth().setPeer(io);
    tr_logAddTraceIo(io, fmt::format("bandwidth is {}; its parent is {}", fmt::ptr(&io->bandwidth()), fmt::ptr(parent)));
    return io;
}

std::shared_ptr<tr_peerIo> tr_peerIo::new_incoming(tr_session* session, tr_bandwidth* parent, tr_peer_socket socket)
{
    TR_ASSERT(session != nullptr);

    auto peer_io = tr_peerIo::create(session, parent, nullptr, true, false);
    peer_io->set_socket(std::move(socket));
    return peer_io;
}

std::shared_ptr<tr_peerIo> tr_peerIo::new_outgoing(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const& addr,
    tr_port port,
    tr_sha1_digest_t const& info_hash,
    bool is_seed,
    bool utp)
{
    TR_ASSERT(!tr_peer_socket::limit_reached(session));
    TR_ASSERT(session != nullptr);
    TR_ASSERT(addr.is_valid());
    TR_ASSERT(utp || session->allowsTCP());

    if (!addr.is_valid_for_peers(port))
    {
        return {};
    }

    auto peer_io = tr_peerIo::create(session, parent, &info_hash, false, is_seed);

#ifdef WITH_UTP
    if (utp)
    {
        auto* const sock = utp_create_socket(session->utp_context);
        utp_set_userdata(sock, peer_io.get());
        peer_io->set_socket(tr_peer_socket{ addr, port, sock });

        auto const [ss, sslen] = addr.to_sockaddr(port);
        if (utp_connect(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) == 0)
        {
            return peer_io;
        }
    }
#endif

    if (!peer_io->socket_.is_valid())
    {
        if (auto sock = tr_netOpenPeerSocket(session, addr, port, is_seed); sock.is_valid())
        {
            peer_io->set_socket(std::move(sock));
            return peer_io;
        }
    }

    return {};
}

tr_peerIo::~tr_peerIo()
{
    auto const lock = session_->unique_lock();

    clear_callbacks();
    tr_logAddTraceIo(this, "in tr_peerIo destructor");
    event_disable(EV_READ | EV_WRITE);
    close();
}

// ---

void tr_peerIo::set_socket(tr_peer_socket socket_in)
{
    close(); // tear down the previous socket, if any

    socket_ = std::move(socket_in);

    if (socket_.is_tcp())
    {
        event_read_.reset(event_new(session_->eventBase(), socket_.handle.tcp, EV_READ, &tr_peerIo::event_read_cb, this));
        event_write_.reset(event_new(session_->eventBase(), socket_.handle.tcp, EV_WRITE, &tr_peerIo::event_write_cb, this));
    }
#ifdef WITH_UTP
    else if (socket_.is_utp())
    {
        utp_set_userdata(socket_.handle.utp, this);
    }
#endif
    else
    {
        TR_ASSERT_MSG(false, "unsupported peer socket type");
    }
}

void tr_peerIo::close()
{
    socket_.close();
    event_write_.reset();
    event_read_.reset();
}

void tr_peerIo::clear()
{
    clear_callbacks();
    set_enabled(TR_UP, false);
    set_enabled(TR_DOWN, false);
    close();
}

bool tr_peerIo::reconnect()
{
    TR_ASSERT(!this->is_incoming());
    TR_ASSERT(this->session_->allowsTCP());

    short int const pending_events = this->pending_events_;
    event_disable(EV_READ | EV_WRITE);

    close();

    if (tr_peer_socket::limit_reached(session_))
    {
        return false;
    }

    auto const [addr, port] = socket_address();
    socket_ = tr_netOpenPeerSocket(session_, addr, port, is_seed());

    if (!socket_.is_tcp())
    {
        return false;
    }

    this->event_read_.reset(event_new(session_->eventBase(), socket_.handle.tcp, EV_READ, event_read_cb, this));
    this->event_write_.reset(event_new(session_->eventBase(), socket_.handle.tcp, EV_WRITE, event_write_cb, this));

    event_enable(pending_events);

    return true;
}

// ---

void tr_peerIo::did_write_wrapper(size_t bytes_transferred)
{
    auto const keep_alive = shared_from_this();

    while (bytes_transferred != 0 && !std::empty(outbuf_info_))
    {
        auto& [n_bytes_left, is_piece_data] = outbuf_info_.front();

        size_t const payload = std::min(uint64_t{ n_bytes_left }, uint64_t{ bytes_transferred });
        /* For µTP sockets, the overhead is computed in utp_on_overhead. */
        size_t const overhead = socket_.guess_packet_overhead(payload);
        uint64_t const now = tr_time_msec();

        bandwidth().notifyBandwidthConsumed(TR_UP, payload, is_piece_data, now);

        if (overhead > 0)
        {
            bandwidth().notifyBandwidthConsumed(TR_UP, overhead, false, now);
        }

        if (did_write_ != nullptr)
        {
            did_write_(this, payload, is_piece_data, user_data_);
        }

        bytes_transferred -= payload;
        n_bytes_left -= payload;
        if (n_bytes_left == 0)
        {
            outbuf_info_.pop_front();
        }
    }
}

size_t tr_peerIo::try_write(size_t max)
{
    static auto constexpr Dir = TR_UP;

    if (max == 0)
    {
        return {};
    }

    auto& buf = outbuf_;
    max = std::min(max, std::size(buf));
    max = bandwidth().clamp(Dir, max);
    if (max == 0)
    {
        set_enabled(Dir, false);
        return {};
    }

    tr_error* error = nullptr;
    auto const n_written = socket_.try_write(buf, max, &error);
    // enable further writes if there's more data to write
    set_enabled(Dir, !std::empty(buf) && (error == nullptr || canRetryFromError(error->code)));

    if (error != nullptr)
    {
        if (!canRetryFromError(error->code))
        {
            tr_logAddTraceIo(
                this,
                fmt::format("try_write err: wrote:{}, errno:{} ({})", n_written, error->code, error->message));
            call_error_callback(*error);
        }

        tr_error_clear(&error);
    }
    else if (n_written > 0U)
    {
        did_write_wrapper(n_written);
    }

    return n_written;
}

void tr_peerIo::event_write_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* const io = static_cast<tr_peerIo*>(vio);
    tr_logAddTraceIo(io, "libevent says this peer socket is ready for writing");

    TR_ASSERT(io->socket_.is_tcp());
    TR_ASSERT(io->socket_.handle.tcp == fd);

    io->pending_events_ &= ~EV_WRITE;

    // Write as much as possible. Since the socket is non-blocking,
    // write() will return if it can't write any more without blocking
    io->try_write(SIZE_MAX);
}

// ---

void tr_peerIo::can_read_wrapper()
{
    // try to consume the input buffer

    if (can_read_ == nullptr)
    {
        return;
    }

    auto const lock = session_->unique_lock();
    auto const keep_alive = shared_from_this();

    auto const now = tr_time_msec();
    auto done = bool{ false };
    auto err = bool{ false };

    while (!done && !err)
    {
        size_t piece = 0;
        auto const old_len = read_buffer_size();
        auto const read_state = can_read_ != nullptr ? can_read_(this, user_data_, &piece) : READ_ERR;
        auto const used = old_len - read_buffer_size();
        auto const overhead = socket_.guess_packet_overhead(used);

        if (piece != 0 || piece != used)
        {
            if (piece != 0)
            {
                bandwidth().notifyBandwidthConsumed(TR_DOWN, piece, true, now);
            }

            if (used != piece)
            {
                bandwidth().notifyBandwidthConsumed(TR_DOWN, used - piece, false, now);
            }
        }

        if (overhead > 0)
        {
            bandwidth().notifyBandwidthConsumed(TR_DOWN, overhead, false, now);
        }

        switch (read_state)
        {
        case READ_NOW:
            if (!std::empty(inbuf_))
            {
                continue;
            }

            done = true;
            break;

        case READ_LATER:
            done = true;
            break;

        case READ_ERR:
            err = true;
            break;
        }
    }
}

size_t tr_peerIo::try_read(size_t max)
{
    static auto constexpr Dir = TR_DOWN;

    if (max == 0)
    {
        return {};
    }

    // Do not write more than the bandwidth allows.
    // If there is no bandwidth left available, disable writes.
    max = bandwidth().clamp(TR_DOWN, max);
    if (max == 0)
    {
        set_enabled(Dir, false);
        return {};
    }

    auto& buf = inbuf_;
    tr_error* error = nullptr;
    auto const n_read = socket_.try_read(buf, max, &error);
    set_enabled(Dir, error == nullptr || canRetryFromError(error->code));

    if (error != nullptr)
    {
        if (!canRetryFromError(error->code))
        {
            tr_logAddTraceIo(this, fmt::format("try_read err: n_read:{} errno:{} ({})", n_read, error->code, error->message));
            call_error_callback(*error);
        }

        tr_error_clear(&error);
    }
    else if (!std::empty(buf))
    {
        can_read_wrapper();
    }

    return n_read;
}

void tr_peerIo::event_read_cb([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vio)
{
    static auto constexpr MaxLen = RcvBuf;

    auto* const io = static_cast<tr_peerIo*>(vio);
    tr_logAddTraceIo(io, "libevent says this peer socket is ready for reading");

    TR_ASSERT(io->socket_.is_tcp());
    TR_ASSERT(io->socket_.handle.tcp == fd);

    io->pending_events_ &= ~EV_READ;

    // if we don't have any bandwidth left, stop reading
    auto const n_used = std::size(io->inbuf_);
    auto const n_left = n_used >= MaxLen ? 0 : MaxLen - n_used;
    io->try_read(n_left);
}

// ---

void tr_peerIo::event_enable(short event)
{
    TR_ASSERT(session_ != nullptr);

    bool const need_events = socket_.is_tcp();
    TR_ASSERT(!need_events || event_read_);
    TR_ASSERT(!need_events || event_write_);

    if ((event & EV_READ) != 0 && (pending_events_ & EV_READ) == 0)
    {
        tr_logAddTraceIo(this, "enabling ready-to-read polling");

        if (need_events)
        {
            event_add(event_read_.get(), nullptr);
        }

        pending_events_ |= EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (pending_events_ & EV_WRITE) == 0)
    {
        tr_logAddTraceIo(this, "enabling ready-to-write polling");

        if (need_events)
        {
            event_add(event_write_.get(), nullptr);
        }

        pending_events_ |= EV_WRITE;
    }
}

void tr_peerIo::event_disable(short event)
{
    bool const need_events = socket_.is_tcp();
    TR_ASSERT(!need_events || event_read_);
    TR_ASSERT(!need_events || event_write_);

    if ((event & EV_READ) != 0 && (pending_events_ & EV_READ) != 0)
    {
        tr_logAddTraceIo(this, "disabling ready-to-read polling");

        if (need_events)
        {
            event_del(event_read_.get());
        }

        pending_events_ &= ~EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (pending_events_ & EV_WRITE) != 0)
    {
        tr_logAddTraceIo(this, "disabling ready-to-write polling");

        if (need_events)
        {
            event_del(event_write_.get());
        }

        pending_events_ &= ~EV_WRITE;
    }
}

void tr_peerIo::set_enabled(tr_direction dir, bool is_enabled)
{
    TR_ASSERT(tr_isDirection(dir));

    short const event = dir == TR_UP ? EV_WRITE : EV_READ;

    if (is_enabled)
    {
        event_enable(event);
    }
    else
    {
        event_disable(event);
    }
}

size_t tr_peerIo::flush(tr_direction dir, size_t limit)
{
    TR_ASSERT(tr_isDirection(dir));

    return dir == TR_DOWN ? try_read(limit) : try_write(limit);
}

size_t tr_peerIo::flush_outgoing_protocol_msgs()
{
    size_t byte_count = 0;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for (auto const& [n_bytes, is_piece_data] : outbuf_info_)
    {
        if (is_piece_data)
        {
            break;
        }

        byte_count += n_bytes;
    }

    return flush(TR_UP, byte_count);
}

// ---

size_t tr_peerIo::get_write_buffer_space(uint64_t now) const noexcept
{
    size_t const desired_len = get_desired_output_buffer_size(this, now);
    size_t const current_len = std::size(outbuf_);
    return desired_len > current_len ? desired_len - current_len : 0U;
}

void tr_peerIo::write(libtransmission::Buffer& buf, bool is_piece_data)
{
    auto [bytes, len] = buf.pullup();
    encrypt(len, bytes);
    outbuf_info_.emplace_back(std::size(buf), is_piece_data);
    outbuf_.add(buf);
}

void tr_peerIo::write_bytes(void const* bytes, size_t n_bytes, bool is_piece_data)
{
    auto const old_size = std::size(outbuf_);

    outbuf_.reserve(old_size + n_bytes);
    outbuf_.add(bytes, n_bytes);

    for (auto iter = std::begin(outbuf_) + old_size, end = std::end(outbuf_); iter != end; ++iter)
    {
        encrypt(1, &*iter);
    }

    outbuf_info_.emplace_back(n_bytes, is_piece_data);
}

// ---

void tr_peerIo::read_bytes(void* bytes, size_t byte_count)
{
    TR_ASSERT(read_buffer_size() >= byte_count);

    inbuf_.to_buf(bytes, byte_count);

    if (is_encrypted())
    {
        decrypt(byte_count, bytes);
    }
}

void tr_peerIo::read_uint16(uint16_t* setme)
{
    auto tmp = uint16_t{};
    read_bytes(&tmp, sizeof(tmp));
    *setme = ntohs(tmp);
}

void tr_peerIo::read_uint32(uint32_t* setme)
{
    auto tmp = uint32_t{};
    read_bytes(&tmp, sizeof(tmp));
    *setme = ntohl(tmp);
}

void tr_peerIo::read_buffer_drain(size_t byte_count)
{
    auto buf = std::array<char, 4096>{};

    while (byte_count > 0)
    {
        auto const this_pass = std::min(byte_count, std::size(buf));
        read_bytes(std::data(buf), this_pass);
        byte_count -= this_pass;
    }
}

// --- UTP

#ifdef WITH_UTP

void tr_peerIo::on_utp_state_change(int state)
{
    if (state == UTP_STATE_CONNECT)
    {
        tr_logAddTraceIo(this, "utp_on_state_change -- changed to connected");
        utp_supported_ = true;
    }
    else if (state == UTP_STATE_WRITABLE)
    {
        tr_logAddTraceIo(this, "utp_on_state_change -- changed to writable");

        if ((pending_events_ & EV_WRITE) != 0)
        {
            try_write(SIZE_MAX);
        }
    }
    else if (state == UTP_STATE_EOF)
    {
        tr_error* error = nullptr;
        tr_error_set_from_errno(&error, ENOTCONN);
        call_error_callback(*error);
        tr_error_clear(&error);
    }
    else if (state == UTP_STATE_DESTROYING)
    {
        tr_logAddErrorIo(this, "Impossible state UTP_STATE_DESTROYING");
    }
    else
    {
        tr_logAddErrorIo(this, fmt::format(_("Unknown state: {state}"), fmt::arg("state", state)));
    }
}

void tr_peerIo::on_utp_error(int errcode)
{
    tr_logAddTraceIo(this, fmt::format("utp_on_error -- {}", utp_error_code_names[errcode]));

    if (got_error_ == nullptr)
    {
        return;
    }

    tr_error* error = nullptr;
    switch (errcode)
    {
    case UTP_ECONNREFUSED:
        tr_error_set_from_errno(&error, ECONNREFUSED);
        break;
    case UTP_ECONNRESET:
        tr_error_set_from_errno(&error, ECONNRESET);
        break;
    case UTP_ETIMEDOUT:
        tr_error_set_from_errno(&error, ETIMEDOUT);
        break;
    default:
        tr_error_set(&error, errcode, utp_error_code_names[errcode]);
    }
    call_error_callback(*error);
    tr_error_clear(&error);
}

#endif /* #ifdef WITH_UTP */

void tr_peerIo::utp_init([[maybe_unused]] struct_utp_context* ctx)
{
#ifdef WITH_UTP
    utp_context_set_option(ctx, UTP_RCVBUF, RcvBuf);

    // note: all the callback handlers here need to check `userdata` for nullptr
    // because libutp can fire callbacks on a socket after utp_close() is called

    utp_set_callback(
        ctx,
        UTP_ON_READ,
        [](utp_callback_arguments* args) -> uint64
        {
            if (auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket)); io != nullptr)
            {
                io->inbuf_.add(args->buf, args->len);
                io->set_enabled(TR_DOWN, true);
                io->can_read_wrapper();
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_GET_READ_BUFFER_SIZE,
        [](utp_callback_arguments* args) -> uint64
        {
            if (auto const* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket)); io != nullptr)
            {
                // We use this callback to enforce speed limits by telling
                // libutp to read no more than `target_dl_bytes` bytes.
                auto const target_dl_bytes = io->bandwidth_.clamp(TR_DOWN, RcvBuf);

                // libutp's private function get_rcv_window() allows libutp
                // to read up to (UTP_RCVBUF - READ_BUFFER_SIZE) bytes and
                // UTP_RCVBUF is set to `RcvBuf` by tr_peerIo::utp_init().
                // So to limit dl to `target_dl_bytes`, we need to return
                // N where (`target_dl_bytes` == RcvBuf - N).
                return RcvBuf - target_dl_bytes;
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_ERROR,
        [](utp_callback_arguments* args) -> uint64
        {
            if (auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket)); io != nullptr)
            {
                io->on_utp_error(args->error_code);
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_OVERHEAD_STATISTICS,
        [](utp_callback_arguments* args) -> uint64
        {
            if (auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket)); io != nullptr)
            {
                tr_logAddTraceIo(io, fmt::format("{:d} overhead bytes via utp", args->len));
                io->bandwidth().notifyBandwidthConsumed(args->send != 0 ? TR_UP : TR_DOWN, args->len, false, tr_time_msec());
            }
            return {};
        });

    utp_set_callback(
        ctx,
        UTP_ON_STATE_CHANGE,
        [](utp_callback_arguments* args) -> uint64
        {
            if (auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket)); io != nullptr)
            {
                io->on_utp_state_change(args->state);
            }
            return {};
        });
#endif
}
