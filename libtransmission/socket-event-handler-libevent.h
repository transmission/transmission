// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/util.h> // for evutil_socket_t

#include "libtransmission/socket-event-handler.h"

namespace libtransmission
{

class SocketEventHandlerLibevent : public SocketEventHandler
{
public:
    SocketEventHandlerLibevent(tr_session& session, tr_socket_t socket, Callback callback);
    ~SocketEventHandlerLibevent() override;
    void start() override;
    void stop() override;

private:
    static void on_readable(evutil_socket_t s, short type, void* vself);
    tr_socket_t socket_ = TR_BAD_SOCKET;
    struct event* socket_event_ = nullptr;
};

} // namespace libtransmission
