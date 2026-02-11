// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include <event2/event.h>
#include <event2/util.h>

#include <fmt/format.h>

#include "libtransmission/socks5.h"
#include "libtransmission/log.h"
#include "libtransmission/net.h"
#include "libtransmission/utils-ev.h"

using namespace std::literals;

namespace
{
auto constexpr Timeout = timeval{ socks5::HandshakeTimeoutSecs, 0 };
} // namespace

namespace socks5
{

char const* reply_code_str(ReplyCode code)
{
    switch (code)
    {
    case ReplyCode::Succeeded:
        return "succeeded";
    case ReplyCode::GeneralFailure:
        return "general SOCKS server failure";
    case ReplyCode::ConnectionNotAllowed:
        return "connection not allowed by ruleset";
    case ReplyCode::NetworkUnreachable:
        return "network unreachable";
    case ReplyCode::HostUnreachable:
        return "host unreachable";
    case ReplyCode::ConnectionRefused:
        return "connection refused";
    case ReplyCode::TTLExpired:
        return "TTL expired";
    case ReplyCode::CommandNotSupported:
        return "command not supported";
    case ReplyCode::AddressTypeNotSupported:
        return "address type not supported";
    default:
        return "unknown SOCKS5 error";
    }
}

} // namespace socks5

// ---

tr_socks5_handshake::tr_socks5_handshake(
    event_base* evbase,
    tr_socket_t socket,
    tr_socks5_config config,
    socks5::DoneCallback on_done)
    : evbase_{ evbase }
    , socket_{ socket }
    , config_{ std::move(config) }
    , on_done_{ std::move(on_done) }
{
}

tr_socks5_handshake::~tr_socks5_handshake() = default;

void tr_socks5_handshake::event_cb(evutil_socket_t /*fd*/, short what, void* ctx)
{
    static_cast<tr_socks5_handshake*>(ctx)->on_event(what);
}

void tr_socks5_handshake::start()
{
    // Wait for the TCP connection to the proxy to complete
    state_ = State::WaitingForConnect;
    event_.reset(tr::evhelpers::event_new_pri2(evbase_, socket_, EV_WRITE, event_cb, this));
    event_add(event_.get(), &Timeout);
}

void tr_socks5_handshake::on_event(short what)
{
    if ((what & EV_TIMEOUT) != 0)
    {
        finish({ false, "SOCKS5 handshake timed out" });
        return;
    }

    switch (state_)
    {
    case State::WaitingForConnect:
        {
            // Check if the connection to the proxy succeeded
            int err = 0;
            socklen_t len = sizeof(err);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0 || err != 0)
            {
                finish({ false, fmt::format("failed to connect to SOCKS5 proxy: {}", tr_net_strerror(err)) });
                return;
            }
            send_greeting();
        }
        break;

    case State::SendPending:
        flush_send_buf();
        break;

    case State::ReadMethodSelection:
        read_method_selection();
        break;

    case State::ReadAuthResponse:
        read_auth_response();
        break;

    case State::ReadConnectResponseHeader:
        read_connect_response_header();
        break;

    case State::ReadConnectResponseAddress:
        read_connect_response_address();
        break;

    default:
        finish({ false, "SOCKS5 handshake in unexpected state" });
        break;
    }
}

// --- Send helpers ---

void tr_socks5_handshake::queue_send(uint8_t const* data, size_t len, State next_read_state)
{
    send_buf_.assign(data, data + len);
    send_offset_ = 0;
    after_send_state_ = next_read_state;
    state_ = State::SendPending;
    flush_send_buf();
}

void tr_socks5_handshake::flush_send_buf()
{
    auto const remaining = send_buf_.size() - send_offset_;
    auto const n = send(socket_, reinterpret_cast<char const*>(send_buf_.data() + send_offset_), remaining, 0);

    if (n < 0)
    {
        if (sockerrno == EAGAIN || sockerrno == EWOULDBLOCK)
        {
            // Re-register for write
            event_.reset(tr::evhelpers::event_new_pri2(evbase_, socket_, EV_WRITE, event_cb, this));
            event_add(event_.get(), &Timeout);
            return;
        }
        finish({ false, fmt::format("failed to send SOCKS5 data: {}", tr_net_strerror(sockerrno)) });
        return;
    }

    send_offset_ += static_cast<size_t>(n);

    if (send_offset_ < send_buf_.size())
    {
        // Partial send -- wait for socket to become writable again
        event_.reset(tr::evhelpers::event_new_pri2(evbase_, socket_, EV_WRITE, event_cb, this));
        event_add(event_.get(), &Timeout);
        return;
    }

    // All data sent. Transition to reading the response.
    send_buf_.clear();
    send_offset_ = 0;
    state_ = after_send_state_;
    register_for_read();
}

void tr_socks5_handshake::register_for_read()
{
    event_.reset(tr::evhelpers::event_new_pri2(evbase_, socket_, EV_READ, event_cb, this));
    event_add(event_.get(), &Timeout);
}

// --- Protocol steps ---

void tr_socks5_handshake::send_greeting()
{
    // SOCKS5 greeting: [version, nmethods, methods...]
    uint8_t msg[4];
    size_t msg_len;

    if (config_.auth_enabled)
    {
        // Offer both no-auth and username/password
        msg[0] = socks5::Version;
        msg[1] = 2; // number of methods
        msg[2] = static_cast<uint8_t>(socks5::AuthMethod::NoAuth);
        msg[3] = static_cast<uint8_t>(socks5::AuthMethod::UsernamePass);
        msg_len = 4;
    }
    else
    {
        // Only offer no-auth
        msg[0] = socks5::Version;
        msg[1] = 1; // number of methods
        msg[2] = static_cast<uint8_t>(socks5::AuthMethod::NoAuth);
        msg_len = 3;
    }

    buf_len_ = 0;
    buf_needed_ = 2; // expect 2-byte method selection response
    queue_send(msg, msg_len, State::ReadMethodSelection);
}

void tr_socks5_handshake::read_method_selection()
{
    auto const n = recv(socket_, reinterpret_cast<char*>(buf_.data() + buf_len_), buf_needed_ - buf_len_, 0);
    if (n <= 0)
    {
        if (n == 0)
        {
            finish({ false, "SOCKS5 proxy closed connection during method selection" });
        }
        else if (sockerrno == EAGAIN || sockerrno == EWOULDBLOCK)
        {
            register_for_read();
        }
        else
        {
            finish({ false, fmt::format("failed to read SOCKS5 method selection: {}", tr_net_strerror(sockerrno)) });
        }
        return;
    }

    buf_len_ += static_cast<size_t>(n);
    if (buf_len_ < buf_needed_)
    {
        register_for_read();
        return;
    }

    // Parse: [version, method]
    if (buf_[0] != socks5::Version)
    {
        finish({ false, fmt::format("SOCKS5 proxy returned unexpected version {}", buf_[0]) });
        return;
    }

    auto const method = static_cast<socks5::AuthMethod>(buf_[1]);

    if (method == socks5::AuthMethod::UsernamePass)
    {
        send_auth();
    }
    else if (method == socks5::AuthMethod::NoAuth)
    {
        send_connect_request();
    }
    else
    {
        finish({ false, "SOCKS5 proxy did not accept any offered authentication method" });
    }
}

void tr_socks5_handshake::send_auth()
{
    // RFC 1929 username/password auth
    // [version=0x01, ulen, username..., plen, password...]
    auto const& uname = config_.username;
    auto const& passwd = config_.password;

    if (uname.size() > 255 || passwd.size() > 255)
    {
        finish({ false, "SOCKS5 username or password too long (max 255 bytes)" });
        return;
    }

    uint8_t msg[515]; // max: 1 + 1 + 255 + 1 + 255
    size_t pos = 0;
    msg[pos++] = socks5::AuthVersion;
    msg[pos++] = static_cast<uint8_t>(uname.size());
    std::memcpy(&msg[pos], uname.data(), uname.size());
    pos += uname.size();
    msg[pos++] = static_cast<uint8_t>(passwd.size());
    std::memcpy(&msg[pos], passwd.data(), passwd.size());
    pos += passwd.size();

    buf_len_ = 0;
    buf_needed_ = 2; // expect 2-byte auth response
    queue_send(msg, pos, State::ReadAuthResponse);
}

void tr_socks5_handshake::read_auth_response()
{
    auto const n = recv(socket_, reinterpret_cast<char*>(buf_.data() + buf_len_), buf_needed_ - buf_len_, 0);
    if (n <= 0)
    {
        if (n == 0)
        {
            finish({ false, "SOCKS5 proxy closed connection during auth" });
        }
        else if (sockerrno == EAGAIN || sockerrno == EWOULDBLOCK)
        {
            register_for_read();
        }
        else
        {
            finish({ false, fmt::format("failed to read SOCKS5 auth response: {}", tr_net_strerror(sockerrno)) });
        }
        return;
    }

    buf_len_ += static_cast<size_t>(n);
    if (buf_len_ < buf_needed_)
    {
        register_for_read();
        return;
    }

    // [version, status] -- status 0x00 = success
    if (buf_[1] != 0x00)
    {
        finish({ false, "SOCKS5 proxy authentication failed" });
        return;
    }

    send_connect_request();
}

void tr_socks5_handshake::send_connect_request()
{
    // CONNECT request: [ver, cmd=CONNECT, rsv, atyp, dst.addr, dst.port]
    auto const& [addr, port] = config_.target;

    uint8_t msg[22]; // max for IPv6: 4 + 16 + 2
    size_t pos = 0;
    msg[pos++] = socks5::Version;
    msg[pos++] = socks5::CmdConnect;
    msg[pos++] = socks5::Reserved;

    if (addr.is_ipv4())
    {
        msg[pos++] = static_cast<uint8_t>(socks5::AddressType::IPv4);
        auto const addr4 = addr.addr.addr4.s_addr; // already network byte order
        std::memcpy(&msg[pos], &addr4, 4);
        pos += 4;
    }
    else // IPv6
    {
        msg[pos++] = static_cast<uint8_t>(socks5::AddressType::IPv6);
        std::memcpy(&msg[pos], addr.addr.addr6.s6_addr, 16);
        pos += 16;
    }

    auto const port_n = htons(port.host());
    std::memcpy(&msg[pos], &port_n, 2);
    pos += 2;

    buf_len_ = 0;
    buf_needed_ = 4; // expect 4-byte connect response header [ver, rep, rsv, atyp]
    queue_send(msg, pos, State::ReadConnectResponseHeader);
}

void tr_socks5_handshake::read_connect_response_header()
{
    auto const n = recv(socket_, reinterpret_cast<char*>(buf_.data() + buf_len_), buf_needed_ - buf_len_, 0);
    if (n <= 0)
    {
        if (n == 0)
        {
            finish({ false, "SOCKS5 proxy closed connection during connect" });
        }
        else if (sockerrno == EAGAIN || sockerrno == EWOULDBLOCK)
        {
            register_for_read();
        }
        else
        {
            finish({ false, fmt::format("failed to read SOCKS5 connect response: {}", tr_net_strerror(sockerrno)) });
        }
        return;
    }

    buf_len_ += static_cast<size_t>(n);
    if (buf_len_ < buf_needed_)
    {
        register_for_read();
        return;
    }

    // Parse: [ver, rep, rsv, atyp]
    if (buf_[0] != socks5::Version)
    {
        finish({ false, fmt::format("SOCKS5 connect response has unexpected version {}", buf_[0]) });
        return;
    }

    auto const reply = static_cast<socks5::ReplyCode>(buf_[1]);
    if (reply != socks5::ReplyCode::Succeeded)
    {
        finish({ false, fmt::format("SOCKS5 connect failed: {}", socks5::reply_code_str(reply)) });
        return;
    }

    // Determine how many more bytes to read for the bound address + port
    response_addr_type_ = static_cast<socks5::AddressType>(buf_[3]);
    switch (response_addr_type_)
    {
    case socks5::AddressType::IPv4:
        buf_needed_ = 4 + 2; // 4 bytes addr + 2 bytes port
        break;
    case socks5::AddressType::IPv6:
        buf_needed_ = 16 + 2; // 16 bytes addr + 2 bytes port
        break;
    case socks5::AddressType::DomainName:
        // First byte is length of domain; we'll handle this specially
        // For now, read 1 byte to get the length
        buf_needed_ = 1;
        break;
    default:
        finish({ false, fmt::format("SOCKS5 connect response has unknown address type {}", buf_[3]) });
        return;
    }

    // Read the remaining bound address bytes
    state_ = State::ReadConnectResponseAddress;
    buf_len_ = 0;
    register_for_read();
}

void tr_socks5_handshake::read_connect_response_address()
{
    auto const n = recv(socket_, reinterpret_cast<char*>(buf_.data() + buf_len_), buf_needed_ - buf_len_, 0);
    if (n <= 0)
    {
        if (n == 0)
        {
            finish({ false, "SOCKS5 proxy closed connection during address read" });
        }
        else if (sockerrno == EAGAIN || sockerrno == EWOULDBLOCK)
        {
            register_for_read();
        }
        else
        {
            finish({ false, fmt::format("failed to read SOCKS5 bound address: {}", tr_net_strerror(sockerrno)) });
        }
        return;
    }

    buf_len_ += static_cast<size_t>(n);

    // If domain name, and we just got the length byte, adjust buf_needed_
    if (response_addr_type_ == socks5::AddressType::DomainName && buf_needed_ == 1 && buf_len_ >= 1)
    {
        buf_needed_ = 1 + buf_[0] + 2; // length byte + domain + port
    }

    if (buf_len_ < buf_needed_)
    {
        register_for_read();
        return;
    }

    // SOCKS5 handshake complete! The socket is now tunneled to the target.
    tr_logAddDebug(fmt::format("SOCKS5 tunnel established to {}", config_.target.display_name()));
    finish({ true, {} });
}

void tr_socks5_handshake::finish(socks5::Result result)
{
    state_ = result.success ? State::Done : State::Error;
    event_.reset();

    if (on_done_)
    {
        on_done_(result);
    }
}
