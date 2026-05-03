// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/socket-event-handler.h"

namespace libtransmission
{

template<SocketEventType EventType>
class SocketEventHandlerLibuv final : public SocketEventHandler<EventType>
{
public:
    using Callback = typename SocketEventHandler<EventType>::Callback;

    SocketEventHandlerLibuv(struct uv_loop_s* loop, tr_socket_t socket, Callback callback);
    SocketEventHandlerLibuv(SocketEventHandlerLibuv&&) = delete;
    SocketEventHandlerLibuv(SocketEventHandlerLibuv const&) = delete;
    SocketEventHandlerLibuv operator=(SocketEventHandlerLibuv&&) = delete;
    SocketEventHandlerLibuv operator=(SocketEventHandlerLibuv const&) = delete;
    ~SocketEventHandlerLibuv() override;
    void start() override;
    void stop() override;

private:
    static void on_event(struct uv_poll_s* handle, int status, int events);

    tr_socket_t socket_ = TR_BAD_SOCKET;
    struct uv_poll_s* socket_poll_ = nullptr;
};

class SocketEventHandlerLibuvMaker final : public SocketEventHandlerMaker
{
public:
    explicit SocketEventHandlerLibuvMaker(struct uv_loop_s* loop) noexcept
        : uv_loop_{ loop }
    {
    }

    [[nodiscard]] std::unique_ptr<SocketReadEventHandler> create_read(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) override;
    [[nodiscard]] std::unique_ptr<SocketWriteEventHandler> create_write(
        tr_socket_t socket,
        std::function<void(tr_socket_t)> callback) override;

private:
    struct uv_loop_s* uv_loop_;
};

} // namespace libtransmission
