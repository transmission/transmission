// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "libtransmission/net.h"
#include "libtransmission/utils-ev.h"

struct event_base;
struct tr_session;

namespace socks5
{

// SOCKS5 protocol constants (RFC 1928)
inline constexpr uint8_t Version = 0x05;
inline constexpr uint8_t AuthVersion = 0x01; // RFC 1929
inline constexpr uint8_t CmdConnect = 0x01;
inline constexpr uint8_t Reserved = 0x00;

enum class AuthMethod : uint8_t
{
    NoAuth = 0x00,
    UsernamePass = 0x02,
    NoAcceptable = 0xFF,
};

enum class AddressType : uint8_t
{
    IPv4 = 0x01,
    DomainName = 0x03,
    IPv6 = 0x04,
};

enum class ReplyCode : uint8_t
{
    Succeeded = 0x00,
    GeneralFailure = 0x01,
    ConnectionNotAllowed = 0x02,
    NetworkUnreachable = 0x03,
    HostUnreachable = 0x04,
    ConnectionRefused = 0x05,
    TTLExpired = 0x06,
    CommandNotSupported = 0x07,
    AddressTypeNotSupported = 0x08,
};

// Timeout for the entire SOCKS5 handshake (connect + negotiate)
inline constexpr int HandshakeTimeoutSecs = 30;

[[nodiscard]] char const* reply_code_str(ReplyCode code);

struct Result
{
    bool success = false;
    std::string error_message;
};

using DoneCallback = std::function<void(Result const&)>;

} // namespace socks5

struct tr_socks5_config
{
    uint16_t proxy_port = 1080;
    bool auth_enabled = false;
    std::string username;
    std::string password;
    tr_socket_address target;
};

// Manages a non-blocking SOCKS5 handshake on a connected socket.
// After construction, call start() to begin the handshake.
// The socket must already be in the process of connecting to the SOCKS5 proxy
// (i.e. connect() returned EINPROGRESS).
// On completion (success or failure), the done callback is invoked.
class tr_socks5_handshake
{
public:
    tr_socks5_handshake(event_base* evbase, tr_socket_t socket, tr_socks5_config config, socks5::DoneCallback on_done);

    ~tr_socks5_handshake();

    tr_socks5_handshake(tr_socks5_handshake const&) = delete;
    tr_socks5_handshake& operator=(tr_socks5_handshake const&) = delete;

    void start();

private:
    enum class State : uint8_t
    {
        WaitingForConnect,
        SendPending, // draining send_buf_ via non-blocking send()
        ReadMethodSelection,
        ReadAuthResponse,
        ReadConnectResponseHeader,
        ReadConnectResponseAddress,
        Done,
        Error,
    };

    void on_event(short what);
    void send_greeting();
    void read_method_selection();
    void send_auth();
    void read_auth_response();
    void send_connect_request();
    void read_connect_response_header();
    void read_connect_response_address();
    void finish(socks5::Result result);

    // Queue data into send_buf_ and register for EV_WRITE.
    // The next state is set to read_state so that after the send
    // completes, the state machine transitions to reading.
    void queue_send(uint8_t const* data, size_t len, State next_read_state);
    // Drain send_buf_ via send(). Called on EV_WRITE events.
    void flush_send_buf();

    void register_for_read();

    static void event_cb(evutil_socket_t fd, short what, void* ctx);

    event_base* evbase_;
    tr_socket_t socket_;
    tr_socks5_config config_;
    socks5::DoneCallback on_done_;
    State state_ = State::WaitingForConnect;
    tr::evhelpers::event_unique_ptr event_;

    // Buffer for outgoing data (handles partial sends)
    std::vector<uint8_t> send_buf_;
    size_t send_offset_ = 0;
    State after_send_state_ = State::Done; // state to enter after send completes

    // Buffer for partial reads
    std::array<uint8_t, 512> buf_{};
    size_t buf_len_ = 0;
    size_t buf_needed_ = 0;

    // For the connect response, we need to know the address type
    // to determine how many bytes to read
    socks5::AddressType response_addr_type_ = socks5::AddressType::IPv4;
};
