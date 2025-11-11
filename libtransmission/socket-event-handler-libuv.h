// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/socket-event-handler.h"

namespace libtransmission
{

template<SocketEventType EventType>
class SocketEventHandlerLibuv : public SocketEventHandler<EventType>
{
public:
    using Callback = typename SocketEventHandler<EventType>::Callback;

    SocketEventHandlerLibuv(tr_session& session, tr_socket_t socket, Callback callback);
    ~SocketEventHandlerLibuv() override;
    void start() override;
    void stop() override;

private:
    static void on_event(struct uv_poll_s* handle, int status, int events);

    tr_socket_t socket_ = TR_BAD_SOCKET;
    struct uv_poll_s* socket_poll_ = nullptr;
};

} // namespace libtransmission
