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

enum class SocketEventType : uint8_t
{
    Read,
    Write
};

/** @brief base class for socket event handlers, own and manages the event, but does not own the socket. */
template<SocketEventType EventType>
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

protected:
    Callback callback_;
};

using SocketReadEventHandler = SocketEventHandler<SocketEventType::Read>;
using SocketWriteEventHandler = SocketEventHandler<SocketEventType::Write>;

class SocketEventHandlerMaker
{
public:
    virtual ~SocketEventHandlerMaker() = default;

    [[nodiscard]] virtual std::unique_ptr<SocketReadEventHandler> create_read(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) = 0;
    [[nodiscard]] virtual std::unique_ptr<SocketWriteEventHandler> create_write(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) = 0;
};

} // namespace libtransmission
