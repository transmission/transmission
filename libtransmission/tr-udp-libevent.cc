// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <utility>

#include <event2/event.h>

#include "libtransmission/session.h"
#include "libtransmission/tr-assert.h"

void tr_session::tr_udp_core_libevent_handler::on_udp_readable([[maybe_unused]] evutil_socket_t s, [[maybe_unused]] short type, void* vself)
{
    TR_ASSERT(vself != nullptr);
    TR_ASSERT(type == EV_READ);

    auto* self = static_cast<tr_session::tr_udp_core_libevent_handler*>(vself);
    self->callback_(self->socket_);
}

tr_session::tr_udp_core_libevent_handler::tr_udp_core_libevent_handler(
    tr_session& session,
    tr_socket_t socket,
    Callback callback)
    : tr_udp_core::EventHandler(std::move(callback))
{
    socket_event_ = event_new(session.event_base(), socket, EV_READ | EV_PERSIST, on_udp_readable, this);
}

tr_session::tr_udp_core_libevent_handler::~tr_udp_core_libevent_handler()
{
    stop();
    event_free(socket_event_);
}

void tr_session::tr_udp_core_libevent_handler::start()
{
    event_add(socket_event_, nullptr);
}

void tr_session::tr_udp_core_libevent_handler::stop()
{
    event_del(socket_event_);
}
