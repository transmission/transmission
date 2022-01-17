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

#include <cstdint>
#include <libutp/utp.h>

#include "transmission.h"
#include "log.h"
#include "net.h"
#include "session.h"
#include "crypto-utils.h" /* tr_rand_int_weak() */
#include "peer-mgr.h"
#include "peer-socket.h"
#include "tr-utp.h"
#include "utils.h"

#ifndef WITH_UTP

#define MY_NAME "UTP"

#define dbgmsg(...) tr_logAddDeepNamed(MY_NAME, __VA_ARGS__)

void UTP_Close(struct UTPSocket* socket)
{
    tr_logAddNamedError(MY_NAME, "UTP_Close(%p) was called.", socket);
    dbgmsg("UTP_Close(%p) was called.", socket);
}

void UTP_RBDrained(struct UTPSocket* socket)
{
    tr_logAddNamedError(MY_NAME, "UTP_RBDrained(%p) was called.", socket);
    dbgmsg("UTP_RBDrained(%p) was called.", socket);
}

bool UTP_Write(struct UTPSocket* socket, size_t count)
{
    tr_logAddNamedError(MY_NAME, "UTP_RBDrained(%p, %zu) was called.", socket, count);
    dbgmsg("UTP_RBDrained(%p, %zu) was called.", socket, count);
    return false;
}

int tr_utpPacket(
    unsigned char const* /*buf*/,
    size_t /*buflen*/,
    sockaddr const* /*from*/,
    socklen_t /*fromlen*/,
    tr_session* /*ss*/)
{
    return -1;
}

struct UTPSocket* UTP_Create(
    SendToProc* /*send_to_proc*/,
    void* /*send_to_userdata*/,
    sockaddr const* /*addr*/,
    socklen_t /*addrlen*/)
{
    errno = ENOSYS;
    return nullptr;
}

void tr_utpClose(tr_session* /*ss*/)
{
}

void tr_utpSendTo(
    void* /*closure*/,
    unsigned char const* /*buf*/,
    size_t /*buflen*/,
    struct sockaddr const* /*to*/,
    socklen_t /*tolen*/)
{
}

#else

/* Greg says 50ms works for them. */
static auto constexpr UtpIntervalUs = int{ 50000 };

static void incoming(void* vsession, struct UTPSocket* s)
{
    auto* session = static_cast<tr_session*>(vsession);
    struct sockaddr_storage from_storage;
    struct sockaddr* from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof(from_storage);
    tr_address addr;
    tr_port port = 0;

    if (!tr_sessionIsUTPEnabled(session))
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

    tr_peerMgrAddIncoming(session->peerMgr, &addr, port, tr_peer_socket_utp_create(s));
}

void tr_utpSendTo(void* closure, unsigned char const* buf, size_t buflen, struct sockaddr const* to, socklen_t tolen)
{
    auto const* const ss = static_cast<tr_session const*>(closure);

    if (to->sa_family == AF_INET && ss->udp_socket != TR_BAD_SOCKET)
    {
        (void)sendto(ss->udp_socket, reinterpret_cast<char const*>(buf), buflen, 0, to, tolen);
    }
    else if (to->sa_family == AF_INET6 && ss->udp6_socket != TR_BAD_SOCKET)
    {
        (void)sendto(ss->udp6_socket, reinterpret_cast<char const*>(buf), buflen, 0, to, tolen);
    }
}

static void reset_timer(tr_session* ss)
{
    int sec = 0;
    int usec = 0;

    if (tr_sessionIsUTPEnabled(ss))
    {
        sec = 0;
        usec = UtpIntervalUs / 2 + tr_rand_int_weak(UtpIntervalUs);
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

static void timer_callback(evutil_socket_t /*s*/, short /*type*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);
    UTP_CheckTimeouts();
    reset_timer(session);
}

int tr_utpPacket(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss)
{
    if (!ss->isClosed && ss->utp_timer == nullptr)
    {
        ss->utp_timer = evtimer_new(ss->event_base, timer_callback, ss);

        if (ss->utp_timer == nullptr)
        {
            return -1;
        }

        reset_timer(ss);
    }

    return UTP_IsIncomingUTP(incoming, tr_utpSendTo, ss, buf, buflen, from, fromlen);
}

void tr_utpClose(tr_session* session)
{
    if (session->utp_timer != nullptr)
    {
        evtimer_del(session->utp_timer);
        session->utp_timer = nullptr;
    }
}

#endif /* #ifndef WITH_UTP ... else */
