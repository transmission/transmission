// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>

#include <fmt/format.h>
#include <memory>
#include <optional>
#include <ranges>
#include <variant>
#include <string>
#include <string_view>
#include <utility>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <rtc/rtc.hpp>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-rtc.h"
#include "libtransmission/tr-assert.h"

#if defined(_WIN32) && defined(__clang_analyzer__)
// libdatachannel may throw; work around LLVM #44701 for Windows clang-tidy.
#define WORKAROUND_CLANG_TIDY_GH44701
#endif

namespace
{

class tr_peer_socket_rtc_impl final
    : public tr_peer_socket_rtc
    , public std::enable_shared_from_this<tr_peer_socket_rtc_impl>
{
public:
    tr_peer_socket_rtc_impl(
        tr_socket_address const& socket_address,
        std::shared_ptr<rtc::DataChannel> data_channel,
        Dispatcher dispatcher,
        TerminalCallback on_terminal)
        : tr_peer_socket_rtc{ socket_address }
        , dc_{ std::move(data_channel) }
        , dispatcher_{ std::move(dispatcher) }
        , on_terminal_{ std::move(on_terminal) }
    {
        TR_ASSERT(dc_ != nullptr);
    }

    tr_peer_socket_rtc_impl(tr_peer_socket_rtc_impl const&) = delete;
    tr_peer_socket_rtc_impl& operator=(tr_peer_socket_rtc_impl const&) = delete;
    tr_peer_socket_rtc_impl(tr_peer_socket_rtc_impl&&) = delete;
    tr_peer_socket_rtc_impl& operator=(tr_peer_socket_rtc_impl&&) = delete;

    ~tr_peer_socket_rtc_impl() override
    {
        if (dc_)
        {
            dc_->onAvailable(nullptr);
            dc_->onBufferedAmountLow(nullptr);
            dc_->onClosed(nullptr);
            dc_->onError(nullptr);
        }
    }

    void init()
    {
        std::weak_ptr<tr_peer_socket_rtc_impl> const weak_self = shared_from_this();

        dc_->setBufferedAmountLowThreshold(WriteBufferLowThreshold);

        // Leave messages queued in libdatachannel until Transmission requests a read.
        dc_->onAvailable(
            [weak_self]()
            {
                if (auto self = weak_self.lock())
                {
                    self->on_incoming_available();
                }
            });

        dc_->onBufferedAmountLow(
            [weak_self]()
            {
                if (auto self = weak_self.lock())
                {
                    self->on_buffered_amount_low();
                }
            });

        dc_->onClosed(
            [weak_self]()
            {
                if (auto self = weak_self.lock())
                {
                    self->on_channel_closed();
                }
            });

        dc_->onError(
            [weak_self](std::string const& error_msg)
            {
                if (auto self = weak_self.lock())
                {
                    self->on_channel_error(error_msg);
                }
            });
    }

    [[nodiscard]] std::shared_ptr<rtc::DataChannel> data_channel() const noexcept override
    {
        return dc_;
    }

    [[nodiscard]] uint64_t bytes_queued() const noexcept override
    {
        return bytes_queued_.load();
    }

    [[nodiscard]] uint64_t bytes_received() const noexcept override
    {
        return bytes_received_.load();
    }

    [[nodiscard]] bool is_closed() const noexcept override
    {
        return is_closed_.load();
    }

    [[nodiscard]] constexpr Type type() const noexcept override
    {
        return Type::RTC;
    }

    void set_read_enabled(bool const enabled) override
    {
        is_read_enabled_ = enabled;
        if (enabled)
        {
            if (pending_message_ || dc_->availableAmount() != 0U)
            {
                dispatch(
                    [weak_self = weak_from_this()]()
                    {
                        if (auto self = weak_self.lock(); self && self->is_read_enabled())
                        {
                            self->read_cb();
                        }
                    });
            }
        }
    }

    void set_write_enabled(bool const enabled) override
    {
        is_write_enabled_ = enabled;
    }

    [[nodiscard]] bool is_read_enabled() const override
    {
        return is_read_enabled_;
    }

    [[nodiscard]] bool is_write_enabled() const override
    {
        return is_write_enabled_;
    }

private:
    static auto constexpr WriteBufferLowThreshold = 2 * TrBlockSize;

    void dispatch(std::function<void()> fn) const
    {
        if (dispatcher_)
        {
            dispatcher_(std::move(fn));
        }
        else
        {
            fn();
        }
    }

    void on_incoming_available()
    {
        dispatch(
            [weak_self = weak_from_this()]()
            {
                if (auto self = weak_self.lock(); self && self->is_read_enabled() && !self->is_closed())
                {
                    self->read_cb();
                }
            });
    }

    void on_buffered_amount_low()
    {
        dispatch(
            [weak_self = weak_from_this()]()
            {
                if (auto self = weak_self.lock(); self && self->is_write_enabled())
                {
                    self->write_cb();
                }
            });
    }

    void on_channel_closed()
    {
        if (is_closed_.exchange(true))
        {
            return;
        }
        if (on_terminal_)
        {
            on_terminal_("data-channel-closed");
        }
        dispatch(
            [weak_self = weak_from_this()]()
            {
                if (auto self = weak_self.lock())
                {
                    auto error = tr_error{};
                    error.set_from_errno(ENOTCONN);
                    self->error_cb(error);
                }
            });
    }

    void on_channel_error(std::string const& message)
    {
        if (is_closed_.exchange(true))
        {
            return;
        }
        auto detail = message.substr(0, 160U);
        std::ranges::replace_if(detail, [](unsigned char c) { return c < 0x20U || c == 0x7FU; }, ' ');
        tr_logAddDebug(fmt::format("WebRTC data channel error endpoint={} detail={}", display_name(), detail), "WebRTC");
        if (on_terminal_)
        {
            on_terminal_("data-channel-error");
        }
        dispatch(
            [weak_self = weak_from_this()]()
            {
                if (auto self = weak_self.lock())
                {
                    auto error = tr_error{};
                    error.set(EIO, "WebRTC data channel error");
                    self->error_cb(error);
                }
            });
    }

    size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* error) override
    {
        // Reads and the partial-message state belong to Transmission's session thread.
        auto received = size_t{};
        while (received < n_bytes)
        {
            if (!pending_message_)
            {
                auto message = dc_->receive();
                if (!message)
                {
                    break;
                }
                pending_message_ = std::move(*message);
                pending_offset_ = 0;
            }

            auto const message_size = std::visit([](auto const& message) { return message.size(); }, *pending_message_);
            auto const count = std::min(n_bytes - received, message_size - pending_offset_);
            if (count != 0U)
            {
                std::visit([&](auto const& message) { buf.add(message.data() + pending_offset_, count); }, *pending_message_);
                pending_offset_ += count;
                received += count;
            }
            if (pending_offset_ == message_size)
            {
                pending_message_.reset();
                pending_offset_ = 0;
            }
        }
        bytes_received_ += received;

        // set_read_enabled() schedules a follow-up if bytes remain queued.
        if (received == 0U && is_closed_ && error != nullptr)
        {
            error->set_from_errno(ENOTCONN);
        }
        return received;
    }

    size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* error) override
    {
        n_bytes = std::min(n_bytes, std::size(buf));
        if (n_bytes == 0U)
        {
            return {};
        }

        if (!dc_ || !dc_->isOpen() || is_closed_)
        {
            on_channel_closed();
            if (error != nullptr)
            {
                error->set_from_errno(ENOTCONN);
            }
            return {};
        }

        // Apply backpressure before handing bytes to libdatachannel.
        if (dc_->bufferedAmount() > WriteBufferLowThreshold)
        {
            if (error != nullptr)
            {
                error->set_from_errno(EAGAIN);
            }
            return {};
        }

        if (auto const max_msg = dc_->maxMessageSize(); max_msg > 0)
        {
            n_bytes = std::min(n_bytes, max_msg);
        }

        auto const* data_ptr = reinterpret_cast<std::byte const*>(std::data(buf));
        if (data_ptr == nullptr)
        {
            return {};
        }

#ifndef WORKAROUND_CLANG_TIDY_GH44701
        try
#endif
        {
            // A false result means the connected channel buffered the message.
            if (!dc_->send(data_ptr, n_bytes) && !dc_->isOpen())
            {
                on_channel_closed();
                if (error != nullptr)
                {
                    error->set_from_errno(ENOTCONN);
                }
                return {};
            }
        }
#ifndef WORKAROUND_CLANG_TIDY_GH44701
        catch (std::exception const&)
        {
            on_channel_error("WebRTC data channel send failed");
            if (error != nullptr)
            {
                error->set(EIO, "WebRTC data channel send failed");
            }
            return {};
        }
#endif

        bytes_queued_ += n_bytes;
        buf.drain(n_bytes);
        return n_bytes;
    }

    std::shared_ptr<rtc::DataChannel> const dc_;
    Dispatcher const dispatcher_;
    TerminalCallback const on_terminal_;
    std::atomic<uint64_t> bytes_queued_{ 0 };
    std::atomic<uint64_t> bytes_received_{ 0 };

    std::optional<rtc::message_variant> pending_message_;
    size_t pending_offset_ = 0;

    std::atomic<bool> is_read_enabled_ = false;
    std::atomic<bool> is_write_enabled_ = false;
    std::atomic<bool> is_closed_ = false;
};

} // namespace

tr_peer_socket_rtc::tr_peer_socket_rtc(tr_socket_address const& socket_address)
    : tr_peer_socket{ socket_address }
{
}

std::shared_ptr<tr_peer_socket_rtc> tr_peer_socket_rtc::create(
    tr_socket_address const& socket_address,
    std::shared_ptr<rtc::DataChannel> data_channel,
    Dispatcher dispatcher,
    TerminalCallback on_terminal)
{
    auto sock = std::make_shared<tr_peer_socket_rtc_impl>(
        socket_address,
        std::move(data_channel),
        std::move(dispatcher),
        std::move(on_terminal));
    sock->init();
    return sock;
}
