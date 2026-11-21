// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <functional>
#include <cstdint>
#include <memory>
#include <string_view>

#include "libtransmission/peer-socket.h"

namespace rtc
{
class DataChannel;
}

class tr_peer_socket_rtc : public tr_peer_socket
{
public:
    using Dispatcher = std::function<void(std::function<void()>)>;
    using TerminalCallback = std::function<void(std::string_view)>;

    [[nodiscard]] static std::shared_ptr<tr_peer_socket_rtc> create(
        tr_socket_address const& socket_address,
        std::shared_ptr<rtc::DataChannel> data_channel,
        Dispatcher dispatcher = {},
        TerminalCallback on_terminal = {});

    [[nodiscard]] virtual std::shared_ptr<rtc::DataChannel> data_channel() const noexcept = 0;
    [[nodiscard]] virtual uint64_t bytes_queued() const noexcept = 0;
    [[nodiscard]] virtual uint64_t bytes_received() const noexcept = 0;

protected:
    explicit tr_peer_socket_rtc(tr_socket_address const& socket_address);
};
