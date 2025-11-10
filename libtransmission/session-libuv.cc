// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/core.h>
#include <uv.h>

#include "libtransmission/session.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"

// ---

tr_session::BoundSocketLibuv::BoundSocketLibuv(
    struct uv_loop_s* loop,
    tr_address const& addr,
    tr_port port,
    IncomingCallback cb,
    void* cb_data)
    : cb_{ cb }
    , cb_data_{ cb_data }
    , socket_{ tr_netBindTCP(addr, port, false) }
{
    if (socket_ == TR_BAD_SOCKET)
    {
        return;
    }

    tr_logAddInfo(fmt::format(
        fmt::runtime(_("Listening to incoming peer connections on {hostport}")),
        fmt::arg("hostport", tr_socket_address::display_name(addr, port))));

    // Allocate and initialize the poll handle
    poll_handle_ = new uv_poll_t{};
    uv_poll_init_socket(loop, poll_handle_, socket_);
    poll_handle_->data = this;

    // Start polling for readable events
    uv_poll_start(poll_handle_, UV_READABLE, &BoundSocketLibuv::onCanRead);
}

tr_session::BoundSocketLibuv::~BoundSocketLibuv()
{
    if (poll_handle_ != nullptr)
    {
        // Stop polling
        uv_poll_stop(poll_handle_);

        // Close the handle asynchronously
        uv_close(
            reinterpret_cast<uv_handle_t*>(poll_handle_),
            [](uv_handle_t* handle) { delete reinterpret_cast<uv_poll_t*>(handle); });

        poll_handle_ = nullptr;
    }

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(socket_);
        socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::BoundSocketLibuv::onCanRead(struct uv_poll_s* handle, int status, int events)
{
    if (status < 0)
    {
        // Error occurred
        tr_logAddError(fmt::format("Poll error on bound socket: {}", uv_strerror(status)));
        return;
    }

    if ((events & UV_READABLE) == 0)
    {
        return;
    }

    auto* const self = static_cast<BoundSocketLibuv*>(handle->data);
    self->cb_(self->socket_, self->cb_data_);
}
