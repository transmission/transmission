// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <utility>

#include <event2/event.h>

#include "libtransmission/session.h"
#include "libtransmission/socket-event-handler-libevent.h"
#include "libtransmission/tr-assert.h"

namespace libtransmission {

// static
void SocketEventHandlerLibevent::on_readable([[maybe_unused]] evutil_socket_t s, [[maybe_unused]] short type, void* vself)
{
    TR_ASSERT(vself != nullptr);
    TR_ASSERT(type == EV_READ);

    auto* self = static_cast<SocketEventHandlerLibevent*>(vself);
    self->callback_(self->socket_);
}

SocketEventHandlerLibevent::SocketEventHandlerLibevent(
    tr_session& session,
    tr_socket_t socket,
    Callback callback)
    : SocketEventHandler(std::move(callback))
{
    socket_event_ = event_new(session.event_base(), socket, EV_READ | EV_PERSIST, on_readable, this);
}

SocketEventHandlerLibevent::~SocketEventHandlerLibevent()
{
    stop();
    event_free(socket_event_);
}

void SocketEventHandlerLibevent::start()
{
    event_add(socket_event_, nullptr);
}

void SocketEventHandlerLibevent::stop()
{
    event_del(socket_event_);
}

// static
std::unique_ptr<SocketEventHandler> SocketEventHandler::create_libevent_handler(tr_session& session, tr_socket_t socket, Callback callback)
{
    return std::make_unique<SocketEventHandlerLibevent>(session, socket, std::move(callback));
}

} // namespace libtransmission