// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstddef> // std::byte, size_t
#include <string_view>
#include <tuple>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <libtransmission/net.h>
#include <libtransmission/peer-mgr.h>

#include "gtest/gtest.h"

using NetTest = ::testing::Test;
using namespace std::literals;

TEST_F(NetTest, conversionsIPv4)
{
    static auto constexpr Port = tr_port::from_host(80);
    static auto constexpr AddrStr = "127.0.0.1"sv;

    auto addr = tr_address::from_string(AddrStr);
    ASSERT_TRUE(addr.has_value());
    EXPECT_EQ(AddrStr, addr->display_name());

    auto [ss, sslen] = tr_socket_address::to_sockaddr(*addr, Port);
    EXPECT_EQ(AF_INET, ss.ss_family);
    EXPECT_EQ(Port.network(), reinterpret_cast<sockaddr_in const*>(&ss)->sin_port);

    auto addrport = tr_socket_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&ss));
    ASSERT_TRUE(addrport.has_value());
    EXPECT_EQ(addr, addrport->address());
    EXPECT_EQ(Port, addrport->port());
}

TEST_F(NetTest, trAddress)
{
    EXPECT_EQ("0.0.0.0", tr_address::any(TR_AF_INET).display_name());
    EXPECT_EQ("::", tr_address::any(TR_AF_INET6).display_name());
}

TEST_F(NetTest, compact4)
{
    static auto constexpr ExpectedReadable = "10.10.10.5"sv;
    static auto constexpr ExpectedPort = tr_port::from_host(128);
    static auto constexpr Compact4Bytes = tr_socket_address::CompactSockAddrBytes[TR_AF_INET];
    static auto constexpr Compact4 = std::array<std::byte, Compact4Bytes>{ std::byte{ 0x0A }, std::byte{ 0x0A },
                                                                           std::byte{ 0x0A }, std::byte{ 0x05 },
                                                                           std::byte{ 0x00 }, std::byte{ 0x80 } };

    /// compact <--> tr_address, port

    // extract the address and port from a compact stream...
    auto [socket_address, in] = tr_socket_address::from_compact_ipv4(std::data(Compact4));
    auto const& [addr, port] = socket_address;
    EXPECT_EQ(std::data(Compact4) + std::size(Compact4), in);
    EXPECT_EQ(ExpectedReadable, addr.display_name());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto buf = std::array<std::byte, tr_address::CompactAddrMaxBytes>{};
    auto out = std::data(buf);
    out = socket_address.to_compact(out);
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(buf)));
    EXPECT_TRUE(std::equal(std::begin(Compact4), std::end(Compact4), std::data(buf)));

    /// tr_address --> compact
    buf.fill(std::byte{});
    out = std::data(buf);
    out = addr.to_compact(out);
    EXPECT_EQ(std::size(Compact4) - tr_port::CompactPortBytes, static_cast<size_t>(out - std::data(buf)));
    EXPECT_TRUE(
        std::equal(std::data(Compact4), std::data(Compact4) + std::size(Compact4) - tr_port::CompactPortBytes, std::data(buf)));
    EXPECT_TRUE(std::all_of(
        std::begin(buf) + std::size(Compact4) - tr_port::CompactPortBytes,
        std::end(buf),
        [](std::byte const& byte) { return static_cast<unsigned char>(byte) == 0U; }));

    /// sockaddr --> compact

    auto [ss, sslen] = socket_address.to_sockaddr();
    buf.fill(std::byte{});
    out = std::data(buf);
    out = tr_socket_address::to_compact(out, &ss);
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(buf)));
    EXPECT_TRUE(std::equal(std::begin(Compact4), std::end(Compact4), std::data(buf)));

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::from_compact_ipv4(std::data(buf), out - std::data(buf), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().socket_address.address());
    EXPECT_EQ(port, pex.front().socket_address.port());

    // ...serialize that back again too
    buf.fill(std::byte{});
    out = std::data(buf);
    out = tr_pex::to_compact(out, std::data(pex), std::size(pex));
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(buf)));
    EXPECT_TRUE(std::equal(std::begin(Compact4), std::end(Compact4), std::data(buf)));
}

TEST_F(NetTest, compact6)
{
    static auto constexpr ExpectedReadable = "1002:1035:4527:3546:7854:1237:3247:3217"sv;
    static auto constexpr ExpectedPort = tr_port::from_host(6881);
    static auto constexpr Compact6Bytes = tr_socket_address::CompactSockAddrBytes[TR_AF_INET6];
    static auto constexpr Compact6 = std::array<std::byte, Compact6Bytes>{
        std::byte{ 0x10 }, std::byte{ 0x02 }, std::byte{ 0x10 }, std::byte{ 0x35 }, std::byte{ 0x45 }, std::byte{ 0x27 },
        std::byte{ 0x35 }, std::byte{ 0x46 }, std::byte{ 0x78 }, std::byte{ 0x54 }, std::byte{ 0x12 }, std::byte{ 0x37 },
        std::byte{ 0x32 }, std::byte{ 0x47 }, std::byte{ 0x32 }, std::byte{ 0x17 }, std::byte{ 0x1A }, std::byte{ 0xE1 }
    };

    /// compact <--> tr_address, tr_port

    // extract the address and port from a compact stream...
    auto [socket_address, in] = tr_socket_address::from_compact_ipv6(std::data(Compact6));
    auto const& [addr, port] = socket_address;
    EXPECT_EQ(std::data(Compact6) + std::size(Compact6), in);
    EXPECT_EQ(ExpectedReadable, addr.display_name());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto compact6 = std::array<std::byte, Compact6Bytes>{};
    auto out = std::data(compact6);
    out = socket_address.to_compact(out);
    EXPECT_EQ(std::size(Compact6), static_cast<size_t>(out - std::data(compact6)));
    EXPECT_EQ(Compact6, compact6);

    /// tr_address --> compact
    compact6.fill(std::byte{});
    out = std::data(compact6);
    out = addr.to_compact(out);
    EXPECT_EQ(std::size(Compact6) - tr_port::CompactPortBytes, static_cast<size_t>(out - std::data(compact6)));
    EXPECT_TRUE(std::equal(
        std::data(Compact6),
        std::data(Compact6) + std::size(Compact6) - tr_port::CompactPortBytes,
        std::data(compact6)));
    EXPECT_TRUE(std::all_of(
        std::begin(compact6) + std::size(Compact6) - tr_port::CompactPortBytes,
        std::end(compact6),
        [](std::byte const& byte) { return static_cast<unsigned char>(byte) == 0U; }));

    /// sockaddr --> compact

    auto [ss, sslen] = socket_address.to_sockaddr();
    compact6.fill(std::byte{});
    out = std::data(compact6);
    out = tr_socket_address::to_compact(out, &ss);
    EXPECT_EQ(out, std::data(compact6) + std::size(compact6));
    EXPECT_EQ(Compact6, compact6);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::from_compact_ipv6(std::data(compact6), std::size(compact6), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().socket_address.address());
    EXPECT_EQ(port, pex.front().socket_address.port());

    // ...serialize that back again too
    std::fill(std::begin(compact6), std::end(compact6), std::byte{});
    out = std::data(compact6);
    out = tr_pex::to_compact(out, std::data(pex), std::size(pex));
    EXPECT_EQ(std::data(compact6) + std::size(compact6), out);
    EXPECT_EQ(Compact6, compact6);
}

TEST_F(NetTest, isGlobalUnicastAddress)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 17>{ {
        { "0.0.0.0"sv, false },
        { "1.0.0.0"sv, true },
        { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },
        { "10.255.0.255"sv, false },
        { "100.64.0.0"sv, false },
        { "100.128.0.0"sv, true },
        { "126.0.0.0"sv, true },
        { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, true },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, true },
        { "223.0.0.0"sv, true },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, true },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_global_unicast()) << presentation;
    }
}

TEST_F(NetTest, isIPv4CurrentNetwork)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 19>{ {
        { "0.0.0.0"sv, true },
        { "0.25.37.132"sv, true },
        { "0.255.255.255"sv, true },
        { "1.0.0.0"sv, false },
        { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },
        { "10.255.0.255"sv, false },
        { "100.64.0.0"sv, false },
        { "100.128.0.0"sv, false },
        { "126.0.0.0"sv, false },
        { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, false },
        { "223.0.0.0"sv, false },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_current_network()) << presentation;
    }
}

TEST_F(NetTest, isIPv4And10Private)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 18>{ {
        { "0.0.0.0"sv, false },
        { "9.255.255.255"sv, false },
        { "10.0.0.0"sv, true },
        { "10.255.0.0"sv, true },
        { "10.255.0.255"sv, true },
        { "10.255.255.255"sv, true },
        { "11.0.0.0"sv, false },
        { "100.128.0.0"sv, false },
        { "126.0.0.0"sv, false },
        { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, false },
        { "223.0.0.0"sv, false },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_10_private()) << presentation;
    }
}

TEST_F(NetTest, isIPv4CarrierGradeNAT)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 19>{ {
        { "0.0.0.0"sv, false },
        { "1.0.0.0"sv, false },
        { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },
        { "100.63.255.255"sv, false },
        { "100.64.0.0"sv, true },
        { "100.100.32.0"sv, true },
        { "100.127.255.255"sv, true },
        { "100.128.0.0"sv, false },
        { "126.0.0.0"sv, false },
        { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, false },
        { "223.0.0.0"sv, false },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_carrier_grade_nat()) << presentation;
    }
}

TEST_F(NetTest, isIPv4Loopback)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 19>{ {
        { "0.0.0.0"sv, false },
        { "1.0.0.0"sv, false },
        { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },
        { "10.255.0.255"sv, false },
        { "100.64.0.0"sv, false },
        { "100.128.0.0"sv, false },
        { "126.255.255.255"sv, false },
        { "127.0.0.0"sv, true },
        { "127.12.12.57"sv, true },
        { "127.255.255.255"sv, true },
        { "128.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, false },
        { "223.0.0.0"sv, false },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_loopback()) << presentation;
    }
}

TEST_F(NetTest, isIPv4LinkLocal)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 18>{ {
        { "0.0.0.0"sv, false },
        { "1.0.0.0"sv, false },
        { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },
        { "10.255.0.255"sv, false },
        { "100.64.0.0"sv, false },
        { "100.128.0.0"sv, false },
        { "126.0.0.0"sv, false },
        { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, true },
        { "169.254.235.12"sv, true },
        { "169.254.255.255"sv, true },
        { "169.255.0.0"sv, false },
        { "223.0.0.0"sv, false },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_link_local()) << presentation;
    }
}

TEST_F(NetTest, isIPv4And172Private)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.0"sv, false },      { "10.255.0.255"sv, false },
        { "100.64.0.0"sv, false },      { "100.128.0.0"sv, false },
        { "126.0.0.0"sv, false },       { "127.0.0.0"sv, false },
        { "169.253.255.255"sv, false }, { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false }, { "172.15.255.255"sv, false },
        { "172.16.0.0"sv, true },       { "172.17.78.245"sv, true },
        { "172.31.255.255"sv, true },   { "172.32.0.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_172_private()) << presentation;
    }
}

TEST_F(NetTest, isIPv4IetfProtocolAssignment)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "191.255.255.255"sv, false },
        { "192.0.0.0"sv, true },        { "192.0.0.14"sv, true },
        { "192.0.0.255"sv, true },      { "192.0.1.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_ietf_protocol_assignment()) << presentation;
    }
}

TEST_F(NetTest, isIPv4TestNet1)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "192.0.1.255"sv, false },
        { "192.0.2.0"sv, true },        { "192.0.2.14"sv, true },
        { "192.0.2.225"sv, true },      { "192.0.3.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_test_net_1()) << presentation;
    }
}

TEST_F(NetTest, isIPv4And6to4Relay)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "192.88.98.255"sv, false },
        { "192.88.99.0"sv, true },      { "192.88.99.14"sv, true },
        { "192.88.99.225"sv, true },    { "192.88.100.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_6to4_relay()) << presentation;
    }
}

TEST_F(NetTest, isIPv4And192Private)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "192.167.255.255"sv, false },
        { "192.168.0.0"sv, true },      { "192.168.99.14"sv, true },
        { "192.168.255.225"sv, true },  { "192.169.0.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_192_168_private()) << presentation;
    }
}

TEST_F(NetTest, isIPv4Benchmark)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "198.17.255.255"sv, false },
        { "198.18.0.0"sv, true },       { "198.19.99.14"sv, true },
        { "198.19.255.225"sv, true },   { "198.20.0.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_benchmark()) << presentation;
    }
}

TEST_F(NetTest, isIPv4TestNet2)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "198.51.99.255"sv, false },
        { "198.51.100.0"sv, true },     { "198.51.100.45"sv, true },
        { "198.51.100.255"sv, true },   { "198.51.101.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_test_net_2()) << presentation;
    }
}

TEST_F(NetTest, isIPv4TestNet3)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "203.0.112.255"sv, false },
        { "203.0.113.0"sv, true },      { "203.0.113.45"sv, true },
        { "203.0.113.255"sv, true },    { "203.0.114.0"sv, false },
        { "223.0.0.0"sv, false },       { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_test_net_3()) << presentation;
    }
}

TEST_F(NetTest, isIPv4Multicast)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "203.0.113.255"sv, false },
        { "203.0.114.0"sv, false },     { "223.255.255.255"sv, false },
        { "224.0.0.0"sv, true },        { "230.124.45.18"sv, true },
        { "239.255.255.255"sv, true },  { "240.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_multicast()) << presentation;
    }
}

TEST_F(NetTest, isIPv4McastTestNet)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "203.0.113.255"sv, false },
        { "203.0.114.0"sv, false },     { "233.251.255.255"sv, false },
        { "233.252.0.0"sv, true },      { "233.252.0.18"sv, true },
        { "233.252.0.255"sv, true },    { "233.252.1.0"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_mcast_test_net()) << presentation;
    }
}

TEST_F(NetTest, isIPv4ReservedClassE)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "203.0.113.255"sv, false },
        { "203.0.114.0"sv, false },     { "239.255.255.255"sv, false },
        { "240.0.0.0"sv, true },        { "247.252.0.18"sv, true },
        { "255.255.255.254"sv, true },  { "255.255.255.255"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_reserved_class_e()) << presentation;
    }
}

TEST_F(NetTest, isIPv4LimitedBroadcast)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 20>{ {
        { "0.0.0.0"sv, false },         { "10.0.0.0"sv, false },
        { "10.255.0.255"sv, false },    { "100.64.0.0"sv, false },
        { "127.0.0.0"sv, false },       { "169.253.255.255"sv, false },
        { "169.254.0.0"sv, false },     { "169.254.255.255"sv, false },
        { "172.16.0.0"sv, false },      { "172.17.78.245"sv, false },
        { "172.31.255.255"sv, false },  { "203.0.113.255"sv, false },
        { "203.0.114.0"sv, false },     { "239.255.255.255"sv, false },
        { "240.0.0.0"sv, false },       { "247.252.0.18"sv, false },
        { "255.255.255.254"sv, false }, { "255.255.255.255"sv, true },
        { "0:0:0:0:0:0:0:1"sv, false }, { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv4_limited_broadcast()) << presentation;
    }
}

TEST_F(NetTest, isIPv6Unspecified)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, true },
        { "0:0:0:0:0:0:0:0"sv, true },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_unspecified()) << presentation;
    }
}

TEST_F(NetTest, isIPv6Loopback)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, true },
        { "0:0:0:0:0:0:0:1"sv, true },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_loopback()) << presentation;
    }
}

TEST_F(NetTest, isIPv6IPv4Mapped)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, true },
        { "::ffff:255.255.255.255"sv, true },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_ipv4_mapped()) << presentation;
    }
}

TEST_F(NetTest, isIPv6Teredo)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, true },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, true },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, true },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_teredo()) << presentation;
    }
}

TEST_F(NetTest, isIPv6And6to4)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, true },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, true },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_6to4()) << presentation;
    }
}

TEST_F(NetTest, isIPv6LinkLocal)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, true },
        { "fe80::1234:5678:9876:5432"sv, true },
        { "fe80::ffff:ffff:ffff:ffff"sv, true },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, false },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_link_local()) << presentation;
    }
}

TEST_F(NetTest, isIPv6Multicast)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 26>{ {
        { "0.0.0.0"sv, false },
        { "169.254.0.0"sv, false },
        { "::"sv, false },
        { "0:0:0:0:0:0:0:0"sv, false },
        { "::1"sv, false },
        { "0:0:0:0:0:0:0:1"sv, false },
        { "0:0:0:0:0:0:0:2"sv, false },
        { "::fffe:ffff:ffff"sv, false },
        { "::ffff:0:0"sv, false },
        { "::ffff:255.255.255.255"sv, false },
        { "::1:0:0:0"sv, false },
        { "2000:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001::"sv, false },
        { "2001:0:0eab:dead::a0:abcd:4e"sv, false },
        { "2001:0:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2001:1::"sv, false },
        { "2001:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2002::"sv, false },
        { "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "2003::"sv, false },
        { "fe80::"sv, false },
        { "fe80::1234:5678:9876:5432"sv, false },
        { "fe80::ffff:ffff:ffff:ffff"sv, false },
        { "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, false },
        { "ff00::"sv, true },
        { "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff"sv, true },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        ASSERT_TRUE(address.has_value());
        EXPECT_EQ(expected, address->is_ipv6_multicast()) << presentation;
    }
}

TEST_F(NetTest, ipCompare)
{
    static constexpr auto IpPairs = std::array{
        std::tuple{ "223.18.245.229"sv, "8.8.8.8"sv, 1 },
        std::tuple{ "0.0.0.0"sv, "255.255.255.255"sv, -1 },
        std::tuple{ "8.8.8.8"sv, "8.8.8.8"sv, 0 },
        std::tuple{ "8.8.8.8"sv, "2001:0:0eab:dead::a0:abcd:4e"sv, -1 },
        std::tuple{ "2001:1890:1112:1::20"sv, "2001:0:0eab:dead::a0:abcd:4e"sv, 1 },
        std::tuple{ "2001:1890:1112:1::20"sv, "[2001:0:0eab:dead::a0:abcd:4e]"sv, 1 },
        std::tuple{ "2001:1890:1112:1::20"sv, "2001:1890:1112:1::20"sv, 0 },
        std::tuple{ "2001:1890:1112:1::20"sv, "[2001:1890:1112:1::20]"sv, 0 },
    };

    for (auto const& [sv1, sv2, res] : IpPairs)
    {
        auto const ip1 = *tr_address::from_string(sv1);
        auto const ip2 = *tr_address::from_string(sv2);

        EXPECT_EQ(ip1.compare(ip2) < 0, res < 0) << sv1 << ' ' << sv2;
        EXPECT_EQ(ip1.compare(ip2) > 0, res > 0) << sv1 << ' ' << sv2;
        EXPECT_EQ(ip1.compare(ip2) == 0, res == 0) << sv1 << ' ' << sv2;
        EXPECT_EQ(ip1 < ip2, res < 0) << sv1 << ' ' << sv2;
        EXPECT_EQ(ip1 > ip2, res > 0) << sv1 << ' ' << sv2;
        EXPECT_EQ(ip1 == ip2, res == 0) << sv1 << ' ' << sv2;
    }
}

TEST_F(NetTest, IPv4MappedAddress)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, std::string_view>, 14>{ {
        { "::ffff:1.0.0.0"sv, "1.0.0.0"sv },
        { "::ffff:10.0.0.0"sv, "10.0.0.0"sv },
        { "::ffff:10.255.0.0"sv, "10.255.0.0"sv },
        { "::ffff:10.255.0.255"sv, "10.255.0.255"sv },
        { "::ffff:100.64.0.0"sv, "100.64.0.0"sv },
        { "::ffff:100.128.0.0"sv, "100.128.0.0"sv },
        { "::ffff:126.0.0.0"sv, "126.0.0.0"sv },
        { "::ffff:127.0.0.0"sv, "127.0.0.0"sv },
        { "::ffff:169.253.255.255"sv, "169.253.255.255"sv },
        { "::ffff:169.254.0.0"sv, "169.254.0.0"sv },
        { "::ffff:169.254.255.255"sv, "169.254.255.255"sv },
        { "::ffff:169.255.0.0"sv, "169.255.0.0"sv },
        { "::ffff:223.0.0.0"sv, "223.0.0.0"sv },
        { "::ffff:224.0.0.0"sv, "224.0.0.0"sv },
    } };

    for (auto const& [mapped_sv, native_sv] : Tests)
    {
        auto const mapped = tr_address::from_string(mapped_sv);
        ASSERT_TRUE(mapped);

        auto const native = mapped->from_ipv4_mapped();
        ASSERT_TRUE(native);

        EXPECT_EQ(native_sv, native->display_name());
    }
}
