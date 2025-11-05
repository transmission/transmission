// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>
#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <memory>

// Forward declarations
class tr_peerIo;
struct event_base;
struct uv_loop_s;

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

    virtual ~PeerIoEventHandler() = default;

    // Enable/disable event monitoring
    virtual void enable_read() = 0;
    virtual void enable_write() = 0;
    virtual void disable_read() = 0;
    virtual void disable_write() = 0;

    // Check if events are enabled
    [[nodiscard]] virtual bool is_read_enabled() const noexcept = 0;
    [[nodiscard]] virtual bool is_write_enabled() const noexcept = 0;

    // Close and cleanup
    virtual void close() = 0;
};

// Factory functions for creating event handlers
std::unique_ptr<PeerIoEventHandler> create_libevent_handler(tr_peerIo* io, struct event_base* base, int socket_fd);

std::unique_ptr<PeerIoEventHandler> create_libuv_handler(tr_peerIo* io, struct uv_loop_s* loop, int socket_fd);

} // namespace libtransmission
