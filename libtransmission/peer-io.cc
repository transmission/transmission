// This file Copyright © 2007-2022 Mnemosyne LLC.
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
#include "utils.h"

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

/* The amount of read bufferring that we allow for µTP sockets. */

static constexpr auto UtpReadBufferSize = 256 * 1024;

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->display_name())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

/***
****
***/

void tr_peerIo::did_write_wrapper(size_t bytes_transferred)
{
    auto const keep_alive = shared_from_this();

    while (bytes_transferred != 0 && !std::empty(outbuf_info))
    {
        auto& [n_bytes_left, is_piece_data] = outbuf_info.front();

        size_t const payload = std::min(uint64_t{ n_bytes_left }, uint64_t{ bytes_transferred });
        /* For µTP sockets, the overhead is computed in utp_on_overhead. */
        size_t const overhead = socket.guess_packet_overhead(payload);
        uint64_t const now = tr_time_msec();

        bandwidth().notifyBandwidthConsumed(TR_UP, payload, is_piece_data, now);

        if (overhead > 0)
        {
            bandwidth().notifyBandwidthConsumed(TR_UP, overhead, false, now);
        }

        if (didWrite != nullptr)
        {
            didWrite(this, payload, is_piece_data, userData);
        }

        bytes_transferred -= payload;
        n_bytes_left -= payload;
        if (n_bytes_left == 0)
        {
            outbuf_info.pop_front();
        }
    }
}

void tr_peerIo::can_read_wrapper()
{
    /* try to consume the input buffer */
    if (!canRead)
    {
        return;
    }

    auto const lock = session->unique_lock();
    auto const keep_alive = shared_from_this();

    auto const now = tr_time_msec();
    auto done = bool{ false };
    auto err = bool{ false };

    while (!done && !err)
    {
        size_t piece = 0;
        auto const old_len = readBufferSize();
        auto const read_state = canRead ? canRead(this, userData, &piece) : READ_ERR;
        auto const used = old_len - readBufferSize();
        auto const overhead = socket.guess_packet_overhead(used);

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
            bandwidth().notifyBandwidthConsumed(TR_UP, overhead, false, now);
        }

        switch (read_state)
        {
        case READ_NOW:
            if (readBufferSize() != 0)
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

// Helps us to ignore errors that say "try again later"
// since that's what peer-io does by default anyway.
[[nodiscard]] static auto constexpr canRetryFromError(int error_code) noexcept
{
    return error_code == 0 || error_code == EAGAIN || error_code == EINTR || error_code == EINPROGRESS;
}

void tr_peerIo::event_read_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    static auto constexpr MaxLen = size_t{ 256 * 1024 }; // don't let inbuf get too big

    auto* const io = static_cast<tr_peerIo*>(vio);
    tr_logAddTraceIo(io, "libevent says this peer socket is ready for reading");

    TR_ASSERT(io->socket.is_tcp());
    TR_ASSERT(io->socket.handle.tcp == fd);

    io->pendingEvents &= ~EV_READ;

    // if we don't have any bandwidth left, stop reading
    auto const n_used = std::size(io->inbuf);
    auto const n_left = n_used >= MaxLen ? 0 : MaxLen - n_used;
    io->try_read(n_left);
}

void tr_peerIo::event_write_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* const io = static_cast<tr_peerIo*>(vio);
    tr_logAddTraceIo(io, "libevent says this peer socket is ready for writing");

    TR_ASSERT(io->socket.is_tcp());
    TR_ASSERT(io->socket.handle.tcp == fd);

    io->pendingEvents &= ~EV_WRITE;

    // Write as much as possible. Since the socket is non-blocking,
    // write() will return if it can't write any more without blocking
    io->try_write(SIZE_MAX);
}

/**
***
**/

#ifdef WITH_UTP
/* µTP callbacks */

void tr_peerIo::readBufferAdd(void const* data, size_t n_bytes)
{
    inbuf.add(data, n_bytes);
    setEnabled(TR_DOWN, true);
    can_read_wrapper();
}

static size_t utp_get_rb_size(tr_peerIo* const io)
{
    auto const bytes = io->bandwidth().clamp(TR_DOWN, UtpReadBufferSize);

    tr_logAddTraceIo(io, fmt::format("utp_get_rb_size is saying it's ready to read {} bytes", bytes));
    return UtpReadBufferSize - bytes;
}

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

        if ((pendingEvents & EV_WRITE) != 0)
        {
            try_write(SIZE_MAX);
        }
    }
    else if (state == UTP_STATE_EOF)
    {
        tr_error* error = nullptr;
        tr_error_set(&error, ENOTCONN, tr_strerror(ENOTCONN));
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

static void utp_on_error(tr_peerIo* const io, int const errcode)
{
    if (errcode == UTP_ETIMEDOUT)
    {
        // high frequency error: we log as trace
        tr_logAddTraceIo(io, fmt::format("utp_on_error -- UTP_ETIMEDOUT"));
    }
    else
    {
        tr_logAddDebugIo(io, fmt::format("utp_on_error -- {}", utp_error_code_names[errcode]));
    }

    if (io->gotError != nullptr)
    {
        tr_error* error = nullptr;
        tr_error_set(&error, errcode, utp_error_code_names[errcode]);
        io->call_error_callback(*error);
        tr_error_clear(&error);
    }
}

static void utp_on_overhead(tr_peerIo* const io, bool const send, size_t const count, int /*type*/)
{
    tr_logAddTraceIo(io, fmt::format("utp_on_overhead -- count is {}", count));

    io->bandwidth().notifyBandwidthConsumed(send ? TR_UP : TR_DOWN, count, false, tr_time_msec());
}

static uint64 utp_callback(utp_callback_arguments* args)
{
    auto const type = args->callback_type;

    // utp_close() code comment: "Data will keep to try being delivered after the close."
    // That comes through this callback, so it's possible for `io` to be nullptr here.
    auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket));
    if (io == nullptr)
    {
        return {};
    }

    switch (type)
    {
    case UTP_ON_READ:
        io->readBufferAdd(args->buf, args->len);
        break;

    case UTP_GET_READ_BUFFER_SIZE:
        return io == nullptr ? 0U : utp_get_rb_size(io);

    case UTP_ON_STATE_CHANGE:
        io->on_utp_state_change(args->u1.state);
        break;

    case UTP_ON_ERROR:
        utp_on_error(io, args->u1.error_code);
        break;

    case UTP_ON_OVERHEAD_STATISTICS:
        utp_on_overhead(io, args->u1.send != 0, args->len, args->u2.type);
        break;

    default:
        TR_ASSERT_MSG(false, utp_callback_names[type]);
        break;
    }

    return {};
}

#endif /* #ifdef WITH_UTP */

tr_peerIo::tr_peerIo(
    tr_session* session_in,
    tr_sha1_digest_t const* torrent_hash,
    bool is_incoming,
    bool is_seed,
    tr_bandwidth* parent_bandwidth)
    : session{ session_in }
    , bandwidth_{ parent_bandwidth }
    , torrent_hash_{ torrent_hash != nullptr ? *torrent_hash : tr_sha1_digest_t{} }
    , is_seed_{ is_seed }
    , is_incoming_{ is_incoming }
{
}

void tr_peerIo::set_socket(tr_peer_socket socket_in)
{
    // tear down the previous socket, if any
    event_read.reset();
    event_write.reset();
    socket.close(session);

    socket = std::move(socket_in);

    if (socket.is_tcp())
    {
        event_read.reset(event_new(session->eventBase(), socket.handle.tcp, EV_READ, &tr_peerIo::event_read_cb, this));
        event_write.reset(event_new(session->eventBase(), socket.handle.tcp, EV_WRITE, event_write_cb, this));
    }
#ifdef WITH_UTP
    else if (socket.is_utp())
    {
        utp_set_userdata(socket.handle.utp, this);
    }
#endif
    else
    {
        TR_ASSERT_MSG(false, "unsupported peer socket type");
    }
}

std::shared_ptr<tr_peerIo> tr_peerIo::create(
    tr_session* session,
    tr_bandwidth* parent,
    tr_sha1_digest_t const* torrent_hash,
    bool is_incoming,
    bool is_seed)
{
    TR_ASSERT(session != nullptr);
    auto lock = session->unique_lock();

    auto io = std::make_shared<tr_peerIo>(session, torrent_hash, is_incoming, is_seed, parent);
    io->bandwidth().setPeer(io);
    tr_logAddTraceIo(io, fmt::format("bandwidth is {}; its parent is {}", fmt::ptr(&io->bandwidth()), fmt::ptr(parent)));
    return io;
}

void tr_peerIo::utpInit([[maybe_unused]] struct_utp_context* ctx)
{
#ifdef WITH_UTP

    utp_set_callback(ctx, UTP_GET_READ_BUFFER_SIZE, &utp_callback);
    utp_set_callback(ctx, UTP_ON_ERROR, &utp_callback);
    utp_set_callback(ctx, UTP_ON_OVERHEAD_STATISTICS, &utp_callback);
    utp_set_callback(ctx, UTP_ON_READ, &utp_callback);
    utp_set_callback(ctx, UTP_ON_STATE_CHANGE, &utp_callback);

    utp_context_set_option(ctx, UTP_RCVBUF, UtpReadBufferSize);

#endif
}

std::shared_ptr<tr_peerIo> tr_peerIo::newIncoming(tr_session* session, tr_bandwidth* parent, tr_peer_socket socket)
{
    TR_ASSERT(session != nullptr);

    auto peer_io = tr_peerIo::create(session, parent, nullptr, true, false);
    peer_io->set_socket(std::move(socket));
    return peer_io;
}

std::shared_ptr<tr_peerIo> tr_peerIo::newOutgoing(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const& addr,
    tr_port port,
    tr_sha1_digest_t const& torrent_hash,
    bool is_seed,
    bool utp)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(addr.is_valid());
    TR_ASSERT(utp || session->allowsTCP());

    if (!addr.is_valid_for_peers(port))
    {
        return {};
    }

    auto peer_io = tr_peerIo::create(session, parent, &torrent_hash, false, is_seed);
    if (!peer_io)
    {
        return {};
    }

#ifdef WITH_UTP
    if (utp)
    {
        auto* const sock = utp_create_socket(session->utp_context);
        utp_set_userdata(sock, peer_io.get());
        peer_io->set_socket(tr_peer_socket{ addr, port, sock });

        auto const [ss, sslen] = addr.to_sockaddr(port);
        if (utp_connect(sock, reinterpret_cast<sockaddr const*>(&ss), sslen) == -1)
        {
            peer_io->socket.close(session);
        }
    }
#endif

    if (!peer_io->socket.is_valid())
    {
        peer_io->set_socket(tr_netOpenPeerSocket(session, addr, port, is_seed));
    }

    if (peer_io->socket.is_valid())
    {
        return peer_io;
    }

    return {};
}

/***
****
***/

static void event_enable(tr_peerIo* io, short event)
{
    TR_ASSERT(io->session != nullptr);

    bool const need_events = io->socket.is_tcp();
    TR_ASSERT(!need_events || io->event_read);
    TR_ASSERT(!need_events || io->event_write);

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) == 0)
    {
        tr_logAddTraceIo(io, "enabling ready-to-read polling");

        if (need_events)
        {
            event_add(io->event_read.get(), nullptr);
        }

        io->pendingEvents |= EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) == 0)
    {
        tr_logAddTraceIo(io, "enabling ready-to-write polling");

        if (need_events)
        {
            event_add(io->event_write.get(), nullptr);
        }

        io->pendingEvents |= EV_WRITE;
    }
}

static void event_disable(tr_peerIo* io, short event)
{
    bool const need_events = io->socket.is_tcp();
    TR_ASSERT(!need_events || io->event_read);
    TR_ASSERT(!need_events || io->event_write);

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) != 0)
    {
        tr_logAddTraceIo(io, "disabling ready-to-read polling");

        if (need_events)
        {
            event_del(io->event_read.get());
        }

        io->pendingEvents &= ~EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) != 0)
    {
        tr_logAddTraceIo(io, "disabling ready-to-write polling");

        if (need_events)
        {
            event_del(io->event_write.get());
        }

        io->pendingEvents &= ~EV_WRITE;
    }
}

void tr_peerIo::setEnabled(tr_direction dir, bool is_enabled)
{
    TR_ASSERT(tr_isDirection(dir));

    short const event = dir == TR_UP ? EV_WRITE : EV_READ;

    if (is_enabled)
    {
        event_enable(this, event);
    }
    else
    {
        event_disable(this, event);
    }
}

/***
****
***/

static void io_close_socket(tr_peerIo* io)
{
    io->socket.close(io->session);
    io->event_write.reset();
    io->event_read.reset();
    io->socket = {};
}

tr_peerIo::~tr_peerIo()
{
    auto const lock = session->unique_lock();

    clearCallbacks();
    tr_logAddTraceIo(this, "in tr_peerIo destructor");
    event_disable(this, EV_READ | EV_WRITE);
    io_close_socket(this);
}

void tr_peerIo::setCallbacks(tr_can_read_cb readcb, tr_did_write_cb writecb, tr_net_error_cb errcb, void* user_data)
{
    this->canRead = readcb;
    this->didWrite = writecb;
    this->gotError = errcb;
    this->userData = user_data;
}

void tr_peerIo::clear()
{
    clearCallbacks();
    setEnabled(TR_UP, false);
    setEnabled(TR_DOWN, false);
    io_close_socket(this);
}

bool tr_peerIo::reconnect()
{
    TR_ASSERT(!this->isIncoming());
    TR_ASSERT(this->session->allowsTCP());

    short int const pending_events = this->pendingEvents;
    event_disable(this, EV_READ | EV_WRITE);

    io_close_socket(this);

    auto const [addr, port] = socketAddress();
    this->socket = tr_netOpenPeerSocket(session, addr, port, this->isSeed());

    if (!this->socket.is_tcp())
    {
        return false;
    }

    this->event_read.reset(event_new(session->eventBase(), this->socket.handle.tcp, EV_READ, event_read_cb, this));
    this->event_write.reset(event_new(session->eventBase(), this->socket.handle.tcp, EV_WRITE, event_write_cb, this));

    event_enable(this, pending_events);

    return true;
}

/**
***
**/

static size_t getDesiredOutputBufferSize(tr_peerIo const* io, uint64_t now)
{
    /* this is all kind of arbitrary, but what seems to work well is
     * being large enough to hold the next 20 seconds' worth of input,
     * or a few blocks, whichever is bigger.
     * It's okay to tweak this as needed */
    auto const current_speed_bytes_per_second = io->bandwidth().getPieceSpeedBytesPerSecond(now, TR_UP);
    unsigned int const period = 15U; /* arbitrary */
    /* the 3 is arbitrary; the .5 is to leave room for messages */
    static auto const ceiling = static_cast<size_t>(tr_block_info::BlockSize * 3.5);
    return std::max(ceiling, current_speed_bytes_per_second * period);
}

size_t tr_peerIo::getWriteBufferSpace(uint64_t now) const noexcept
{
    size_t const desired_len = getDesiredOutputBufferSize(this, now);
    size_t const current_len = std::size(outbuf);
    return desired_len > current_len ? desired_len - current_len : 0U;
}

/**
***
**/

void tr_peerIo::write(libtransmission::Buffer& buf, bool is_piece_data)
{
    auto [bytes, len] = buf.pullup();
    encrypt(len, bytes);
    outbuf_info.emplace_back(std::size(buf), is_piece_data);
    outbuf.add(buf);
}

void tr_peerIo::writeBytes(void const* bytes, size_t n_bytes, bool is_piece_data)
{
    auto const old_size = std::size(outbuf);

    outbuf.reserve(old_size + n_bytes);
    outbuf.add(bytes, n_bytes);

    for (auto iter = std::begin(outbuf) + old_size, end = std::end(outbuf); iter != end; ++iter)
    {
        encrypt(1, &*iter);
    }

    outbuf_info.emplace_back(n_bytes, is_piece_data);
}

/***
****
***/

void tr_peerIo::readBytes(void* bytes, size_t byte_count)
{
    TR_ASSERT(readBufferSize() >= byte_count);

    inbuf.toBuf(bytes, byte_count);

    if (isEncrypted())
    {
        decrypt(byte_count, bytes);
    }
}

void tr_peerIo::readUint16(uint16_t* setme)
{
    auto tmp = uint16_t{};
    readBytes(&tmp, sizeof(tmp));
    *setme = ntohs(tmp);
}

void tr_peerIo::readUint32(uint32_t* setme)
{
    auto tmp = uint32_t{};
    readBytes(&tmp, sizeof(tmp));
    *setme = ntohl(tmp);
}

void tr_peerIo::readBufferDrain(size_t byte_count)
{
    auto buf = std::array<char, 4096>{};

    while (byte_count > 0)
    {
        auto const this_pass = std::min(byte_count, std::size(buf));
        readBytes(std::data(buf), this_pass);
        byte_count -= this_pass;
    }
}

/***
****
***/

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
        setEnabled(Dir, false);
        return {};
    }

    auto& buf = inbuf;
    tr_error* error = nullptr;
    auto const n_read = socket.try_read(buf, max, &error);
    setEnabled(Dir, error == nullptr || canRetryFromError(error->code));

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

size_t tr_peerIo::try_write(size_t max)
{
    static auto constexpr Dir = TR_UP;

    if (max == 0)
    {
        return {};
    }

    auto& buf = outbuf;
    max = std::min(max, std::size(buf));
    max = bandwidth().clamp(Dir, max);
    if (max == 0)
    {
        setEnabled(Dir, false);
        return {};
    }

    tr_error* error = nullptr;
    auto const n_written = socket.try_write(buf, max, &error);
    // enable further writes if there's more data to write
    setEnabled(Dir, !std::empty(buf) && (error == nullptr || canRetryFromError(error->code)));

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

size_t tr_peerIo::flush(tr_direction dir, size_t limit)
{
    TR_ASSERT(tr_isDirection(dir));

    return dir == TR_DOWN ? try_read(limit) : try_write(limit);
}

size_t tr_peerIo::flushOutgoingProtocolMsgs()
{
    size_t byte_count = 0;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for (auto const& [n_bytes, is_piece_data] : outbuf_info)
    {
        if (is_piece_data)
        {
            break;
        }

        byte_count += n_bytes;
    }

    return flush(TR_UP, byte_count);
}
