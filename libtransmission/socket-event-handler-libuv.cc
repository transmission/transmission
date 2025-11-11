// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>
#include <uv.h>

#include "libtransmission/session.h"
#include "libtransmission/socket-event-handler-libuv.h"

namespace libtransmission
{

// static
template<SocketEventType EventType>
void SocketEventHandlerLibuv<EventType>::on_event(struct uv_poll_s* handle, int status, int events)
{
    if (status < 0)
    {
        tr_logAddError(fmt::format("UDP poll error (libuv): {}", uv_strerror(status)));
        return;
    }

    constexpr short UvType = (EventType == SocketEventType::Read) ? UV_READABLE : UV_WRITABLE;

    if ((events & UvType) == 0)
    {
        return;
    }

    auto* self = static_cast<SocketEventHandlerLibuv<EventType>*>(handle->data);
    self->callback_(self->socket_);
}

template<SocketEventType EventType>
SocketEventHandlerLibuv<EventType>::SocketEventHandlerLibuv(tr_session& session, tr_socket_t socket, Callback callback)
    : SocketEventHandler<EventType>(std::move(callback)), socket_{ socket }
{
    socket_poll_ = new uv_poll_t{};
    uv_poll_init_socket(session.uv_loop(), socket_poll_, socket);
    socket_poll_->data = this;
}

template<SocketEventType EventType>
SocketEventHandlerLibuv<EventType>::~SocketEventHandlerLibuv()
{
    stop();
    uv_close(
        reinterpret_cast<uv_handle_t*>(socket_poll_),
        [](uv_handle_t* handle) { delete reinterpret_cast<uv_poll_t*>(handle); });
}

template<SocketEventType EventType>
void SocketEventHandlerLibuv<EventType>::start()
{
    constexpr short UvType = (EventType == SocketEventType::Read) ? UV_READABLE : UV_WRITABLE;
    uv_poll_start(socket_poll_, UvType, on_event);
}

template<SocketEventType EventType>
void SocketEventHandlerLibuv<EventType>::stop()
{
    uv_poll_stop(socket_poll_);
}

// static
template<SocketEventType EventType>
std::unique_ptr<SocketEventHandler<EventType>> SocketEventHandler<EventType>::create_libuv_handler(
    tr_session& session,
    tr_socket_t socket,
    Callback callback)
{
    return std::make_unique<SocketEventHandlerLibuv<EventType>>(session, socket, std::move(callback));
}

// Explicit instantiation to ensure symbols are emitted in this TU.
template
std::unique_ptr<SocketReadEventHandler> SocketReadEventHandler::create_libuv_handler(
    tr_session& session,
    tr_socket_t socket,
    Callback callback);

// Explicit instantiation to ensure symbols are emitted in this TU.
template
std::unique_ptr<SocketWriteEventHandler> SocketWriteEventHandler::create_libuv_handler(
    tr_session& session,
    tr_socket_t socket,
    Callback callback);

} // namespace libtransmission
