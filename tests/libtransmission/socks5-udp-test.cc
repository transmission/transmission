// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <gtest/gtest.h>

#include <event2/event.h>
#include <event2/util.h>

#include <libtransmission/net.h>
#include <libtransmission/socks5-udp.h>
#include <libtransmission/utils-ev.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace
{

constexpr uint8_t Socks5Version = 0x05;
constexpr uint8_t Socks5AuthNone = 0x00;
constexpr uint8_t Socks5CmdUdpAssociate = 0x03;
constexpr uint8_t Socks5AtypIpv4 = 0x01;
constexpr uint8_t Socks5AtypIpv6 = 0x04;
constexpr uint8_t Socks5ReplySuccess = 0x00;

void closeSocket(tr_socket_t& socket)
{
    if (socket != TR_BAD_SOCKET)
    {
        tr_net_close_socket(socket);
        socket = TR_BAD_SOCKET;
    }
}

[[nodiscard]] std::optional<tr_socket_address> localSocketAddress(tr_socket_t socket)
{
    auto ss = sockaddr_storage{};
    auto sslen = socklen_t{ sizeof(ss) };

    if (getsockname(socket, reinterpret_cast<sockaddr*>(&ss), &sslen) != 0)
    {
        return {};
    }

    return tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&ss));
}

[[nodiscard]] std::vector<uint8_t> makeSocks5UdpPacket(sockaddr const* from, std::vector<uint8_t> const& payload)
{
    auto const socket_addr = tr_socket_address::from_sockaddr(from);
    if (!socket_addr)
    {
        return {};
    }

    auto const& address = socket_addr->address();
    auto const header_size = address.is_ipv4() ? size_t{ 10 } : size_t{ 22 };
    auto packet = std::vector<uint8_t>(header_size + std::size(payload));

    packet[0] = 0;
    packet[1] = 0;
    packet[2] = 0;

    if (address.is_ipv4())
    {
        packet[3] = Socks5AtypIpv4;
        std::memcpy(std::data(packet) + 4, &address.addr.addr4, 4);
        auto const port = socket_addr->port().network();
        std::memcpy(std::data(packet) + 8, &port, 2);
    }
    else
    {
        packet[3] = Socks5AtypIpv6;
        std::memcpy(std::data(packet) + 4, &address.addr.addr6, 16);
        auto const port = socket_addr->port().network();
        std::memcpy(std::data(packet) + 20, &port, 2);
    }

    std::memcpy(std::data(packet) + header_size, std::data(payload), std::size(payload));
    return packet;
}

class MockSocks5UdpServer
{
public:
    explicit MockSocks5UdpServer(event_base* base)
        : base_{ base }
    {
        openUdpRelay();
        openTcpListener();
    }

    ~MockSocks5UdpServer()
    {
        tcp_listen_event_.reset();
        tcp_client_event_.reset();
        udp_event_.reset();

        closeSocket(tcp_client_socket_);
        closeSocket(tcp_listen_socket_);
        closeSocket(udp_socket_);
    }

    [[nodiscard]] uint16_t tcp_port() const noexcept
    {
        return tcp_port_;
    }

    [[nodiscard]] bool got_greeting() const noexcept
    {
        return got_greeting_;
    }

    [[nodiscard]] bool got_udp_associate() const noexcept
    {
        return got_udp_associate_;
    }

    [[nodiscard]] bool got_udp_datagram() const noexcept
    {
        return got_udp_datagram_;
    }

    [[nodiscard]] std::optional<tr_socket_address> associate_destination() const
    {
        return associate_destination_;
    }

    [[nodiscard]] std::optional<tr_socket_address> udp_destination() const
    {
        return udp_destination_;
    }

    [[nodiscard]] auto const& udp_payload() const noexcept
    {
        return udp_payload_;
    }

    [[nodiscard]] auto const& response_payload() const noexcept
    {
        return response_payload_;
    }

    void close_tcp_control()
    {
        tcp_client_event_.reset();
        closeSocket(tcp_client_socket_);
    }

private:
    void openTcpListener()
    {
        tcp_listen_socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ASSERT_NE(tcp_listen_socket_, TR_BAD_SOCKET);

        auto const one = int{ 1 };
        setsockopt(tcp_listen_socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char const*>(&one), sizeof(one));
        ASSERT_EQ(evutil_make_socket_nonblocking(tcp_listen_socket_), 0);

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        ASSERT_EQ(bind(tcp_listen_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        ASSERT_EQ(listen(tcp_listen_socket_, 1), 0);

        auto const socket_address = localSocketAddress(tcp_listen_socket_);
        ASSERT_TRUE(socket_address);
        tcp_port_ = socket_address->port().host();

        tcp_listen_event_.reset(event_new(base_, tcp_listen_socket_, EV_READ | EV_PERSIST, onTcpAccept, this));
        ASSERT_NE(tcp_listen_event_.get(), nullptr);
        ASSERT_EQ(event_add(tcp_listen_event_.get(), nullptr), 0);
    }

    void openUdpRelay()
    {
        udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ASSERT_NE(udp_socket_, TR_BAD_SOCKET);
        ASSERT_EQ(evutil_make_socket_nonblocking(udp_socket_), 0);

        auto addr = sockaddr_in{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;

        ASSERT_EQ(bind(udp_socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);

        auto const socket_address = localSocketAddress(udp_socket_);
        ASSERT_TRUE(socket_address);
        udp_port_ = socket_address->port().host();

        udp_event_.reset(event_new(base_, udp_socket_, EV_READ | EV_PERSIST, onUdpReadable, this));
        ASSERT_NE(udp_event_.get(), nullptr);
        ASSERT_EQ(event_add(udp_event_.get(), nullptr), 0);
    }

    static void onTcpAccept(evutil_socket_t /*fd*/, short /*what*/, void* arg)
    {
        static_cast<MockSocks5UdpServer*>(arg)->acceptTcpClient();
    }

    void acceptTcpClient()
    {
        auto client_addr = sockaddr_storage{};
        auto client_addr_len = socklen_t{ sizeof(client_addr) };
        auto const socket = accept(tcp_listen_socket_, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
        if (socket == TR_BAD_SOCKET)
        {
            return;
        }

        closeSocket(tcp_client_socket_);
        tcp_client_socket_ = socket;
        ASSERT_EQ(evutil_make_socket_nonblocking(tcp_client_socket_), 0);

        tcp_client_event_.reset(event_new(base_, tcp_client_socket_, EV_READ | EV_PERSIST, onTcpReadable, this));
        ASSERT_NE(tcp_client_event_.get(), nullptr);
        ASSERT_EQ(event_add(tcp_client_event_.get(), nullptr), 0);
    }

    static void onTcpReadable(evutil_socket_t /*fd*/, short /*what*/, void* arg)
    {
        static_cast<MockSocks5UdpServer*>(arg)->readTcpClient();
    }

    void readTcpClient()
    {
        auto buf = std::array<uint8_t, 256>{};
        auto const n_read = recv(tcp_client_socket_, reinterpret_cast<char*>(std::data(buf)), std::size(buf), 0);
        if (n_read <= 0)
        {
            return;
        }

        tcp_buffer_.insert(std::end(tcp_buffer_), std::begin(buf), std::begin(buf) + static_cast<size_t>(n_read));
        processTcpBuffer();
    }

    void processTcpBuffer()
    {
        if (!got_greeting_)
        {
            if (std::size(tcp_buffer_) < 2U)
            {
                return;
            }

            auto const n_methods = tcp_buffer_[1];
            auto const greeting_size = size_t{ 2U + n_methods };
            if (std::size(tcp_buffer_) < greeting_size)
            {
                return;
            }

            EXPECT_EQ(tcp_buffer_[0], Socks5Version);
            EXPECT_EQ(n_methods, 1U);
            EXPECT_EQ(tcp_buffer_[2], Socks5AuthNone);

            uint8_t const response[] = { Socks5Version, Socks5AuthNone };
            auto const n_sent = send(tcp_client_socket_, reinterpret_cast<char const*>(response), sizeof(response), 0);
            EXPECT_GE(n_sent, 0);
            EXPECT_EQ(static_cast<size_t>(n_sent), sizeof(response));

            tcp_buffer_.erase(std::begin(tcp_buffer_), std::begin(tcp_buffer_) + greeting_size);
            got_greeting_ = true;
        }

        if (!got_udp_associate_)
        {
            if (std::size(tcp_buffer_) < 10U)
            {
                return;
            }

            EXPECT_EQ(tcp_buffer_[0], Socks5Version);
            EXPECT_EQ(tcp_buffer_[1], Socks5CmdUdpAssociate);
            EXPECT_EQ(tcp_buffer_[2], 0U);
            EXPECT_EQ(tcp_buffer_[3], Socks5AtypIpv4);

            auto associate_addr = sockaddr_in{};
            associate_addr.sin_family = AF_INET;
            std::memcpy(&associate_addr.sin_addr, std::data(tcp_buffer_) + 4, 4);
            std::memcpy(&associate_addr.sin_port, std::data(tcp_buffer_) + 8, 2);
            associate_destination_ = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&associate_addr));

            auto response = std::array<uint8_t, 10>{};
            response[0] = Socks5Version;
            response[1] = Socks5ReplySuccess;
            response[2] = 0;
            response[3] = Socks5AtypIpv4;
            response[4] = 127;
            response[5] = 0;
            response[6] = 0;
            response[7] = 1;
            auto const relay_port = htons(udp_port_);
            std::memcpy(std::data(response) + 8, &relay_port, 2);
            auto const n_sent = send(
                tcp_client_socket_,
                reinterpret_cast<char const*>(std::data(response)),
                std::size(response),
                0);
            EXPECT_GE(n_sent, 0);
            EXPECT_EQ(static_cast<size_t>(n_sent), std::size(response));

            tcp_buffer_.erase(std::begin(tcp_buffer_), std::begin(tcp_buffer_) + 10);
            got_udp_associate_ = true;
        }
    }

    static void onUdpReadable(evutil_socket_t /*fd*/, short /*what*/, void* arg)
    {
        static_cast<MockSocks5UdpServer*>(arg)->readUdpRelay();
    }

    void readUdpRelay()
    {
        auto buf = std::array<uint8_t, 2048>{};
        auto client_addr = sockaddr_storage{};
        auto client_addr_len = socklen_t{ sizeof(client_addr) };
        auto const n_read = recvfrom(
            udp_socket_,
            reinterpret_cast<char*>(std::data(buf)),
            std::size(buf),
            0,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_addr_len);

        if (n_read <= 0)
        {
            return;
        }

        auto destination = sockaddr_storage{};
        auto destination_len = socklen_t{};
        auto payload_len = size_t{};
        auto const* payload = tr_socks5_udp::unwrap_udp_packet(
            std::data(buf),
            static_cast<size_t>(n_read),
            payload_len,
            destination,
            destination_len);

        if (payload == nullptr)
        {
            return;
        }

        udp_payload_.assign(payload, payload + payload_len);
        udp_destination_ = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&destination));
        got_udp_datagram_ = true;

        auto const packet = makeSocks5UdpPacket(reinterpret_cast<sockaddr const*>(&destination), response_payload_);
        EXPECT_FALSE(std::empty(packet));
        auto const n_sent = sendto(
            udp_socket_,
            reinterpret_cast<char const*>(std::data(packet)),
            std::size(packet),
            0,
            reinterpret_cast<sockaddr const*>(&client_addr),
            client_addr_len);
        EXPECT_GE(n_sent, 0);
        EXPECT_EQ(static_cast<size_t>(n_sent), std::size(packet));
    }

    event_base* base_ = nullptr;

    tr_socket_t tcp_listen_socket_ = TR_BAD_SOCKET;
    tr_socket_t tcp_client_socket_ = TR_BAD_SOCKET;
    tr_socket_t udp_socket_ = TR_BAD_SOCKET;

    tr::evhelpers::event_unique_ptr tcp_listen_event_;
    tr::evhelpers::event_unique_ptr tcp_client_event_;
    tr::evhelpers::event_unique_ptr udp_event_;

    uint16_t tcp_port_ = 0;
    uint16_t udp_port_ = 0;

    bool got_greeting_ = false;
    bool got_udp_associate_ = false;
    bool got_udp_datagram_ = false;

    std::vector<uint8_t> tcp_buffer_;
    std::optional<tr_socket_address> associate_destination_;
    std::optional<tr_socket_address> udp_destination_;
    std::vector<uint8_t> udp_payload_;
    std::vector<uint8_t> response_payload_ = { 0x72, 0x65, 0x6c, 0x61, 0x79, 0x2d, 0x6f, 0x6b };
};

} // namespace

class Socks5UdpTest : public ::tr::test::TransmissionTest
{
};

TEST_F(Socks5UdpTest, proxiesUdpDatagramsThroughMockServer)
{
    auto evbase = tr::evhelpers::evbase_unique_ptr{ event_base_new() };
    ASSERT_NE(evbase.get(), nullptr);

    auto server = MockSocks5UdpServer{ evbase.get() };
    auto client_ready = false;
    auto client = tr_socks5_udp{ evbase.get(), "127.0.0.1"sv, server.tcp_port(), [&client_ready]() { client_ready = true; } };

    ASSERT_TRUE(
        tr::test::waitFor(
            evbase.get(),
            [&client, &client_ready, &server]()
            { return client_ready && client.is_ready() && server.got_greeting() && server.got_udp_associate(); }));

    auto const client_udp_address = localSocketAddress(client.relay_socket());
    ASSERT_TRUE(client_udp_address);

    auto const associate_destination = server.associate_destination();
    ASSERT_TRUE(associate_destination);
    EXPECT_TRUE(associate_destination->address().is_any());
    EXPECT_EQ(associate_destination->port(), client_udp_address->port());

    auto incoming_called = false;
    auto incoming_payload = std::vector<uint8_t>{};
    auto incoming_source = std::optional<tr_socket_address>{};
    client.set_incoming_callback(
        [&incoming_called,
         &incoming_payload,
         &incoming_source](uint8_t const* payload, size_t payload_len, sockaddr const* from, socklen_t /*fromlen*/)
        {
            incoming_called = true;
            incoming_payload.assign(payload, payload + payload_len);
            incoming_source = tr_socket_address::from_sockaddr(from);
        });

    auto const tracker_address = tr_address::from_string("203.0.113.9"sv);
    ASSERT_TRUE(tracker_address);
    auto const tracker_socket_address = tr_socket_address{ *tracker_address, tr_port::from_host(6969) };
    auto const [tracker_sockaddr, tracker_sockaddr_len] = tracker_socket_address.to_sockaddr();

    auto const request_payload = std::vector<uint8_t>{ 0xde, 0xad, 0xbe, 0xef };
    client.sendto(
        std::data(request_payload),
        std::size(request_payload),
        reinterpret_cast<sockaddr const*>(&tracker_sockaddr),
        tracker_sockaddr_len);

    ASSERT_TRUE(
        tr::test::waitFor(
            evbase.get(),
            [&incoming_called, &server]() { return incoming_called && server.got_udp_datagram(); }));

    EXPECT_EQ(server.udp_payload(), request_payload);

    auto const udp_destination = server.udp_destination();
    ASSERT_TRUE(udp_destination);
    EXPECT_EQ(*udp_destination, tracker_socket_address);

    ASSERT_TRUE(incoming_source);
    EXPECT_EQ(*incoming_source, tracker_socket_address);
    EXPECT_EQ(incoming_payload, server.response_payload());
}

TEST_F(Socks5UdpTest, detectsTcpControlCloseAfterAssociate)
{
    auto evbase = tr::evhelpers::evbase_unique_ptr{ event_base_new() };
    ASSERT_NE(evbase.get(), nullptr);

    auto server = MockSocks5UdpServer{ evbase.get() };
    auto client = tr_socks5_udp{ evbase.get(), "127.0.0.1"sv, server.tcp_port(), tr_socks5_udp::ReadyCallback{} };

    ASSERT_TRUE(
        tr::test::waitFor(evbase.get(), [&client, &server]() { return client.is_ready() && server.got_udp_associate(); }));

    server.close_tcp_control();

    EXPECT_TRUE(tr::test::waitFor(evbase.get(), [&client]() { return client.state() == tr_socks5_udp::State::Error; }));
    EXPECT_FALSE(client.is_ready());
}

//  Tests for unwrap_udp_packet (static, no network needed)

TEST_F(Socks5UdpTest, unwrapValidIpv4Packet)
{
    // RSV(2)=0x0000 FRAG(1)=0x00 ATYP(1)=0x01 ADDR(4) PORT(2) PAYLOAD
    auto const payload_data = std::array<uint8_t, 4>{ 0xDE, 0xAD, 0xBE, 0xEF };
    auto packet = std::array<uint8_t, 10 + 4>{};
    // RSV
    packet[0] = 0x00;
    packet[1] = 0x00;
    // FRAG
    packet[2] = 0x00;
    // ATYP = IPv4
    packet[3] = 0x01;
    // ADDR = 192.168.1.100
    packet[4] = 192;
    packet[5] = 168;
    packet[6] = 1;
    packet[7] = 100;
    // PORT = 6881 (big endian)
    uint16_t port = htons(6881);
    std::memcpy(&packet[8], &port, 2);
    // PAYLOAD
    std::memcpy(&packet[10], payload_data.data(), 4);

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(payload_len, 4U);
    EXPECT_EQ(std::memcmp(result, payload_data.data(), 4), 0);

    // Verify source address was parsed
    EXPECT_EQ(fromlen, sizeof(sockaddr_in));
    auto const* sin = reinterpret_cast<sockaddr_in const*>(&from);
    EXPECT_EQ(sin->sin_family, AF_INET);
    EXPECT_EQ(ntohs(sin->sin_port), 6881);

    auto addr_bytes = std::array<uint8_t, 4>{};
    std::memcpy(addr_bytes.data(), &sin->sin_addr, 4);
    EXPECT_EQ(addr_bytes[0], 192);
    EXPECT_EQ(addr_bytes[1], 168);
    EXPECT_EQ(addr_bytes[2], 1);
    EXPECT_EQ(addr_bytes[3], 100);
}

TEST_F(Socks5UdpTest, unwrapValidIpv6Packet)
{
    // SOCKS5 UDP relay packet with IPv6
    auto const payload_data = std::array<uint8_t, 3>{ 0x01, 0x02, 0x03 };
    auto packet = std::array<uint8_t, 22 + 3>{};
    // RSV
    packet[0] = 0x00;
    packet[1] = 0x00;
    // FRAG
    packet[2] = 0x00;
    // ATYP = IPv6
    packet[3] = 0x04;
    // ADDR = ::1 (16 bytes)
    std::memset(&packet[4], 0, 15);
    packet[19] = 1;
    // PORT = 8080 (big endian)
    uint16_t port = htons(8080);
    std::memcpy(&packet[20], &port, 2);
    // PAYLOAD
    std::memcpy(&packet[22], payload_data.data(), 3);

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(payload_len, 3U);
    EXPECT_EQ(std::memcmp(result, payload_data.data(), 3), 0);

    EXPECT_EQ(fromlen, sizeof(sockaddr_in6));
    auto const* sin6 = reinterpret_cast<sockaddr_in6 const*>(&from);
    EXPECT_EQ(sin6->sin6_family, AF_INET6);
    EXPECT_EQ(ntohs(sin6->sin6_port), 8080);
}

TEST_F(Socks5UdpTest, unwrapRejectsTooShort)
{
    // Less than minimum 10 bytes
    auto packet = std::array<uint8_t, 5>{ 0x00, 0x00, 0x00, 0x01, 0xFF };

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    EXPECT_EQ(result, nullptr);
}

TEST_F(Socks5UdpTest, unwrapRejectsNonZeroRsv)
{
    // RSV field must be 0x0000
    auto packet = std::array<uint8_t, 10>{};
    packet[0] = 0x01; // non-zero RSV
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x01; // IPv4

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    EXPECT_EQ(result, nullptr);
}

TEST_F(Socks5UdpTest, unwrapRejectsFragmented)
{
    // FRAG != 0 should be rejected (no reassembly support)
    auto packet = std::array<uint8_t, 10>{};
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x01; // non-zero FRAG
    packet[3] = 0x01; // IPv4

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    EXPECT_EQ(result, nullptr);
}

TEST_F(Socks5UdpTest, unwrapRejectsUnsupportedAtyp)
{
    // ATYP = 0x03 (domain name) is unsupported
    auto packet = std::array<uint8_t, 10>{};
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x03; // domain name ATYP

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    EXPECT_EQ(result, nullptr);
}

TEST_F(Socks5UdpTest, unwrapHandlesZeroLengthPayload)
{
    // Valid header with no payload (IPv4, exactly 10 bytes)
    auto packet = std::array<uint8_t, 10>{};
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x01; // IPv4
    // ADDR = 10.0.0.1
    packet[4] = 10;
    packet[5] = 0;
    packet[6] = 0;
    packet[7] = 1;
    // PORT = 0
    packet[8] = 0;
    packet[9] = 0;

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    ASSERT_NE(result, nullptr);
    EXPECT_EQ(payload_len, 0U);
}

TEST_F(Socks5UdpTest, unwrapIpv6TooShort)
{
    // ATYP=IPv6 but only 15 bytes total (need 22 minimum)
    auto packet = std::array<uint8_t, 15>{};
    packet[0] = 0x00;
    packet[1] = 0x00;
    packet[2] = 0x00;
    packet[3] = 0x04; // IPv6

    auto from = sockaddr_storage{};
    auto fromlen = socklen_t{};
    size_t payload_len = 0;

    auto const* result = tr_socks5_udp::unwrap_udp_packet(packet.data(), packet.size(), payload_len, from, fromlen);

    EXPECT_EQ(result, nullptr);
}
