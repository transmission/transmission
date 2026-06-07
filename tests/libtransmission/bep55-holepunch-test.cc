// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "libtransmission/bep55-holepunch.h"
#include "libtransmission/net.h"

using namespace bep55;

static tr_socket_address make_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    auto addr = tr_address{};
    addr.type = TR_AF_INET;
    auto const* p = reinterpret_cast<uint8_t*>(&addr.addr.addr4.s_addr);
    // NOLINTBEGIN(cppcoreguidelines-pro-type-member-init)
    const_cast<uint8_t*>(p)[0] = a;
    const_cast<uint8_t*>(p)[1] = b;
    const_cast<uint8_t*>(p)[2] = c;
    const_cast<uint8_t*>(p)[3] = d;
    // NOLINTEND(cppcoreguidelines-pro-type-member-init)
    return tr_socket_address{ addr, tr_port::from_host(port) };
}

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
    auto payload = std::string(8, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv4);
    // 10.0.0.1
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 1 };
    // port 51413 in network byte order
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);

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
    auto payload = std::string(8, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgConnect);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 192 };
    data[3] = std::byte{ 168 };
    data[4] = std::byte{ 1 };
    data[5] = std::byte{ 50 };
    auto const nport = htons(6881);
    std::memcpy(data + 6, &nport, 2);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgConnect);
    EXPECT_EQ(msg->socket_address.port().host(), 6881);
}

TEST(Bep55, DecodeIPv4RendezvousAnacrolix)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 1 };
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = uint32_t{ 0 };
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
    EXPECT_EQ(msg->err_code, 0u);
}

TEST(Bep55, DecodeIPv4ConnectAnacrolix)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgConnect);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 172 };
    data[3] = std::byte{ 16 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 5 };
    auto const nport = htons(8080);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = uint32_t{ 0 };
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgConnect);
    EXPECT_EQ(msg->socket_address.port().host(), 8080);
}

TEST(Bep55, DecodeIPv4ErrorNoSuchPeer)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 2 };
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(ErrNoSuchPeer);
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNoSuchPeer);
}

TEST(Bep55, DecodeIPv4ErrorNotConnected)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 3 };
    auto const nport = htons(12345);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(ErrNotConnected);
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNotConnected);
}

TEST(Bep55, DecodeIPv4ErrorNoSupport)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 4 };
    auto const nport = htons(12345);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(ErrNoSupport);
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->err_code, ErrNoSupport);
}

TEST(Bep55, DecodeIPv4ErrorNoSelf)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 5 };
    auto const nport = htons(12345);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(ErrNoSelf);
    std::memcpy(data + 8, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->err_code, ErrNoSelf);
}

TEST(Bep55, DecodeIPv6RendezvousStrict)
{
    auto payload = std::string(20, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv6);
    // 2001:db8::1
    data[2] = std::byte{ 0x20 };
    data[3] = std::byte{ 0x01 };
    data[4] = std::byte{ 0x0d };
    data[5] = std::byte{ 0xb8 };
    data[17] = std::byte{ 0x01 };
    auto const nport = htons(6881);
    std::memcpy(data + 18, &nport, 2);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_TRUE(msg->socket_address.address().is_ipv6());
    EXPECT_EQ(msg->socket_address.port().host(), 6881);
}

TEST(Bep55, DecodeIPv6RendezvousAnacrolix)
{
    auto payload = std::string(24, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv6);
    data[2] = std::byte{ 0xfe };
    data[3] = std::byte{ 0x80 };
    data[17] = std::byte{ 0x01 };
    auto const nport = htons(51413);
    std::memcpy(data + 18, &nport, 2);
    auto const nerr = uint32_t{ 0 };
    std::memcpy(data + 20, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgRendezvous);
    EXPECT_EQ(msg->socket_address.port().host(), 51413);
}

TEST(Bep55, DecodeIPv6Error)
{
    auto payload = std::string(24, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv6);
    data[2] = std::byte{ 0x20 };
    data[3] = std::byte{ 0x01 };
    data[17] = std::byte{ 0x01 };
    auto const nport = htons(8080);
    std::memcpy(data + 18, &nport, 2);
    auto const nerr = htonl(ErrNoSupport);
    std::memcpy(data + 20, &nerr, 4);

    auto const msg = decode(payload);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->msg_type, MsgError);
    EXPECT_EQ(msg->err_code, ErrNoSupport);
}

TEST(Bep55, RejectTooShort)
{
    EXPECT_FALSE(decode(std::string_view("\x00\x00", 2)).has_value());
    EXPECT_FALSE(decode(std::string_view("", 0)).has_value());
    EXPECT_FALSE(decode(std::string_view("\x00", 1)).has_value());
    EXPECT_FALSE(decode(std::string_view("\x00\x00\x0a\x00\x00\x01\x00", 7)).has_value());
}

TEST(Bep55, RejectTooShortIPv6)
{
    auto payload = std::string(19, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv6);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectErrorTooShortIPv4)
{
    auto payload = std::string(11, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv4);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectErrorTooShortIPv6)
{
    auto payload = std::string(23, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgError);
    data[1] = static_cast<std::byte>(AddrIPv6);
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectUnknownAddressType)
{
    auto payload = std::string(8, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(2); // invalid
    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectRendezvousWithNonZeroTrailingErrorCode)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 1 };
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(42u); // non-zero
    std::memcpy(data + 8, &nerr, 4);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectConnectWithNonZeroTrailingErrorCode)
{
    auto payload = std::string(12, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgConnect);
    data[1] = static_cast<std::byte>(AddrIPv4);
    data[2] = std::byte{ 10 };
    data[3] = std::byte{ 0 };
    data[4] = std::byte{ 0 };
    data[5] = std::byte{ 1 };
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);
    auto const nerr = htonl(1u); // non-zero
    std::memcpy(data + 8, &nerr, 4);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectUnknownMessageType)
{
    auto payload = std::string(8, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(uint8_t{ 99 }); // unknown
    data[1] = static_cast<std::byte>(AddrIPv4);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RejectTrailingBytesBeyondErrorCode)
{
    auto payload = std::string(13, '\0');
    auto* data = reinterpret_cast<std::byte*>(payload.data());
    data[0] = static_cast<std::byte>(MsgRendezvous);
    data[1] = static_cast<std::byte>(AddrIPv4);
    auto const nport = htons(51413);
    std::memcpy(data + 6, &nport, 2);

    EXPECT_FALSE(decode(payload).has_value());
}

TEST(Bep55, RoundTripIPv4Rendezvous)
{
    auto const sa = make_ipv4(10, 0, 0, 1, 51413);
    auto const encoded = encode(MsgRendezvous, sa);
    auto const decoded = decode(encoded);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv4());
    EXPECT_EQ(decoded->socket_address.port().host(), 51413);
    EXPECT_EQ(decoded->err_code, 0u);
}

TEST(Bep55, RoundTripIPv4Connect)
{
    auto const sa = make_ipv4(192, 168, 1, 100, 6881);
    auto const encoded = encode(MsgConnect, sa);
    auto const decoded = decode(encoded);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgConnect);
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv4Error)
{
    auto const sa = make_ipv4(172, 16, 0, 5, 8080);
    auto const encoded = encode(MsgError, sa, ErrNotConnected);
    auto const decoded = decode(encoded);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgError);
    EXPECT_EQ(decoded->err_code, ErrNotConnected);
    EXPECT_EQ(decoded->socket_address.port().host(), 8080);
}

TEST(Bep55, RoundTripIPv6Rendezvous)
{
    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    auto const encoded = encode(MsgRendezvous, sa);
    auto const decoded = decode(encoded);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->msg_type, MsgRendezvous);
    EXPECT_TRUE(decoded->socket_address.address().is_ipv6());
    EXPECT_EQ(decoded->socket_address.port().host(), 6881);
}

TEST(Bep55, RoundTripIPv6Error)
{
    auto const sa = make_ipv6(
        { 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        51413);
    auto const encoded = encode(MsgError, sa, ErrNoSelf);
    auto const decoded = decode(encoded);

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
    auto const sa4 = make_ipv4(10, 0, 0, 1, 51413);
    EXPECT_TRUE(decode(encode(MsgRendezvous, sa4)).has_value());
    EXPECT_TRUE(decode(encode(MsgConnect, sa4)).has_value());
    EXPECT_TRUE(decode(encode(MsgError, sa4, ErrNotConnected)).has_value());

    auto const sa6 = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 },
        6881);
    EXPECT_TRUE(decode(encode(MsgRendezvous, sa6)).has_value());
    EXPECT_TRUE(decode(encode(MsgConnect, sa6)).has_value());
    EXPECT_TRUE(decode(encode(MsgError, sa6, ErrNoSupport)).has_value());
}

TEST(Bep55, IPv4AddressPreserved)
{
    auto const sa = make_ipv4(123, 45, 67, 89, 9999);
    auto const decoded = decode(encode(MsgRendezvous, sa));
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
    auto const sa = make_ipv6(
        { 0x20, 0x01, 0x0d, 0xb8, 0x85, 0xa3, 0x00, 0x00, 0x8a, 0x2e, 0x03, 0x70, 0x73, 0x34, 0x00, 0x01 },
        443);
    auto const decoded = decode(encode(MsgRendezvous, sa));
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
    auto const sa = make_ipv4(10, 0, 0, 1, 51413);

    EXPECT_EQ(decode(encode(MsgError, sa, ErrNoSuchPeer))->err_code, ErrNoSuchPeer);
    EXPECT_EQ(decode(encode(MsgError, sa, ErrNotConnected))->err_code, ErrNotConnected);
    EXPECT_EQ(decode(encode(MsgError, sa, ErrNoSupport))->err_code, ErrNoSupport);
    EXPECT_EQ(decode(encode(MsgError, sa, ErrNoSelf))->err_code, ErrNoSelf);
}

TEST(Bep55, NonErrorZeroedErrCode)
{
    auto const sa = make_ipv4(10, 0, 0, 1, 51413);

    auto const rv = decode(encode(MsgRendezvous, sa));
    ASSERT_TRUE(rv.has_value());
    EXPECT_EQ(rv->err_code, 0u);

    auto const ct = decode(encode(MsgConnect, sa));
    ASSERT_TRUE(ct.has_value());
    EXPECT_EQ(ct->err_code, 0u);
}
