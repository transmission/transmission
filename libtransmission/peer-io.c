/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <assert.h>
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
#include "trevent.h" /* tr_runInEventThread () */
#include "tr-utp.h"
#include "utils.h"


#ifdef _WIN32
 #undef  EAGAIN
 #define EAGAIN       WSAEWOULDBLOCK
 #undef  EINTR
 #define EINTR        WSAEINTR
 #undef  EINPROGRESS
 #define EINPROGRESS  WSAEINPROGRESS
 #undef  EPIPE
 #define EPIPE        WSAECONNRESET
#endif

/* The amount of read bufferring that we allow for uTP sockets. */

#define UTP_READ_BUFFER_SIZE (256 * 1024)

static size_t
guessPacketOverhead (size_t d)
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
    const double assumed_payload_data_rate = 94.0;

    return (unsigned int)(d * (100.0 / assumed_payload_data_rate) - d);
}

/**
***
**/

#define dbgmsg(io, ...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, tr_peerIoGetAddrStr (io), __VA_ARGS__); \
    } \
  while (0)

/**
***
**/

struct tr_datatype
{
    struct tr_datatype * next;
    size_t length;
    bool isPieceData;
};

static struct tr_datatype * datatype_pool = NULL;

static const struct tr_datatype TR_DATATYPE_INIT = { NULL, 0, false };

static struct tr_datatype *
datatype_new (void)
{
    struct tr_datatype * ret;

    if (datatype_pool == NULL)
        ret = tr_new (struct tr_datatype, 1);
    else {
        ret = datatype_pool;
        datatype_pool = datatype_pool->next;
    }

    *ret = TR_DATATYPE_INIT;
    return ret;
}

static void
datatype_free (struct tr_datatype * datatype)
{
    datatype->next = datatype_pool;
    datatype_pool = datatype;
}

static void
peer_io_pull_datatype (tr_peerIo * io)
{
    struct tr_datatype * tmp;

    if ((tmp = io->outbuf_datatypes))
    {
        io->outbuf_datatypes = tmp->next;
        datatype_free (tmp);
    }
}

static void
peer_io_push_datatype (tr_peerIo * io, struct tr_datatype * datatype)
{
    struct tr_datatype * tmp;

    if ((tmp = io->outbuf_datatypes)) {
        while (tmp->next != NULL)
            tmp = tmp->next;
        tmp->next = datatype;
    } else {
        io->outbuf_datatypes = datatype;
    }
}

/***
****
***/

static void
didWriteWrapper (tr_peerIo * io, unsigned int bytes_transferred)
{
     while (bytes_transferred && tr_isPeerIo (io))
     {
        struct tr_datatype * next = io->outbuf_datatypes;

        const unsigned int payload = MIN (next->length, bytes_transferred);
        /* For uTP sockets, the overhead is computed in utp_on_overhead. */
        const unsigned int overhead =
            io->socket != TR_BAD_SOCKET ? guessPacketOverhead (payload) : 0;
        const uint64_t now = tr_time_msec ();

        tr_bandwidthUsed (&io->bandwidth, TR_UP, payload, next->isPieceData, now);

        if (overhead > 0)
            tr_bandwidthUsed (&io->bandwidth, TR_UP, overhead, false, now);

        if (io->didWrite)
            io->didWrite (io, payload, next->isPieceData, io->userData);

        if (tr_isPeerIo (io))
        {
            bytes_transferred -= payload;
            next->length -= payload;
            if (!next->length)
                peer_io_pull_datatype (io);
        }
    }
}

static void
canReadWrapper (tr_peerIo * io)
{
    bool err = false;
    bool done = false;
    tr_session * session;

    dbgmsg (io, "canRead");

    tr_peerIoRef (io);

    session = io->session;

    /* try to consume the input buffer */
    if (io->canRead)
    {
        const uint64_t now = tr_time_msec ();

        tr_sessionLock (session);

        while (!done && !err)
        {
            size_t piece = 0;
            const size_t oldLen = evbuffer_get_length (io->inbuf);
            const int ret = io->canRead (io, io->userData, &piece);
            const size_t used = oldLen - evbuffer_get_length (io->inbuf);
            const unsigned int overhead = guessPacketOverhead (used);

            if (piece || (piece!=used))
            {
                if (piece)
                    tr_bandwidthUsed (&io->bandwidth, TR_DOWN, piece, true, now);

                if (used != piece)
                    tr_bandwidthUsed (&io->bandwidth, TR_DOWN, used - piece, false, now);
            }

            if (overhead > 0)
                tr_bandwidthUsed (&io->bandwidth, TR_UP, overhead, false, now);

            switch (ret)
            {
                case READ_NOW:
                    if (evbuffer_get_length (io->inbuf))
                        continue;
                    done = true;
                    break;

                case READ_LATER:
                    done = true;
                    break;

                case READ_ERR:
                    err = true;
                    break;
            }

            assert (tr_isPeerIo (io));
        }

        tr_sessionUnlock (session);
    }

    tr_peerIoUnref (io);
}

static void
event_read_cb (evutil_socket_t fd, short event UNUSED, void * vio)
{
    int res;
    int e;
    tr_peerIo * io = vio;

    /* Limit the input buffer to 256K, so it doesn't grow too large */
    unsigned int howmuch;
    unsigned int curlen;
    const tr_direction dir = TR_DOWN;
    const unsigned int max = 256 * 1024;

    assert (tr_isPeerIo (io));
    assert (io->socket != TR_BAD_SOCKET);

    io->pendingEvents &= ~EV_READ;

    curlen = evbuffer_get_length (io->inbuf);
    howmuch = curlen >= max ? 0 : max - curlen;
    howmuch = tr_bandwidthClamp (&io->bandwidth, TR_DOWN, howmuch);

    dbgmsg (io, "libevent says this peer is ready to read");

    /* if we don't have any bandwidth left, stop reading */
    if (howmuch < 1) {
        tr_peerIoSetEnabled (io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR (0);
    res = evbuffer_read (io->inbuf, fd, (int)howmuch);
    e = EVUTIL_SOCKET_ERROR ();

    if (res > 0)
    {
        tr_peerIoSetEnabled (io, dir, true);

        /* Invoke the user callback - must always be called last */
        canReadWrapper (io);
    }
    else
    {
        char errstr[512];
        short what = BEV_EVENT_READING;

        if (res == 0) /* EOF */
            what |= BEV_EVENT_EOF;
        else if (res == -1) {
            if (e == EAGAIN || e == EINTR) {
                tr_peerIoSetEnabled (io, dir, true);
                return;
            }
            what |= BEV_EVENT_ERROR;
        }

        dbgmsg (io, "event_read_cb got an error. res is %d, what is %hd, errno is %d (%s)",
                res, what, e, tr_net_strerror (errstr, sizeof (errstr), e));

        if (io->gotError != NULL)
            io->gotError (io, what, io->userData);
    }
}

static int
tr_evbuffer_write (tr_peerIo * io, int fd, size_t howmuch)
{
    int e;
    int n;
    char errstr[256];

    EVUTIL_SET_SOCKET_ERROR (0);
    n = evbuffer_write_atmost (io->outbuf, fd, howmuch);
    e = EVUTIL_SOCKET_ERROR ();
    dbgmsg (io, "wrote %d to peer (%s)", n, (n==-1?tr_net_strerror (errstr,sizeof (errstr),e):""));

    return n;
}

static void
event_write_cb (evutil_socket_t fd, short event UNUSED, void * vio)
{
    int res = 0;
    int e;
    short what = BEV_EVENT_WRITING;
    tr_peerIo * io = vio;
    size_t howmuch;
    const tr_direction dir = TR_UP;
    char errstr[1024];

    assert (tr_isPeerIo (io));
    assert (io->socket != TR_BAD_SOCKET);

    io->pendingEvents &= ~EV_WRITE;

    dbgmsg (io, "libevent says this peer is ready to write");

    /* Write as much as possible, since the socket is non-blocking, write () will
     * return if it can't write any more data without blocking */
    howmuch = tr_bandwidthClamp (&io->bandwidth, dir, evbuffer_get_length (io->outbuf));

    /* if we don't have any bandwidth left, stop writing */
    if (howmuch < 1) {
        tr_peerIoSetEnabled (io, dir, false);
        return;
    }

    EVUTIL_SET_SOCKET_ERROR (0);
    res = tr_evbuffer_write (io, fd, howmuch);
    e = EVUTIL_SOCKET_ERROR ();

    if (res == -1) {
        if (!e || e == EAGAIN || e == EINTR || e == EINPROGRESS)
            goto reschedule;
        /* error case */
        what |= BEV_EVENT_ERROR;
    } else if (res == 0) {
        /* eof case */
        what |= BEV_EVENT_EOF;
    }
    if (res <= 0)
        goto error;

    if (evbuffer_get_length (io->outbuf))
        tr_peerIoSetEnabled (io, dir, true);

    didWriteWrapper (io, res);
    return;

 reschedule:
    if (evbuffer_get_length (io->outbuf))
        tr_peerIoSetEnabled (io, dir, true);
    return;

 error:

    tr_net_strerror (errstr, sizeof (errstr), e);
    dbgmsg (io, "event_write_cb got an error. res is %d, what is %hd, errno is %d (%s)", res, what, e, errstr);

    if (io->gotError != NULL)
        io->gotError (io, what, io->userData);
}

/**
***
**/

static void
maybeSetCongestionAlgorithm (tr_socket_t   socket,
                             const char  * algorithm)
{
    if (algorithm && *algorithm)
        tr_netSetCongestionControl (socket, algorithm);
}

#ifdef WITH_UTP
/* UTP callbacks */

static void
utp_on_read (void *closure, const unsigned char *buf, size_t buflen)
{
    int rc;
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    rc = evbuffer_add (io->inbuf, buf, buflen);
    dbgmsg (io, "utp_on_read got %zu bytes", buflen);

    if (rc < 0) {
        tr_logAddNamedError ("UTP", "On read evbuffer_add");
        return;
    }

    tr_peerIoSetEnabled (io, TR_DOWN, true);
    canReadWrapper (io);
}

static void
utp_on_write (void *closure, unsigned char *buf, size_t buflen)
{
    int rc;
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    rc = evbuffer_remove (io->outbuf, buf, buflen);
    dbgmsg (io, "utp_on_write sending %zu bytes... evbuffer_remove returned %d", buflen, rc);
    assert (rc == (int)buflen); /* if this fails, we've corrupted our bookkeeping somewhere */
    if (rc < (long)buflen) {
        tr_logAddNamedError ("UTP", "Short write: %d < %ld", rc, (long)buflen);
    }

    didWriteWrapper (io, buflen);
}

static size_t
utp_get_rb_size (void *closure)
{
    size_t bytes;
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    bytes = tr_bandwidthClamp (&io->bandwidth, TR_DOWN, UTP_READ_BUFFER_SIZE);

    dbgmsg (io, "utp_get_rb_size is saying it's ready to read %zu bytes", bytes);
    return UTP_READ_BUFFER_SIZE - bytes;
}

static int tr_peerIoTryWrite (tr_peerIo * io, size_t howmuch);

static void
utp_on_writable (tr_peerIo *io)
{
    int n;

    dbgmsg (io, "libutp says this peer is ready to write");

    n = tr_peerIoTryWrite (io, SIZE_MAX);
    tr_peerIoSetEnabled (io, TR_UP, n && evbuffer_get_length (io->outbuf));
}

static void
utp_on_state_change (void *closure, int state)
{
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    if (state == UTP_STATE_CONNECT) {
        dbgmsg (io, "utp_on_state_change -- changed to connected");
        io->utpSupported = true;
    } else if (state == UTP_STATE_WRITABLE) {
        dbgmsg (io, "utp_on_state_change -- changed to writable");
        if (io->pendingEvents & EV_WRITE)
            utp_on_writable (io);
    } else if (state == UTP_STATE_EOF) {
        if (io->gotError)
            io->gotError (io, BEV_EVENT_EOF, io->userData);
    } else if (state == UTP_STATE_DESTROYING) {
        tr_logAddNamedError ("UTP", "Impossible state UTP_STATE_DESTROYING");
        return;
    } else {
        tr_logAddNamedError ("UTP", "Unknown state %d", state);
    }
}

static void
utp_on_error (void *closure, int errcode)
{
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    dbgmsg (io, "utp_on_error -- errcode is %d", errcode);

    if (io->gotError) {
        errno = errcode;
        io->gotError (io, BEV_EVENT_ERROR, io->userData);
    }
}

static void
utp_on_overhead (void *closure, uint8_t send, size_t count, int type UNUSED)
{
    tr_peerIo *io = closure;
    assert (tr_isPeerIo (io));

    dbgmsg (io, "utp_on_overhead -- count is %zu", count);

    tr_bandwidthUsed (&io->bandwidth, send ? TR_UP : TR_DOWN,
                      count, false, tr_time_msec ());
}

static struct UTPFunctionTable utp_function_table = {
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

static void
dummy_read (void * closure UNUSED, const unsigned char *buf UNUSED, size_t buflen UNUSED)
{
    /* This cannot happen, as far as I'm aware. */
    tr_logAddNamedError ("UTP", "On_read called on closed socket");

}

static void
dummy_write (void * closure UNUSED, unsigned char *buf, size_t buflen)
{
    /* This can very well happen if we've shut down a peer connection that
       had unflushed buffers.  Complain and send zeroes. */
    tr_logAddNamedDbg ("UTP", "On_write called on closed socket");
    memset (buf, 0, buflen);
}

static size_t
dummy_get_rb_size (void * closure UNUSED)
{
    return 0;
}

static void
dummy_on_state_change (void * closure UNUSED, int state UNUSED)
{
    return;
}

static void
dummy_on_error (void * closure UNUSED, int errcode UNUSED)
{
    return;
}

static void
dummy_on_overhead (void *closure UNUSED, uint8_t send UNUSED, size_t count UNUSED, int type UNUSED)
{
    return;
}

static struct UTPFunctionTable dummy_utp_function_table = {
    .on_read = dummy_read,
    .on_write = dummy_write,
    .get_rb_size = dummy_get_rb_size,
    .on_state = dummy_on_state_change,
    .on_error = dummy_on_error,
    .on_overhead = dummy_on_overhead
};

#endif /* #ifdef WITH_UTP */

static tr_peerIo*
tr_peerIoNew (tr_session       * session,
              tr_bandwidth     * parent,
              const tr_address * addr,
              tr_port            port,
              const uint8_t    * torrentHash,
              bool               isIncoming,
              bool               isSeed,
              tr_socket_t        socket,
              struct UTPSocket * utp_socket)
{
    tr_peerIo * io;

    assert (session != NULL);
    assert (session->events != NULL);
    assert (tr_isBool (isIncoming));
    assert (tr_isBool (isSeed));
    assert (tr_amInEventThread (session));
    assert ((socket == TR_BAD_SOCKET) == (utp_socket != NULL));
#ifndef WITH_UTP
    assert (socket != TR_BAD_SOCKET);
#endif

    if (socket != TR_BAD_SOCKET) {
        tr_netSetTOS (socket, session->peerSocketTOS);
        maybeSetCongestionAlgorithm (socket, session->peer_congestion_algorithm);
    }

    io = tr_new0 (tr_peerIo, 1);
    io->magicNumber = PEER_IO_MAGIC_NUMBER;
    io->refCount = 1;
    tr_cryptoConstruct (&io->crypto, torrentHash, isIncoming);
    io->session = session;
    io->addr = *addr;
    io->isSeed = isSeed;
    io->port = port;
    io->socket = socket;
    io->utp_socket = utp_socket;
    io->isIncoming = isIncoming;
    io->timeCreated = tr_time ();
    io->inbuf = evbuffer_new ();
    io->outbuf = evbuffer_new ();
    tr_bandwidthConstruct (&io->bandwidth, session, parent);
    tr_bandwidthSetPeer (&io->bandwidth, io);
    dbgmsg (io, "bandwidth is %p; its parent is %p", (void*)&io->bandwidth, (void*)parent);
    dbgmsg (io, "socket is %"TR_PRI_SOCK", utp_socket is %p", socket, (void*)utp_socket);

    if (io->socket != TR_BAD_SOCKET) {
        io->event_read = event_new (session->event_base,
                                    io->socket, EV_READ, event_read_cb, io);
        io->event_write = event_new (session->event_base,
                                     io->socket, EV_WRITE, event_write_cb, io);
    }
#ifdef WITH_UTP
    else {
        UTP_SetSockopt (utp_socket, SO_RCVBUF, UTP_READ_BUFFER_SIZE);
        dbgmsg (io, "%s", "calling UTP_SetCallbacks &utp_function_table");
        UTP_SetCallbacks (utp_socket,
                          &utp_function_table,
                          io);
        if (!isIncoming) {
            dbgmsg (io, "%s", "calling UTP_Connect");
            UTP_Connect (utp_socket);
        }
    }
#endif

    return io;
}

tr_peerIo*
tr_peerIoNewIncoming (tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      tr_socket_t         fd,
                      struct UTPSocket  * utp_socket)
{
    assert (session);
    assert (tr_address_is_valid (addr));

    return tr_peerIoNew (session, parent, addr, port, NULL, true, false,
                         fd, utp_socket);
}

tr_peerIo*
tr_peerIoNewOutgoing (tr_session        * session,
                      tr_bandwidth      * parent,
                      const tr_address  * addr,
                      tr_port             port,
                      const uint8_t     * torrentHash,
                      bool                isSeed,
                      bool                utp)
{
    tr_socket_t fd = TR_BAD_SOCKET;
    struct UTPSocket * utp_socket = NULL;

    assert (session);
    assert (tr_address_is_valid (addr));
    assert (torrentHash);

    if (utp)
        utp_socket = tr_netOpenPeerUTPSocket (session, addr, port, isSeed);

    if (!utp_socket) {
        fd = tr_netOpenPeerSocket (session, addr, port, isSeed);
        dbgmsg (NULL, "tr_netOpenPeerSocket returned fd %"TR_PRI_SOCK, fd);
    }

    if (fd == TR_BAD_SOCKET && utp_socket == NULL)
        return NULL;

    return tr_peerIoNew (session, parent, addr, port,
                         torrentHash, false, isSeed, fd, utp_socket);
}

/***
****
***/

static void
event_enable (tr_peerIo * io, short event)
{
    assert (tr_amInEventThread (io->session));
    assert (io->session != NULL);
    assert (io->session->events != NULL);

    if (io->socket != TR_BAD_SOCKET)
    {
        assert (event_initialized (io->event_read));
        assert (event_initialized (io->event_write));
    }

    if ((event & EV_READ) && ! (io->pendingEvents & EV_READ))
    {
        dbgmsg (io, "enabling ready-to-read polling");
        if (io->socket != TR_BAD_SOCKET)
            event_add (io->event_read, NULL);
        io->pendingEvents |= EV_READ;
    }

    if ((event & EV_WRITE) && ! (io->pendingEvents & EV_WRITE))
    {
        dbgmsg (io, "enabling ready-to-write polling");
        if (io->socket != TR_BAD_SOCKET)
            event_add (io->event_write, NULL);
        io->pendingEvents |= EV_WRITE;
    }
}

static void
event_disable (struct tr_peerIo * io, short event)
{
    assert (tr_amInEventThread (io->session));
    assert (io->session != NULL);
    assert (io->session->events != NULL);

    if (io->socket != TR_BAD_SOCKET)
    {
        assert (event_initialized (io->event_read));
        assert (event_initialized (io->event_write));
    }

    if ((event & EV_READ) && (io->pendingEvents & EV_READ))
    {
        dbgmsg (io, "disabling ready-to-read polling");
        if (io->socket != TR_BAD_SOCKET)
            event_del (io->event_read);
        io->pendingEvents &= ~EV_READ;
    }

    if ((event & EV_WRITE) && (io->pendingEvents & EV_WRITE))
    {
        dbgmsg (io, "disabling ready-to-write polling");
        if (io->socket != TR_BAD_SOCKET)
            event_del (io->event_write);
        io->pendingEvents &= ~EV_WRITE;
    }
}

void
tr_peerIoSetEnabled (tr_peerIo    * io,
                     tr_direction   dir,
                     bool           isEnabled)
{
    const short event = dir == TR_UP ? EV_WRITE : EV_READ;

    assert (tr_isPeerIo (io));
    assert (tr_isDirection (dir));
    assert (tr_amInEventThread (io->session));
    assert (io->session->events != NULL);

    if (isEnabled)
        event_enable (io, event);
    else
        event_disable (io, event);
}

/***
****
***/
static void
io_close_socket (tr_peerIo * io)
{
    if (io->socket != TR_BAD_SOCKET) {
        tr_netClose (io->session, io->socket);
        io->socket = TR_BAD_SOCKET;
    }

    if (io->event_read != NULL) {
        event_free (io->event_read);
        io->event_read = NULL;
    }

    if (io->event_write != NULL) {
        event_free (io->event_write);
        io->event_write = NULL;
    }

#ifdef WITH_UTP
    if (io->utp_socket) {
        UTP_SetCallbacks (io->utp_socket,
                          &dummy_utp_function_table,
                          NULL);
        UTP_Close (io->utp_socket);

        io->utp_socket = NULL;
    }
#endif
}

static void
io_dtor (void * vio)
{
    tr_peerIo * io = vio;

    assert (tr_isPeerIo (io));
    assert (tr_amInEventThread (io->session));
    assert (io->session->events != NULL);

    dbgmsg (io, "in tr_peerIo destructor");
    event_disable (io, EV_READ | EV_WRITE);
    tr_bandwidthDestruct (&io->bandwidth);
    evbuffer_free (io->outbuf);
    evbuffer_free (io->inbuf);
    io_close_socket (io);
    tr_cryptoDestruct (&io->crypto);

    while (io->outbuf_datatypes != NULL)
        peer_io_pull_datatype (io);

    memset (io, ~0, sizeof (tr_peerIo));
    tr_free (io);
}

static void
tr_peerIoFree (tr_peerIo * io)
{
    if (io)
    {
        dbgmsg (io, "in tr_peerIoFree");
        io->canRead = NULL;
        io->didWrite = NULL;
        io->gotError = NULL;
        tr_runInEventThread (io->session, io_dtor, io);
    }
}

void
tr_peerIoRefImpl (const char * file, int line, tr_peerIo * io)
{
    assert (tr_isPeerIo (io));

    dbgmsg (io, "%s:%d is incrementing the IO's refcount from %d to %d",
                file, line, io->refCount, io->refCount+1);

    ++io->refCount;
}

void
tr_peerIoUnrefImpl (const char * file, int line, tr_peerIo * io)
{
    assert (tr_isPeerIo (io));

    dbgmsg (io, "%s:%d is decrementing the IO's refcount from %d to %d",
                file, line, io->refCount, io->refCount-1);

    if (!--io->refCount)
        tr_peerIoFree (io);
}

const tr_address*
tr_peerIoGetAddress (const tr_peerIo * io, tr_port   * port)
{
    assert (tr_isPeerIo (io));

    if (port)
        *port = io->port;

    return &io->addr;
}

const char*
tr_peerIoAddrStr (const tr_address * addr, tr_port port)
{
    static char buf[512];
    tr_snprintf (buf, sizeof (buf), "[%s]:%u", tr_address_to_string (addr), ntohs (port));
    return buf;
}

const char* tr_peerIoGetAddrStr (const tr_peerIo * io)
{
    return tr_isPeerIo (io) ? tr_peerIoAddrStr (&io->addr, io->port) : "error";
}

void
tr_peerIoSetIOFuncs (tr_peerIo        * io,
                     tr_can_read_cb     readcb,
                     tr_did_write_cb    writecb,
                     tr_net_error_cb    errcb,
                     void             * userData)
{
    io->canRead = readcb;
    io->didWrite = writecb;
    io->gotError = errcb;
    io->userData = userData;
}

void
tr_peerIoClear (tr_peerIo * io)
{
    tr_peerIoSetIOFuncs (io, NULL, NULL, NULL, NULL);
    tr_peerIoSetEnabled (io, TR_UP, false);
    tr_peerIoSetEnabled (io, TR_DOWN, false);
}

int
tr_peerIoReconnect (tr_peerIo * io)
{
    short int pendingEvents;
    tr_session * session;

    assert (tr_isPeerIo (io));
    assert (!tr_peerIoIsIncoming (io));

    session = tr_peerIoGetSession (io);

    pendingEvents = io->pendingEvents;
    event_disable (io, EV_READ | EV_WRITE);

    io_close_socket (io);

    io->socket = tr_netOpenPeerSocket (session, &io->addr, io->port, io->isSeed);
    io->event_read = event_new (session->event_base, io->socket, EV_READ, event_read_cb, io);
    io->event_write = event_new (session->event_base, io->socket, EV_WRITE, event_write_cb, io);

    if (io->socket != TR_BAD_SOCKET)
    {
        event_enable (io, pendingEvents);
        tr_netSetTOS (io->socket, session->peerSocketTOS);
        maybeSetCongestionAlgorithm (io->socket, session->peer_congestion_algorithm);
        return 0;
    }

    return -1;
}

/**
***
**/

void
tr_peerIoSetTorrentHash (tr_peerIo *     io,
                         const uint8_t * hash)
{
    assert (tr_isPeerIo (io));

    tr_cryptoSetTorrentHash (&io->crypto, hash);
}

const uint8_t*
tr_peerIoGetTorrentHash (tr_peerIo * io)
{
    assert (tr_isPeerIo (io));

    return tr_cryptoGetTorrentHash (&io->crypto);
}

bool
tr_peerIoHasTorrentHash (const tr_peerIo * io)
{
    assert (tr_isPeerIo (io));

    return tr_cryptoHasTorrentHash (&io->crypto);
}

/**
***
**/

void
tr_peerIoSetPeersId (tr_peerIo * io, const uint8_t * peer_id)
{
    assert (tr_isPeerIo (io));

    if ((io->peerIdIsSet = peer_id != NULL))
        memcpy (io->peerId, peer_id, 20);
    else
        memset (io->peerId, 0, 20);
}

/**
***
**/

static unsigned int
getDesiredOutputBufferSize (const tr_peerIo * io, uint64_t now)
{
    /* this is all kind of arbitrary, but what seems to work well is
     * being large enough to hold the next 20 seconds' worth of input,
     * or a few blocks, whichever is bigger.
     * It's okay to tweak this as needed */
    const unsigned int currentSpeed_Bps = tr_bandwidthGetPieceSpeed_Bps (&io->bandwidth, now, TR_UP);
    const unsigned int period = 15u; /* arbitrary */
    /* the 3 is arbitrary; the .5 is to leave room for messages */
    static const unsigned int ceiling = (unsigned int)(MAX_BLOCK_SIZE * 3.5);
    return MAX (ceiling, currentSpeed_Bps*period);
}

size_t
tr_peerIoGetWriteBufferSpace (const tr_peerIo * io, uint64_t now)
{
    const size_t desiredLen = getDesiredOutputBufferSize (io, now);
    const size_t currentLen = evbuffer_get_length (io->outbuf);
    size_t freeSpace = 0;

    if (desiredLen > currentLen)
        freeSpace = desiredLen - currentLen;

    return freeSpace;
}

/**
***
**/

void
tr_peerIoSetEncryption (tr_peerIo * io, tr_encryption_type encryption_type)
{
    assert (tr_isPeerIo (io));
    assert (encryption_type == PEER_ENCRYPTION_NONE
         || encryption_type == PEER_ENCRYPTION_RC4);

    io->encryption_type = encryption_type;
}

/**
***
**/

static inline void
processBuffer (tr_crypto        * crypto,
               struct evbuffer  * buffer,
               size_t             offset,
               size_t             size,
               void            (* callback) (tr_crypto *, size_t, const void *, void *))
{
    struct evbuffer_ptr pos;
    struct evbuffer_iovec iovec;

    evbuffer_ptr_set (buffer, &pos, offset, EVBUFFER_PTR_SET);

    do
    {
        if (evbuffer_peek (buffer, size, &pos, &iovec, 1) <= 0)
            break;

        callback (crypto, iovec.iov_len, iovec.iov_base, iovec.iov_base);

        assert (size >= iovec.iov_len);
        size -= iovec.iov_len;
    }
    while (!evbuffer_ptr_set (buffer, &pos, iovec.iov_len, EVBUFFER_PTR_ADD));

    assert (size == 0);
}

static void
addDatatype (tr_peerIo * io, size_t byteCount, bool isPieceData)
{
    struct tr_datatype * d;
    d = datatype_new ();
    d->isPieceData = isPieceData;
    d->length = byteCount;
    peer_io_push_datatype (io, d);
}

static inline void
maybeEncryptBuffer (tr_peerIo       * io,
                    struct evbuffer * buf,
                    size_t            offset,
                    size_t            size)
{
    if (io->encryption_type == PEER_ENCRYPTION_RC4)
        processBuffer (&io->crypto, buf, offset, size, &tr_cryptoEncrypt);
}

void
tr_peerIoWriteBuf (tr_peerIo * io, struct evbuffer * buf, bool isPieceData)
{
    const size_t byteCount = evbuffer_get_length (buf);
    maybeEncryptBuffer (io, buf, 0, byteCount);
    evbuffer_add_buffer (io->outbuf, buf);
    addDatatype (io, byteCount, isPieceData);
}

void
tr_peerIoWriteBytes (tr_peerIo * io, const void * bytes, size_t byteCount, bool isPieceData)
{
    struct evbuffer_iovec iovec;
    evbuffer_reserve_space (io->outbuf, byteCount, &iovec, 1);

    iovec.iov_len = byteCount;
    if (io->encryption_type == PEER_ENCRYPTION_RC4)
        tr_cryptoEncrypt (&io->crypto, iovec.iov_len, bytes, iovec.iov_base);
    else
        memcpy (iovec.iov_base, bytes, iovec.iov_len);
    evbuffer_commit_space (io->outbuf, &iovec, 1);

    addDatatype (io, byteCount, isPieceData);
}

/***
****
***/

void
evbuffer_add_uint8 (struct evbuffer * outbuf, uint8_t byte)
{
    evbuffer_add (outbuf, &byte, 1);
}

void
evbuffer_add_uint16 (struct evbuffer * outbuf, uint16_t addme_hs)
{
    const uint16_t ns = htons (addme_hs);
    evbuffer_add (outbuf, &ns, sizeof (ns));
}

void
evbuffer_add_uint32 (struct evbuffer * outbuf, uint32_t addme_hl)
{
    const uint32_t nl = htonl (addme_hl);
    evbuffer_add (outbuf, &nl, sizeof (nl));
}

void
evbuffer_add_uint64 (struct evbuffer * outbuf, uint64_t addme_hll)
{
    const uint64_t nll = tr_htonll (addme_hll);
    evbuffer_add (outbuf, &nll, sizeof (nll));
}

/***
****
***/

static inline void
maybeDecryptBuffer (tr_peerIo       * io,
                    struct evbuffer * buf,
                    size_t            offset,
                    size_t            size)
{
    if (io->encryption_type == PEER_ENCRYPTION_RC4)
        processBuffer (&io->crypto, buf, offset, size, &tr_cryptoDecrypt);
}

void
tr_peerIoReadBytesToBuf (tr_peerIo * io, struct evbuffer * inbuf, struct evbuffer * outbuf, size_t byteCount)
{
    struct evbuffer * tmp;
    const size_t old_length = evbuffer_get_length (outbuf);

    assert (tr_isPeerIo (io));
    assert (evbuffer_get_length (inbuf) >= byteCount);

    /* append it to outbuf */
    tmp = evbuffer_new ();
    evbuffer_remove_buffer (inbuf, tmp, byteCount);
    evbuffer_add_buffer (outbuf, tmp);
    evbuffer_free (tmp);

    maybeDecryptBuffer (io, outbuf, old_length, byteCount);
}

void
tr_peerIoReadBytes (tr_peerIo * io, struct evbuffer * inbuf, void * bytes, size_t byteCount)
{
    assert (tr_isPeerIo (io));
    assert (evbuffer_get_length (inbuf)  >= byteCount);

    switch (io->encryption_type)
    {
        case PEER_ENCRYPTION_NONE:
            evbuffer_remove (inbuf, bytes, byteCount);
            break;

        case PEER_ENCRYPTION_RC4:
            evbuffer_remove (inbuf, bytes, byteCount);
            tr_cryptoDecrypt (&io->crypto, byteCount, bytes, bytes);
            break;

        default:
            assert (false);
    }
}

void
tr_peerIoReadUint16 (tr_peerIo        * io,
                     struct evbuffer  * inbuf,
                     uint16_t         * setme)
{
    uint16_t tmp;
    tr_peerIoReadBytes (io, inbuf, &tmp, sizeof (uint16_t));
    *setme = ntohs (tmp);
}

void tr_peerIoReadUint32 (tr_peerIo        * io,
                          struct evbuffer  * inbuf,
                          uint32_t         * setme)
{
    uint32_t tmp;
    tr_peerIoReadBytes (io, inbuf, &tmp, sizeof (uint32_t));
    *setme = ntohl (tmp);
}

void
tr_peerIoDrain (tr_peerIo       * io,
                struct evbuffer * inbuf,
                size_t            byteCount)
{
    char buf[4096];
    const size_t buflen = sizeof (buf);

    while (byteCount > 0)
    {
        const size_t thisPass = MIN (byteCount, buflen);
        tr_peerIoReadBytes (io, inbuf, buf, thisPass);
        byteCount -= thisPass;
    }
}

/***
****
***/

static int
tr_peerIoTryRead (tr_peerIo * io, size_t howmuch)
{
    int res = 0;

    if ((howmuch = tr_bandwidthClamp (&io->bandwidth, TR_DOWN, howmuch)))
    {
        if (io->utp_socket != NULL) /* utp peer connection */
        {
            /* UTP_RBDrained notifies libutp that your read buffer is emtpy.
             * It opens up the congestion window by sending an ACK (soonish)
             * if one was not going to be sent. */
            if (evbuffer_get_length (io->inbuf) == 0)
                UTP_RBDrained (io->utp_socket);
        }
        else /* tcp peer connection */
        {
            int e;
            char err_buf[512];

            EVUTIL_SET_SOCKET_ERROR (0);
            res = evbuffer_read (io->inbuf, io->socket, (int)howmuch);
            e = EVUTIL_SOCKET_ERROR ();

            dbgmsg (io, "read %d from peer (%s)", res,
                    (res==-1?tr_net_strerror (err_buf, sizeof (err_buf), e):""));

            if (evbuffer_get_length (io->inbuf))
                canReadWrapper (io);

            if ((res <= 0) && (io->gotError) && (e != EAGAIN) && (e != EINTR) && (e != EINPROGRESS))
            {
                short what = BEV_EVENT_READING | BEV_EVENT_ERROR;
                if (res == 0)
                    what |= BEV_EVENT_EOF;
                dbgmsg (io, "tr_peerIoTryRead got an error. res is %d, what is %hd, errno is %d (%s)",
                        res, what, e, tr_net_strerror (err_buf, sizeof (err_buf), e));
                io->gotError (io, what, io->userData);
            }
        }
    }

    return res;
}

static int
tr_peerIoTryWrite (tr_peerIo * io, size_t howmuch)
{
    int n = 0;
    const size_t old_len = evbuffer_get_length (io->outbuf);
    dbgmsg (io, "in tr_peerIoTryWrite %zu", howmuch);

    if (howmuch > old_len)
        howmuch = old_len;

    if ((howmuch = tr_bandwidthClamp (&io->bandwidth, TR_UP, howmuch)))
    {
        if (io->utp_socket != NULL) /* utp peer connection */
        {
            UTP_Write (io->utp_socket, howmuch);
            n = old_len - evbuffer_get_length (io->outbuf);
        }
        else
        {
            int e;

            EVUTIL_SET_SOCKET_ERROR (0);
            n = tr_evbuffer_write (io, io->socket, howmuch);
            e = EVUTIL_SOCKET_ERROR ();

            if (n > 0)
                didWriteWrapper (io, n);

            if ((n < 0) && (io->gotError) && e && (e != EPIPE) && (e != EAGAIN) && (e != EINTR) && (e != EINPROGRESS))
            {
                char errstr[512];
                const short what = BEV_EVENT_WRITING | BEV_EVENT_ERROR;

                dbgmsg (io, "tr_peerIoTryWrite got an error. res is %d, what is %hd, errno is %d (%s)",
                        n, what, e, tr_net_strerror (errstr, sizeof (errstr), e));

                if (io->gotError != NULL)
                    io->gotError (io, what, io->userData);
            }
        }
    }

    return n;
}

int
tr_peerIoFlush (tr_peerIo  * io, tr_direction dir, size_t limit)
{
    int bytesUsed = 0;

    assert (tr_isPeerIo (io));
    assert (tr_isDirection (dir));

    if (dir == TR_DOWN)
        bytesUsed = tr_peerIoTryRead (io, limit);
    else
        bytesUsed = tr_peerIoTryWrite (io, limit);

    dbgmsg (io, "flushing peer-io, direction %d, limit %zu, bytesUsed %d", (int)dir, limit, bytesUsed);
    return bytesUsed;
}

int
tr_peerIoFlushOutgoingProtocolMsgs (tr_peerIo * io)
{
    size_t byteCount = 0;
    const struct tr_datatype * it;

    /* count up how many bytes are used by non-piece-data messages
       at the front of our outbound queue */
    for (it=io->outbuf_datatypes; it!=NULL; it=it->next)
        if (it->isPieceData)
            break;
        else
            byteCount += it->length;

    return tr_peerIoFlush (io, TR_UP, byteCount);
}
