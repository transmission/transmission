// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>
#include <uv.h>

#include "libtransmission/session.h"
#include "libtransmission/socket-event-handler-libuv.h"

namespace libtransmission {

// static
void SocketEventHandlerLibuv::on_readable(struct uv_poll_s* handle, int status, int events)
{
    if (status < 0)
    {
        tr_logAddError(fmt::format("UDP poll error (libuv): {}", uv_strerror(status)));
        return;
    }

    if ((events & UV_READABLE) == 0)
    {
        return;
    }

    auto* self = static_cast<SocketEventHandlerLibuv*>(handle->data);
    self->callback_(self->socket_);
}

SocketEventHandlerLibuv::SocketEventHandlerLibuv(tr_session& session, tr_socket_t socket, Callback callback)
    : SocketEventHandler(std::move(callback)), socket_{ socket }
{
    socket_poll_ = new uv_poll_t{};
    uv_poll_init_socket(session.uv_loop(), socket_poll_, socket);
    socket_poll_->data = this;
}

SocketEventHandlerLibuv::~SocketEventHandlerLibuv()
{
    stop();
    uv_close(
        reinterpret_cast<uv_handle_t*>(socket_poll_),
        [](uv_handle_t* handle) { delete reinterpret_cast<uv_poll_t*>(handle); });
}

void SocketEventHandlerLibuv::start()
{
    uv_poll_start(socket_poll_, UV_READABLE, on_readable);
}

void SocketEventHandlerLibuv::stop()
{
    uv_poll_stop(socket_poll_);
}

// static
std::unique_ptr<SocketEventHandler> SocketEventHandler::create_libuv_handler(tr_session& session, tr_socket_t socket, Callback callback)
{
    return std::make_unique<SocketEventHandlerLibuv>(session, socket, std::move(callback));
}

} // namespace libtransmission
