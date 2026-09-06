// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

#include <event2/dns.h>
#include <event2/util.h>

#include "libtransmission/net.h"
#include "libtransmission/utils-ev.h"

struct event_base;

// Manages a SOCKS5 UDP ASSOCIATE relay (RFC 1928) for UDP tracker traffic.
// The TCP control connection is kept open for the lifetime of the association;
// outbound datagrams are wrapped with the SOCKS5 UDP header, and relay replies
// are unwrapped before being passed to the existing UDP announcer path.
class tr_socks5_udp
{
public:
    enum class State : uint8_t
    {
        Idle,
        Resolving,
        Connecting,
        SentGreeting,
        SentUdpAssociate,
        Ready,
        Error,
    };

    using ReadyCallback = std::function<void()>;
    using IncomingCallback = std::function<
        void(uint8_t const* payload, size_t payload_len, sockaddr const* from, socklen_t fromlen)>;

    tr_socks5_udp(struct event_base* base, std::string_view proxy_host, uint16_t proxy_port, ReadyCallback on_ready);
    ~tr_socks5_udp();

    tr_socks5_udp(tr_socks5_udp const&) = delete;
    tr_socks5_udp& operator=(tr_socks5_udp const&) = delete;
    tr_socks5_udp(tr_socks5_udp&&) = delete;
    tr_socks5_udp& operator=(tr_socks5_udp&&) = delete;

    void set_incoming_callback(IncomingCallback cb)
    {
        on_incoming_ = std::move(cb);
    }

    [[nodiscard]] std::optional<tr_socket_address> relay_address() const noexcept;

    [[nodiscard]] State state() const noexcept
    {
        return state_;
    }

    [[nodiscard]] bool is_ready() const noexcept
    {
        return state_ == State::Ready;
    }

    // `dest` is the final destination (tracker), not the relay.
    void sendto(void const* buf, size_t buflen, sockaddr const* dest, socklen_t destlen);

    [[nodiscard]] tr_socket_t relay_socket() const noexcept
    {
        return udp_socket_;
    }

    // Returns the inner payload start, or nullptr if malformed.
    // Domain-name ATYP packets cannot be represented as sockaddr and are rejected.
    [[nodiscard]] static uint8_t const* unwrap_udp_packet(
        uint8_t const* data,
        size_t datalen,
        size_t& payload_len,
        sockaddr_storage& from,
        socklen_t& fromlen);

private:
    void start_connect();
    void do_connect(sockaddr const* sa, socklen_t salen);
    void on_tcp_writable();
    void on_tcp_readable();
    void send_greeting();
    void send_udp_associate();
    void handle_greeting_response();
    void handle_udp_associate_response();
    void set_error(std::string_view msg);
    void setup_udp_read_event();
    void queue_tcp_write(uint8_t const* data, size_t len);
    void flush_pending_write();

    static void on_tcp_event(evutil_socket_t fd, short what, void* arg);
    static void on_udp_readable(evutil_socket_t fd, short what, void* arg);
    static void on_dns_result(int result, evutil_addrinfo* res, void* arg);

    struct event_base* base_;
    std::string proxy_host_;
    uint16_t proxy_port_;
    ReadyCallback on_ready_;
    IncomingCallback on_incoming_;

    State state_ = State::Idle;
    tr_socket_t tcp_socket_ = TR_BAD_SOCKET;
    tr_socket_t udp_socket_ = TR_BAD_SOCKET;
    tr::evhelpers::event_unique_ptr tcp_event_;
    tr::evhelpers::event_unique_ptr udp_event_;

    std::optional<tr_socket_address> relay_addr_;

    std::optional<tr_address> proxy_addr_;
    int proxy_family_ = AF_UNSPEC;

    evdns_base* dns_base_ = nullptr;
    evdns_getaddrinfo_request* dns_req_ = nullptr;

    std::vector<uint8_t> tcp_write_buf_;
    size_t tcp_write_sent_ = 0;
    State pending_state_ = State::Idle;

    static constexpr size_t kTcpBufSize = 512;
    uint8_t tcp_buf_[kTcpBufSize] = {};
    size_t tcp_buf_used_ = 0;
};
