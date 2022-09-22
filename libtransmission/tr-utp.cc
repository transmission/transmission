// This file Copyright © 2010 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <chrono>

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
#include "utils.h"

using namespace std::literals;

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

void tr_session::utp_init()
{
}

bool tr_session::utp_packet(unsigned char const* /*buf*/, size_t /*buflen*/, sockaddr const* /*from*/, socklen_t /*fromlen*/)
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

void tr_session::utp_close()
{
}

#else

/* Greg says 50ms works for them. */
static auto constexpr UtpInterval = 50ms;

static void utp_on_accept(tr_session* const session, UTPSocket* const s)
{
    struct sockaddr_storage from_storage;
    auto* const from = (struct sockaddr*)&from_storage;
    socklen_t fromlen = sizeof(from_storage);
    tr_address addr;
    tr_port port;

    if (!session->allowsUTP())
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
    tr_session const* const ss,
    uint8_t const* const buf,
    size_t const buflen,
    struct sockaddr const* const to,
    socklen_t const tolen)
{
    ss->udp_core_->sendto(buf, buflen, to, tolen);
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

    TR_ASSERT(session != nullptr);

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

void tr_session::utp_reset_timer() const
{
    auto interval = std::chrono::milliseconds{};
    auto const random_percent = tr_rand_int_weak(1000) / 1000.0;

    if (allowsUTP())
    {
        static auto constexpr MinInterval = UtpInterval * 0.5;
        static auto constexpr MaxInterval = UtpInterval * 1.5;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    }
    else
    {
        /* If somebody has disabled uTP, then we still want to run
           utp_check_timeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        static auto constexpr MinInterval = 2s;
        static auto constexpr MaxInterval = 3s;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    }

    utp_timer_->startSingleShot(interval);
}

void tr_session::utp_timer_callback() const
{
    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(utp_context_);

    utp_check_timeouts(utp_context_);
    utp_reset_timer();
}

void tr_session::utp_init()
{
    if (utp_context_ != nullptr)
    {
        return;
    }

    auto* const ctx = ::utp_init(2);

    if (ctx == nullptr)
    {
        return;
    }

    utp_context_set_userdata(ctx, this);

    utp_set_callback(ctx, UTP_ON_ACCEPT, &utp_callback);
    utp_set_callback(ctx, UTP_SENDTO, &utp_callback);

    tr_peerIo::utpInit(ctx);

#ifdef TR_UTP_TRACE

    utp_set_callback(ctx, UTP_LOG, &utp_callback);

    utp_context_set_option(ctx, UTP_LOG_NORMAL, 1);
    utp_context_set_option(ctx, UTP_LOG_MTU, 1);
    utp_context_set_option(ctx, UTP_LOG_DEBUG, 1);

#endif

    utp_context_ = ctx;
}

bool tr_session::utp_packet(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen)
{
    if (!isClosed() && !utp_timer_)
    {
        utp_timer_ = timerMaker().create([this]() { utp_timer_callback(); });
        utp_reset_timer();
    }

    auto const ret = utp_process_udp(utp_context_, buf, buflen, from, fromlen);

    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(utp_context_);

    return ret != 0;
}

void tr_session::utp_uninit()
{
    utp_timer_.reset();

    if (utp_context_ != nullptr)
    {
        utp_context_set_userdata(utp_context_, nullptr);
        utp_destroy(utp_context_);
        utp_context_ = nullptr;
    }
}

#endif /* #ifndef WITH_UTP ... else */
