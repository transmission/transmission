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

#include <event2/event.h>

#include <libutp/utp.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "crypto-utils.h" /* tr_rand_int_weak() */
#include "peer-mgr.h"
#include "peer-socket.h"
#include "tr-assert.h"
#include "tr-utp.h"
#include "utils.h"

#define MY_NAME "UTP"

#define dbgmsg(...) tr_logAddDeepNamed(MY_NAME, __VA_ARGS__)

#ifndef WITH_UTP

void UTP_Close(struct UTPSocket* socket)
{
    tr_logAddNamedError(MY_NAME, "UTP_Close(%p) was called.", socket);
    dbgmsg("UTP_Close(%p) was called.", socket);
    TR_ASSERT(false); /* FIXME: this is too much for the long term, but probably needed in the short term */
}

void UTP_RBDrained(struct UTPSocket* socket)
{
    tr_logAddNamedError(MY_NAME, "UTP_RBDrained(%p) was called.", socket);
    dbgmsg("UTP_RBDrained(%p) was called.", socket);
    TR_ASSERT(false); /* FIXME: this is too much for the long term, but probably needed in the short term */
}

bool UTP_Write(struct UTPSocket* socket, size_t count)
{
    tr_logAddNamedError(MY_NAME, "UTP_RBDrained(%p, %zu) was called.", socket, count);
    dbgmsg("UTP_RBDrained(%p, %zu) was called.", socket, count);
    TR_ASSERT(false); /* FIXME: this is too much for the long term, but probably needed in the short term */
    return false;
}

int tr_utpPacket(unsigned char const* buf UNUSED, size_t buflen UNUSED, struct sockaddr const* from UNUSED,
    socklen_t fromlen UNUSED, tr_session* ss UNUSED)
{
    return -1;
}

struct UTPSocket* UTP_Create(SendToProc* send_to_proc UNUSED, void* send_to_userdata UNUSED, struct sockaddr const* addr UNUSED,
    socklen_t addrlen UNUSED)
{
    errno = ENOSYS;
    return NULL;
}

void tr_utpClose(tr_session* ss UNUSED)
{
}

void tr_utpSendTo(void* closure UNUSED, unsigned char const* buf UNUSED, size_t buflen UNUSED, struct sockaddr const* to UNUSED,
    socklen_t tolen UNUSED)
{
}

#else

/* Greg says 50ms works for them. */

#define UTP_INTERVAL_US 50000

static void incoming(void* closure, struct UTPSocket* s)
{
    tr_session* ss = closure;
    struct sockaddr_storage from_storage;
    struct sockaddr* from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof(from_storage);
    tr_address addr;
    tr_port port;

    if (!tr_sessionIsUTPEnabled(ss))
    {
        UTP_Close(s);
        return;
    }

    UTP_GetPeerName(s, from, &fromlen);

    if (!tr_address_from_sockaddr_storage(&addr, &port, &from_storage))
    {
        tr_logAddNamedError("UTP", "Unknown socket family");
        UTP_Close(s);
        return;
    }

    tr_peerMgrAddIncoming(ss->peerMgr, &addr, port, tr_peer_socket_utp_create(s));
}

void tr_utpSendTo(void* closure, unsigned char const* buf, size_t buflen, struct sockaddr const* to, socklen_t tolen)
{
    tr_session* ss = closure;

    if (to->sa_family == AF_INET && ss->udp_socket != TR_BAD_SOCKET)
    {
        sendto(ss->udp_socket, (void const*)buf, buflen, 0, to, tolen);
    }
    else if (to->sa_family == AF_INET6 && ss->udp6_socket != TR_BAD_SOCKET)
    {
        sendto(ss->udp6_socket, (void const*)buf, buflen, 0, to, tolen);
    }
}

static void reset_timer(tr_session* ss)
{
    int sec;
    int usec;

    if (tr_sessionIsUTPEnabled(ss))
    {
        sec = 0;
        usec = UTP_INTERVAL_US / 2 + tr_rand_int_weak(UTP_INTERVAL_US);
    }
    else
    {
        /* If somebody has disabled uTP, then we still want to run
           UTP_CheckTimeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        sec = 2;
        usec = tr_rand_int_weak(1000000);
    }

    tr_timerAdd(ss->utp_timer, sec, usec);
}

static void timer_callback(evutil_socket_t s UNUSED, short type UNUSED, void* closure)
{
    tr_session* ss = closure;
    UTP_CheckTimeouts();
    reset_timer(ss);
}

int tr_utpPacket(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss)
{
    if (!ss->isClosed && ss->utp_timer == NULL)
    {
        ss->utp_timer = evtimer_new(ss->event_base, timer_callback, ss);

        if (ss->utp_timer == NULL)
        {
            return -1;
        }

        reset_timer(ss);
    }

    return UTP_IsIncomingUTP(incoming, tr_utpSendTo, ss, buf, buflen, from, fromlen);
}

void tr_utpClose(tr_session* session)
{
    if (session->utp_timer != NULL)
    {
        evtimer_del(session->utp_timer);
        session->utp_timer = NULL;
    }
}

#endif /* #ifndef WITH_UTP ... else */
