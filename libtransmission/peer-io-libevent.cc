// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/event.h>

#include <fmt/core.h>

#include "libtransmission/peer-io.h"
#include "libtransmission/peer-io-event-handler.h"
#include "libtransmission/log.h"
#include "libtransmission/utils-ev.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

namespace libtransmission
{

// ---
// Libevent Event Handler Implementation
// ---

class LibeventEventHandler final : public PeerIoEventHandler
{
public:
    LibeventEventHandler(tr_peerIo* io, struct event_base* base, int socket_fd)
        : io_{ io }
        , event_read_{ event_new(base, socket_fd, EV_READ, &on_read_event, this) }
        , event_write_{ event_new(base, socket_fd, EV_WRITE, &on_write_event, this) }
    {
    }

    ~LibeventEventHandler() override
    {
        close();
    }

    void enable_read() override
    {
        if (!read_enabled_)
        {
            tr_logAddTraceIo(io_, "enabling ready-to-read polling (libevent)");
            event_add(event_read_.get(), nullptr);
            read_enabled_ = true;
        }
    }

    void enable_write() override
    {
        if (!write_enabled_)
        {
            tr_logAddTraceIo(io_, "enabling ready-to-write polling (libevent)");
            event_add(event_write_.get(), nullptr);
            write_enabled_ = true;
        }
    }

    void disable_read() override
    {
        if (read_enabled_)
        {
            tr_logAddTraceIo(io_, "disabling ready-to-read polling (libevent)");
            event_del(event_read_.get());
            read_enabled_ = false;
        }
    }

    void disable_write() override
    {
        if (write_enabled_)
        {
            tr_logAddTraceIo(io_, "disabling ready-to-write polling (libevent)");
            event_del(event_write_.get());
            write_enabled_ = false;
        }
    }

    [[nodiscard]] bool is_read_enabled() const noexcept override
    {
        return read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const noexcept override
    {
        return write_enabled_;
    }

    void close() override
    {
        disable_read();
        disable_write();
        event_read_.reset();
        event_write_.reset();
    }

private:
    static void on_read_event([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vself)
    {
        auto* self = static_cast<LibeventEventHandler*>(vself);
        tr_logAddTraceIo(self->io_, "socket is ready to read (libevent)");
        self->io_->handle_read_ready();
    }

    static void on_write_event([[maybe_unused]] evutil_socket_t fd, short /*event*/, void* vself)
    {
        auto* self = static_cast<LibeventEventHandler*>(vself);
        tr_logAddTraceIo(self->io_, "socket is ready to write (libevent)");
        self->io_->handle_write_ready();
    }

    tr_peerIo* io_;
    evhelpers::event_unique_ptr event_read_;
    evhelpers::event_unique_ptr event_write_;
    bool read_enabled_ = false;
    bool write_enabled_ = false;
};

// ---
// Factory functions
// ---

std::unique_ptr<PeerIoEventHandler> create_libevent_handler(tr_peerIo* io, struct event_base* base, int socket_fd)
{
    return std::make_unique<LibeventEventHandler>(io, base, socket_fd);
}

} // namespace libtransmission
