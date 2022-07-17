// This file Copyright © 2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include <event2/event.h>

#include <fmt/core.h>
#include <fmt/format.h>

#include <libutp/utp.h>

#include "transmission.h"

#include "crypto-utils.h" /* tr_rand_int_weak() */
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-socket.h"
#include "session.h"
#include "tr-utp.h"
#include "utils.h"

#ifndef WITH_UTP

void utp_close(UTPSocket* socket)
{
    tr_logAddTrace(fmt::format("utp_close({}) was called.", fmt::ptr(socket)));
}

void utp_read_drained(UTPSocket* socket)
{
    tr_logAddTrace(fmt::format("utp_read_drained({}) was called.", fmt::ptr(socket)));
}

ssize_t utp_write(UTPSocket* socket, void* buf, size_t count)
{
    tr_logAddTrace(fmt::format("utp_write({}, {}, {}) was called.", fmt::ptr(socket), fmt::ptr(socket), count));
    return -1;
}

void tr_utpInit(tr_session* /*session*/)
{
}

bool tr_utpPacket(
    unsigned char const* /*buf*/,
    size_t /*buflen*/,
    sockaddr const* /*from*/,
    socklen_t /*fromlen*/,
    tr_session* /*ss*/)
{
    return false;
}

struct UTPSocket* utp_create_socket(struct_utp_context* /*ctx*/)
{
    return nullptr;
}

int utp_connect(UTPSocket* /*socket*/, sockaddr const* /*to*/, socklen_t /*tolen*/)
{
    return -1;
}

void tr_utpClose(tr_session* /*session*/)
{
}

#else

/* Greg says 50ms works for them. */
static auto constexpr UtpIntervalUs = int{ 500000 };

static void utp_on_accept(tr_session* const session, UTPSocket* const s)
{
    struct sockaddr_storage from_storage;
    auto* const from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof(from_storage);
    tr_address addr;
    tr_port port;

    if (!tr_sessionIsUTPEnabled(session))
    {
        utp_close(s);
        return;
    }

    utp_getpeername(s, from, &fromlen);

    if (!tr_address_from_sockaddr_storage(&addr, &port, &from_storage))
    {
        tr_logAddWarn(_("Unknown socket family"));
        utp_close(s);
        return;
    }

    tr_peerMgrAddIncoming(session->peerMgr, &addr, port, tr_peer_socket_utp_create(s));
}

static void utp_send_to(
    tr_session* const ss,
    uint8_t const* const buf,
    size_t const buflen,
    struct sockaddr const* const to,
    socklen_t const tolen)
{
    if (to->sa_family == AF_INET && ss->udp_socket != TR_BAD_SOCKET)
    {
        (void)sendto(ss->udp_socket, reinterpret_cast<char const*>(buf), buflen, 0, to, tolen);
    }
    else if (to->sa_family == AF_INET6 && ss->udp6_socket != TR_BAD_SOCKET)
    {
        (void)sendto(ss->udp6_socket, reinterpret_cast<char const*>(buf), buflen, 0, to, tolen);
    }
}

#ifdef TR_UTP_TRACE

static void utp_log(tr_session* const /*session*/, char const* const msg)
{
    fmt::print(stderr, FMT_STRING("[utp] {}\n"), msg);
}

#endif

static uint64 utp_callback(utp_callback_arguments* args)
{
    auto* const session = static_cast<tr_session*>(utp_context_get_userdata(args->context));

    TR_ASSERT(tr_isSession(session));
    TR_ASSERT(session->utp_context == args->context);

    switch (args->callback_type)
    {
#ifdef TR_UTP_TRACE

    case UTP_LOG:
        utp_log(session, args->buf);
        break;

#endif

    case UTP_ON_ACCEPT:
        utp_on_accept(session, args->socket);
        break;

    case UTP_SENDTO:
        utp_send_to(session, args->buf, args->len, args->u1.address, args->u2.address_len);
        break;
    }

    return 0;
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
           utp_check_timeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        sec = 2;
        usec = tr_rand_int_weak(1000000);
    }

    tr_timerAdd(*ss->utp_timer, sec, usec);
}

static void timer_callback(evutil_socket_t /*s*/, short /*type*/, void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(session->utp_context);

    utp_check_timeouts(session->utp_context);
    reset_timer(session);
}

void tr_utpInit(tr_session* session)
{
    if (session->utp_context != nullptr)
    {
        return;
    }

    auto* const ctx = utp_init(2);

    if (ctx == nullptr)
    {
        return;
    }

    utp_context_set_userdata(ctx, session);

    utp_set_callback(ctx, UTP_ON_ACCEPT, &utp_callback);
    utp_set_callback(ctx, UTP_SENDTO, &utp_callback);

    tr_peerIoUtpInit(ctx);

#ifdef TR_UTP_TRACE

    utp_set_callback(ctx, UTP_LOG, &utp_callback);

    utp_context_set_option(ctx, UTP_LOG_NORMAL, 1);
    utp_context_set_option(ctx, UTP_LOG_MTU, 1);
    utp_context_set_option(ctx, UTP_LOG_DEBUG, 1);

#endif

    session->utp_context = ctx;
}

bool tr_utpPacket(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss)
{
    if (!ss->isClosed && ss->utp_timer == nullptr)
    {
        ss->utp_timer = evtimer_new(ss->event_base, timer_callback, ss);

        if (ss->utp_timer == nullptr)
        {
            return false;
        }

        reset_timer(ss);
    }

    auto const ret = utp_process_udp(ss->utp_context, buf, buflen, from, fromlen);

    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(ss->utp_context);

    return ret != 0;
}

void tr_utpClose(tr_session* session)
{
    if (session->utp_timer != nullptr)
    {
        evtimer_del(session->utp_timer);
        session->utp_timer = nullptr;
    }

    if (session->utp_context != nullptr)
    {
        utp_context_set_userdata(session->utp_context, nullptr);
        utp_destroy(session->utp_context);
        session->utp_context = nullptr;
    }
}

#endif /* #ifndef WITH_UTP ... else */
