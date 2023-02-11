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

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->addrStr())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->addrStr())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->addrStr())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->addrStr())

[[nodiscard]] static constexpr auto isSupportedSocket(tr_peer_socket const& sock)
{
#ifdef WITH_UTP
    return sock.type == TR_PEER_SOCKET_TYPE_TCP || sock.type == TR_PEER_SOCKET_TYPE_UTP;
#else
    return sock.type == TR_PEER_SOCKET_TYPE_TCP;
#endif
}

static constexpr size_t guessPacketOverhead(size_t d)
{
    /**
     * http://sd.wareonearth.com/~phil/net/overhead/
     *
     * TCP over Ethernet:
     * Assuming no header compression (e.g. not PPP)
     * Add 20 IPv4 header or 40 IPv6 header (no options)
     * Add 20 TCP header
     * Add 12 bytes optional TCP timestamps
     * Max TCP Payload data rates over ethernet are thus:
     * (1500-40)/ (38+1500) = 94.9285 %  IPv4, minimal headers
     * (1500-52)/ (38+1500) = 94.1482 %  IPv4, TCP timestamps
     * (1500-52)/ (42+1500) = 93.9040 %  802.1q, IPv4, TCP timestamps
     * (1500-60)/ (38+1500) = 93.6281 %  IPv6, minimal headers
     * (1500-72)/ (38+1500) = 92.8479 %  IPv6, TCP timestamps
     * (1500-72)/ (42+1500) = 92.6070 %  802.1q, IPv6, ICP timestamps
     */
    double const assumed_payload_data_rate = 94.0;

    return (size_t)(d * (100.0 / assumed_payload_data_rate) - d);
}

/***
****
***/

static void didWriteWrapper(tr_peerIo* io, size_t bytes_transferred)
{
    while (bytes_transferred != 0 && tr_isPeerIo(io) && !std::empty(io->outbuf_info))
    {
        auto& [n_bytes_left, is_piece_data] = io->outbuf_info.front();

        size_t const payload = std::min(uint64_t{ n_bytes_left }, uint64_t{ bytes_transferred });
        /* For µTP sockets, the overhead is computed in utp_on_overhead. */
        size_t const overhead = io->socket.type == TR_PEER_SOCKET_TYPE_TCP ? guessPacketOverhead(payload) : 0;
        uint64_t const now = tr_time_msec();

        io->bandwidth().notifyBandwidthConsumed(TR_UP, payload, is_piece_data, now);

        if (overhead > 0)
        {
            io->bandwidth().notifyBandwidthConsumed(TR_UP, overhead, false, now);
        }

        if (io->didWrite != nullptr)
        {
            io->didWrite(io, payload, is_piece_data, io->userData);
        }

        if (!tr_isPeerIo(io))
        {
            break;
        }

        bytes_transferred -= payload;
        n_bytes_left -= payload;
        if (n_bytes_left == 0)
        {
            io->outbuf_info.pop_front();
        }
    }
}

static void canReadWrapper(tr_peerIo* io_in)
{
    auto const io = io_in->shared_from_this();
    tr_logAddTraceIo(io, "canRead");

    tr_session const* const session = io->session;

    /* try to consume the input buffer */
    if (io->canRead == nullptr)
    {
        return;
    }

    auto const lock = session->unique_lock();

    auto const now = tr_time_msec();
    auto done = bool{ false };
    auto err = bool{ false };

    while (!done && !err)
    {
        size_t piece = 0;
        size_t const old_len = io->readBufferSize();
        int const ret = io->canRead(io.get(), io->userData, &piece);
        size_t const used = old_len - io->readBufferSize();
        auto const overhead = guessPacketOverhead(used);

        if (piece != 0 || piece != used)
        {
            if (piece != 0)
            {
                io->bandwidth().notifyBandwidthConsumed(TR_DOWN, piece, true, now);
            }

            if (used != piece)
            {
                io->bandwidth().notifyBandwidthConsumed(TR_DOWN, used - piece, false, now);
            }
        }

        if (overhead > 0)
        {
            io->bandwidth().notifyBandwidthConsumed(TR_UP, overhead, false, now);
        }

        switch (ret)
        {
        case READ_NOW:
            if (io->readBufferSize() != 0)
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

static void event_read_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* io = static_cast<tr_peerIo*>(vio);

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    /* Limit the input buffer to 256K, so it doesn't grow too large */
    tr_direction const dir = TR_DOWN;
    size_t const max = 256 * 1024;

    io->pendingEvents &= ~EV_READ;

    auto const curlen = io->readBufferSize();
    auto howmuch = static_cast<unsigned int>(curlen >= max ? 0 : max - curlen);
    howmuch = io->bandwidth().clamp(TR_DOWN, howmuch);

    tr_logAddTraceIo(io, "libevent says this peer is ready to read");

    /* if we don't have any bandwidth left, stop reading */
    if (howmuch < 1)
    {
        io->setEnabled(dir, false);
        return;
    }

    tr_error* error = nullptr;
    if (auto const res = io->inbuf.addSocket(fd, howmuch, &error); res > 0)
    {
        io->setEnabled(dir, true);

        /* Invoke the user callback - must always be called last */
        canReadWrapper(io);
    }
    else
    {
        short what = BEV_EVENT_READING;

        if (res == 0) /* EOF */
        {
            what |= BEV_EVENT_EOF;
        }
        if (error != nullptr)
        {
            if (error->code == EAGAIN || error->code == EINTR)
            {
                io->setEnabled(dir, true);
                return;
            }

            what |= BEV_EVENT_ERROR;

            tr_logAddDebugIo(
                io,
                fmt::format("event_read_cb err: res:{}, what:{}, errno:{} ({})", res, what, error->code, error->message));
        }

        if (io->gotError != nullptr)
        {
            io->gotError(io, what, io->userData);
        }
    }

    tr_error_clear(&error);
}

// Helps us to ignore errors that say "try again later"
// since that's what peer-io does by default anyway.
[[nodiscard]] static auto constexpr canRetryFromError(int error_code)
{
    return error_code == 0 || error_code == EAGAIN || error_code == EINTR || error_code == EINPROGRESS;
}

static void event_write_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* io = static_cast<tr_peerIo*>(vio);

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    io->pendingEvents &= ~EV_WRITE;

    tr_logAddTraceIo(io, "libevent says this peer is ready to write");

    /* Write as much as possible, since the socket is non-blocking, write() will
     * return if it can't write any more data without blocking */
    auto constexpr Dir = TR_UP;
    auto const howmuch = io->bandwidth().clamp(Dir, std::size(io->outbuf));

    // if we don't have any bandwidth left, stop writing
    if (howmuch < 1)
    {
        io->setEnabled(Dir, false);
        return;
    }

    tr_error* error = nullptr;
    auto const n_written = io->outbuf.toSocket(fd, howmuch, &error);
    auto const should_retry = (error == nullptr) || canRetryFromError(error->code);

    // schedule another write if we have more data to write & think future writes would succeed
    if (!std::empty(io->outbuf) && (n_written > 0 || should_retry))
    {
        io->setEnabled(Dir, true);
    }

    if (n_written > 0)
    {
        didWriteWrapper(io, n_written);
    }
    else
    {
        auto const what = BEV_EVENT_WRITING | (n_written == 0 ? BEV_EVENT_EOF : BEV_EVENT_ERROR);

        tr_logAddDebugIo(
            io,
            fmt::format(
                "event_write_cb got an err. n_written:{}, what:{}, errno:{} ({})",
                n_written,
                what,
                (error != nullptr ? error->code : 0),
                (error != nullptr ? error->message : "EOF")));

        if (io->gotError != nullptr)
        {
            io->gotError(io, what, io->userData);
        }
    }

    tr_error_clear(&error);
}

/**
***
**/

static void maybeSetCongestionAlgorithm(tr_socket_t socket, std::string const& algorithm)
{
    if (!std::empty(algorithm))
    {
        tr_netSetCongestionControl(socket, algorithm.c_str());
    }
}

#ifdef WITH_UTP
/* µTP callbacks */

void tr_peerIo::readBufferAdd(void const* data, size_t n_bytes)
{
    inbuf.add(data, n_bytes);
    setEnabled(TR_DOWN, true);
    canReadWrapper(this);
}

static size_t utp_get_rb_size(tr_peerIo* const io)
{
    size_t const bytes = io->bandwidth().clamp(TR_DOWN, UtpReadBufferSize);

    tr_logAddTraceIo(io, fmt::format("utp_get_rb_size is saying it's ready to read {} bytes", bytes));
    return UtpReadBufferSize - bytes;
}

static size_t tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch, tr_error** error = nullptr);

static void utp_on_writable(tr_peerIo* io)
{
    tr_logAddTraceIo(io, "libutp says this peer is ready to write");

    auto const n = tr_peerIoTryWrite(io, SIZE_MAX);
    io->setEnabled(TR_UP, n != 0 && !std::empty(io->outbuf));
}

static void utp_on_state_change(tr_peerIo* const io, int const state)
{
    if (state == UTP_STATE_CONNECT)
    {
        tr_logAddTraceIo(io, "utp_on_state_change -- changed to connected");
        io->utp_supported_ = true;
    }
    else if (state == UTP_STATE_WRITABLE)
    {
        tr_logAddTraceIo(io, "utp_on_state_change -- changed to writable");

        if ((io->pendingEvents & EV_WRITE) != 0)
        {
            utp_on_writable(io);
        }
    }
    else if (state == UTP_STATE_EOF)
    {
        if (io->gotError != nullptr)
        {
            io->gotError(io, BEV_EVENT_EOF, io->userData);
        }
    }
    else if (state == UTP_STATE_DESTROYING)
    {
        tr_logAddErrorIo(io, "Impossible state UTP_STATE_DESTROYING");
    }
    else
    {
        tr_logAddErrorIo(io, fmt::format(_("Unknown state: {state}"), fmt::arg("state", state)));
    }
}

static void utp_on_error(tr_peerIo* const io, int const errcode)
{
    tr_logAddDebugIo(io, fmt::format("utp_on_error -- errcode is {}", errcode));

    if (io->gotError != nullptr)
    {
        errno = errcode;
        io->gotError(io, BEV_EVENT_ERROR, io->userData);
    }
}

static void utp_on_overhead(tr_peerIo* const io, bool const send, size_t const count, int /*type*/)
{
    tr_logAddTraceIo(io, fmt::format("utp_on_overhead -- count is {}", count));

    io->bandwidth().notifyBandwidthConsumed(send ? TR_UP : TR_DOWN, count, false, tr_time_msec());
}

static uint64 utp_callback(utp_callback_arguments* args)
{
    auto* const io = static_cast<tr_peerIo*>(utp_get_userdata(args->socket));

    if (io == nullptr)
    {
#ifdef TR_UTP_TRACE

        if (args->callback_type != UTP_ON_STATE_CHANGE || args->u1.state != UTP_STATE_DESTROYING)
        {
            fmt::print(
                stderr,
                FMT_STRING("[µTP] [{}:{}] [{}] io is null! buf={}, len={}, flags={}, send/error_code/state={}, type={}\n"),
                fmt::ptr(args->context),
                fmt::ptr(args->socket),
                utp_callback_names[args->callback_type],
                fmt::ptr(args->buf),
                args->len,
                args->flags,
                args->u1.send,
                args->u2.type);
        }

#endif

        return 0;
    }

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.handle.utp == args->socket);

    switch (args->callback_type)
    {
    case UTP_ON_READ:
        io->readBufferAdd(args->buf, args->len);
        break;

    case UTP_GET_READ_BUFFER_SIZE:
        return utp_get_rb_size(io);

    case UTP_ON_STATE_CHANGE:
        utp_on_state_change(io, args->u1.state);
        break;

    case UTP_ON_ERROR:
        utp_on_error(io, args->u1.error_code);
        break;

    case UTP_ON_OVERHEAD_STATISTICS:
        utp_on_overhead(io, args->u1.send != 0, args->len, args->u2.type);
        break;
    }

    return 0;
}

#endif /* #ifdef WITH_UTP */

std::shared_ptr<tr_peerIo> tr_peerIo::create(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const* addr,
    tr_port port,
    time_t current_time,
    tr_sha1_digest_t const* torrent_hash,
    bool is_incoming,
    bool is_seed,
    struct tr_peer_socket const socket)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(session->events != nullptr);
    auto lock = session->unique_lock();

    TR_ASSERT(isSupportedSocket(socket));
    TR_ASSERT(session->allowsTCP() || socket.type != TR_PEER_SOCKET_TYPE_TCP);

    if (socket.type == TR_PEER_SOCKET_TYPE_TCP)
    {
        session->setSocketTOS(socket.handle.tcp, addr->type);
        maybeSetCongestionAlgorithm(socket.handle.tcp, session->peerCongestionAlgorithm());
    }

    auto io = std::shared_ptr<tr_peerIo>{
        new tr_peerIo{ session, torrent_hash, is_incoming, *addr, port, is_seed, current_time, parent }
    };
    io->socket = socket;
    io->bandwidth().setPeer(io);
    tr_logAddTraceIo(io, fmt::format("bandwidth is {}; its parent is {}", fmt::ptr(&io->bandwidth()), fmt::ptr(parent)));

    switch (socket.type)
    {
    case TR_PEER_SOCKET_TYPE_TCP:
        tr_logAddTraceIo(io, fmt::format("socket (tcp) is {}", socket.handle.tcp));
        io->event_read = event_new(session->eventBase(), socket.handle.tcp, EV_READ, event_read_cb, io.get());
        io->event_write = event_new(session->eventBase(), socket.handle.tcp, EV_WRITE, event_write_cb, io.get());
        break;

#ifdef WITH_UTP

    case TR_PEER_SOCKET_TYPE_UTP:
        tr_logAddTraceIo(io, fmt::format("socket (µTP) is {}", fmt::ptr(socket.handle.utp)));
        utp_set_userdata(socket.handle.utp, io.get());
        break;

#endif

    default:
        TR_ASSERT_MSG(false, fmt::format("unsupported peer socket type {:d}", socket.type));
    }

    return io;
}

void tr_peerIo::utpInit([[maybe_unused]] struct_utp_context* ctx)
{
#ifdef WITH_UTP

    utp_set_callback(ctx, UTP_ON_READ, &utp_callback);
    utp_set_callback(ctx, UTP_GET_READ_BUFFER_SIZE, &utp_callback);
    utp_set_callback(ctx, UTP_ON_STATE_CHANGE, &utp_callback);
    utp_set_callback(ctx, UTP_ON_ERROR, &utp_callback);
    utp_set_callback(ctx, UTP_ON_OVERHEAD_STATISTICS, &utp_callback);

    utp_context_set_option(ctx, UTP_RCVBUF, UtpReadBufferSize);

#endif
}

std::shared_ptr<tr_peerIo> tr_peerIo::newIncoming(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const* addr,
    tr_port port,
    time_t current_time,
    struct tr_peer_socket const socket)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_address_is_valid(addr));

    return tr_peerIo::create(session, parent, addr, port, current_time, nullptr, true, false, socket);
}

std::shared_ptr<tr_peerIo> tr_peerIo::newOutgoing(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const* addr,
    tr_port port,
    time_t current_time,
    tr_sha1_digest_t const& torrent_hash,
    bool is_seed,
    bool utp)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_address_is_valid(addr));
    TR_ASSERT(utp || session->allowsTCP());

    auto socket = tr_peer_socket{};

    if (utp)
    {
        socket = tr_netOpenPeerUTPSocket(session, addr, port, is_seed);
    }

    if (socket.type == TR_PEER_SOCKET_TYPE_NONE)
    {
        socket = tr_netOpenPeerSocket(session, addr, port, is_seed);
        tr_logAddDebug(fmt::format(
            "tr_netOpenPeerSocket returned {}",
            socket.type != TR_PEER_SOCKET_TYPE_NONE ? socket.handle.tcp : TR_BAD_SOCKET));
    }

    if (socket.type == TR_PEER_SOCKET_TYPE_NONE)
    {
        return nullptr;
    }

    return create(session, parent, addr, port, current_time, &torrent_hash, false, is_seed, socket);
}

/***
****
***/

static void event_enable(tr_peerIo* io, short event)
{
    TR_ASSERT(io->session != nullptr);
    TR_ASSERT(io->session->events != nullptr);

    bool const need_events = io->socket.type == TR_PEER_SOCKET_TYPE_TCP;

    if (need_events)
    {
        TR_ASSERT(event_initialized(io->event_read));
        TR_ASSERT(event_initialized(io->event_write));
    }

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) == 0)
    {
        tr_logAddTraceIo(io, "enabling ready-to-read polling");

        if (need_events)
        {
            event_add(io->event_read, nullptr);
        }

        io->pendingEvents |= EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) == 0)
    {
        tr_logAddTraceIo(io, "enabling ready-to-write polling");

        if (need_events)
        {
            event_add(io->event_write, nullptr);
        }

        io->pendingEvents |= EV_WRITE;
    }
}

static void event_disable(tr_peerIo* io, short event)
{
    TR_ASSERT(io->session->events != nullptr);

    bool const need_events = io->socket.type == TR_PEER_SOCKET_TYPE_TCP;

    if (need_events)
    {
        TR_ASSERT(event_initialized(io->event_read));
        TR_ASSERT(event_initialized(io->event_write));
    }

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) != 0)
    {
        tr_logAddTraceIo(io, "disabling ready-to-read polling");

        if (need_events)
        {
            event_del(io->event_read);
        }

        io->pendingEvents &= ~EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) != 0)
    {
        tr_logAddTraceIo(io, "disabling ready-to-write polling");

        if (need_events)
        {
            event_del(io->event_write);
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
    switch (io->socket.type)
    {
    case TR_PEER_SOCKET_TYPE_NONE:
        break;

    case TR_PEER_SOCKET_TYPE_TCP:
        tr_netClose(io->session, io->socket.handle.tcp);
        break;

#ifdef WITH_UTP

    case TR_PEER_SOCKET_TYPE_UTP:
        utp_set_userdata(io->socket.handle.utp, nullptr);
        utp_close(io->socket.handle.utp);
        break;

#endif

    default:
        tr_logAddDebugIo(io, fmt::format("unsupported peer socket type {}", io->socket.type));
    }

    io->socket = {};

    if (io->event_read != nullptr)
    {
        event_free(io->event_read);
        io->event_read = nullptr;
    }

    if (io->event_write != nullptr)
    {
        event_free(io->event_write);
        io->event_write = nullptr;
    }
}

tr_peerIo::~tr_peerIo()
{
    auto const lock = session->unique_lock();
    TR_ASSERT(session->events != nullptr);

    clearCallbacks();
    tr_logAddTraceIo(this, "in tr_peerIo destructor");
    event_disable(this, EV_READ | EV_WRITE);
    io_close_socket(this);
}

std::string tr_peerIo::addrStr() const
{
    return tr_isPeerIo(this) ? this->addr_.readable(this->port_) : "error";
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

int tr_peerIo::reconnect()
{
    TR_ASSERT(tr_isPeerIo(this));
    TR_ASSERT(!this->isIncoming());
    TR_ASSERT(this->session->allowsTCP());

    short int const pending_events = this->pendingEvents;
    event_disable(this, EV_READ | EV_WRITE);

    io_close_socket(this);

    auto const [addr, port] = this->socketAddress();
    this->socket = tr_netOpenPeerSocket(session, &addr, port, this->isSeed());

    if (this->socket.type != TR_PEER_SOCKET_TYPE_TCP)
    {
        return -1;
    }

    this->event_read = event_new(session->eventBase(), this->socket.handle.tcp, EV_READ, event_read_cb, this);
    this->event_write = event_new(session->eventBase(), this->socket.handle.tcp, EV_WRITE, event_write_cb, this);

    event_enable(this, pending_events);
    this->session->setSocketTOS(this->socket.handle.tcp, addr.type);
    maybeSetCongestionAlgorithm(this->socket.handle.tcp, session->peerCongestionAlgorithm());

    return 0;
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
    auto const n_bytes = std::size(buf);

    auto const old_size = std::size(outbuf);
    outbuf.add(buf);
    for (auto iter = std::begin(outbuf) + old_size, end = std::end(outbuf); iter != end; ++iter)
    {
        encrypt(1, &*iter);
    }

    outbuf_info.emplace_back(n_bytes, is_piece_data);
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

static size_t tr_peerIoTryRead(tr_peerIo* io, size_t howmuch, tr_error** error)
{
    auto n_read = size_t{ 0U };

    howmuch = io->bandwidth().clamp(TR_DOWN, howmuch);
    if (howmuch == 0)
    {
        return n_read;
    }

    TR_ASSERT(isSupportedSocket(io->socket));
    if (io->socket.type == TR_PEER_SOCKET_TYPE_TCP)
    {
        tr_error* my_error = nullptr;
        n_read = io->inbuf.addSocket(io->socket.handle.tcp, howmuch, &my_error);
        if (io->readBufferSize() != 0)
        {
            canReadWrapper(io);
        }

        if (my_error != nullptr)
        {
            if (canRetryFromError(my_error->code))
            {
                tr_error_clear(&my_error);
            }
            else
            {
                short const what = BEV_EVENT_READING | BEV_EVENT_ERROR | (n_read == 0 ? BEV_EVENT_EOF : 0);
                auto const msg = fmt::format(
                    "tr_peerIoTryRead err: res:{} what:{}, errno:{} ({})",
                    n_read,
                    what,
                    my_error->code,
                    my_error->message);
                tr_logAddTraceIo(io, msg);

                if (io->gotError != nullptr)
                {
                    io->gotError(io, what, io->userData);
                }

                tr_error_propagate(error, &my_error);
            }
        }
    }
#ifdef WITH_UTP
    else if (io->socket.type == TR_PEER_SOCKET_TYPE_UTP)
    {
        // UTP_RBDrained notifies libutp that your read buffer is empty.
        // It opens up the congestion window by sending an ACK (soonish)
        // if one was not going to be sent.
        if (io->readBufferSize() == 0)
        {
            utp_read_drained(io->socket.handle.utp);
        }
    }
#endif

    return n_read;
}

static size_t tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch, tr_error** error)
{
    auto n_written = size_t{ 0U };

    auto const old_len = std::size(io->outbuf);

    howmuch = std::min(howmuch, old_len);
    howmuch = io->bandwidth().clamp(TR_UP, howmuch);
    if (howmuch == 0)
    {
        return n_written;
    }

    if (io->socket.type == TR_PEER_SOCKET_TYPE_TCP)
    {
        tr_error* my_error = nullptr;
        n_written = io->outbuf.toSocket(io->socket.handle.tcp, howmuch, &my_error);

        if (n_written > 0)
        {
            didWriteWrapper(io, n_written);
        }

        if (my_error != nullptr)
        {
            if (canRetryFromError(my_error->code))
            {
                tr_error_clear(&my_error);
            }
            else
            {
                short constexpr What = BEV_EVENT_WRITING | BEV_EVENT_ERROR;
                tr_logAddTraceIo(
                    io,
                    fmt::format(
                        "tr_peerIoTryWrite err: res:{}, what:{}, errno:{} ({})",
                        n_written,
                        What,
                        my_error->code,
                        my_error->message));

                if (io->gotError != nullptr)
                {
                    io->gotError(io, What, io->userData);
                }

                tr_error_propagate(error, &my_error);
            }
        }
    }
#ifdef WITH_UTP
    else if (io->socket.type == TR_PEER_SOCKET_TYPE_UTP)
    {
        auto iov = io->outbuf.vecs(howmuch);
        errno = 0;
        auto const n = utp_writev(io->socket.handle.utp, reinterpret_cast<struct utp_iovec*>(std::data(iov)), std::size(iov));
        auto const error_code = errno;
        if (n > 0)
        {
            n_written = static_cast<size_t>(n);
            io->outbuf.drain(n);
            didWriteWrapper(io, n);
        }
        else if (n < 0 && !canRetryFromError(error_code))
        {
            tr_error_set(error, error_code, tr_strerror(error_code));
        }
    }
#endif

    return n_written;
}

size_t tr_peerIo::flush(tr_direction dir, size_t limit, tr_error** error)
{
    TR_ASSERT(tr_isDirection(dir));

    auto const bytes_used = dir == TR_DOWN ? tr_peerIoTryRead(this, limit, error) : tr_peerIoTryWrite(this, limit, error);
    tr_logAddTraceIo(this, fmt::format("flushing peer-io, direction:{}, limit:{}, byte_used:{}", dir, limit, bytes_used));
    return bytes_used;
}

size_t tr_peerIo::flushOutgoingProtocolMsgs(tr_error** error)
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

    return flush(TR_UP, byte_count, error);
}
