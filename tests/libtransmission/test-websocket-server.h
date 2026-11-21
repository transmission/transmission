// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Minimal local RFC 6455 server used only by WebSocket tracker tests.
// Production WebSocket connections use libcurl. This helper does not depend on
// libdatachannel's optional WebSocket implementation.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "libtransmission/crypto-utils.h"

namespace test_ws
{
using binary = std::vector<std::byte>;
#ifdef _WIN32
using Socket = SOCKET;
constexpr auto InvalidSocket = INVALID_SOCKET;
constexpr auto ShutdownBoth = SD_BOTH;
inline void closeSocket(Socket socket)
{
    closesocket(socket);
}
#else
using Socket = int;
constexpr auto InvalidSocket = -1;
constexpr auto ShutdownBoth = SHUT_RDWR;
inline void closeSocket(Socket socket)
{
    ::close(socket);
}
#endif

inline bool readAll(Socket socket, void* destination, size_t count)
{
    auto* out = static_cast<char*>(destination);
    while (count != 0U)
    {
        auto const n = recv(socket, out, static_cast<int>(std::min(count, size_t{ 16384 })), 0);
        if (n <= 0)
        {
            return false;
        }
        out += n;
        count -= static_cast<size_t>(n);
    }
    return true;
}

inline bool writeAll(Socket socket, void const* source, size_t count)
{
    auto const* in = static_cast<char const*>(source);
    while (count != 0U)
    {
        auto const n = send(socket, in, static_cast<int>(std::min(count, size_t{ 16384 })), 0);
        if (n <= 0)
        {
            return false;
        }
        in += n;
        count -= static_cast<size_t>(n);
    }
    return true;
}

class WebSocket final : public std::enable_shared_from_this<WebSocket>
{
public:
    explicit WebSocket(Socket socket)
        : socket_{ socket }
    {
    }
    ~WebSocket()
    {
        close();
    }

    WebSocket(WebSocket const&) = delete;
    WebSocket& operator=(WebSocket const&) = delete;
    WebSocket(WebSocket&&) = delete;
    WebSocket& operator=(WebSocket&&) = delete;

    void onMessage(std::function<void(binary)> binary_cb, std::function<void(std::string)> text_cb)
    {
        auto const lock = std::lock_guard{ callbacks_mutex_ };
        binary_cb_ = std::move(binary_cb);
        text_cb_ = std::move(text_cb);
    }

    void onMessage(std::nullptr_t)
    {
        auto const lock = std::lock_guard{ callbacks_mutex_ };
        binary_cb_ = nullptr;
        text_cb_ = nullptr;
    }

    void start()
    {
        reader_ = std::thread{ [self = shared_from_this()]
                               {
                                   self->readLoop();
                               } };
    }

    bool send(std::string_view message)
    {
        return sendFrame(0x1U, message);
    }

    void close()
    {
        auto const socket = socket_.exchange(InvalidSocket);
        if (socket != InvalidSocket)
        {
            shutdown(socket, ShutdownBoth);
            closeSocket(socket);
        }
        if (reader_.joinable() && reader_.get_id() != std::this_thread::get_id())
        {
            reader_.join();
        }
    }

private:
    bool sendFrame(uint8_t opcode, std::string_view message)
    {
        auto const lock = std::lock_guard{ send_mutex_ };
        auto const socket = socket_.load();
        if (socket == InvalidSocket)
        {
            return false;
        }
        auto header = std::array<uint8_t, 10>{ static_cast<uint8_t>(0x80U | opcode) };
        size_t header_size = 2;
        if (message.size() < 126U)
        {
            header[1] = static_cast<uint8_t>(message.size());
        }
        else if (message.size() <= 0xFFFFU)
        {
            header[1] = 126;
            header[2] = static_cast<uint8_t>(message.size() >> 8U);
            header[3] = static_cast<uint8_t>(message.size());
            header_size = 4;
        }
        else
        {
            header[1] = 127;
            for (size_t i = 0; i < 8U; ++i)
            {
                header[2U + i] = static_cast<uint8_t>(message.size() >> (8U * (7U - i)));
            }
            header_size = 10;
        }
        return writeAll(socket, header.data(), header_size) && writeAll(socket, message.data(), message.size());
    }

    void readLoop()
    {
        auto assembled = std::string{};
        uint8_t fragmented_opcode = 0;
        while (socket_.load() != InvalidSocket)
        {
            auto header = std::array<uint8_t, 2>{};
            auto const socket = socket_.load();
            if (!readAll(socket, header.data(), header.size()))
            {
                break;
            }
            auto const opcode = static_cast<uint8_t>(header[0] & 0x0FU);
            auto const final = (header[0] & 0x80U) != 0U;
            if ((header[1] & 0x80U) == 0U) // Clients must mask their frames.
            {
                break;
            }
            uint64_t length = header[1] & 0x7FU;
            if (length == 126U)
            {
                auto extended = std::array<uint8_t, 2>{};
                if (!readAll(socket, extended.data(), extended.size()))
                {
                    break;
                }
                length = (static_cast<uint64_t>(extended[0]) << 8U) | extended[1];
            }
            else if (length == 127U)
            {
                auto extended = std::array<uint8_t, 8>{};
                if (!readAll(socket, extended.data(), extended.size()))
                {
                    break;
                }
                length = 0;
                for (auto byte : extended)
                {
                    length = (length << 8U) | byte;
                }
            }
            // Deliberately cap test messages to avoid unbounded allocation.
            if (length > uint64_t{ 1024U } * 1024U || assembled.size() + length > uint64_t{ 1024U } * 1024U)
            {
                break;
            }
            auto mask = std::array<uint8_t, 4>{};
            if (!readAll(socket, mask.data(), mask.size()))
            {
                break;
            }
            auto data = std::string(static_cast<size_t>(length), '\0');
            if (!readAll(socket, data.data(), data.size()))
            {
                break;
            }
            for (size_t i = 0; i < data.size(); ++i)
            {
                data[i] = static_cast<char>(static_cast<uint8_t>(data[i]) ^ mask[i % mask.size()]);
            }
            if (opcode == 0x8U)
            {
                break;
            }
            if (opcode == 0x9U)
            {
                if (!sendFrame(0xAU, data))
                {
                    break;
                }
                continue;
            }
            if (opcode == 0xAU)
            {
                continue;
            }
            if (opcode != 0U && opcode != 1U && opcode != 2U)
            {
                break;
            }
            if (opcode != 0U)
            {
                if (fragmented_opcode != 0U)
                {
                    break;
                }
                fragmented_opcode = opcode;
            }
            else if (fragmented_opcode == 0U)
            {
                break;
            }
            assembled += data;
            if (final)
            {
                auto const lock = std::lock_guard{ callbacks_mutex_ };
                if (fragmented_opcode == 1U && text_cb_)
                {
                    text_cb_(std::move(assembled));
                }
                else if (fragmented_opcode == 2U && binary_cb_)
                {
                    auto bytes = binary{};
                    bytes.reserve(assembled.size());
                    for (unsigned char ch : assembled)
                    {
                        bytes.push_back(static_cast<std::byte>(ch));
                    }
                    binary_cb_(std::move(bytes));
                }
                assembled.clear();
                fragmented_opcode = 0;
            }
        }
        auto const socket = socket_.exchange(InvalidSocket);
        if (socket != InvalidSocket)
        {
            shutdown(socket, ShutdownBoth);
            closeSocket(socket);
        }
    }

    std::atomic<Socket> socket_;
    std::thread reader_;
    std::mutex send_mutex_;
    std::mutex callbacks_mutex_;
    std::function<void(binary)> binary_cb_;
    std::function<void(std::string)> text_cb_;
};

class WebSocketServer final
{
public:
    struct Configuration
    {
        uint16_t port = 0;
        std::string bindAddress = "127.0.0.1";
    };

    explicit WebSocketServer(Configuration const& config)
    {
#ifdef _WIN32
        auto wsa = WSADATA{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            error_ = "WSAStartup failed";
            return;
        }
        wsa_started_ = true;
#endif
        if (config.bindAddress != "127.0.0.1")
        {
            error_ = "Test WebSocket server must bind to loopback";
            return;
        }
        auto const socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket == InvalidSocket)
        {
            error_ = "Unable to create local WebSocket listener";
            return;
        }
        listener_.store(socket);
        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(config.port);
        if (bind(listener_.load(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
            listen(listener_.load(), SOMAXCONN) != 0)
        {
            closeSocket(listener_.load());
            listener_.store(InvalidSocket);
            error_ = "Unable to start local WebSocket listener";
            return;
        }
#ifdef _WIN32
        auto len = int{ sizeof(addr) };
#else
        auto len = socklen_t{ sizeof(addr) };
#endif
        if (getsockname(listener_.load(), reinterpret_cast<sockaddr*>(&addr), &len) != 0)
        {
            closeSocket(listener_.load());
            listener_.store(InvalidSocket);
            error_ = "Unable to determine test WebSocket port";
            return;
        }
        port_ = ntohs(addr.sin_port);
        accept_thread_ = std::thread{ [this]
                                      {
                                          acceptLoop();
                                      } };
    }

    ~WebSocketServer()
    {
        stop();
    }
    WebSocketServer(WebSocketServer const&) = delete;
    WebSocketServer& operator=(WebSocketServer const&) = delete;
    WebSocketServer(WebSocketServer&&) = delete;
    WebSocketServer& operator=(WebSocketServer&&) = delete;

    [[nodiscard]] bool ready() const noexcept
    {
        return error_.empty();
    }

    [[nodiscard]] std::string const& error() const noexcept
    {
        return error_;
    }

    [[nodiscard]] uint16_t port() const noexcept
    {
        return port_;
    }

    void onClient(std::function<void(std::shared_ptr<WebSocket>)> fn)
    {
        auto const lock = std::lock_guard{ mutex_ };
        on_client_ = std::move(fn);
    }

    void stop()
    {
        auto const socket = listener_.exchange(InvalidSocket);
        if (socket != InvalidSocket)
        {
            // A socket close from another thread does not always wake accept().
            auto const wake = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (wake != InvalidSocket)
            {
                auto addr = sockaddr_in{};
                addr.sin_family = AF_INET;
                addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                addr.sin_port = htons(port_);
                if (connect(wake, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
                {
                    shutdown(socket, ShutdownBoth);
                }
                closeSocket(wake);
            }
            else
            {
                shutdown(socket, ShutdownBoth);
            }
            closeSocket(socket);
        }
        if (accept_thread_.joinable())
        {
            accept_thread_.join();
        }
        auto clients = std::vector<std::shared_ptr<WebSocket>>{};
        {
            auto const lock = std::lock_guard{ mutex_ };
            on_client_ = nullptr;
            clients.swap(clients_);
        }
        for (auto const& client : clients)
        {
            client->close();
        }
#ifdef _WIN32
        if (!stopped_.exchange(true) && wsa_started_)
        {
            WSACleanup();
        }
#endif
    }

private:
    static bool handshake(Socket socket)
    {
        auto request = std::string{};
        while (request.size() < 8192U && request.find("\r\n\r\n") == std::string::npos)
        {
            char ch = 0;
            if (!readAll(socket, &ch, 1U))
            {
                return false;
            }
            request += ch;
        }
        if (request.find("\r\n\r\n") == std::string::npos)
        {
            return false;
        }
        auto lower = request;
        std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        auto const header = std::string{ "sec-websocket-key:" };
        auto pos = lower.find(header);
        if (pos == std::string::npos)
        {
            return false;
        }
        pos += header.size();
        while (pos < request.size() && (request[pos] == ' ' || request[pos] == '\t'))
        {
            ++pos;
        }
        auto const end = request.find("\r\n", pos);
        if (end == std::string::npos)
        {
            return false;
        }
        auto const key = request.substr(pos, end - pos);
        auto const hash = tr_sha1::digest(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        auto const accept = tr_base64_encode({ reinterpret_cast<char const*>(hash.data()), hash.size() });
        auto const response =
            std::string{
                "HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: "
            } +
            accept + "\r\n\r\n";
        return writeAll(socket, response.data(), response.size());
    }

    void acceptLoop()
    {
        while (listener_.load() != InvalidSocket)
        {
            auto addr = sockaddr_in{};
#ifdef _WIN32
            auto len = int{ sizeof(addr) };
#else
            auto len = socklen_t{ sizeof(addr) };
#endif
            auto const socket = accept(listener_.load(), reinterpret_cast<sockaddr*>(&addr), &len);
            if (socket == InvalidSocket)
            {
                break;
            }
            if (listener_.load() == InvalidSocket)
            {
                closeSocket(socket);
                break;
            }
            if (!handshake(socket))
            {
                closeSocket(socket);
                continue;
            }
            auto client = std::make_shared<WebSocket>(socket);
            auto fn = std::function<void(std::shared_ptr<WebSocket>)>{};
            {
                auto const lock = std::lock_guard{ mutex_ };
                clients_.push_back(client);
                fn = on_client_;
            }
            if (fn)
            {
                fn(client);
            }
            client->start();
        }
    }

    std::atomic<Socket> listener_{ InvalidSocket };
    uint16_t port_ = 0;
    std::thread accept_thread_;
    std::mutex mutex_;
    std::function<void(std::shared_ptr<WebSocket>)> on_client_;
    std::vector<std::shared_ptr<WebSocket>> clients_;
    std::string error_;
#ifdef _WIN32
    bool wsa_started_ = false;
    std::atomic<bool> stopped_ = false;
#endif
};
} // namespace test_ws
