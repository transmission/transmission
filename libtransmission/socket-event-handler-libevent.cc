// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <utility>

#include <event2/event.h>

#include "libtransmission/session.h"
#include "libtransmission/socket-event-handler-libevent.h"
#include "libtransmission/socket-event-handler.h"
#include "libtransmission/tr-assert.h"

namespace libtransmission {

// static
template<SocketEventType EventType>
void SocketEventHandlerLibevent<EventType>::on_event([[maybe_unused]] evutil_socket_t s, [[maybe_unused]] short type, void* vself)
{
    TR_ASSERT(vself != nullptr);
    constexpr short EvType = (EventType == SocketEventType::Read) ? EV_READ : EV_WRITE;
    TR_ASSERT(EvType == EV_READ);

    auto* self = static_cast<SocketEventHandlerLibevent<EventType>*>(vself);
    self->callback_(self->socket_);
}

template<SocketEventType EventType>
SocketEventHandlerLibevent<EventType>::SocketEventHandlerLibevent(
    struct event_base* event_base,
    tr_socket_t socket,
    Callback callback)
    : SocketEventHandler<EventType>(std::move(callback))
{
    constexpr short EvType = (EventType == SocketEventType::Read) ? EV_READ : EV_WRITE;
    socket_event_ = event_new(event_base, socket, EvType | EV_PERSIST, on_event, this);
}

template<SocketEventType EventType>
SocketEventHandlerLibevent<EventType>::~SocketEventHandlerLibevent()
{
    stop();
    event_free(socket_event_);
}

template<SocketEventType EventType>
void SocketEventHandlerLibevent<EventType>::start()
{
    event_add(socket_event_, nullptr);
}

template<SocketEventType EventType>
void SocketEventHandlerLibevent<EventType>::stop()
{
    event_del(socket_event_);
}

std::unique_ptr<SocketReadEventHandler> SocketEventHandlerLibeventMaker::create_read(tr_socket_t socket, std::function<void(tr_socket_t)> callback)
{
    return std::make_unique<SocketEventHandlerLibevent<SocketEventType::Read>>(event_base_, socket, std::move(callback));
}

std::unique_ptr<SocketWriteEventHandler> SocketEventHandlerLibeventMaker::create_write(tr_socket_t socket, std::function<void(tr_socket_t)> callback)
{
    return std::make_unique<SocketEventHandlerLibevent<SocketEventType::Write>>(event_base_, socket, std::move(callback));
}

} // namespace libtransmission