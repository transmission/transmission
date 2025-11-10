// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/event.h>
#include <fmt/core.h>

#include "libtransmission/session.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"

// ---


tr_session::BoundSocketLibevent::BoundSocketLibevent(
    struct event_base* evbase,
    tr_address const& addr,
    tr_port port,
    IncomingCallback cb,
    void* cb_data)
    : cb_{ cb }
    , cb_data_{ cb_data }
    , socket_{ tr_netBindTCP(addr, port, false) }
    , ev_{ event_new(evbase, socket_, EV_READ | EV_PERSIST, &BoundSocketLibevent::onCanRead, this) }
{
    if (socket_ == TR_BAD_SOCKET)
    {
        return;
    }

    tr_logAddInfo(fmt::format(
        fmt::runtime(_("Listening to incoming peer connections on {hostport}")),
        fmt::arg("hostport", tr_socket_address::display_name(addr, port))));
    event_add(ev_.get(), nullptr);
}

tr_session::BoundSocketLibevent::~BoundSocketLibevent()
{
    ev_.reset();

    if (socket_ != TR_BAD_SOCKET)
    {
        tr_net_close_socket(socket_);
        socket_ = TR_BAD_SOCKET;
    }
}

void tr_session::BoundSocketLibevent::onCanRead(evutil_socket_t fd, short /*what*/, void* vself)
{
    auto* const self = static_cast<BoundSocketLibevent*>(vself);
    self->cb_(fd, self->cb_data_);
}
