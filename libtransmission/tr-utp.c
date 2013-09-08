/*
Copyright (c) 2010 by Juliusz Chroboczek

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <assert.h>

#include <event2/event.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "crypto.h" /* tr_cryptoWeakRandInt () */
#include "peer-mgr.h"
#include "tr-utp.h"
#include "utils.h"

#define MY_NAME "UTP"

#define dbgmsg(...) \
    do { \
        if (tr_logGetDeepEnabled ()) \
            tr_logAddDeep (__FILE__, __LINE__, MY_NAME, __VA_ARGS__); \
    } while (0)

#ifndef WITH_UTP

void
UTP_Close (struct UTPSocket * socket)
{
    tr_logAddNamedError (MY_NAME, "UTP_Close (%p) was called.", socket);
    dbgmsg ("UTP_Close (%p) was called.", socket);
    assert (0); /* FIXME: this is too much for the long term, but probably needed in the short term */
}

void
UTP_RBDrained (struct UTPSocket *socket)
{
    tr_logAddNamedError (MY_NAME, "UTP_RBDrained (%p) was called.", socket);
    dbgmsg ("UTP_RBDrained (%p) was called.", socket);
    assert (0); /* FIXME: this is too much for the long term, but probably needed in the short term */
}

bool
UTP_Write (struct UTPSocket *socket, size_t count)
{
    tr_logAddNamedError (MY_NAME, "UTP_RBDrained (%p, %"TR_PRIuSIZE") was called.", socket, count);
    dbgmsg ("UTP_RBDrained (%p, %"TR_PRIuSIZE") was called.", socket, count);
    assert (0); /* FIXME: this is too much for the long term, but probably needed in the short term */
    return false;
}

int tr_utpPacket (const unsigned char *buf UNUSED, size_t buflen UNUSED,
                 const struct sockaddr *from UNUSED, socklen_t fromlen UNUSED,
                 tr_session *ss UNUSED) { return -1; }

struct UTPSocket *UTP_Create (SendToProc *send_to_proc UNUSED,
                             void *send_to_userdata UNUSED,
                             const struct sockaddr *addr UNUSED,
                             socklen_t addrlen UNUSED)
{
    errno = ENOSYS;
    return NULL;
}

void tr_utpClose (tr_session * ss UNUSED) { }

void tr_utpSendTo (void *closure UNUSED,
                  const unsigned char *buf UNUSED, size_t buflen UNUSED,
                  const struct sockaddr *to UNUSED, socklen_t tolen UNUSED) { }

#else

/* Greg says 50ms works for them. */

#define UTP_INTERVAL_US 50000

static struct event *utp_timer = NULL;

static void
incoming (void *closure, struct UTPSocket *s)
{
    tr_session *ss = closure;
    struct sockaddr_storage from_storage;
    struct sockaddr *from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof (from_storage);
    tr_address addr;
    tr_port port;

    if (!tr_sessionIsUTPEnabled (ss)) {
        UTP_Close (s);
        return;
    }

    UTP_GetPeerName (s, from, &fromlen);
    if (!tr_address_from_sockaddr_storage (&addr, &port, &from_storage))
    {
        tr_logAddNamedError ("UTP", "Unknown socket family");
        UTP_Close (s);
        return;
    }

    tr_peerMgrAddIncoming (ss->peerMgr, &addr, port, -1, s);
}

void
tr_utpSendTo (void *closure, const unsigned char *buf, size_t buflen,
             const struct sockaddr *to, socklen_t tolen)
{
    tr_session *ss = closure;

    if (to->sa_family == AF_INET && ss->udp_socket)
        sendto (ss->udp_socket, buf, buflen, 0, to, tolen);
    else if (to->sa_family == AF_INET6 && ss->udp_socket)
        sendto (ss->udp6_socket, buf, buflen, 0, to, tolen);
}

static void
reset_timer (tr_session *ss)
{
    int sec;
    int usec;
    if (tr_sessionIsUTPEnabled (ss)) {
        sec = 0;
        usec = UTP_INTERVAL_US / 2 + tr_cryptoWeakRandInt (UTP_INTERVAL_US);
    } else {
        /* If somebody has disabled uTP, then we still want to run
           UTP_CheckTimeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        sec = 2;
        usec = tr_cryptoWeakRandInt (1000000);
    }
    tr_timerAdd (utp_timer, sec, usec);
}

static void
timer_callback (int s UNUSED, short type UNUSED, void *closure)
{
    tr_session *ss = closure;
    UTP_CheckTimeouts ();
    reset_timer (ss);
}

int
tr_utpPacket (const unsigned char *buf, size_t buflen,
             const struct sockaddr *from, socklen_t fromlen,
             tr_session *ss)
{
    if (!ss->isClosed && !utp_timer)
    {
        utp_timer = evtimer_new (ss->event_base, timer_callback, ss);
        if (utp_timer == NULL)
            return -1;
        reset_timer (ss);
    }

    return UTP_IsIncomingUTP (incoming, tr_utpSendTo, ss,
                             buf, buflen, from, fromlen);
}

void
tr_utpClose (tr_session * session UNUSED)
{
    if (utp_timer)
    {
        evtimer_del (utp_timer);
        utp_timer = NULL;
    }
}

#endif /* #ifndef WITH_UTP ... else */
