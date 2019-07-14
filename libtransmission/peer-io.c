/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <errno.h>
#include <string.h>

#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "log.h"
#include "net.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
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

#define UTP_READ_BUFFER_SIZE (256 * 1024)

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

/**
***
**/

#define dbgmsg(io, ...) tr_logAddDeepNamed(tr_peerIoGetAddrStr(io), __VA_ARGS__)

/**
***
**/

struct tr_datatype
{
    struct tr_datatype* next;
    size_t length;
    bool isPieceData;
};

static struct tr_datatype* datatype_pool = NULL;

static struct tr_datatype const TR_DATATYPE_INIT =
{
    .next = NULL,
    .length = 0,
    .isPieceData = false
};

static struct tr_datatype* datatype_new(void)
{
    struct tr_datatype* ret;

    if (datatype_pool == NULL)
    {
        ret = tr_new(struct tr_datatype, 1);
    }
    else
    {
        ret = datatype_pool;
        datatype_pool = datatype_pool->next;
    }

    *ret = TR_DATATYPE_INIT;
    return ret;
}

static void datatype_free(struct tr_datatype* datatype)
{
    datatype->next = datatype_pool;
    datatype_pool = datatype;
}

static void peer_io_pull_datatype(tr_peerIo* io)
{
    struct tr_datatype* tmp;

    if ((tmp = io->outbuf_datatypes) != NULL)
    {
        io->outbuf_datatypes = tmp->next;
        datatype_free(tmp);
    }
}

static void peer_io_push_datatype(tr_peerIo* io, struct tr_datatype* datatype)
{
    struct tr_datatype* tmp;

    if ((tmp = io->outbuf_datatypes) != NULL)
    {
        while (tmp->next != NULL)
        {
            tmp = tmp->next;
        }

        tmp->next = datatype;
    }
    else
    {
        io->outbuf_datatypes = datatype;
    }
}

/***
****
***/

static void didWriteWrapper(tr_peerIo* io, unsigned int bytes_transferred)
{
    while (bytes_transferred != 0 && tr_isPeerIo(io))
    {
        struct tr_datatype* next = io->outbuf_datatypes;

        unsigned int const payload = MIN(next->length, bytes_transferred);
        /* For uTP sockets, the overhead is computed in utp_on_overhead. */
        unsigned int const overhead = io->socket.type == TR_PEER_SOCKET_TYPE_TCP ? guessPacketOverhead(payload) : 0;
        uint64_t const now = tr_time_msec();

        tr_bandwidthUsed(&io->bandwidth, TR_UP, payload, next->isPieceData, now);

        if (overhead > 0)
        {
            tr_bandwidthUsed(&io->bandwidth, TR_UP, overhead, false, now);
        }

        if (io->didWrite != NULL)
        {
            io->didWrite(io, payload, next->isPieceData, io->userData);
        }

        if (tr_isPeerIo(io))
        {
            bytes_transferred -= payload;
            next->length -= payload;

            if (next->length == 0)
            {
                peer_io_pull_datatype(io);
            }
        }
    }
}

static void canReadWrapper(tr_peerIo* io)
{
    bool err = false;
    bool done = false;
    tr_session* session;

    dbgmsg(io, "canRead");

    tr_peerIoRef(io);

    session = io->session;

    /* try to consume the input buffer */
    if (io->canRead != NULL)
    {
        uint64_t const now = tr_time_msec();

        tr_sessionLock(session);

        while (!done && !err)
        {
            size_t piece = 0;
            size_t const oldLen = evbuffer_get_length(io->inbuf);
            int const ret = io->canRead(io, io->userData, &piece);
            size_t const used = oldLen - evbuffer_get_length(io->inbuf);
            unsigned int const overhead = guessPacketOverhead(used);

            if (piece != 0 || piece != used)
            {
                if (piece != 0)
                {
                    tr_bandwidthUsed(&io->bandwidth, TR_DOWN, piece, true, now);
                }

                if (used != piece)
                {
                    tr_bandwidthUsed(&io->bandwidth, TR_DOWN, used - piece, false, now);
                }
            }

            if (overhead > 0)
            {
                tr_bandwidthUsed(&io->bandwidth, TR_UP, overhead, false, now);
            }

            switch (ret)
            {
            case READ_NOW:
                if (evbuffer_get_length(io->inbuf) != 0)
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

        tr_sessionUnlock(session);
    }

    tr_peerIoUnref(io);
}

static void event_read_cb(evutil_socket_t fd, short event UNUSED, void* vio)
{
    tr_peerIo* io = vio;

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    int res;
    int e;

    /* Limit the input buffer to 256K, so it doesn't grow too large */
    unsigned int howmuch;
    unsigned int curlen;
    tr_direction const dir = TR_DOWN;
    unsigned int const max = 256 * 1024;

    io->pendingEvents &= ~EV_READ;

    curlen = evbuffer_get_length(io->inbuf);
    howmuch = curlen >= max ? 0 : max - curlen;
    howmuch = tr_bandwidthClamp(&io->bandwidth, TR_DOWN, howmuch);

    dbgmsg(io, "libevent says this peer is ready to read");

    /* if we don't have any bandwidth left, stop reading */
    if (howmuch < 1)
    {
        tr_peerIoSetEnabled(io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR(0);
    res = evbuffer_read(io->inbuf, fd, (int)howmuch);
    e = EVUTIL_SOCKET_ERROR();

    if (res > 0)
    {
        tr_peerIoSetEnabled(io, dir, true);

        /* Invoke the user callback - must always be called last */
        canReadWrapper(io);
    }
    else
    {
        char errstr[512];
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

        dbgmsg(io, "event_read_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e,
            tr_net_strerror(errstr, sizeof(errstr), e));

        if (io->gotError != NULL)
        {
            io->gotError(io, what, io->userData);
        }
    }
}

static int tr_evbuffer_write(tr_peerIo* io, int fd, size_t howmuch)
{
    int e;
    int n;
    char errstr[256];

    EVUTIL_SET_SOCKET_ERROR(0);
    n = evbuffer_write_atmost(io->outbuf, fd, howmuch);
    e = EVUTIL_SOCKET_ERROR();
    dbgmsg(io, "wrote %d to peer (%s)", n, (n == -1 ? tr_net_strerror(errstr, sizeof(errstr), e) : ""));

    return n;
}

static void event_write_cb(evutil_socket_t fd, short event UNUSED, void* vio)
{
    tr_peerIo* io = vio;

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(io->socket.type == TR_PEER_SOCKET_TYPE_TCP);

    int res = 0;
    int e;
    short what = BEV_EVENT_WRITING;
    size_t howmuch;
    tr_direction const dir = TR_UP;
    char errstr[1024];

    io->pendingEvents &= ~EV_WRITE;

    dbgmsg(io, "libevent says this peer is ready to write");

    /* Write as much as possible, since the socket is non-blocking, write() will
     * return if it can't write any more data without blocking */
    howmuch = tr_bandwidthClamp(&io->bandwidth, dir, evbuffer_get_length(io->outbuf));

    /* if we don't have any bandwidth left, stop writing */
    if (howmuch < 1)
    {
        tr_peerIoSetEnabled(io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR(0);
    res = tr_evbuffer_write(io, fd, howmuch);
    e = EVUTIL_SOCKET_ERROR();

    if (res == -1)
    {
        if (e == 0 || e == EAGAIN || e == EINTR || e == EINPROGRESS)
        {
            goto reschedule;
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
        goto error;
    }

    if (evbuffer_get_length(io->outbuf) != 0)
    {
        tr_peerIoSetEnabled(io, dir, true);
    }

    didWriteWrapper(io, res);
    return;

reschedule:
    if (evbuffer_get_length(io->outbuf) != 0)
    {
        tr_peerIoSetEnabled(io, dir, true);
    }

    return;

error:
    tr_net_strerror(errstr, sizeof(errstr), e);
    dbgmsg(io, "event_write_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, errstr);

    if (io->gotError != NULL)
    {
        io->gotError(io, what, io->userData);
    }
}

/**
***
**/

static void maybeSetCongestionAlgorithm(tr_socket_t socket, char const* algorithm)
{
    if (!tr_str_is_empty(algorithm))
    {
        tr_netSetCongestionControl(socket, algorithm);
    }
}

#ifdef WITH_UTP
/* UTP callbacks */

static void utp_on_read(void* closure, unsigned char const* buf, size_t buflen)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    int rc = evbuffer_add(io->inbuf, buf, buflen);
    dbgmsg(io, "utp_on_read got %zu bytes", buflen);

    if (rc < 0)
    {
        tr_logAddNamedError("UTP", "On read evbuffer_add");
        return;
    }

    tr_peerIoSetEnabled(io, TR_DOWN, true);
    canReadWrapper(io);
}

static void utp_on_write(void* closure, unsigned char* buf, size_t buflen)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    int rc = evbuffer_remove(io->outbuf, buf, buflen);
    dbgmsg(io, "utp_on_write sending %zu bytes... evbuffer_remove returned %d", buflen, rc);
    TR_ASSERT(rc == (int)buflen); /* if this fails, we've corrupted our bookkeeping somewhere */

    if (rc < (long)buflen)
    {
        tr_logAddNamedError("UTP", "Short write: %d < %ld", rc, (long)buflen);
    }

    didWriteWrapper(io, buflen);
}

static size_t utp_get_rb_size(void* closure)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    size_t bytes = tr_bandwidthClamp(&io->bandwidth, TR_DOWN, UTP_READ_BUFFER_SIZE);

    dbgmsg(io, "utp_get_rb_size is saying it's ready to read %zu bytes", bytes);
    return UTP_READ_BUFFER_SIZE - bytes;
}

static int tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch);

static void utp_on_writable(tr_peerIo* io)
{
    int n;

    dbgmsg(io, "libutp says this peer is ready to write");

    n = tr_peerIoTryWrite(io, SIZE_MAX);
    tr_peerIoSetEnabled(io, TR_UP, n != 0 && evbuffer_get_length(io->outbuf) != 0);
}

static void utp_on_state_change(void* closure, int state)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    if (state == UTP_STATE_CONNECT)
    {
        dbgmsg(io, "utp_on_state_change -- changed to connected");
        io->utpSupported = true;
    }
    else if (state == UTP_STATE_WRITABLE)
    {
        dbgmsg(io, "utp_on_state_change -- changed to writable");

        if ((io->pendingEvents & EV_WRITE) != 0)
        {
            utp_on_writable(io);
        }
    }
    else if (state == UTP_STATE_EOF)
    {
        if (io->gotError != NULL)
        {
            io->gotError(io, BEV_EVENT_EOF, io->userData);
        }
    }
    else if (state == UTP_STATE_DESTROYING)
    {
        tr_logAddNamedError("UTP", "Impossible state UTP_STATE_DESTROYING");
        return;
    }
    else
    {
        tr_logAddNamedError("UTP", "Unknown state %d", state);
    }
}

static void utp_on_error(void* closure, int errcode)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    dbgmsg(io, "utp_on_error -- errcode is %d", errcode);

    if (io->gotError != NULL)
    {
        errno = errcode;
        io->gotError(io, BEV_EVENT_ERROR, io->userData);
    }
}

static void utp_on_overhead(void* closure, uint8_t /* bool */ send, size_t count, int type UNUSED)
{
    tr_peerIo* io = closure;

    TR_ASSERT(tr_isPeerIo(io));

    dbgmsg(io, "utp_on_overhead -- count is %zu", count);

    tr_bandwidthUsed(&io->bandwidth, send ? TR_UP : TR_DOWN, count, false, tr_time_msec());
}

static struct UTPFunctionTable utp_function_table =
{
    .on_read = utp_on_read,
    .on_write = utp_on_write,
    .get_rb_size = utp_get_rb_size,
    .on_state = utp_on_state_change,
    .on_error = utp_on_error,
    .on_overhead = utp_on_overhead
};

/* Dummy UTP callbacks. */
/* We switch a UTP socket to use these after the associated peerIo has been
   destroyed -- see io_dtor. */

static void dummy_read(void* closure UNUSED, unsigned char const* buf UNUSED, size_t buflen UNUSED)
{
    /* This cannot happen, as far as I'm aware. */
    tr_logAddNamedError("UTP", "On_read called on closed socket");
}

static void dummy_write(void* closure UNUSED, unsigned char* buf, size_t buflen)
{
    /* This can very well happen if we've shut down a peer connection that
       had unflushed buffers.  Complain and send zeroes. */
    tr_logAddNamedDbg("UTP", "On_write called on closed socket");
    memset(buf, 0, buflen);
}

static size_t dummy_get_rb_size(void* closure UNUSED)
{
    return 0;
}

static void dummy_on_state_change(void* closure UNUSED, int state UNUSED)
{
}

static void dummy_on_error(void* closure UNUSED, int errcode UNUSED)
{
}

static void dummy_on_overhead(void* closure UNUSED, uint8_t /* bool */ send UNUSED, size_t count UNUSED, int type UNUSED)
{
}

static struct UTPFunctionTable dummy_utp_function_table =
{
    .on_read = dummy_read,
    .on_write = dummy_write,
    .get_rb_size = dummy_get_rb_size,
    .on_state = dummy_on_state_change,
    .on_error = dummy_on_error,
    .on_overhead = dummy_on_overhead
};

#endif /* #ifdef WITH_UTP */

static tr_peerIo* tr_peerIoNew(tr_session* session, tr_bandwidth* parent, tr_address const* addr, tr_port port,
    uint8_t const* torrentHash, bool isIncoming, bool isSeed, struct tr_peer_socket const socket)
{
    TR_ASSERT(session != NULL);
    TR_ASSERT(session->events != NULL);
    TR_ASSERT(tr_amInEventThread(session));

#ifdef WITH_UTP
    TR_ASSERT(socket.type == TR_PEER_SOCKET_TYPE_TCP || socket.type == TR_PEER_SOCKET_TYPE_UTP);
#else
    TR_ASSERT(socket.type == TR_PEER_SOCKET_TYPE_TCP);
#endif

    if (socket.type == TR_PEER_SOCKET_TYPE_TCP)
    {
        tr_netSetTOS(socket.handle.tcp, session->peerSocketTOS, addr->type);
        maybeSetCongestionAlgorithm(socket.handle.tcp, session->peer_congestion_algorithm);
    }

    tr_peerIo* io = tr_new0(tr_peerIo, 1);
    io->magicNumber = PEER_IO_MAGIC_NUMBER;
    io->refCount = 1;
    tr_cryptoConstruct(&io->crypto, torrentHash, isIncoming);
    io->session = session;
    io->addr = *addr;
    io->isSeed = isSeed;
    io->port = port;
    io->socket = socket;
    io->isIncoming = isIncoming;
    io->timeCreated = tr_time();
    io->inbuf = evbuffer_new();
    io->outbuf = evbuffer_new();
    tr_bandwidthConstruct(&io->bandwidth, session, parent);
    tr_bandwidthSetPeer(&io->bandwidth, io);
    dbgmsg(io, "bandwidth is %p; its parent is %p", (void*)&io->bandwidth, (void*)parent);

    switch (socket.type)
    {
    case TR_PEER_SOCKET_TYPE_TCP:
        dbgmsg(io, "socket (tcp) is %" PRIdMAX, (intmax_t)socket.handle.tcp);
        io->event_read = event_new(session->event_base, socket.handle.tcp, EV_READ, event_read_cb, io);
        io->event_write = event_new(session->event_base, socket.handle.tcp, EV_WRITE, event_write_cb, io);
        break;

#ifdef WITH_UTP

    case TR_PEER_SOCKET_TYPE_UTP:
        dbgmsg(io, "socket (utp) is %p", (void*)socket.handle.utp);
        UTP_SetSockopt(socket.handle.utp, SO_RCVBUF, UTP_READ_BUFFER_SIZE);
        dbgmsg(io, "%s", "calling UTP_SetCallbacks &utp_function_table");
        UTP_SetCallbacks(socket.handle.utp, &utp_function_table, io);

        if (!isIncoming)
        {
            dbgmsg(io, "%s", "calling UTP_Connect");
            UTP_Connect(socket.handle.utp);
        }

        break;

#endif

    default:
        TR_ASSERT_MSG(false, "unsupported peer socket type %d", socket.type);
    }

    return io;
}

tr_peerIo* tr_peerIoNewIncoming(tr_session* session, tr_bandwidth* parent, tr_address const* addr, tr_port port,
    struct tr_peer_socket const socket)
{
    TR_ASSERT(session != NULL);
    TR_ASSERT(tr_address_is_valid(addr));

    return tr_peerIoNew(session, parent, addr, port, NULL, true, false, socket);
}

tr_peerIo* tr_peerIoNewOutgoing(tr_session* session, tr_bandwidth* parent, tr_address const* addr, tr_port port,
    uint8_t const* torrentHash, bool isSeed, bool utp)
{
    TR_ASSERT(session != NULL);
    TR_ASSERT(tr_address_is_valid(addr));
    TR_ASSERT(torrentHash != NULL);

    struct tr_peer_socket socket = TR_PEER_SOCKET_INIT;

    if (utp)
    {
        socket = tr_netOpenPeerUTPSocket(session, addr, port, isSeed);
    }

    if (socket.type == TR_PEER_SOCKET_TYPE_NONE)
    {
        socket = tr_netOpenPeerSocket(session, addr, port, isSeed);
        dbgmsg(NULL, "tr_netOpenPeerSocket returned fd %" PRIdMAX, (intmax_t)(socket.type != TR_PEER_SOCKET_TYPE_NONE ?
            socket.handle.tcp : TR_BAD_SOCKET));
    }

    if (socket.type == TR_PEER_SOCKET_TYPE_NONE)
    {
        return NULL;
    }

    return tr_peerIoNew(session, parent, addr, port, torrentHash, false, isSeed, socket);
}

/***
****
***/

static void event_enable(tr_peerIo* io, short event)
{
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session != NULL);
    TR_ASSERT(io->session->events != NULL);

    bool const need_events = io->socket.type == TR_PEER_SOCKET_TYPE_TCP;

    if (need_events)
    {
        TR_ASSERT(event_initialized(io->event_read));
        TR_ASSERT(event_initialized(io->event_write));
    }

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) == 0)
    {
        dbgmsg(io, "enabling ready-to-read polling");

        if (need_events)
        {
            event_add(io->event_read, NULL);
        }

        io->pendingEvents |= EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) == 0)
    {
        dbgmsg(io, "enabling ready-to-write polling");

        if (need_events)
        {
            event_add(io->event_write, NULL);
        }

        io->pendingEvents |= EV_WRITE;
    }
}

static void event_disable(struct tr_peerIo* io, short event)
{
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session != NULL);
    TR_ASSERT(io->session->events != NULL);

    bool const need_events = io->socket.type == TR_PEER_SOCKET_TYPE_TCP;

    if (need_events)
    {
        TR_ASSERT(event_initialized(io->event_read));
        TR_ASSERT(event_initialized(io->event_write));
    }

    if ((event & EV_READ) != 0 && (io->pendingEvents & EV_READ) != 0)
    {
        dbgmsg(io, "disabling ready-to-read polling");

        if (need_events)
        {
            event_del(io->event_read);
        }

        io->pendingEvents &= ~EV_READ;
    }

    if ((event & EV_WRITE) != 0 && (io->pendingEvents & EV_WRITE) != 0)
    {
        dbgmsg(io, "disabling ready-to-write polling");

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
    TR_ASSERT(io->session->events != NULL);

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
        UTP_SetCallbacks(io->socket.handle.utp, &dummy_utp_function_table, NULL);
        UTP_Close(io->socket.handle.utp);
        break;

#endif

    default:
        TR_ASSERT_MSG(false, "unsupported peer socket type %d", io->socket.type);
    }

    io->socket = TR_PEER_SOCKET_INIT;

    if (io->event_read != NULL)
    {
        event_free(io->event_read);
        io->event_read = NULL;
    }

    if (io->event_write != NULL)
    {
        event_free(io->event_write);
        io->event_write = NULL;
    }
}

static void io_dtor(void* vio)
{
    tr_peerIo* io = vio;

    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(tr_amInEventThread(io->session));
    TR_ASSERT(io->session->events != NULL);

    dbgmsg(io, "in tr_peerIo destructor");
    event_disable(io, EV_READ | EV_WRITE);
    tr_bandwidthDestruct(&io->bandwidth);
    evbuffer_free(io->outbuf);
    evbuffer_free(io->inbuf);
    io_close_socket(io);
    tr_cryptoDestruct(&io->crypto);

    while (io->outbuf_datatypes != NULL)
    {
        peer_io_pull_datatype(io);
    }

    memset(io, ~0, sizeof(tr_peerIo));
    tr_free(io);
}

static void tr_peerIoFree(tr_peerIo* io)
{
    if (io != NULL)
    {
        dbgmsg(io, "in tr_peerIoFree");
        io->canRead = NULL;
        io->didWrite = NULL;
        io->gotError = NULL;
        tr_runInEventThread(io->session, io_dtor, io);
    }
}

void tr_peerIoRefImpl(char const* file, int line, tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    dbgmsg(io, "%s:%d is incrementing the IO's refcount from %d to %d", file, line, io->refCount, io->refCount + 1);

    ++io->refCount;
}

void tr_peerIoUnrefImpl(char const* file, int line, tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    dbgmsg(io, "%s:%d is decrementing the IO's refcount from %d to %d", file, line, io->refCount, io->refCount - 1);

    if (--io->refCount == 0)
    {
        tr_peerIoFree(io);
    }
}

tr_address const* tr_peerIoGetAddress(tr_peerIo const* io, tr_port* port)
{
    TR_ASSERT(tr_isPeerIo(io));

    if (port != NULL)
    {
        *port = io->port;
    }

    return &io->addr;
}

char const* tr_peerIoAddrStr(tr_address const* addr, tr_port port)
{
    static char buf[512];
    tr_snprintf(buf, sizeof(buf), "[%s]:%u", tr_address_to_string(addr), ntohs(port));
    return buf;
}

char const* tr_peerIoGetAddrStr(tr_peerIo const* io)
{
    return tr_isPeerIo(io) ? tr_peerIoAddrStr(&io->addr, io->port) : "error";
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
    tr_peerIoSetIOFuncs(io, NULL, NULL, NULL, NULL);
    tr_peerIoSetEnabled(io, TR_UP, false);
    tr_peerIoSetEnabled(io, TR_DOWN, false);
}

int tr_peerIoReconnect(tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(!tr_peerIoIsIncoming(io));

    tr_session* session = tr_peerIoGetSession(io);

    short int pendingEvents = io->pendingEvents;
    event_disable(io, EV_READ | EV_WRITE);

    io_close_socket(io);

    io->socket = tr_netOpenPeerSocket(session, &io->addr, io->port, io->isSeed);

    if (io->socket.type != TR_PEER_SOCKET_TYPE_TCP)
    {
        return -1;
    }

    io->event_read = event_new(session->event_base, io->socket.handle.tcp, EV_READ, event_read_cb, io);
    io->event_write = event_new(session->event_base, io->socket.handle.tcp, EV_WRITE, event_write_cb, io);

    event_enable(io, pendingEvents);
    tr_netSetTOS(io->socket.handle.tcp, session->peerSocketTOS, io->addr.type);
    maybeSetCongestionAlgorithm(io->socket.handle.tcp, session->peer_congestion_algorithm);

    return 0;
}

/**
***
**/

void tr_peerIoSetTorrentHash(tr_peerIo* io, uint8_t const* hash)
{
    TR_ASSERT(tr_isPeerIo(io));

    tr_cryptoSetTorrentHash(&io->crypto, hash);
}

uint8_t const* tr_peerIoGetTorrentHash(tr_peerIo* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    return tr_cryptoGetTorrentHash(&io->crypto);
}

bool tr_peerIoHasTorrentHash(tr_peerIo const* io)
{
    TR_ASSERT(tr_isPeerIo(io));

    return tr_cryptoHasTorrentHash(&io->crypto);
}

/**
***
**/

void tr_peerIoSetPeersId(tr_peerIo* io, uint8_t const* peer_id)
{
    TR_ASSERT(tr_isPeerIo(io));

    if ((io->peerIdIsSet = peer_id != NULL))
    {
        memcpy(io->peerId, peer_id, 20);
    }
    else
    {
        memset(io->peerId, '\0', 20);
    }
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
    unsigned int const currentSpeed_Bps = tr_bandwidthGetPieceSpeed_Bps(&io->bandwidth, now, TR_UP);
    unsigned int const period = 15U; /* arbitrary */
    /* the 3 is arbitrary; the .5 is to leave room for messages */
    static unsigned int const ceiling = (unsigned int)(MAX_BLOCK_SIZE * 3.5);
    return MAX(ceiling, currentSpeed_Bps * period);
}

size_t tr_peerIoGetWriteBufferSpace(tr_peerIo const* io, uint64_t now)
{
    size_t const desiredLen = getDesiredOutputBufferSize(io, now);
    size_t const currentLen = evbuffer_get_length(io->outbuf);
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

void tr_peerIoSetEncryption(tr_peerIo* io, tr_encryption_type encryption_type)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(encryption_type == PEER_ENCRYPTION_NONE || encryption_type == PEER_ENCRYPTION_RC4);

    io->encryption_type = encryption_type;
}

/**
***
**/

static inline void processBuffer(tr_crypto* crypto, struct evbuffer* buffer, size_t offset, size_t size, void (* callback)(
    tr_crypto*, size_t, void const*, void*))
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

        callback(crypto, iovec.iov_len, iovec.iov_base, iovec.iov_base);

        TR_ASSERT(size >= iovec.iov_len);
        size -= iovec.iov_len;
    }
    while (evbuffer_ptr_set(buffer, &pos, iovec.iov_len, EVBUFFER_PTR_ADD) == 0);

    TR_ASSERT(size == 0);
}

static void addDatatype(tr_peerIo* io, size_t byteCount, bool isPieceData)
{
    struct tr_datatype* d;
    d = datatype_new();
    d->isPieceData = isPieceData;
    d->length = byteCount;
    peer_io_push_datatype(io, d);
}

static inline void maybeEncryptBuffer(tr_peerIo* io, struct evbuffer* buf, size_t offset, size_t size)
{
    if (io->encryption_type == PEER_ENCRYPTION_RC4)
    {
        processBuffer(&io->crypto, buf, offset, size, &tr_cryptoEncrypt);
    }
}

void tr_peerIoWriteBuf(tr_peerIo* io, struct evbuffer* buf, bool isPieceData)
{
    size_t const byteCount = evbuffer_get_length(buf);
    maybeEncryptBuffer(io, buf, 0, byteCount);
    evbuffer_add_buffer(io->outbuf, buf);
    addDatatype(io, byteCount, isPieceData);
}

void tr_peerIoWriteBytes(tr_peerIo* io, void const* bytes, size_t byteCount, bool isPieceData)
{
    struct evbuffer_iovec iovec;
    evbuffer_reserve_space(io->outbuf, byteCount, &iovec, 1);

    iovec.iov_len = byteCount;

    if (io->encryption_type == PEER_ENCRYPTION_RC4)
    {
        tr_cryptoEncrypt(&io->crypto, iovec.iov_len, bytes, iovec.iov_base);
    }
    else
    {
        memcpy(iovec.iov_base, bytes, iovec.iov_len);
    }

    evbuffer_commit_space(io->outbuf, &iovec, 1);

    addDatatype(io, byteCount, isPieceData);
}

/***
****
***/

void evbuffer_add_uint8(struct evbuffer* outbuf, uint8_t byte)
{
    evbuffer_add(outbuf, &byte, 1);
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

static inline void maybeDecryptBuffer(tr_peerIo* io, struct evbuffer* buf, size_t offset, size_t size)
{
    if (io->encryption_type == PEER_ENCRYPTION_RC4)
    {
        processBuffer(&io->crypto, buf, offset, size, &tr_cryptoDecrypt);
    }
}

void tr_peerIoReadBytesToBuf(tr_peerIo* io, struct evbuffer* inbuf, struct evbuffer* outbuf, size_t byteCount)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(evbuffer_get_length(inbuf) >= byteCount);

    size_t const old_length = evbuffer_get_length(outbuf);

    /* append it to outbuf */
    struct evbuffer* tmp = evbuffer_new();
    evbuffer_remove_buffer(inbuf, tmp, byteCount);
    evbuffer_add_buffer(outbuf, tmp);
    evbuffer_free(tmp);

    maybeDecryptBuffer(io, outbuf, old_length, byteCount);
}

void tr_peerIoReadBytes(tr_peerIo* io, struct evbuffer* inbuf, void* bytes, size_t byteCount)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(evbuffer_get_length(inbuf) >= byteCount);

    switch (io->encryption_type)
    {
    case PEER_ENCRYPTION_NONE:
        evbuffer_remove(inbuf, bytes, byteCount);
        break;

    case PEER_ENCRYPTION_RC4:
        evbuffer_remove(inbuf, bytes, byteCount);
        tr_cryptoDecrypt(&io->crypto, byteCount, bytes, bytes);
        break;

    default:
        TR_ASSERT_MSG(false, "unhandled encryption type %d", (int)io->encryption_type);
    }
}

void tr_peerIoReadUint16(tr_peerIo* io, struct evbuffer* inbuf, uint16_t* setme)
{
    uint16_t tmp;
    tr_peerIoReadBytes(io, inbuf, &tmp, sizeof(uint16_t));
    *setme = ntohs(tmp);
}

void tr_peerIoReadUint32(tr_peerIo* io, struct evbuffer* inbuf, uint32_t* setme)
{
    uint32_t tmp;
    tr_peerIoReadBytes(io, inbuf, &tmp, sizeof(uint32_t));
    *setme = ntohl(tmp);
}

void tr_peerIoDrain(tr_peerIo* io, struct evbuffer* inbuf, size_t byteCount)
{
    char buf[4096];
    size_t const buflen = sizeof(buf);

    while (byteCount > 0)
    {
        size_t const thisPass = MIN(byteCount, buflen);
        tr_peerIoReadBytes(io, inbuf, buf, thisPass);
        byteCount -= thisPass;
    }
}

/***
****
***/

static int tr_peerIoTryRead(tr_peerIo* io, size_t howmuch)
{
    int res = 0;

    if ((howmuch = tr_bandwidthClamp(&io->bandwidth, TR_DOWN, howmuch)) != 0)
    {
        switch (io->socket.type)
        {
        case TR_PEER_SOCKET_TYPE_UTP:
            /* UTP_RBDrained notifies libutp that your read buffer is emtpy.
             * It opens up the congestion window by sending an ACK (soonish)
             * if one was not going to be sent. */
            if (evbuffer_get_length(io->inbuf) == 0)
            {
                UTP_RBDrained(io->socket.handle.utp);
            }

            break;

        case TR_PEER_SOCKET_TYPE_TCP:
            {
                int e;
                char err_buf[512];

                EVUTIL_SET_SOCKET_ERROR(0);
                res = evbuffer_read(io->inbuf, io->socket.handle.tcp, (int)howmuch);
                e = EVUTIL_SOCKET_ERROR();

                dbgmsg(io, "read %d from peer (%s)", res, res == -1 ? tr_net_strerror(err_buf, sizeof(err_buf), e) : "");

                if (evbuffer_get_length(io->inbuf) != 0)
                {
                    canReadWrapper(io);
                }

                if (res <= 0 && io->gotError != NULL && e != EAGAIN && e != EINTR && e != EINPROGRESS)
                {
                    short what = BEV_EVENT_READING | BEV_EVENT_ERROR;

                    if (res == 0)
                    {
                        what |= BEV_EVENT_EOF;
                    }

                    dbgmsg(io, "tr_peerIoTryRead got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e,
                        tr_net_strerror(err_buf, sizeof(err_buf), e));

                    io->gotError(io, what, io->userData);
                }

                break;
            }

        default:
            TR_ASSERT_MSG(false, "unsupported peer socket type %d", io->socket.type);
        }
    }

    return res;
}

static int tr_peerIoTryWrite(tr_peerIo* io, size_t howmuch)
{
    int n = 0;
    size_t const old_len = evbuffer_get_length(io->outbuf);
    dbgmsg(io, "in tr_peerIoTryWrite %zu", howmuch);

    if (howmuch > old_len)
    {
        howmuch = old_len;
    }

    if ((howmuch = tr_bandwidthClamp(&io->bandwidth, TR_UP, howmuch)) != 0)
    {
        switch (io->socket.type)
        {
        case TR_PEER_SOCKET_TYPE_UTP:
            UTP_Write(io->socket.handle.utp, howmuch);
            n = old_len - evbuffer_get_length(io->outbuf);
            break;

        case TR_PEER_SOCKET_TYPE_TCP:
            {
                int e;

                EVUTIL_SET_SOCKET_ERROR(0);
                n = tr_evbuffer_write(io, io->socket.handle.tcp, howmuch);
                e = EVUTIL_SOCKET_ERROR();

                if (n > 0)
                {
                    didWriteWrapper(io, n);
                }

                if (n < 0 && io->gotError != NULL && e != 0 && e != EPIPE && e != EAGAIN && e != EINTR && e != EINPROGRESS)
                {
                    char errstr[512];
                    short const what = BEV_EVENT_WRITING | BEV_EVENT_ERROR;

                    dbgmsg(io, "tr_peerIoTryWrite got an error. res is %d, what is %hd, errno is %d (%s)", n, what, e,
                        tr_net_strerror(errstr, sizeof(errstr), e));

                    io->gotError(io, what, io->userData);
                }

                break;
            }

        default:
            TR_ASSERT_MSG(false, "unsupported peer socket type %d", io->socket.type);
        }
    }

    return n;
}

int tr_peerIoFlush(tr_peerIo* io, tr_direction dir, size_t limit)
{
    TR_ASSERT(tr_isPeerIo(io));
    TR_ASSERT(tr_isDirection(dir));

    int bytesUsed = 0;

    if (dir == TR_DOWN)
    {
        bytesUsed = tr_peerIoTryRead(io, limit);
    }
    else
    {
        bytesUsed = tr_peerIoTryWrite(io, limit);
    }

    dbgmsg(io, "flushing peer-io, direction %d, limit %zu, bytesUsed %d", (int)dir, limit, bytesUsed);
    return bytesUsed;
}

int tr_peerIoFlushOutgoingProtocolMsgs(tr_peerIo* io)
{
    size_t byteCount = 0;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for (struct tr_datatype const* it = io->outbuf_datatypes; it != NULL; it = it->next)
    {
        if (it->isPieceData)
        {
            break;
        }
        else
        {
            byteCount += it->length;
        }
    }

    return tr_peerIoFlush(io, TR_UP, byteCount);
}
