// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <event2/event.h>
#include <event2/util.h>

#include <gtest/gtest.h>

#include <libtransmission/net.h>
#include <libtransmission/socks5.h>
#include <libtransmission/utils-ev.h>

#include "test-fixtures.h"

using namespace std::literals;

#define LOCAL_SOCKETPAIR_AF TR_IF_WIN32(AF_INET, AF_UNIX)

namespace
{

// Helper: write all bytes to a socket (blocking)
void write_all(tr_socket_t fd, void const* data, size_t len)
{
    auto const* p = static_cast<char const*>(data);
    while (len > 0)
    {
        auto const n = send(fd, p, len, 0);
        ASSERT_GT(n, 0);
        p += n;
        len -= static_cast<size_t>(n);
    }
}

// Helper: read exactly n bytes from a socket (blocking)
void read_all(tr_socket_t fd, void* data, size_t len)
{
    auto* p = static_cast<char*>(data);
    while (len > 0)
    {
        auto const n = recv(fd, p, len, 0);
        ASSERT_GT(n, 0);
        p += n;
        len -= static_cast<size_t>(n);
    }
}

} // namespace

namespace tr::test
{

class Socks5Test : public TransmissionTest
{
protected:
    void SetUp() override
    {
        TransmissionTest::SetUp();
        evbase_ = event_base_new();
        ASSERT_NE(nullptr, evbase_);
    }

    void TearDown() override
    {
        event_base_free(evbase_);
        TransmissionTest::TearDown();
    }

    // Creates a connected socket pair. fds[0] is the "client" side
    // (given to tr_socks5_handshake), fds[1] is the "proxy" side
    // (used by the test to simulate the SOCKS5 server).
    void createSocketPair(tr_socket_t (&fds)[2])
    {
        auto const rc = evutil_socketpair(LOCAL_SOCKETPAIR_AF, SOCK_STREAM, 0, fds);
        ASSERT_EQ(0, rc);
        // Make client side non-blocking (as in real usage)
        evutil_make_socket_nonblocking(fds[0]);
    }

    event_base* evbase_ = nullptr;
};

// ---

// Test a successful SOCKS5 handshake with no authentication
TEST_F(Socks5Test, noAuthSuccess)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    // Configure handshake
    auto config = tr_socks5_config{};
    config.auth_enabled = false;
    auto addr = tr_address::from_string("192.168.1.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    // Pump the event loop once to let the handshake detect the "connected" socket
    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    // Proxy side: read the greeting
    uint8_t buf[16]{};
    read_all(proxy_fd, buf, 3); // [version=5, nmethods=1, method=0x00]
    EXPECT_EQ(0x05, buf[0]);
    EXPECT_EQ(1, buf[1]);
    EXPECT_EQ(0x00, buf[2]);

    // Proxy side: reply with no-auth selected
    uint8_t method_reply[] = { 0x05, 0x00 };
    write_all(proxy_fd, method_reply, 2);

    // Pump to process method selection and send connect request
    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // Proxy side: read the connect request
    // [version=5, cmd=1, rsv=0, atyp=1, addr(4 bytes), port(2 bytes)] = 10 bytes
    read_all(proxy_fd, buf, 10);
    EXPECT_EQ(0x05, buf[0]); // version
    EXPECT_EQ(0x01, buf[1]); // CONNECT
    EXPECT_EQ(0x00, buf[2]); // reserved
    EXPECT_EQ(0x01, buf[3]); // IPv4
    // addr = 192.168.1.1
    EXPECT_EQ(192, buf[4]);
    EXPECT_EQ(168, buf[5]);
    EXPECT_EQ(1, buf[6]);
    EXPECT_EQ(1, buf[7]);
    // port = 6881 = 0x1AE1
    auto port_n = uint16_t{};
    std::memcpy(&port_n, &buf[8], 2);
    EXPECT_EQ(6881, ntohs(port_n));

    // Proxy side: reply with success + bound address
    uint8_t connect_reply[] = {
        0x05, 0x00, 0x00, 0x01, // ver, success, rsv, atyp=IPv4
        0x00, 0x00, 0x00, 0x00, // bound addr (0.0.0.0)
        0x00, 0x00 // bound port (0)
    };
    write_all(proxy_fd, connect_reply, 10);

    // Wait for completion
    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_TRUE(result->success);
    EXPECT_TRUE(result->error_message.empty());

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test SOCKS5 handshake with username/password authentication
TEST_F(Socks5Test, authSuccess)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = true;
    config.username = "testuser";
    config.password = "testpass";
    auto addr = tr_address::from_string("10.0.0.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(51413) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    // Pump to detect connection and send greeting
    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    // Read greeting: [version=5, nmethods=2, methods=0x00,0x02]
    uint8_t buf[600]{};
    read_all(proxy_fd, buf, 4);
    EXPECT_EQ(0x05, buf[0]);
    EXPECT_EQ(2, buf[1]);
    EXPECT_EQ(0x00, buf[2]);
    EXPECT_EQ(0x02, buf[3]);

    // Reply: select username/password auth
    uint8_t method_reply[] = { 0x05, 0x02 };
    write_all(proxy_fd, method_reply, 2);

    // Pump to process method selection and send auth
    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // Read auth: [version=1, ulen=8, "testuser", plen=8, "testpass"]
    // Total: 1 + 1 + 8 + 1 + 8 = 19 bytes
    read_all(proxy_fd, buf, 19);
    EXPECT_EQ(0x01, buf[0]); // auth version
    EXPECT_EQ(8, buf[1]); // username length
    EXPECT_EQ(0, std::memcmp(&buf[2], "testuser", 8));
    EXPECT_EQ(8, buf[10]); // password length
    EXPECT_EQ(0, std::memcmp(&buf[11], "testpass", 8));

    // Reply: auth success
    uint8_t auth_reply[] = { 0x01, 0x00 };
    write_all(proxy_fd, auth_reply, 2);

    // Pump to process auth response and send connect request
    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // Read connect request: 10 bytes for IPv4
    read_all(proxy_fd, buf, 10);
    EXPECT_EQ(0x05, buf[0]);
    EXPECT_EQ(0x01, buf[1]);
    EXPECT_EQ(0x01, buf[3]); // IPv4

    // Reply: success
    uint8_t connect_reply[] = { 0x05, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    write_all(proxy_fd, connect_reply, 10);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_TRUE(result->success);

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test SOCKS5 handshake with auth failure
TEST_F(Socks5Test, authFailure)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = true;
    config.username = "user";
    config.password = "wrong";
    auto addr = tr_address::from_string("10.0.0.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    // Pump greeting
    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    // Read and discard greeting
    uint8_t buf[32]{};
    read_all(proxy_fd, buf, 4);

    // Select username/password
    uint8_t method_reply[] = { 0x05, 0x02 };
    write_all(proxy_fd, method_reply, 2);

    // Pump auth send
    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // Read and discard auth (1 + 1 + 4 + 1 + 5 = 12 bytes)
    read_all(proxy_fd, buf, 12);

    // Reply: auth failure (status != 0)
    uint8_t auth_reply[] = { 0x01, 0x01 };
    write_all(proxy_fd, auth_reply, 2);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_FALSE(result->success);
    EXPECT_NE(std::string::npos, result->error_message.find("authentication failed"));

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test SOCKS5 connect failure (e.g., connection refused by target)
TEST_F(Socks5Test, connectRefused)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = false;
    auto addr = tr_address::from_string("10.0.0.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    uint8_t buf[16]{};
    read_all(proxy_fd, buf, 3);

    // No-auth
    uint8_t method_reply[] = { 0x05, 0x00 };
    write_all(proxy_fd, method_reply, 2);

    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    read_all(proxy_fd, buf, 10);

    // Reply: connection refused (0x05)
    uint8_t connect_reply[] = { 0x05, 0x05, 0x00, 0x01, // ver, rep=CONNECTION_REFUSED, rsv, atyp
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    write_all(proxy_fd, connect_reply, 10);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_FALSE(result->success);
    EXPECT_NE(std::string::npos, result->error_message.find("connection refused"));

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test SOCKS5 proxy closes connection mid-handshake
TEST_F(Socks5Test, proxyDisconnects)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = false;
    auto addr = tr_address::from_string("10.0.0.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    // Read greeting then close
    uint8_t buf[16]{};
    read_all(proxy_fd, buf, 3);
    tr_net_close_socket(proxy_fd);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_FALSE(result->success);

    tr_net_close_socket(client_fd);
}

// Test SOCKS5 with IPv6 target address
TEST_F(Socks5Test, ipv6Target)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = false;
    auto addr = tr_address::from_string("::1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    uint8_t buf[32]{};
    read_all(proxy_fd, buf, 3);

    uint8_t method_reply[] = { 0x05, 0x00 };
    write_all(proxy_fd, method_reply, 2);

    for (int i = 0; i < 10 && !result.has_value(); ++i)
    {
        event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    // Read connect request for IPv6: [ver, cmd, rsv, atyp=4, addr(16), port(2)] = 22 bytes
    read_all(proxy_fd, buf, 22);
    EXPECT_EQ(0x05, buf[0]);
    EXPECT_EQ(0x01, buf[1]);
    EXPECT_EQ(0x04, buf[3]); // IPv6

    // Reply with IPv6 bound address
    uint8_t connect_reply[22] = {};
    connect_reply[0] = 0x05; // ver
    connect_reply[1] = 0x00; // success
    connect_reply[2] = 0x00; // rsv
    connect_reply[3] = 0x04; // atyp=IPv6
    // rest is zeros (bound addr + port)
    write_all(proxy_fd, connect_reply, 22);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_TRUE(result->success);

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test that no-acceptable-method is properly handled
TEST_F(Socks5Test, noAcceptableMethod)
{
    tr_socket_t fds[2];
    createSocketPair(fds);

    auto const client_fd = fds[0];
    auto const proxy_fd = fds[1];

    auto config = tr_socks5_config{};
    config.auth_enabled = false;
    auto addr = tr_address::from_string("10.0.0.1"sv);
    ASSERT_TRUE(addr.has_value());
    config.target = tr_socket_address{ *addr, tr_port::from_host(6881) };

    std::optional<socks5::Result> result;
    auto handshake = tr_socks5_handshake{ evbase_,
                                          client_fd,
                                          std::move(config),
                                          [&result](socks5::Result const& r) { result = r; } };
    handshake.start();

    event_base_loop(evbase_, EVLOOP_ONCE | EVLOOP_NONBLOCK);

    uint8_t buf[16]{};
    read_all(proxy_fd, buf, 3);

    // Reply with 0xFF = no acceptable methods
    uint8_t method_reply[] = { 0x05, 0xFF };
    write_all(proxy_fd, method_reply, 2);

    ASSERT_TRUE(waitFor(evbase_, [&] { return result.has_value(); }));
    EXPECT_FALSE(result->success);
    EXPECT_NE(std::string::npos, result->error_message.find("authentication method"));

    tr_net_close_socket(client_fd);
    tr_net_close_socket(proxy_fd);
}

// Test reply_code_str covers known codes
TEST_F(Socks5Test, replyCodeStr)
{
    EXPECT_STREQ("succeeded", socks5::reply_code_str(socks5::ReplyCode::Succeeded));
    EXPECT_STREQ("general SOCKS server failure", socks5::reply_code_str(socks5::ReplyCode::GeneralFailure));
    EXPECT_STREQ("connection not allowed by ruleset", socks5::reply_code_str(socks5::ReplyCode::ConnectionNotAllowed));
    EXPECT_STREQ("network unreachable", socks5::reply_code_str(socks5::ReplyCode::NetworkUnreachable));
    EXPECT_STREQ("host unreachable", socks5::reply_code_str(socks5::ReplyCode::HostUnreachable));
    EXPECT_STREQ("connection refused", socks5::reply_code_str(socks5::ReplyCode::ConnectionRefused));
    EXPECT_STREQ("TTL expired", socks5::reply_code_str(socks5::ReplyCode::TTLExpired));
    EXPECT_STREQ("command not supported", socks5::reply_code_str(socks5::ReplyCode::CommandNotSupported));
    EXPECT_STREQ("address type not supported", socks5::reply_code_str(socks5::ReplyCode::AddressTypeNotSupported));
    EXPECT_STREQ("unknown SOCKS5 error", socks5::reply_code_str(static_cast<socks5::ReplyCode>(0x09)));
}

// Test hostname resolution helper
TEST_F(Socks5Test, resolveHostname)
{
    // IP literal should be resolved directly
    auto addr = tr_net_resolve_hostname("127.0.0.1");
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ("127.0.0.1", addr->display_name());

    // IPv6 literal
    addr = tr_net_resolve_hostname("::1");
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ("::1", addr->display_name());

    // localhost should resolve (it's always resolvable)
    addr = tr_net_resolve_hostname("localhost");
    ASSERT_TRUE(addr.has_value());

    // Bogus hostname should fail
    addr = tr_net_resolve_hostname("this.host.does.not.exist.invalid");
    EXPECT_FALSE(addr.has_value());
}

} // namespace tr::test
