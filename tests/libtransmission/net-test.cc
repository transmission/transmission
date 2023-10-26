// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cassert>
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
    EXPECT_TRUE(addr.has_value());
    assert(addr.has_value());
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
    auto compact4 = std::array<std::byte, Compact4Bytes>{};
    auto out = std::data(compact4);
    out = socket_address.to_compact(out);
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(compact4)));
    EXPECT_EQ(Compact4, compact4);

    /// tr_address --> compact
    compact4.fill(std::byte{});
    out = std::data(compact4);
    out = addr.to_compact(out);
    EXPECT_EQ(std::size(Compact4) - tr_port::CompactPortBytes, static_cast<size_t>(out - std::data(compact4)));
    EXPECT_TRUE(std::equal(
        std::data(Compact4),
        std::data(Compact4) + std::size(Compact4) - tr_port::CompactPortBytes,
        std::data(compact4)));
    EXPECT_TRUE(std::all_of(
        std::begin(compact4) + std::size(Compact4) - tr_port::CompactPortBytes,
        std::end(compact4),
        [](std::byte const& byte) { return static_cast<unsigned char>(byte) == 0U; }));

    /// sockaddr --> compact

    auto [ss, sslen] = socket_address.to_sockaddr();
    compact4.fill(std::byte{});
    out = std::data(compact4);
    out = tr_socket_address::to_compact(out, &ss);
    EXPECT_EQ(out, std::data(compact4) + std::size(compact4));
    EXPECT_EQ(Compact4, compact4);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::from_compact_ipv4(std::data(compact4), std::size(compact4), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().socket_address.address());
    EXPECT_EQ(port, pex.front().socket_address.port());

    // ...serialize that back again too
    std::fill(std::begin(compact4), std::end(compact4), std::byte{});
    out = std::data(compact4);
    out = tr_pex::to_compact(out, std::data(pex), std::size(pex));
    EXPECT_EQ(std::data(compact4) + std::size(compact4), out);
    EXPECT_EQ(Compact4, compact4);
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
        { "127.0.0.0"sv, true },
        { "169.253.255.255"sv, true },
        { "169.254.0.0"sv, false },
        { "169.254.255.255"sv, false },
        { "169.255.0.0"sv, true },
        { "223.0.0.0"sv, true },
        { "224.0.0.0"sv, false },
        { "0:0:0:0:0:0:0:1", false },
        { "2001:0:0eab:dead::a0:abcd:4e", true },
    } };

    for (auto const& [presentation, expected] : Tests)
    {
        auto const address = tr_address::from_string(presentation);
        EXPECT_TRUE(address.has_value());
        assert(address.has_value());
        EXPECT_EQ(expected, address->is_global_unicast_address()) << presentation;
    }
}

TEST_F(NetTest, ipCompare)
{
    static constexpr auto IpPairs = std::array{ std::tuple{ "223.18.245.229"sv, "8.8.8.8"sv, 1 },
                                                std::tuple{ "0.0.0.0"sv, "255.255.255.255"sv, -1 },
                                                std::tuple{ "8.8.8.8"sv, "8.8.8.8"sv, 0 },
                                                std::tuple{ "8.8.8.8"sv, "2001:0:0eab:dead::a0:abcd:4e"sv, -1 },
                                                std::tuple{ "2001:1890:1112:1::20"sv, "2001:0:0eab:dead::a0:abcd:4e"sv, 1 },
                                                std::tuple{ "2001:1890:1112:1::20"sv, "2001:1890:1112:1::20"sv, 0 } };

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
