// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <string>

#include <event2/event.h>
#include <event2/buffer.h>
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
#include "trevent.h" /* tr_runInEventThread() */
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

/* The amount of read bufferring that we allow for uTP sockets. */

static auto constexpr UtpReadBufferSize = 256 * 1024;

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->addrStr())
#define tr_logAddWarnIo(io, msg) tr_logAddWarn(msg, (io)->addrStr())
#define tr_logAddDebugIo(io, msg) tr_logAddDebug(msg, (io)->addrStr())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->addrStr())

static size_t guessPacketOverhead(size_t d)
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

    return (unsigned int)(d * (100.0 / assumed_payload_data_rate) - d);
}

/***
****
***/

static void didWriteWrapper(tr_peerIo* io, unsigned int bytes_transferred)
{
    while (bytes_transferred != 0 && tr_isPeerIo(io) && !std::empty(io->outbuf_info))
    {
        auto& [n_bytes_left, is_piece_data] = io->outbuf_info.front();

        unsigned int const payload = std::min(uint64_t{ n_bytes_left }, uint64_t{ bytes_transferred });
        /* For uTP sockets, the overhead is computed in utp_on_overhead. */
        unsigned int const overhead = io->socket.type == TR_PEER_SOCKET_TYPE_TCP ? guessPacketOverhead(payload) : 0;
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

static void canReadWrapper(tr_peerIo* io)
{
    tr_logAddTraceIo(io, "canRead");

    tr_peerIoRef(io);

    tr_session const* const session = io->session;

    /* try to consume the input buffer */
    if (io->canRead != nullptr)
    {
        auto const lock = session->unique_lock();

        auto const now = tr_time_msec();
        auto done = bool{ false };
        auto err = bool{ false };

        while (!done && !err)
        {
            size_t piece = 0;
            size_t const oldLen = evbuffer_get_length(io->inbuf.get());
            int const ret = io->canRead(io, io->userData, &piece);
            size_t const used = oldLen - evbuffer_get_length(io->inbuf.get());
            unsigned int const overhead = guessPacketOverhead(used);

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
                if (evbuffer_get_length(io->inbuf.get()) != 0)
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

            TR_ASSERT(tr_isPeerIo(io));
        }
    }

    tr_peerIoUnref(io);
}

static void event_read_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* io = static_cast<tr_peerIo*>(vio);

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    /* Limit the input buffer to 256K, so it doesn't grow too large */
    tr_direction const dir = TR_DOWN;
    unsigned int const max = 256 * 1024;

    io->pendingEvents &= ~EV_READ;

    unsigned int const curlen = evbuffer_get_length(io->inbuf.get());
    unsigned int howmuch = curlen >= max ? 0 : max - curlen;
    howmuch = io->bandwidth().clamp(TR_DOWN, howmuch);

    tr_logAddTraceIo(io, "libevent says this peer is ready to read");

    /* if we don't have any bandwidth left, stop reading */
    if (howmuch < 1)
    {
        tr_peerIoSetEnabled(io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR(0);
    auto const res = evbuffer_read(io->inbuf.get(), fd, (int)howmuch);
    int const e = EVUTIL_SOCKET_ERROR();

    if (res > 0)
    {
        tr_peerIoSetEnabled(io, dir, true);

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
        else if (res == -1)
        {
            if (e == EAGAIN || e == EINTR)
            {
                tr_peerIoSetEnabled(io, dir, true);
                return;
            }

            what |= BEV_EVENT_ERROR;
        }

        tr_logAddDebugIo(
            io,
            fmt::format("event_read_cb err: res:{}, what:{}, errno:{} ({})", res, what, e, tr_net_strerror(e)));

        if (io->gotError != nullptr)
        {
            io->gotError(io, what, io->userData);
        }
    }
}

static int tr_evbuffer_write(tr_peerIo* io, int fd, size_t howmuch)
{
    EVUTIL_SET_SOCKET_ERROR(0);
    int const n = evbuffer_write_atmost(io->outbuf.get(), fd, howmuch);
    int const e = EVUTIL_SOCKET_ERROR();
    tr_logAddTraceIo(io, fmt::format("wrote {} to peer ({})", n, (n == -1 ? tr_net_strerror(e).c_str() : "")));

    return n;
}

static void event_write_cb(evutil_socket_t fd, short /*event*/, void* vio)
{
    auto* io = static_cast<tr_peerIo*>(vio);

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    auto const dir = TR_UP;
    auto res = int{ 0 };
    auto what = short{ BEV_EVENT_WRITING };

    io->pendingEvents &= ~EV_WRITE;

    tr_logAddTraceIo(io, "libevent says this peer is ready to write");

    /* Write as much as possible, since the socket is non-blocking, write() will
     * return if it can't write any more data without blocking */
    size_t const howmuch = io->bandwidth().clamp(dir, evbuffer_get_length(io->outbuf.get()));

    /* if we don't have any bandwidth left, stop writing */
    if (howmuch < 1)
    {
        tr_peerIoSetEnabled(io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR(0);
    res = tr_evbuffer_write(io, fd, howmuch);
    int const e = EVUTIL_SOCKET_ERROR();

    if (res == -1)
    {
        if (e == 0 || e == EAGAIN || e == EINTR || e == EINPROGRESS)
        {
            goto RESCHEDULE;
        }

        /* error case */
        what |= BEV_EVENT_ERROR;
    }
    else if (res == 0)
    {
        /* eof case */
        what |= BEV_EVENT_EOF;
    }

    if (res <= 0)
    {
        goto FAIL;
    }

    if (evbuffer_get_length(io->outbuf.get()) != 0)
    {
        tr_peerIoSetEnabled(io, dir, true);
    }

    didWriteWrapper(io, res);
    return;

RESCHEDULE:
    if (evbuffer_get_length(io->outbuf.get()) != 0)
    {
        tr_peerIoSetEnabled(io, dir, true);
    }

    return;

FAIL:
    auto const errmsg = tr_net_strerror(e);
    tr_logAddDebugIo(io, fmt::format("event_write_cb got an err. res:{}, what:{}, errno:{} ({})", res, what, e, errmsg));

    if (io->gotError != nullptr)
    {
        io->gotError(io, what, io->userData);
    }
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
/* UTP callbacks */

void tr_peerIo::readBufferAdd(void const* data, size_t n_bytes)
{
    if (auto const rc = evbuffer_add(inbuf.get(), data, n_bytes); rc < 0)
    {
        tr_logAddWarn(_("Couldn't write to peer"));
        return;
    }

    tr_peerIoSetEnabled(this, TR_DOWN, true);
    canReadWrapper(this);
}

static size_t utp_get_rb_size(tr_peerIo* const io)
{
    size_t const bytes = io->bandwidth().clamp(TR_DOWN, UtpReadBufferSize);

    tr_logAddTraceIo(io, fmt::format("utp_get_rb_size is saying it's ready to read {} bytes", bytes));
    return UtpReadBufferSize - bytes;
}

static int tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch);

static void utp_on_writable(tr_peerIo* io)
{
    tr_logAddTraceIo(io, "libutp says this peer is ready to write");

    int const n = tr_peerIoTryWrite(io, SIZE_MAX);
    tr_peerIoSetEnabled(io, TR_UP, n != 0 && evbuffer_get_length(io->outbuf.get()) != 0);
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
                FMT_STRING("[utp] [{}:{}] [{}] io is null! buf={}, len={}, flags={}, send/error_code/state={}, type={}\n"),
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

tr_peerIo* tr_peerIoNew(
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

#ifdef WITH_UTP
    TR_ASSERT(socket.type == TR_PEER_SOCKET_TYPE_TCP || socket.type == TR_PEER_SOCKET_TYPE_UTP);
#else
    TR_ASSERT(socket.type == TR_PEER_SOCKET_TYPE_TCP);
#endif

    if (socket.type == TR_PEER_SOCKET_TYPE_TCP)
    {
        session->setSocketTOS(socket.handle.tcp, addr->type);
        maybeSetCongestionAlgorithm(socket.handle.tcp, session->peerCongestionAlgorithm());
    }

    auto* io = new tr_peerIo{ session, torrent_hash, is_incoming, *addr, port, is_seed, current_time, parent };
    io->socket = socket;
    io->bandwidth().setPeer(io);
    tr_logAddTraceIo(io, fmt::format("bandwidth is {}; its parent is {}", fmt::ptr(&io->bandwidth()), fmt::ptr(parent)));

    switch (socket.type)
    {
    case TR_PEER_SOCKET_TYPE_TCP:
        tr_logAddTraceIo(io, fmt::format("socket (tcp) is {}", socket.handle.tcp));
        io->event_read = event_new(session->event_base, socket.handle.tcp, EV_READ, event_read_cb, io);
        io->event_write = event_new(session->event_base, socket.handle.tcp, EV_WRITE, event_write_cb, io);
        break;

#ifdef WITH_UTP

    case TR_PEER_SOCKET_TYPE_UTP:
        tr_logAddTraceIo(io, fmt::format("socket (utp) is {}", fmt::ptr(socket.handle.utp)));
        utp_set_userdata(socket.handle.utp, io);
        break;

#endif

    default:
        TR_ASSERT_MSG(false, fmt::format(FMT_STRING("unsupported peer socket type {:d}"), socket.type));
    }

    return io;
}

void tr_peerIoUtpInit([[maybe_unused]] struct_utp_context* ctx)
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

tr_peerIo* tr_peerIoNewIncoming(
    tr_session* session,
    tr_bandwidth* parent,
    tr_address const* addr,
    tr_port port,
    time_t current_time,
    struct tr_peer_socket const socket)
{
    TR_ASSERT(session != nullptr);
    TR_ASSERT(tr_address_is_valid(addr));

    return tr_peerIoNew(session, parent, addr, port, current_time, nullptr, true, false, socket);
}

tr_peerIo* tr_peerIoNewOutgoing(
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

    return tr_peerIoNew(session, parent, addr, port, current_time, &torrent_hash, false, is_seed, socket);
}

/***
****
***/

static void event_enable(tr_peerIo* io, short event)
{
    TR_ASSERT(tr_amInEventThread(io->session));
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
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session != nullptr);
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

void tr_peerIoSetEnabled(tr_peerIo* io, tr_direction dir, bool isEnabled)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(tr_isDirection(dir));
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session->events != nullptr);

    short const event = dir == TR_UP ? EV_WRITE : EV_READ;

    if (isEnabled)
    {
        event_enable(io, event);
    }
    else
    {
        event_disable(io, event);
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

static void io_dtor(tr_peerIo* const io)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session->events != nullptr);

    tr_logAddTraceIo(io, "in tr_peerIo destructor");
    event_disable(io, EV_READ | EV_WRITE);
    io_close_socket(io);

    io->magic_number = ~0;
    delete io;
}

static void tr_peerIoFree(tr_peerIo* io)
{
    if (io != nullptr)
    {
        tr_logAddTraceIo(io, "in tr_peerIoFree");
        io->canRead = nullptr;
        io->didWrite = nullptr;
        io->gotError = nullptr;
        tr_runInEventThread(io->session, io_dtor, io);
    }
}

void tr_peerIoRefImpl(char const* file, int line, tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    tr_logAddTraceIo(
        io,
        fmt::format("{}:{} incrementing the IO's refcount from {} to {}", file, line, io->refCount, io->refCount + 1));

    ++io->refCount;
}

void tr_peerIoUnrefImpl(char const* file, int line, tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    tr_logAddTraceIo(
        io,
        fmt::format("{}:{} decrementing the IO's refcount from {} to {}", file, line, io->refCount, io->refCount - 1));

    if (--io->refCount == 0)
    {
        tr_peerIoFree(io);
    }
}

std::string tr_peerIo::addrStr() const
{
    return tr_isPeerIo(this) ? this->addr_.readable(this->port_) : "error";
}

void tr_peerIoSetIOFuncs(tr_peerIo* io, tr_can_read_cb readcb, tr_did_write_cb writecb, tr_net_error_cb errcb, void* userData)
{
    io->canRead = readcb;
    io->didWrite = writecb;
    io->gotError = errcb;
    io->userData = userData;
}

void tr_peerIoClear(tr_peerIo* io)
{
    tr_peerIoSetIOFuncs(io, nullptr, nullptr, nullptr, nullptr);
    tr_peerIoSetEnabled(io, TR_UP, false);
    tr_peerIoSetEnabled(io, TR_DOWN, false);
}

int tr_peerIoReconnect(tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(!io->isIncoming());

    tr_session* session = tr_peerIoGetSession(io);

    short int const pendingEvents = io->pendingEvents;
    event_disable(io, EV_READ | EV_WRITE);

    io_close_socket(io);

    auto const [addr, port] = io->socketAddress();
    io->socket = tr_netOpenPeerSocket(session, &addr, port, io->isSeed());

    if (io->socket.type != TR_PEER_SOCKET_TYPE_TCP)
    {
        return -1;
    }

    io->event_read = event_new(session->event_base, io->socket.handle.tcp, EV_READ, event_read_cb, io);
    io->event_write = event_new(session->event_base, io->socket.handle.tcp, EV_WRITE, event_write_cb, io);

    event_enable(io, pendingEvents);
    io->session->setSocketTOS(io->socket.handle.tcp, addr.type);
    maybeSetCongestionAlgorithm(io->socket.handle.tcp, session->peerCongestionAlgorithm());

    return 0;
}

/**
***
**/

static unsigned int getDesiredOutputBufferSize(tr_peerIo const* io, uint64_t now)
{
    /* this is all kind of arbitrary, but what seems to work well is
     * being large enough to hold the next 20 seconds' worth of input,
     * or a few blocks, whichever is bigger.
     * It's okay to tweak this as needed */
    unsigned int const currentSpeed_Bps = io->bandwidth().getPieceSpeedBytesPerSecond(now, TR_UP);
    unsigned int const period = 15U; /* arbitrary */
    /* the 3 is arbitrary; the .5 is to leave room for messages */
    static auto const ceiling = (unsigned int)(tr_block_info::BlockSize * 3.5);
    return std::max(ceiling, currentSpeed_Bps * period);
}

size_t tr_peerIoGetWriteBufferSpace(tr_peerIo const* io, uint64_t now)
{
    size_t const desiredLen = getDesiredOutputBufferSize(io, now);
    size_t const currentLen = evbuffer_get_length(io->outbuf.get());
    size_t freeSpace = 0;

    if (desiredLen > currentLen)
    {
        freeSpace = desiredLen - currentLen;
    }

    return freeSpace;
}

/**
***
**/

static inline void processBuffer(tr_peerIo& io, evbuffer* buffer, size_t offset, size_t size)
{
    struct evbuffer_ptr pos;
    struct evbuffer_iovec iovec;

    evbuffer_ptr_set(buffer, &pos, offset, EVBUFFER_PTR_SET);

    do
    {
        if (evbuffer_peek(buffer, size, &pos, &iovec, 1) <= 0)
        {
            break;
        }

        io.encrypt(iovec.iov_len, iovec.iov_base);

        TR_ASSERT(size >= iovec.iov_len);
        size -= iovec.iov_len;
    } while (evbuffer_ptr_set(buffer, &pos, iovec.iov_len, EVBUFFER_PTR_ADD) == 0);

    TR_ASSERT(size == 0);
}

void tr_peerIoWriteBuf(tr_peerIo* io, struct evbuffer* buf, bool isPieceData)
{
    size_t const byteCount = evbuffer_get_length(buf);

    if (io->isEncrypted())
    {
        processBuffer(*io, buf, 0, byteCount);
    }

    evbuffer_add_buffer(io->outbuf.get(), buf);
    io->outbuf_info.emplace_back(byteCount, isPieceData);
}

void tr_peerIoWriteBytes(tr_peerIo* io, void const* bytes, size_t byteCount, bool isPieceData)
{
    struct evbuffer_iovec iovec;
    evbuffer_reserve_space(io->outbuf.get(), byteCount, &iovec, 1);

    iovec.iov_len = byteCount;

    memcpy(iovec.iov_base, bytes, iovec.iov_len);

    if (io->isEncrypted())
    {
        io->encrypt(iovec.iov_len, iovec.iov_base);
    }

    evbuffer_commit_space(io->outbuf.get(), &iovec, 1);

    io->outbuf_info.emplace_back(byteCount, isPieceData);
}

/***
****
***/

void evbuffer_add_uint8(struct evbuffer* outbuf, uint8_t addme)
{
    evbuffer_add(outbuf, &addme, 1);
}

void evbuffer_add_uint16(struct evbuffer* outbuf, uint16_t addme_hs)
{
    uint16_t const ns = htons(addme_hs);
    evbuffer_add(outbuf, &ns, sizeof(ns));
}

void evbuffer_add_uint32(struct evbuffer* outbuf, uint32_t addme_hl)
{
    uint32_t const nl = htonl(addme_hl);
    evbuffer_add(outbuf, &nl, sizeof(nl));
}

void evbuffer_add_uint64(struct evbuffer* outbuf, uint64_t addme_hll)
{
    uint64_t const nll = tr_htonll(addme_hll);
    evbuffer_add(outbuf, &nll, sizeof(nll));
}

/***
****
***/

void tr_peerIoReadBytes(tr_peerIo* io, struct evbuffer* inbuf, void* bytes, size_t byteCount)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(evbuffer_get_length(inbuf) >= byteCount);

    evbuffer_remove(inbuf, bytes, byteCount);

    if (io->isEncrypted())
    {
        io->decrypt(byteCount, bytes);
    }
}

void tr_peerIoReadUint16(tr_peerIo* io, struct evbuffer* inbuf, uint16_t* setme)
{
    auto tmp = uint16_t{};
    tr_peerIoReadBytes(io, inbuf, &tmp, sizeof(uint16_t));
    *setme = ntohs(tmp);
}

void tr_peerIoReadUint32(tr_peerIo* io, struct evbuffer* inbuf, uint32_t* setme)
{
    auto tmp = uint32_t{};
    tr_peerIoReadBytes(io, inbuf, &tmp, sizeof(uint32_t));
    *setme = ntohl(tmp);
}

void tr_peerIoDrain(tr_peerIo* io, struct evbuffer* inbuf, size_t byteCount)
{
    char buf[4096];
    size_t const buflen = sizeof(buf);

    while (byteCount > 0)
    {
        size_t const thisPass = std::min(byteCount, buflen);
        tr_peerIoReadBytes(io, inbuf, buf, thisPass);
        byteCount -= thisPass;
    }
}

/***
****
***/

static int tr_peerIoTryRead(tr_peerIo* io, size_t howmuch)
{
    howmuch = io->bandwidth().clamp(TR_DOWN, howmuch);
    if (howmuch == 0)
    {
        return 0;
    }

    auto res = int{};
    switch (io->socket.type)
    {
    case TR_PEER_SOCKET_TYPE_UTP:
        /* UTP_RBDrained notifies libutp that your read buffer is empty.
         * It opens up the congestion window by sending an ACK (soonish)
         * if one was not going to be sent. */
        if (evbuffer_get_length(io->inbuf.get()) == 0)
        {
            utp_read_drained(io->socket.handle.utp);
        }

        break;

    case TR_PEER_SOCKET_TYPE_TCP:
        {
            EVUTIL_SET_SOCKET_ERROR(0);
            res = evbuffer_read(io->inbuf.get(), io->socket.handle.tcp, (int)howmuch);
            int const e = EVUTIL_SOCKET_ERROR();

            tr_logAddTraceIo(io, fmt::format("read {} from peer ({})", res, res == -1 ? tr_net_strerror(e).c_str() : ""));

            if (evbuffer_get_length(io->inbuf.get()) != 0)
            {
                canReadWrapper(io);
            }

            if (res <= 0 && io->gotError != nullptr && e != EAGAIN && e != EINTR && e != EINPROGRESS)
            {
                short what = BEV_EVENT_READING | BEV_EVENT_ERROR;

                if (res == 0)
                {
                    what |= BEV_EVENT_EOF;
                }

                tr_logAddTraceIo(
                    io,
                    fmt::format("tr_peerIoTryRead err: res:{} what:{}, errno:{} ({})", res, what, e, tr_net_strerror(e)));

                io->gotError(io, what, io->userData);
            }

            break;
        }

    default:
        tr_logAddDebugIo(io, fmt::format("unsupported peer socket type {}", io->socket.type));
    }

    return res;
}

static int tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch)
{
    auto const old_len = size_t{ evbuffer_get_length(io->outbuf.get()) };

    tr_logAddTraceIo(io, fmt::format("in tr_peerIoTryWrite {}", howmuch));
    howmuch = std::min(howmuch, old_len);
    howmuch = io->bandwidth().clamp(TR_UP, howmuch);
    if (howmuch == 0)
    {
        return 0;
    }

    auto n = int{};
    switch (io->socket.type)
    {
    case TR_PEER_SOCKET_TYPE_UTP:
        n = utp_write(io->socket.handle.utp, evbuffer_pullup(io->outbuf.get(), howmuch), howmuch);

        if (n > 0)
        {
            evbuffer_drain(io->outbuf.get(), n);
            didWriteWrapper(io, n);
        }

        break;

    case TR_PEER_SOCKET_TYPE_TCP:
        {
            EVUTIL_SET_SOCKET_ERROR(0);
            n = tr_evbuffer_write(io, io->socket.handle.tcp, howmuch);
            int const e = EVUTIL_SOCKET_ERROR();

            if (n > 0)
            {
                didWriteWrapper(io, n);
            }

            if (n < 0 && io->gotError != nullptr && e != 0 && e != EPIPE && e != EAGAIN && e != EINTR && e != EINPROGRESS)
            {
                short const what = BEV_EVENT_WRITING | BEV_EVENT_ERROR;

                tr_logAddTraceIo(
                    io,
                    fmt::format("tr_peerIoTryWrite err: res:{}, what:{}, errno:{} ({})", n, what, e, tr_net_strerror(e)));
                io->gotError(io, what, io->userData);
            }

            break;
        }

    default:
        tr_logAddDebugIo(io, fmt::format("unsupported peer socket type {}", io->socket.type));
    }

    return n;
}

int tr_peerIoFlush(tr_peerIo* io, tr_direction dir, size_t limit)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(tr_isDirection(dir));

    int const bytes_used = dir == TR_DOWN ? tr_peerIoTryRead(io, limit) : tr_peerIoTryWrite(io, limit);
    tr_logAddTraceIo(io, fmt::format("flushing peer-io, direction:{}, limit:{}, byte_used:{}", dir, limit, bytes_used));
    return bytes_used;
}

int tr_peerIoFlushOutgoingProtocolMsgs(tr_peerIo* io)
{
    size_t byteCount = 0;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for (auto const& [n_bytes, is_piece_data] : io->outbuf_info)
    {
        if (is_piece_data)
        {
            break;
        }

        byteCount += n_bytes;
    }

    return tr_peerIoFlush(io, TR_UP, byteCount);
}
