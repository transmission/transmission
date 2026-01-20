// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <cstddef> // size_t
#include <functional>
#include <string>
#include <utility> // for std::make_pair()

#include "libtransmission/block-info.h"
#include "libtransmission/net.h"
#include "libtransmission/tr-buffer.h"
#include "libtransmission/tr-macros.h"

struct UTPSocket;
struct tr_session;

class tr_peer_socket
{
public:
    // The buffer size for incoming & outgoing peer messages.
    // Starts off with enough capacity to read a single BT Piece message,
    // but has a 5x GrowthFactor so that it can quickly to high volume.
    using PeerBuffer = libtransmission::StackBuffer<tr_block_info::BlockSize + 16U, std::byte, std::ratio<5, 1>>;

    using InBuf = libtransmission::BufferWriter<std::byte>;
    using OutBuf = libtransmission::BufferReader<std::byte>;

    using RWCb = std::function<void()>;
    using ErrorCb = std::function<void(tr_error const&)>;

    tr_peer_socket(tr_peer_socket&& s) noexcept = delete;
    tr_peer_socket(tr_peer_socket const&) = delete;
    tr_peer_socket& operator=(tr_peer_socket&& s) noexcept = delete;
    tr_peer_socket& operator=(tr_peer_socket const&) = delete;
    virtual ~tr_peer_socket();

    [[nodiscard]] size_t try_read(InBuf& buf, size_t max, tr_error* error);
    [[nodiscard]] size_t try_write(OutBuf& buf, size_t max, tr_error* error);

    virtual void set_read_enabled(bool enabled) = 0;
    virtual void set_write_enabled(bool enabled) = 0;
    [[nodiscard]] virtual bool is_read_enabled() const = 0;
    [[nodiscard]] virtual bool is_write_enabled() const = 0;

    void set_read_cb(RWCb cb)
    {
        read_cb_ = std::move(cb);
    }
    void set_write_cb(RWCb cb)
    {
        write_cb_ = std::move(cb);
    }
    void set_error_cb(ErrorCb cb)
    {
        error_cb_ = std::move(cb);
    }

    [[nodiscard]] constexpr auto const& socket_address() const noexcept
    {
        return socket_address_;
    }

    [[nodiscard]] constexpr auto const& address() const noexcept
    {
        return socket_address().address();
    }

    [[nodiscard]] constexpr auto const& port() const noexcept
    {
        return socket_address().port();
    }

    [[nodiscard]] std::string display_name() const
    {
        return socket_address().display_name();
    }

    [[nodiscard]] TR_CONSTEXPR20 auto is_utp() const noexcept
    {
        return type() == Type::UTP;
    }

    [[nodiscard]] TR_CONSTEXPR20 auto is_tcp() const noexcept
    {
        return type() == Type::TCP;
    }

    [[nodiscard]] static bool limit_reached(tr_session const* session) noexcept;

protected:
    enum class Type : uint8_t
    {
        UTP,
        TCP
    };

    explicit tr_peer_socket(tr_socket_address const& socket_address);

    [[nodiscard]] TR_CONSTEXPR20 virtual Type type() const noexcept = 0;

    [[nodiscard]] virtual size_t try_read_impl(InBuf& buf, size_t n_bytes, tr_error* error) = 0;
    [[nodiscard]] virtual size_t try_write_impl(OutBuf& buf, size_t n_bytes, tr_error* error) = 0;

    void read_cb() const
    {
        if (read_cb_)
        {
            read_cb_();
        }
    }
    void write_cb() const
    {
        if (write_cb_)
        {
            write_cb_();
        }
    }
    void error_cb(tr_error const& error) const
    {
        if (error_cb_)
        {
            error_cb_(error);
        }
    }

    tr_socket_address socket_address_;

    RWCb read_cb_;
    RWCb write_cb_;
    ErrorCb error_cb_;

private:
    static inline std::atomic<size_t> n_open_sockets = {};
};
