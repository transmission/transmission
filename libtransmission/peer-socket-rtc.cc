// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
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

#include <fmt/format.h>

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/peer-socket-rtc.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-buffer.h"

#define tr_logAddTraceSock(sock, msg) tr_logAddTrace(msg, (sock)->display_name())

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
        Dispatcher dispatcher)
        : tr_peer_socket_rtc{ socket_address }
        , dc_{ std::move(data_channel) }
        , dispatcher_{ std::move(dispatcher) }
    {
        TR_ASSERT(dc_ != nullptr);
        tr_logAddTraceSock(this, fmt::format("socket (WebRTC) initialized for channel label '{}'", dc_->label()));
    }

    ~tr_peer_socket_rtc_impl() override
    {
        if (dc_)
        {
            dc_->onMessage(nullptr, nullptr);
            dc_->onBufferedAmountLow(nullptr);
            dc_->onClosed(nullptr);
            dc_->onError(nullptr);
        }
    }

    void init()
    {
        std::weak_ptr<tr_peer_socket_rtc_impl> weak_self = shared_from_this();

        dc_->setBufferedAmountLowThreshold(2 * TrBlockSize);

        dc_->onMessage(
            [weak_self](rtc::binary data)
            {
                if (auto self = weak_self.lock())
                {
                    self->on_incoming_message(data.data(), data.size());
                }
            },
            [weak_self](std::string text)
            {
                if (auto self = weak_self.lock())
                {
                    self->on_incoming_message(text.data(), text.size());
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
            [weak_self](std::string error_msg)
            {
                if (auto self = weak_self.lock())
                {
                    self->on_channel_error(std::move(error_msg));
                }
            });
    }

    [[nodiscard]] std::shared_ptr<rtc::DataChannel> data_channel() const noexcept override
    {
        return dc_;
    }

    [[nodiscard]] constexpr Type type() const noexcept override
    {
        return Type::RTC;
    }

    void set_read_enabled(bool const enabled) override
    {
        if (is_read_enabled_ == enabled)
        {
            return;
        }

        is_read_enabled_ = enabled;
        if (enabled)
        {
            bool has_data = false;
            {
                std::unique_lock lock{ inbuf_mutex_ };
                has_data = inbuf_.size() > 0;
            }

            if (has_data)
            {
                dispatch(
                    [this]()
                    {
                        if (is_read_enabled())
                        {
                            read_cb();
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

    void on_incoming_message(void const* data, size_t size)
    {
        if (size == 0)
        {
            return;
        }

        {
            std::unique_lock lock{ inbuf_mutex_ };
            inbuf_.add(data, size);
        }

        dispatch(
            [this]()
            {
                if (is_read_enabled())
                {
                    read_cb();
                }
            });
    }

    void on_buffered_amount_low()
    {
        dispatch(
            [this]()
            {
                if (is_write_enabled())
                {
                    write_cb();
                }
            });
    }

    void on_channel_closed()
    {
        is_closed_ = true;
        dispatch(
            [this]()
            {
                auto error = tr_error{};
                error.set_from_errno(ENOTCONN);
                error_cb(error);
            });
    }

    void on_channel_error(std::string message)
    {
        is_closed_ = true;
        dispatch(
            [this, msg = std::move(message)]()
            {
                auto error = tr_error{};
                error.set(EIO, msg);
                error_cb(error);
            });
    }

    size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* error) override
    {
        std::unique_lock lock{ inbuf_mutex_ };
        n_bytes = std::min(n_bytes, inbuf_.size());
        if (n_bytes > 0U)
        {
            buf.add(inbuf_.data(), n_bytes);
            inbuf_.drain(n_bytes);
            return n_bytes;
        }

        if (is_closed_)
        {
            if (error != nullptr)
            {
                error->set_from_errno(ENOTCONN);
            }
        }

        return {};
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
            if (error != nullptr)
            {
                error->set_from_errno(ENOTCONN);
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

        if (!dc_->send(data_ptr, n_bytes))
        {
            if (error != nullptr)
            {
                error->set_from_errno(EAGAIN);
            }
            return {};
        }

        buf.drain(n_bytes);
        return n_bytes;
    }

    std::shared_ptr<rtc::DataChannel> const dc_;
    Dispatcher const dispatcher_;

    mutable std::mutex inbuf_mutex_;
    PeerBuffer inbuf_;

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
    Dispatcher dispatcher)
{
    auto sock = std::make_shared<tr_peer_socket_rtc_impl>(socket_address, std::move(data_channel), std::move(dispatcher));
    sock->init();
    return sock;
}
