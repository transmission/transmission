// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include "libtransmission/net.h" // tr_socket_t
#include "libtransmission/socket-event-handler.h"

// Forward declarations
class tr_peerIo;

namespace libtransmission
{

// Abstract interface for peer I/O event handling
// Allows tr_peerIo to work with different event loop backends (libevent, libuv)
class PeerIoEventHandler
{
public:
    enum class EventType : std::uint8_t
    {
        READ,
        WRITE
    };

    PeerIoEventHandler(tr_peerIo* io, tr_socket_t socket);
    ~PeerIoEventHandler();

    // Enable/disable event monitoring
    void enable_read();
    void enable_write();
    void disable_read();
    void disable_write();

    // Check if events are enabled
    [[nodiscard]] bool is_read_enabled() const;
    [[nodiscard]] bool is_write_enabled() const;

    // Close and cleanup
    void close();

private:
    tr_peerIo* io_;
    std::unique_ptr<SocketReadEventHandler> read_event_handler_;
    std::unique_ptr<SocketWriteEventHandler> write_event_handler_;
    bool read_enabled_ = false;
    bool write_enabled_ = false;
};

} // namespace libtransmission
