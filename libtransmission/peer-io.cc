// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <cstdint>
#include <functional>
#include <type_traits> // std::underlying_type_t

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h> // ntohl, ntohs
#endif

#include <fmt/format.h>

#include "libtransmission/transmission.h"

#include "libtransmission/bandwidth.h"
#include "libtransmission/block-info.h" // tr_block_info
#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-io.h"
#include "libtransmission/peer-socket-tcp.h"
#include "libtransmission/peer-socket-utp.h"
#include "libtransmission/peer-socket.h" // tr_peer_socket, tr_netOpen...
#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h" // for _()

struct sockaddr;

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

namespace
{
// Helps us to ignore errors that say "try again later"
// since that's what peer-io does by default anyway.
[[nodiscard]] constexpr auto can_retry_from_error(int error_code) noexcept
{
#ifdef _WIN32
    return error_code == 0 || error_code == WSAEWOULDBLOCK || error_code == WSAEINTR || error_code == WSAEINPROGRESS;
#else
    return error_code == 0 || error_code == EAGAIN || error_code == EWOULDBLOCK || error_code == EINTR ||
        error_code == EINPROGRESS;
#endif
}

size_t get_desired_output_buffer_size(tr_peerIo const* io, uint64_t now)
{
    // this is all kind of arbitrary, but what seems to work well is
    // being large enough to hold the next 20 seconds' worth of input,
    // or a few blocks, whichever is bigger. OK to tweak this as needed.
    static auto constexpr PeriodSecs = uint64_t{ 15U };

    // the 3 is an arbitrary number of blocks;
    // the .5 is to leave room for protocol messages
    static auto constexpr Floor = static_cast<uint64_t>(tr_block_info::BlockSize * 3.5);

    auto const current_speed = io->get_piece_speed(now, TR_UP);
    return std::max(Floor, current_speed.base_quantity() * PeriodSecs);
}
} // namespace

// ---

tr_peerIo::tr_peerIo(
    tr_session* session,
    tr_bandwidth* parent_bandwidth,
    tr_sha1_digest_t const* info_hash,
    bool is_incoming,
    bool client_is_seed)
    : bandwidth_{ parent_bandwidth }
    , info_hash_{ info_hash != nullptr ? *info_hash : tr_sha1_digest_t{} }
    , session_{ session }
    , client_is_seed_{ client_is_seed }
    , is_incoming_{ is_incoming }
{
}

std::shared_ptr<tr_peerIo> tr_peerIo::create(
    tr_session* session,
    std::shared_ptr<tr_peer_socket>&& socket,
    tr_bandwidth* parent,
    tr_sha1_digest_t const* info_hash,
    bool is_incoming,
    bool is_seed)
{
    TR_ASSERT(session != nullptr);
    auto lock = session->unique_lock();

    auto const io = std::shared_ptr<tr_peerIo>{ new tr_peerIo{ session, parent, info_hash, is_incoming, is_seed } };
    io->set_socket(std::move(socket));
    io->bandwidth().set_peer(io);
    tr_logAddTraceIo(io, fmt::format("bandwidth is {}; its parent is {}", fmt::ptr(&io->bandwidth()), fmt::ptr(parent)));
    return io;
}

std::shared_ptr<tr_peerIo> tr_peerIo::new_incoming(
    tr_session* session,
    tr_bandwidth* parent,
    std::shared_ptr<tr_peer_socket>&& socket)
{
    TR_ASSERT(session != nullptr);
    return tr_peerIo::create(session, std::move(socket), parent, nullptr, true, false);
}

std::shared_ptr<tr_peerIo> tr_peerIo::new_outgoing(
    tr_session* session,
    tr_bandwidth* parent,
    tr_socket_address const& socket_address,
    tr_sha1_digest_t const& info_hash,
    bool client_is_seed,
    bool utp)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(socket_address.is_valid());
    TR_ASSERT(!std::empty(session->preferred_transports()));

    // N.B. This array needs to be kept in the same order as
    // the tr_preferred_transport enum.
    auto const get_socket = std::array<std::function<std::shared_ptr<tr_peer_socket>()>, TR_NUM_PREFERRED_TRANSPORT>{
        [&]() -> std::shared_ptr<tr_peer_socket>
        {
            if (utp)
            {
                return tr_peer_socket_utp::create(socket_address, session->utp_context, session->timerMaker());
            }
            return {};
        },

        [&]
        {
            return tr_peer_socket_tcp::create(*session, socket_address, client_is_seed);
        }
    };

    for (auto const& transport : session->preferred_transports())
    {
        if (auto sock = get_socket[transport](); sock)
        {
            return tr_peerIo::create(session, std::move(sock), parent, &info_hash, false, client_is_seed);
        }
    }

    return {};
}

tr_peerIo::~tr_peerIo()
{
    auto const lock = session_->unique_lock();

    clear_callbacks();
    close();
}

// ---

void tr_peerIo::set_socket(std::shared_ptr<tr_peer_socket> socket_in)
{
    TR_ASSERT(socket_in);
    close(); // tear down the previous socket, if any

    socket_ = std::move(socket_in);

    socket_->set_read_cb(
        [weak = weak_from_this()]()
        {
            if (auto const io = weak.lock(); io)
            {
                io->read_cb();
            }
        });
    socket_->set_write_cb(
        [weak = weak_from_this()]()
        {
            if (auto const io = weak.lock(); io)
            {
                io->write_cb();
            }
        });
    socket_->set_error_cb(
        [weak = weak_from_this()](tr_error const& error)
        {
            if (auto const io = weak.lock(); io)
            {
                io->call_error_callback(error);
            }
        });
}

void tr_peerIo::close()
{
    if (socket_)
    {
        tr_logAddTraceIo(this, "closing tr_peerIo");
        socket_.reset();
    }
    inbuf_.clear();
    outbuf_.clear();
    outbuf_info_.clear();
    encrypt_disable();
    decrypt_disable();
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
    TR_ASSERT(!is_incoming());
    if (!session_->allowsTCP())
    {
        return false;
    }

    auto const was_read_enabled = socket_->is_read_enabled();
    auto const was_write_enabled = socket_->is_write_enabled();

    auto const sockaddr = socket_address();
    close();

    auto s = tr_peer_socket_tcp::create(*session_, sockaddr, client_is_seed());
    if (!s)
    {
        return false;
    }

    set_socket(std::move(s));

    socket_->set_read_enabled(was_read_enabled);
    socket_->set_write_enabled(was_write_enabled);

    return true;
}

// ---

void tr_peerIo::did_write_wrapper(size_t bytes_transferred)
{
    auto const keep_alive = shared_from_this();
    auto const now = tr_time_msec();

    if (bytes_transferred > 0U)
    {
        bandwidth().notify_bandwidth_consumed(TR_UP, bytes_transferred, false, now);
    }

    while (bytes_transferred > 0U && !std::empty(outbuf_info_))
    {
        auto& [n_bytes_left, is_piece_data] = outbuf_info_.front();
        auto const payload = std::min(n_bytes_left, bytes_transferred);

        if (is_piece_data)
        {
            bandwidth().notify_bandwidth_consumed(TR_UP, payload, true, now);
        }

        if (did_write_ != nullptr)
        {
            did_write_(this, payload, is_piece_data, user_data_);
        }

        bytes_transferred -= payload;
        n_bytes_left -= payload;
        if (n_bytes_left == 0U)
        {
            outbuf_info_.pop_front();
        }
    }
}

size_t tr_peerIo::try_write(size_t max)
{
    static auto constexpr Dir = TR_UP;

    if (max == 0U)
    {
        return {};
    }

    auto& buf = outbuf_;
    max = std::min(max, std::size(buf));
    max = bandwidth().clamp(Dir, max);
    if (max == 0U)
    {
        set_enabled(Dir, false);
        return {};
    }

    auto error = tr_error{};
    auto const n_written = socket_->try_write(buf, max, &error);
    // enable further writes if there's more data to write
    set_enabled(Dir, !std::empty(buf) && (!error || can_retry_from_error(error.code())));

    if (error)
    {
        if (!can_retry_from_error(error.code()))
        {
            tr_logAddTraceIo(
                this,
                fmt::format("try_write err: wrote:{}, errno:{} ({})", n_written, error.code(), error.message()));
            call_error_callback(error);
        }
    }
    else if (n_written > 0U)
    {
        did_write_wrapper(n_written);
    }

    return n_written;
}

void tr_peerIo::write_cb()
{
    // Write as much as possible. Since the socket is non-blocking,
    // write() will return if it can't write any more without blocking
    auto const n_wrote = try_write(SIZE_MAX);
    tr_logAddTraceIo(this, fmt::format("tr_peerIo::write_cb() wrote {} bytes", n_wrote));
}

// ---

void tr_peerIo::can_read_wrapper(size_t bytes_transferred)
{
    // try to consume the input buffer

    if (can_read_ == nullptr)
    {
        return;
    }

    auto const lock = session_->unique_lock();
    auto const keep_alive = shared_from_this();

    auto const now = tr_time_msec();
    auto done = false;
    auto err = false;

    if (bytes_transferred > 0U)
    {
        bandwidth().notify_bandwidth_consumed(TR_DOWN, bytes_transferred, false, now);
    }

    while (!done && !err)
    {
        auto piece = size_t{};
        auto const read_state = can_read_ != nullptr ? can_read_(this, user_data_, &piece) : ReadState::Err;

        if (piece > 0U)
        {
            bandwidth().notify_bandwidth_consumed(TR_DOWN, piece, true, now);
        }

        switch (read_state)
        {
        case ReadState::Now:
        case ReadState::Break:
            if (std::empty(inbuf_))
            {
                done = true;
            }
            break;

        case ReadState::Later:
            done = true;
            break;

        case ReadState::Err:
            err = true;
            break;
        }
    }
}

size_t tr_peerIo::try_read(size_t max)
{
    static auto constexpr Dir = TR_DOWN;

    if (max == 0U)
    {
        return {};
    }

    // Do not read more than the bandwidth allows.
    // If there is no bandwidth left available, disable reads.
    max = bandwidth().clamp(Dir, max);
    if (max == 0U)
    {
        set_enabled(Dir, false);
        return {};
    }

    auto& buf = inbuf_;
    auto error = tr_error{};
    auto const n_read = socket_->try_read(buf, max, &error);
    set_enabled(Dir, !error || can_retry_from_error(error.code()));

    if (error)
    {
        if (!can_retry_from_error(error.code()))
        {
            tr_logAddTraceIo(this, fmt::format("try_read err: n_read:{} errno:{} ({})", n_read, error.code(), error.message()));
            call_error_callback(error);
        }
    }
    else if (!std::empty(buf))
    {
        can_read_wrapper(n_read);
    }

    return n_read;
}

void tr_peerIo::read_cb()
{
    static auto constexpr MaxLen = RcvBuf;

    // if we don't have any bandwidth left, stop reading
    auto const n_used = read_buffer_size();
    auto const n_left = n_used >= MaxLen ? 0U : MaxLen - n_used;
    auto const n_read = try_read(n_left);
    tr_logAddTraceIo(this, fmt::format("tr_peerIo::read_cb() read {} bytes", n_read));
}

// ---

void tr_peerIo::set_enabled(tr_direction dir, bool is_enabled)
{
    TR_ASSERT(tr_isDirection(dir));

    if (dir == TR_UP)
    {
        socket_->set_write_enabled(is_enabled);
    }
    else
    {
        socket_->set_read_enabled(is_enabled);
    }
}

// ---

size_t tr_peerIo::flush(tr_direction dir, size_t limit)
{
    TR_ASSERT(tr_isDirection(dir));

    return dir == TR_DOWN ? try_read(limit) : try_write(limit);
}

size_t tr_peerIo::flush_outgoing_protocol_msgs()
{
    size_t byte_count = 0U;

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

void tr_peerIo::write_bytes(void const* bytes, size_t n_bytes, bool is_piece_data)
{
    if (n_bytes == 0U)
    {
        return;
    }

    outbuf_info_.emplace_back(n_bytes, is_piece_data);

    auto [resbuf, reslen] = outbuf_.reserve_space(n_bytes);
    filter_.encrypt(reinterpret_cast<std::byte const*>(bytes), n_bytes, resbuf);
    outbuf_.commit_space(n_bytes);

    session_->queue_session_thread(
        [ptr = weak_from_this()]()
        {
            if (auto io = ptr.lock(); io)
            {
                io->try_write(SIZE_MAX);
            }
        });
}

// ---

size_t tr_peerIo::get_write_buffer_space(uint64_t now) const noexcept
{
    size_t const desired_len = get_desired_output_buffer_size(this, now);
    size_t const current_len = std::size(outbuf_);
    return desired_len > current_len ? desired_len - current_len : 0U;
}

// ---

void tr_peerIo::read_bytes(void* bytes, size_t n_bytes)
{
    auto walk = reinterpret_cast<std::byte*>(bytes);
    n_bytes = std::min(n_bytes, std::size(inbuf_));
    if (n_decrypt_remain_)
    {
        if (auto& n_remain = *n_decrypt_remain_; n_remain <= n_bytes)
        {
            filter_.decrypt(std::data(inbuf_), n_remain, walk);
            inbuf_.drain(n_remain);
            if (walk != nullptr)
            {
                walk += n_remain;
            }
            n_bytes -= n_remain;
            filter_.decrypt_disable();
            n_decrypt_remain_.reset();
        }
        else
        {
            n_remain -= n_bytes;
        }
    }
    filter_.decrypt(std::data(inbuf_), n_bytes, walk);
    inbuf_.drain(n_bytes);
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
