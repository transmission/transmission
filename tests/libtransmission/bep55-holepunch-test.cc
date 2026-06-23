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

namespace
{
tr_socket_address makeIpv6(std::array<uint8_t, tr_address::CompactAddrBytes[TR_AF_INET6]> const& bytes, uint16_t port)
{
    auto buf = std::array<uint8_t, tr_socket_address::CompactSockAddrBytes[TR_AF_INET6]>{};
    std::memcpy(buf.data(), bytes.data(), bytes.size());
    buf[tr_address::CompactAddrBytes[TR_AF_INET6]] = static_cast<uint8_t>(port >> 8);
    buf[tr_address::CompactAddrBytes[TR_AF_INET6] + 1] = static_cast<uint8_t>(port);
    return tr_socket_address::from_compact_ipv6(reinterpret_cast<std::byte const*>(buf.data())).first;
}
} // namespace

TEST(Bep55, EncodeIPv4Rendezvous)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv4(192, 168, 1, 100, 6881);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgRendezvous);
    EXPECT_EQ(payload.to_uint8(), AddrIPv4);

    // addr: 192.168.1.100
    EXPECT_EQ(payload.to_uint8(), 192);
    EXPECT_EQ(payload.to_uint8(), 168);
    EXPECT_EQ(payload.to_uint8(), 1);
    EXPECT_EQ(payload.to_uint8(), 100);

    // port: 6881 in network byte order
    EXPECT_EQ(payload.to_uint16(), 6881);

    EXPECT_EQ(payload.to_uint32(), 0U);
}

TEST(Bep55, EncodeIPv4Connect)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv4(10, 0, 0, 1, 51413);
    EXPECT_TRUE(encode(payload, MsgConnect, sa));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgConnect);
    EXPECT_EQ(payload.to_uint8(), AddrIPv4);
}

TEST(Bep55, EncodeIPv4Error)
{
    static auto constexpr N = PayloadFullIPv4;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv4(172, 16, 0, 5, 8080);
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNotConnected));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgError);
    EXPECT_EQ(payload.to_uint8(), AddrIPv4);

    payload.drain(Ipv4CompactSize);

    EXPECT_EQ(payload.to_uint32(), ErrNotConnected);
}

TEST(Bep55, EncodeIPv6Rendezvous)
{
    static auto constexpr N = PayloadFullIPv6;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgRendezvous);
    EXPECT_EQ(payload.to_uint8(), AddrIPv6);

    // IPv6 addr
    EXPECT_EQ(payload.to_uint8(), 0x20);
    EXPECT_EQ(payload.to_uint8(), 0x01);
    EXPECT_EQ(payload.to_uint8(), 0x0d);
    EXPECT_EQ(payload.to_uint8(), 0xb8);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x00);
    EXPECT_EQ(payload.to_uint8(), 0x01);

    // port: 6881 in network byte order
    EXPECT_EQ(payload.to_uint16(), 6881);
}

TEST(Bep55, EncodeIPv6Connect)
{
    static auto constexpr N = PayloadFullIPv6;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv6(
        { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        51413);
    EXPECT_TRUE(encode(payload, MsgConnect, sa));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgConnect);
    EXPECT_EQ(payload.to_uint8(), AddrIPv6);
}

TEST(Bep55, EncodeIPv6Error)
{
    static auto constexpr N = PayloadFullIPv6;

    auto payload = tr::StackBuffer<N>{};

    auto const sa = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        8080);
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNoSupport));

    EXPECT_EQ(payload.size(), N);

    EXPECT_EQ(payload.to_uint8(), MsgError);

    payload.drain(PayloadMinIPv6 - 1);

    EXPECT_EQ(payload.to_uint32(), ErrNoSupport);
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
    EXPECT_EQ(msg->socket_address.address(), makeIpv4(10, 0, 0, 1, 0).address());
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
    EXPECT_EQ(msg->err_code, 0U);
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
    payload.add_uint32(42U); // non-zero
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
    payload.add_uint32(1U); // non-zero
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
    auto const sa = makeIpv4(10, 0, 0, 1, 51413);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv4());
    EXPECT_EQ(decoded->socket_address.port().host(), 51413);
    EXPECT_EQ(decoded->err_code, 0U);
}

TEST(Bep55, RoundTripIPv4Connect)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};
    auto const sa = makeIpv4(192, 168, 1, 100, 6881);
    EXPECT_TRUE(encode(payload, MsgConnect, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgConnect);
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv4Error)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};
    auto const sa = makeIpv4(172, 16, 0, 5, 8080);
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNotConnected));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgError);
    EXPECT_EQ(decoded->err_code, ErrNotConnected);
    EXPECT_EQ(decoded->socket_address.port().host(), 8080);
}

TEST(Bep55, RoundTripIPv6Rendezvous)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};
    auto const sa = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv6());
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv6Error)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};
    auto const sa = makeIpv6(
        { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        51413);
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNoSelf));
    auto const decoded = decode(payload);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgError);
    EXPECT_EQ(decoded->err_code, ErrNoSelf);
    EXPECT_EQ(decoded->socket_address.port().host(), 51413);
}

TEST(Bep55, EncodeAlwaysOutputsFullSize)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    auto const sa4 = makeIpv4(10, 0, 0, 1, 51413);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa4));
    EXPECT_EQ(payload.size(), PayloadFullIPv4);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgConnect, sa4));
    EXPECT_EQ(payload.size(), PayloadFullIPv4);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa4, ErrNoSupport));
    EXPECT_EQ(payload.size(), PayloadFullIPv4);

    auto const sa6 = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa6));
    EXPECT_EQ(payload.size(), PayloadFullIPv6);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgConnect, sa6));
    EXPECT_EQ(payload.size(), PayloadFullIPv6);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa6, ErrNoSupport));
    EXPECT_EQ(payload.size(), PayloadFullIPv6);
}

TEST(Bep55, EncoderOutputAlwaysDecodable)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    auto const sa4 = makeIpv4(10, 0, 0, 1, 51413);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa4));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgConnect, sa4));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa4, ErrNotConnected));
    EXPECT_TRUE(decode(payload).has_value());

    auto const sa6 = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa6));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgConnect, sa6));
    EXPECT_TRUE(decode(payload).has_value());
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa6, ErrNoSupport));
    EXPECT_TRUE(decode(payload).has_value());
}

TEST(Bep55, IPv4AddressPreserved)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = makeIpv4(123, 45, 67, 89, 9999);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));
    auto const decoded = decode(payload);
    ASSERT_TRUE(decoded.has_value());

    auto const& addr = decoded->socket_address;
    EXPECT_TRUE(addr.address().is_ipv4());
    EXPECT_EQ(addr, makeIpv4(123, 45, 67, 89, 9999));
}

TEST(Bep55, IPv6AddressPreserved)
{
    auto payload = tr::StackBuffer<PayloadFullIPv6>{};

    auto const sa = makeIpv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34, 0x00, 0x01 },
        443);
    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));
    auto const decoded = decode(payload);
    ASSERT_TRUE(decoded.has_value());

    auto const& addr = decoded->socket_address;
    EXPECT_TRUE(addr.address().is_ipv6());
    EXPECT_EQ(addr, sa);
}

TEST(Bep55, LtepExtensionIdValue)
{
    // The extension ID must be 2, which is free between UT_PEX_ID (1)
    // and UT_METADATA_ID (3) in the LtepMessageIds enum.
    EXPECT_EQ(LtepExtensionId, 2U);
}

TEST(Bep55, ErrorRoundTripAllCodes)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = makeIpv4(10, 0, 0, 1, 51413);

    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNoSuchPeer));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSuchPeer);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNotConnected));
    EXPECT_EQ(decode(payload)->err_code, ErrNotConnected);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNoSupport));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSupport);
    payload.clear();
    EXPECT_TRUE(encode(payload, MsgError, sa, ErrNoSelf));
    EXPECT_EQ(decode(payload)->err_code, ErrNoSelf);
}

TEST(Bep55, NonErrorZeroedErrCode)
{
    auto payload = tr::StackBuffer<PayloadFullIPv4>{};

    auto const sa = makeIpv4(10, 0, 0, 1, 51413);

    EXPECT_TRUE(encode(payload, MsgRendezvous, sa));
    auto const rv = decode(payload);
    ASSERT_TRUE(rv.has_value());
    EXPECT_EQ(rv->err_code, 0U);

    payload.clear();
    EXPECT_TRUE(encode(payload, MsgConnect, sa));
    auto const ct = decode(payload);
    ASSERT_TRUE(ct.has_value());
    EXPECT_EQ(ct->err_code, 0U);
}
