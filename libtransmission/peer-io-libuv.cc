// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <uv.h>

#include <fmt/core.h>

#include "libtransmission/peer-io.h"
#include "libtransmission/peer-io-event-handler.h"
#include "libtransmission/log.h"

#define tr_logAddErrorIo(io, msg) tr_logAddError(msg, (io)->display_name())
#define tr_logAddTraceIo(io, msg) tr_logAddTrace(msg, (io)->display_name())

namespace libtransmission
{

// ---
// Libuv Event Handler Implementation
// ---

class LibuvEventHandler final : public PeerIoEventHandler
{
public:
    LibuvEventHandler(tr_peerIo* io, struct uv_loop_s* loop, int socket_fd)
        : io_{ io }
        , loop_{ loop }
    {
        // Initialize read poll
        read_poll_ = new uv_poll_t{};
        if (uv_poll_init_socket(loop_, read_poll_, socket_fd) < 0)
        {
            delete read_poll_;
            read_poll_ = nullptr;
            tr_logAddErrorIo(io_, "Failed to initialize read poll handle (libuv)");
        }
        else
        {
            read_poll_->data = this;
        }

        // Initialize write poll
        write_poll_ = new uv_poll_t{};
        if (uv_poll_init_socket(loop_, write_poll_, socket_fd) < 0)
        {
            delete write_poll_;
            write_poll_ = nullptr;
            tr_logAddErrorIo(io_, "Failed to initialize write poll handle (libuv)");
        }
        else
        {
            write_poll_->data = this;
        }
    }

    ~LibuvEventHandler() override
    {
        close();
    }

    void enable_read() override
    {
        if (!read_enabled_ && read_poll_ != nullptr)
        {
            tr_logAddTraceIo(io_, "enabling ready-to-read polling (libuv)");
            uv_poll_start(read_poll_, UV_READABLE, &on_read_event);
            read_enabled_ = true;
        }
    }

    void enable_write() override
    {
        if (!write_enabled_ && write_poll_ != nullptr)
        {
            tr_logAddTraceIo(io_, "enabling ready-to-write polling (libuv)");
            uv_poll_start(write_poll_, UV_WRITABLE, &on_write_event);
            write_enabled_ = true;
        }
    }

    void disable_read() override
    {
        if (read_enabled_ && read_poll_ != nullptr)
        {
            tr_logAddTraceIo(io_, "disabling ready-to-read polling (libuv)");
            uv_poll_stop(read_poll_);
            read_enabled_ = false;
        }
    }

    void disable_write() override
    {
        if (write_enabled_ && write_poll_ != nullptr)
        {
            tr_logAddTraceIo(io_, "disabling ready-to-write polling (libuv)");
            uv_poll_stop(write_poll_);
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

        if (read_poll_ != nullptr)
        {
            uv_close(
                reinterpret_cast<uv_handle_t*>(read_poll_),
                [](uv_handle_t* handle) { delete reinterpret_cast<uv_poll_t*>(handle); });
            read_poll_ = nullptr;
        }

        if (write_poll_ != nullptr)
        {
            uv_close(
                reinterpret_cast<uv_handle_t*>(write_poll_),
                [](uv_handle_t* handle) { delete reinterpret_cast<uv_poll_t*>(handle); });
            write_poll_ = nullptr;
        }
    }

private:
    static void on_read_event(uv_poll_t* handle, int status, int events)
    {
        auto* self = static_cast<LibuvEventHandler*>(handle->data);

        if (status < 0)
        {
            tr_logAddErrorIo(self->io_, fmt::format("Read poll error (libuv): {}", uv_strerror(status)));
            return;
        }

        if ((events & UV_READABLE) != 0)
        {
            self->io_->handle_read_ready();
        }
    }

    static void on_write_event(uv_poll_t* handle, int status, int events)
    {
        auto* self = static_cast<LibuvEventHandler*>(handle->data);

        if (status < 0)
        {
            tr_logAddErrorIo(self->io_, fmt::format("Write poll error (libuv): {}", uv_strerror(status)));
            return;
        }

        if ((events & UV_WRITABLE) != 0)
        {
            self->io_->handle_write_ready();
        }
    }

    tr_peerIo* io_;
    uv_loop_s* loop_;
    uv_poll_t* read_poll_ = nullptr;
    uv_poll_t* write_poll_ = nullptr;
    bool read_enabled_ = false;
    bool write_enabled_ = false;
};

// ---
// Factory functions
// ---

std::unique_ptr<PeerIoEventHandler> create_libuv_handler(tr_peerIo* io, struct uv_loop_s* loop, int socket_fd)
{
    return std::make_unique<LibuvEventHandler>(io, loop, socket_fd);
}

} // namespace libtransmission
