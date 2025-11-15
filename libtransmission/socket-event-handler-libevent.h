// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/util.h> // for evutil_socket_t

#include "libtransmission/socket-event-handler.h"

namespace libtransmission
{

template<SocketEventType EventType>
class SocketEventHandlerLibevent final : public SocketEventHandler<EventType>
{
public:
    using Callback = typename SocketEventHandler<EventType>::Callback;

    SocketEventHandlerLibevent(struct event_base* event_base, tr_socket_t socket, Callback callback);
    SocketEventHandlerLibevent(SocketEventHandlerLibevent&&) = delete;
    SocketEventHandlerLibevent(SocketEventHandlerLibevent const&) = delete;
    SocketEventHandlerLibevent operator=(SocketEventHandlerLibevent&&) = delete;
    SocketEventHandlerLibevent operator=(SocketEventHandlerLibevent const&) = delete;
    ~SocketEventHandlerLibevent() override;
    void start() override;
    void stop() override;

private:
    static void on_event(evutil_socket_t s, short type, void* vself);

    tr_socket_t socket_ = TR_BAD_SOCKET;
    struct event* socket_event_ = nullptr;
};

class SocketEventHandlerLibeventMaker final : public SocketEventHandlerMaker
{
public:
    explicit SocketEventHandlerLibeventMaker(struct event_base* event_base) noexcept
        : event_base_{ event_base }
    {
    }

    [[nodiscard]] std::unique_ptr<SocketReadEventHandler> create_read(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) override;
    [[nodiscard]] std::unique_ptr<SocketWriteEventHandler> create_write(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) override;

private:
    struct event_base* event_base_;
};

} // namespace libtransmission
