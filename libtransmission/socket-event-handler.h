// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <memory>

#include "libtransmission/net.h"

namespace libtransmission
{

/** @brief base class for socket event handlers, own and manages the event, but does not own the socket. */
class SocketEventHandler
{
public:
    using Callback = std::function<void(tr_socket_t)>;
    explicit SocketEventHandler(Callback callback)
        : callback_(std::move(callback))
    {
    }
    virtual ~SocketEventHandler() = default;

    virtual void start() = 0;
    virtual void stop() = 0;

    static std::unique_ptr<SocketEventHandler> create_libevent_handler(tr_session& session, tr_socket_t socket, Callback callback);
    static std::unique_ptr<SocketEventHandler> create_libuv_handler(tr_session& session, tr_socket_t socket, Callback callback);

protected:
    Callback callback_;
};

} // namespace libtransmission
