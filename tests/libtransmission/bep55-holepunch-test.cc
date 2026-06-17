// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string>

#include <gtest/gtest.h>

#include "libtransmission/bep55-holepunch.h"
#include "libtransmission/net.h"
#include "libtransmission/tr-buffer.h"

#include "bep55-test-utils.h"

using namespace bep55;
using namespace std::literals;

static tr_socket_address make_ipv6(std::array<uint8_t, 16> const& bytes, uint16_t port)
{
    auto addr = tr_address{};
    addr.type = TR_AF_INET6;
    std::memcpy(&addr.addr.addr6.s6_addr, bytes.data(), 16);
    return tr_socket_address{ addr, tr_port::from_host(port) };
}

TEST(Bep55, EncodeIPv4Rendezvous)
{
    auto const sa = make_ipv4(192, 168, 1, 100, 6881);
    auto const payload = encode(MsgRendezvous, sa);

    EXPECT_EQ(payload.size(), PayloadFullIPv4);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgRendezvous);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), AddrIPv4);

    // addr: 192.168.1.100
    EXPECT_EQ(static_cast<uint8_t>(data[2]), 192);
    EXPECT_EQ(static_cast<uint8_t>(data[3]), 168);
    EXPECT_EQ(static_cast<uint8_t>(data[4]), 1);
    EXPECT_EQ(static_cast<uint8_t>(data[5]), 100);

    // port: 6881 in network byte order
    uint16_t nport{};
    std::memcpy(&nport, data + 6, 2);
    EXPECT_EQ(ntohs(nport), 6881);

    uint32_t nerr{};
    std::memcpy(&nerr, data + 8, 4);
    EXPECT_EQ(ntohl(nerr), 0u);
}

TEST(Bep55, EncodeIPv4Connect)
{
    auto const sa = make_ipv4(10, 0, 0, 1, 51413);
    auto const payload = encode(MsgConnect, sa);

    EXPECT_EQ(payload.size(), PayloadFullIPv4);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgConnect);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), AddrIPv4);
}

TEST(Bep55, EncodeIPv4Error)
{
    auto const sa = make_ipv4(172, 16, 0, 5, 8080);
    auto const payload = encode(MsgError, sa, ErrNotConnected);

    EXPECT_EQ(payload.size(), PayloadFullIPv4);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgError);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), AddrIPv4);

    uint32_t nerr{};
    std::memcpy(&nerr, data + 8, 4);
    EXPECT_EQ(ntohl(nerr), ErrNotConnected);
}

TEST(Bep55, EncodeIPv6Rendezvous)
{
    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    auto const payload = encode(MsgRendezvous, sa);

    EXPECT_EQ(payload.size(), PayloadFullIPv6);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgRendezvous);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), AddrIPv6);

    // First 2 bytes of IPv6 addr: 0x2001
    EXPECT_EQ(static_cast<uint8_t>(data[2]), 0x20);
    EXPECT_EQ(static_cast<uint8_t>(data[3]), 0x01);

    // Last byte: 0x01
    EXPECT_EQ(static_cast<uint8_t>(data[17]), 0x01);

    // port: 6881 in network byte order
    uint16_t nport{};
    std::memcpy(&nport, data + 18, 2);
    EXPECT_EQ(ntohs(nport), 6881);
}

TEST(Bep55, EncodeIPv6Connect)
{
    auto const sa = make_ipv6(
        { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        51413);
    auto const payload = encode(MsgConnect, sa);

    EXPECT_EQ(payload.size(), PayloadFullIPv6);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgConnect);
    EXPECT_EQ(static_cast<uint8_t>(data[1]), AddrIPv6);
}

TEST(Bep55, EncodeIPv6Error)
{
    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        8080);
    auto const payload = encode(MsgError, sa, ErrNoSupport);

    EXPECT_EQ(payload.size(), PayloadFullIPv6);

    auto const* data = reinterpret_cast<std::byte const*>(payload.data());
    EXPECT_EQ(static_cast<uint8_t>(data[0]), MsgError);

    uint32_t nerr{};
    std::memcpy(&nerr, data + 20, 4);
    EXPECT_EQ(ntohl(nerr), ErrNoSupport);
}

TEST(Bep55, DecodeIPv4RendezvousStrict)
{
    static auto constexpr N = PayloadMinIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv4);
    // 10.0.0.1
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(1);
    // port 51413
    payload.add_uint16(51413);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_TRUE(msg->socket_address.address().is_ipv4());
    EXPECT_EQ(msg->socket_address.address().addr.addr4.s_addr, make_ipv4(10, 0, 0, 1, 0).address().addr.addr4.s_addr);
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
    EXPECT_EQ(msg->err_code, 0u);
}

TEST(Bep55, DecodeIPv4ConnectStrict)
{
    static auto constexpr N = PayloadMinIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgConnect);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(192);
    payload.add_uint8(168);
    payload.add_uint8(1);
    payload.add_uint8(50);
    payload.add_uint16(6881);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgConnect);
    EXPECT_EQ(msg->socket_address.port().host(), 6881);
}

TEST(Bep55, DecodeIPv4RendezvousAnacrolix)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(1);
    payload.add_uint16(51413);
    payload.add_uint32(ErrNonError);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
    EXPECT_EQ(msg->err_code, ErrNonError);
}

TEST(Bep55, DecodeIPv4ConnectAnacrolix)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgConnect);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(172);
    payload.add_uint8(16);
    payload.add_uint8(0);
    payload.add_uint8(5);
    payload.add_uint16(8080);
    payload.add_uint32(ErrNonError);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgConnect);
    EXPECT_EQ(msg->socket_address.port().host(), 8080);
}

TEST(Bep55, DecodeIPv4ErrorNoSuchPeer)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(2);
    payload.add_uint16(51413);
    payload.add_uint32(ErrNoSuchPeer);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNoSuchPeer);
}

TEST(Bep55, DecodeIPv4ErrorNotConnected)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(3);
    payload.add_uint16(12345);
    payload.add_uint32(ErrNotConnected);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNotConnected);
}

TEST(Bep55, DecodeIPv4ErrorNoSupport)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(4);
    payload.add_uint16(12345);
    payload.add_uint32(ErrNoSupport);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->err_code, ErrNoSupport);
}

TEST(Bep55, DecodeIPv4ErrorNoSelf)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(5);
    payload.add_uint16(12345);
    payload.add_uint32(ErrNoSelf);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->err_code, ErrNoSelf);
}

TEST(Bep55, DecodeIPv6RendezvousStrict)
{
    static auto constexpr N = PayloadMinIPv6;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv6);
    // 2001:db8::1
    payload.add_uint8(0x20);
    payload.add_uint8(0x01);
    payload.add_uint8(0x0d);
    payload.add_uint8(0xb8);
    while (payload.size() < sizeof(MsgType) + sizeof(AddrType) + tr_address::CompactAddrBytes[TR_AF_INET6] - 1)
    {
        payload.add_uint8(0);
    }
    payload.add_uint8(0x01);
    payload.add_uint16(6881);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_TRUE(msg->socket_address.address().is_ipv6());
    EXPECT_EQ(msg->socket_address.port().host(), 6881);
}

TEST(Bep55, DecodeIPv6RendezvousAnacrolix)
{
    static auto constexpr N = PayloadFullIPv6;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv6);
    payload.add_uint8(0xfe);
    payload.add_uint8(0x80);
    while (payload.size() < sizeof(MsgType) + sizeof(AddrType) + tr_address::CompactAddrBytes[TR_AF_INET6] - 1)
    {
        payload.add_uint8(0);
    }
    payload.add_uint8(0x01);
    payload.add_uint16(51413);
    payload.add_uint32(ErrNonError);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
}

TEST(Bep55, DecodeIPv6Error)
{
    static auto constexpr N = PayloadFullIPv6;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv6);
    payload.add_uint8(0x20);
    payload.add_uint8(0x01);
    while (payload.size() < sizeof(MsgType) + sizeof(AddrType) + tr_address::CompactAddrBytes[TR_AF_INET6] - 1)
    {
        payload.add_uint8(0);
    }
    payload.add_uint8(0x01);
    payload.add_uint16(8080);
    payload.add_uint32(ErrNoSupport);
    EXPECT_EQ(payload.size(), N);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNoSupport);
}

TEST(Bep55, RejectTooShort)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    payload.add("\x00\x00"sv);
    EXPECT_FALSE(decode(payload).has_value());

    payload.clear();
    EXPECT_FALSE(decode(payload).has_value());

    payload.clear();
    payload.add_uint8(0);
    EXPECT_FALSE(decode(payload).has_value());

    payload.clear();
    payload.add("\x00\x00\x0a\x00\x00\x01\x00"sv);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectTooShortIPv6)
{
    static auto constexpr N = PayloadMinIPv6 - 1;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv6);
    while (payload.size() < N)
    {
        payload.add_uint8(0);
    }
    EXPECT_EQ(payload.size(), N);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectErrorTooShortIPv4)
{
    static auto constexpr N = PayloadMinIPv4 - 1;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv4);
    while (payload.size() < N)
    {
        payload.add_uint8(0);
    }
    EXPECT_EQ(payload.size(), N);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectErrorTooShortIPv6)
{
    static auto constexpr N = PayloadFullIPv6 - 1;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgError);
    payload.add_uint8(AddrIPv6);
    while (payload.size() < N)
    {
        payload.add_uint8(0);
    }
    EXPECT_EQ(payload.size(), N);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectUnknownAddressType)
{
    static auto constexpr N = PayloadMinIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(2); // invalid
    while (payload.size() < N)
    {
        payload.add_uint8(0);
    }
    EXPECT_EQ(payload.size(), N);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectRendezvousWithNonZeroTrailingErrorCode)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(1);
    payload.add_uint16(51413);
    payload.add_uint32(42u); // non-zero
    EXPECT_EQ(payload.size(), N);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectConnectWithNonZeroTrailingErrorCode)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgConnect);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(1);
    payload.add_uint16(51413);
    payload.add_uint32(1u); // non-zero
    EXPECT_EQ(payload.size(), N);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectUnknownMessageType)
{
    static auto constexpr N = PayloadMinIPv4;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(99); // unknown
    payload.add_uint8(AddrIPv4);
    while (payload.size() < N)
    {
        payload.add_uint8(0);
    }
    EXPECT_EQ(payload.size(), N);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectTrailingBytesBeyondErrorCode)
{
    static auto constexpr N = PayloadFullIPv4 + 1;

    auto payload = tr::StackBuffer<N>{};
    payload.add_uint8(MsgRendezvous);
    payload.add_uint8(AddrIPv4);
    payload.add_uint8(10);
    payload.add_uint8(0);
    payload.add_uint8(0);
    payload.add_uint8(1);
    payload.add_uint16(51413);
    payload.add_uint32(0);
    payload.add_uint8(0); // trailing byte
    EXPECT_EQ(payload.size(), N);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RoundTripIPv4Rendezvous)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};
    auto const sa = make_ipv4(10, 0, 0, 1, 51413);
    payload.add(encode(MsgRendezvous, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv4());
    EXPECT_EQ(decoded->socket_address.port().host(), 51413);
    EXPECT_EQ(decoded->err_code, 0u);
}

TEST(Bep55, RoundTripIPv4Connect)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};
    auto const sa = make_ipv4(192, 168, 1, 100, 6881);
    payload.add(encode(MsgConnect, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgConnect);
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv4Error)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};
    auto const sa = make_ipv4(172, 16, 0, 5, 8080);
    payload.add(encode(MsgError, sa, ErrNotConnected));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgError);
    EXPECT_EQ(decoded->err_code, ErrNotConnected);
    EXPECT_EQ(decoded->socket_address.port().host(), 8080);
}

TEST(Bep55, RoundTripIPv6Rendezvous)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};
    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    payload.add(encode(MsgRendezvous, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv6());
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv6Error)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};
    auto const sa = make_ipv6(
        { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        51413);
    payload.add(encode(MsgError, sa, ErrNoSelf));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgError);
    EXPECT_EQ(decoded->err_code, ErrNoSelf);
    EXPECT_EQ(decoded->socket_address.port().host(), 51413);
}

TEST(Bep55, EncodeAlwaysOutputsFullSize)
{
    auto const sa4 = make_ipv4(10, 0, 0, 1, 51413);
    EXPECT_EQ(encode(MsgRendezvous, sa4).size(), PayloadFullIPv4);
    EXPECT_EQ(encode(MsgConnect, sa4).size(), PayloadFullIPv4);
    EXPECT_EQ(encode(MsgError, sa4, ErrNoSupport).size(), PayloadFullIPv4);

    auto const sa6 = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    EXPECT_EQ(encode(MsgRendezvous, sa6).size(), PayloadFullIPv6);
    EXPECT_EQ(encode(MsgConnect, sa6).size(), PayloadFullIPv6);
    EXPECT_EQ(encode(MsgError, sa6, ErrNoSupport).size(), PayloadFullIPv6);
}

TEST(Bep55, EncoderOutputAlwaysDecodable)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    auto const sa4 = make_ipv4(10, 0, 0, 1, 51413);
    payload.add(encode(MsgRendezvous, sa4));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    payload.add(encode(MsgConnect, sa4));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    payload.add(encode(MsgError, sa4, ErrNotConnected));
    EXPECT_TRUE(decode(payload).has_value());

    auto const sa6 = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    payload.clear();
    payload.add(encode(MsgRendezvous, sa6));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    payload.add(encode(MsgConnect, sa6));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    payload.add(encode(MsgError, sa6, ErrNoSupport));
    EXPECT_TRUE(decode(payload).has_value());
}

TEST(Bep55, IPv4AddressPreserved)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = make_ipv4(123, 45, 67, 89, 9999);
    payload.add(encode(MsgRendezvous, sa));
    auto const decoded = decode(payload);
    ASSERT_TRUE(decoded.has_value());

    auto const& addr = decoded->socket_address.address();
    EXPECT_TRUE(addr.is_ipv4());
    auto const* p = reinterpret_cast<uint8_t const*>(&addr.addr.addr4.s_addr);
    EXPECT_EQ(p[0], 123u);
    EXPECT_EQ(p[1], 45u);
    EXPECT_EQ(p[2], 67u);
    EXPECT_EQ(p[3], 89u);
    EXPECT_EQ(decoded->socket_address.port().host(), 9999);
}

TEST(Bep55, IPv6AddressPreserved)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34, 0x00, 0x01 },
        443);
    payload.add(encode(MsgRendezvous, sa));
    auto const decoded = decode(payload);
    ASSERT_TRUE(decoded.has_value());

    auto const& addr = decoded->socket_address.address();
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_EQ(std::memcmp(addr.addr.addr6.s6_addr, sa.address().addr.addr6.s6_addr, 16), 0);
    EXPECT_EQ(decoded->socket_address.port().host(), 443);
}

TEST(Bep55, LtepExtensionIdValue)
{
    // The extension ID must be 2, which is free between UT_PEX_ID (1)
    // and UT_METADATA_ID (3) in the LtepMessageIds enum.
    EXPECT_EQ(LtepExtensionId, 2u);
}

TEST(Bep55, ErrorRoundTripAllCodes)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = make_ipv4(10, 0, 0, 1, 51413);

    payload.add(encode(MsgError, sa, ErrNoSuchPeer));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSuchPeer);
    payload.clear();
    payload.add(encode(MsgError, sa, ErrNotConnected));
    EXPECT_EQ(decode(payload)->err_code, ErrNotConnected);
    payload.clear();
    payload.add(encode(MsgError, sa, ErrNoSupport));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSupport);
    payload.clear();
    payload.add(encode(MsgError, sa, ErrNoSelf));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSelf);
}

TEST(Bep55, NonErrorZeroedErrCode)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = make_ipv4(10, 0, 0, 1, 51413);

    payload.add(encode(MsgRendezvous, sa));
    auto const rv = decode(payload);
    ASSERT_TRUE(rv.has_value());
    EXPECT_EQ(rv->err_code, 0u);

    payload.clear();
    payload.add(encode(MsgConnect, sa));
    auto const ct = decode(payload);
    ASSERT_TRUE(ct.has_value());
    EXPECT_EQ(ct->err_code, 0u);
}
