// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Persistent WS/WSS tracker transport using libcurl (not WebRTC/ICE).
#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <atomic>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class tr_websocket_client final
{
public:
    struct Callbacks
    {
        std::function<void()> opened;
        std::function<void(std::string)> message;
        std::function<void()> writable;
        std::function<void(std::string)> closed;
    };

    tr_websocket_client(std::string url, std::optional<std::string> proxy_url, Callbacks callbacks);
    ~tr_websocket_client();

    tr_websocket_client(tr_websocket_client const&) = delete;
    tr_websocket_client& operator=(tr_websocket_client const&) = delete;
    tr_websocket_client(tr_websocket_client&&) = delete;
    tr_websocket_client& operator=(tr_websocket_client&&) = delete;

    [[nodiscard]] static bool supports_websockets();

    void open();
    bool send(std::string message);
    void close();

private:
    void run();

    std::string const url_;
    std::optional<std::string> const proxy_url_;
    Callbacks const callbacks_;
    std::atomic<bool> stopping_ = false;
    std::mutex mutex_;
    std::deque<std::string> pending_;
    size_t pending_bytes_ = 0;
    bool backpressured_ = false;
    std::thread worker_;
};
