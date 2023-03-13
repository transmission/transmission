// This file Copyright © 2010-2023 Juliusz Chroboczek.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <cstdint>
#include <chrono>

#include <fmt/core.h>
#include <fmt/format.h>

#include <libutp/utp.h>

#include "transmission.h"

#include "crypto-utils.h" // tr_rand_int()
#include "log.h"
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "peer-socket.h"
#include "session.h"
#include "timer.h"
#include "tr-utp.h"
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

namespace
{
/* Greg says 50ms works for them. */
auto constexpr UtpInterval = 50ms;

void utp_on_accept(tr_session* const session, UTPSocket* const utp_sock)
{
    auto from_storage = sockaddr_storage{};
    auto* const from = reinterpret_cast<sockaddr*>(&from_storage);
    socklen_t fromlen = sizeof(from_storage);

    if (!session->allowsUTP() || tr_peer_socket::limit_reached(session))
    {
        utp_close(utp_sock);
        return;
    }

    utp_getpeername(utp_sock, from, &fromlen);

    if (auto addrport = tr_address::from_sockaddr(reinterpret_cast<struct sockaddr*>(&from_storage)); addrport)
    {
        auto const& [addr, port] = *addrport;
        session->addIncoming(tr_peer_socket{ addr, port, utp_sock });
    }
    else
    {
        tr_logAddWarn(_("Unknown socket family"));
        utp_close(utp_sock);
    }
}

void utp_send_to(
    tr_session const* const ss,
    uint8_t const* const buf,
    size_t const buflen,
    struct sockaddr const* const to,
    socklen_t const tolen)
{
    ss->udp_core_->sendto(buf, buflen, to, tolen);
}

uint64 utp_callback(utp_callback_arguments* args)
{
    auto* const session = static_cast<tr_session*>(utp_context_get_userdata(args->context));

    TR_ASSERT(session != nullptr);
    TR_ASSERT(session->utp_context == args->context);

    switch (args->callback_type)
    {
#ifdef TR_UTP_TRACE
    case UTP_LOG:
        fmt::print(stderr, FMT_STRING("[µTP] {}\n"), args->buf);
        break;
#endif

    case UTP_ON_ACCEPT:
        utp_on_accept(session, args->socket);
        break;

    case UTP_SENDTO:
        utp_send_to(session, args->buf, args->len, args->address, args->address_len);
        break;
    }

    return 0;
}

void restart_timer(tr_session* session)
{
    auto interval = std::chrono::milliseconds{};
    auto const random_percent = tr_rand_int(1000U) / 1000.0;

    if (session->allowsUTP())
    {
        static auto constexpr MinInterval = UtpInterval * 0.5;
        static auto constexpr MaxInterval = UtpInterval * 1.5;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    }
    else
    {
        /* If somebody has disabled µTP, then we still want to run
           utp_check_timeouts, in order to let closed sockets finish
           gracefully and so on.  However, since we're not particularly
           interested in that happening in a timely manner, we might as
           well use a large timeout. */
        static auto constexpr MinInterval = 2s;
        static auto constexpr MaxInterval = 3s;
        auto const target = MinInterval + random_percent * (MaxInterval - MinInterval);
        interval = std::chrono::duration_cast<std::chrono::milliseconds>(target);
    }

    session->utp_timer->startSingleShot(interval);
}

void timer_callback(void* vsession)
{
    auto* session = static_cast<tr_session*>(vsession);

    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(session->utp_context);

    utp_check_timeouts(session->utp_context);
    restart_timer(session);
}
} // namespace

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
    tr_peerIo::utp_init(ctx);

#ifdef TR_UTP_TRACE
    utp_set_callback(ctx, UTP_LOG, &utp_callback);
    utp_context_set_option(ctx, UTP_LOG_NORMAL, 1);
    utp_context_set_option(ctx, UTP_LOG_MTU, 1);
    utp_context_set_option(ctx, UTP_LOG_DEBUG, 1);
#endif

    session->utp_context = ctx;
    session->utp_timer = session->timerMaker().create(timer_callback, session);
    restart_timer(session);
}

bool tr_utpPacket(unsigned char const* buf, size_t buflen, struct sockaddr const* from, socklen_t fromlen, tr_session* ss)
{
    auto const ret = utp_process_udp(ss->utp_context, buf, buflen, from, fromlen);

    /* utp_internal.cpp says "Should be called each time the UDP socket is drained" but it's tricky with libevent */
    utp_issue_deferred_acks(ss->utp_context);

    return ret != 0;
}

void tr_utpClose(tr_session* session)
{
    session->utp_timer.reset();

    if (session->utp_context != nullptr)
    {
        utp_context_set_userdata(session->utp_context, nullptr);
        utp_destroy(session->utp_context);
        session->utp_context = nullptr;
    }
}

#endif /* #ifndef WITH_UTP ... else */
