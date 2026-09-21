// This file was part of the Transmission project.
// See LICENSE for licensing information.
//
// Minimal WebSocket client implementation for tracker communication.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tr-macros.h"

namespace libtransmission
{

class WebSocketClient;

using WebSocketConnectCallback = std::function<void(WebSocketClient&)>;
using WebSocketMessageCallback = std::function<void(WebSocketClient&, std::string_view)>;
using WebSocketCloseCallback = std::function<void(WebSocketClient&, int close_code, std::string_view reason)>;

/**
 * A minimal WebSocket client for connecting to BitTorrent tracker servers.
 * Supports ws:// and wss:// (TLS) connections.
 *
 * The BitTorrent tracker protocol over WebSocket uses:
 * - Binary frames for announce/scrape requests
 * - Binary frames for responses
 * - Text frames for error messages
 */
class WebSocketClient
{
public:
    WebSocketClient(std::string const& url,
                    WebSocketConnectCallback&& on_connect,
                    WebSocketMessageCallback&& on_message,
                    WebSocketCloseCallback&& on_close);
    ~WebSocketClient();

    WebSocketClient(WebSocketClient const&) = delete;
    WebSocketClient& operator=(WebSocketClient const&) = delete;

    WebSocketClient(WebSocketClient&&) noexcept;
    WebSocketClient& operator=(WebSocketClient&&) noexcept;

    /**
     * Start the WebSocket connection.
     * The connection is asynchronous; callbacks will be invoked on the event loop thread.
     */
    void connect();

    /**
     * Send a binary message (tracker request).
     */
    void sendBinary(std::string_view data);

    /**
     * Send a text message.
     */
    void sendText(std::string_view data);

    /**
     * Close the WebSocket connection gracefully.
     */
    void close(int code = 1000, std::string_view reason = "");

    /**
     * Check if the connection is open.
     */
    bool isOpen() const;

    /**
     * Check if the connection is closed.
     */
    bool isClosed() const;

    /**
     * Get the remote address.
     */
    std::string const& remoteAddress() const;

    /**
     * Get the local address.
     */
    std::string const& localAddress() const;

    /**
     * Get the URL this client is connected to.
     */
    std::string const& url() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace libtransmission
