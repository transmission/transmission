// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/core.h>
#include <uv.h>

#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/session.h"
#include "libtransmission/socket-event-handler.h"

// ---

tr_session::BoundSocketLibuv::BoundSocketLibuv(
    tr_session& session,
    tr_address const& addr,
    tr_port port,
    IncomingCallback cb,
    void* cb_data)
    : BoundSocket(tr_netBindTCP(addr, port, false), cb, cb_data)
{
    if (socket_ == TR_BAD_SOCKET)
    {
        return;
    }

    tr_logAddInfo(fmt::format(
        fmt::runtime(_("Listening to incoming peer connections on {hostport}")),
        fmt::arg("hostport", tr_socket_address::display_name(addr, port))));

    event_handler_ = libtransmission::SocketReadEventHandler::create_libuv_handler(
          session,
          socket_,
          [this](tr_socket_t socket) { cb_(socket, cb_data_); });
    event_handler_->start();
}

tr_session::BoundSocketLibuv::~BoundSocketLibuv()
{
    if (event_handler_)
    {
        event_handler_->stop();
    }

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(socket_);
        socket_ = TR_BAD_SOCKET;
    }
}
