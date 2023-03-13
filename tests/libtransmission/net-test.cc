// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/net.h>
#include <libtransmission/peer-mgr.h>

#include "test-fixtures.h"

using NetTest = ::testing::Test;
using namespace std::literals;

TEST_F(NetTest, conversionsIPv4)
{
    auto constexpr Port = tr_port::fromHost(80);
    auto constexpr AddrStr = "127.0.0.1"sv;

    auto addr = tr_address::from_string(AddrStr);
    EXPECT_TRUE(addr.has_value());
    assert(addr.has_value());
    EXPECT_EQ(AddrStr, addr->display_name());

    auto [ss, sslen] = addr->to_sockaddr(Port);
    EXPECT_EQ(AF_INET, ss.ss_family);
    EXPECT_EQ(Port.network(), reinterpret_cast<sockaddr_in const*>(&ss)->sin_port);

    auto addrport = tr_address::from_sockaddr(reinterpret_cast<sockaddr const*>(&ss));
    ASSERT_TRUE(addrport.has_value());
    assert(addrport.has_value());
    EXPECT_EQ(addr, addrport->first);
    EXPECT_EQ(Port, addrport->second);
}

TEST_F(NetTest, trAddress)
{
    EXPECT_EQ("0.0.0.0", tr_address::any_ipv4().display_name());
    EXPECT_EQ("::", tr_address::any_ipv6().display_name());
}

TEST_F(NetTest, compact4)
{
    static auto constexpr ExpectedReadable = "10.10.10.5"sv;
    static auto constexpr ExpectedPort = tr_port::fromHost(128);
    static auto constexpr Compact4 = std::array<std::byte, 6>{ std::byte{ 0x0A }, std::byte{ 0x0A }, std::byte{ 0x0A },
                                                               std::byte{ 0x05 }, std::byte{ 0x00 }, std::byte{ 0x80 } };

    /// compact <--> tr_address, port

    // extract the address and port from a compact stream...
    auto in = std::data(Compact4);
    auto addr = tr_address{};
    auto port = tr_port{};
    std::tie(addr, in) = tr_address::from_compact_ipv4(in);
    std::tie(port, in) = tr_port::fromCompact(in);
    EXPECT_EQ(std::data(Compact4) + std::size(Compact4), in);
    EXPECT_EQ(ExpectedReadable, addr.display_name());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto compact4 = std::array<std::byte, 6>{};
    auto out = std::data(compact4);
    out = addr.to_compact_ipv4(out, port);
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(compact4)));
    EXPECT_EQ(Compact4, compact4);

    /// sockaddr --> compact

    auto [ss, sslen] = addr.to_sockaddr(port);
    std::fill(std::begin(compact4), std::end(compact4), std::byte{});
    out = std::data(compact4);
    out = tr_address::to_compact(out, &ss);
    EXPECT_EQ(out, std::data(compact4) + std::size(compact4));
    EXPECT_EQ(Compact4, compact4);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::from_compact_ipv4(std::data(compact4), std::size(compact4), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().addr);
    EXPECT_EQ(port, pex.front().port);

    // ...serialize that back again too
    std::fill(std::begin(compact4), std::end(compact4), std::byte{});
    out = std::data(compact4);
    out = tr_pex::to_compact_ipv4(out, std::data(pex), std::size(pex));
    EXPECT_EQ(std::data(compact4) + std::size(compact4), out);
    EXPECT_EQ(Compact4, compact4);
}

TEST_F(NetTest, compact6)
{
    static auto constexpr ExpectedReadable = "1002:1035:4527:3546:7854:1237:3247:3217"sv;
    static auto constexpr ExpectedPort = tr_port::fromHost(6881);
    static auto constexpr Compact6 = std::array<std::byte, 18>{
        std::byte{ 0x10 }, std::byte{ 0x02 }, std::byte{ 0x10 }, std::byte{ 0x35 }, std::byte{ 0x45 }, std::byte{ 0x27 },
        std::byte{ 0x35 }, std::byte{ 0x46 }, std::byte{ 0x78 }, std::byte{ 0x54 }, std::byte{ 0x12 }, std::byte{ 0x37 },
        std::byte{ 0x32 }, std::byte{ 0x47 }, std::byte{ 0x32 }, std::byte{ 0x17 }, std::byte{ 0x1A }, std::byte{ 0xE1 }
    };

    /// compact <--> tr_address, tr_port

    // extract the address and port from a compact stream...
    auto in = std::data(Compact6);
    auto addr = tr_address{};
    auto port = tr_port{};
    std::tie(addr, in) = tr_address::from_compact_ipv6(in);
    std::tie(port, in) = tr_port::fromCompact(in);
    EXPECT_EQ(std::data(Compact6) + std::size(Compact6), in);
    EXPECT_EQ(ExpectedReadable, addr.display_name());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto compact6 = std::array<std::byte, 18>{};
    auto out = std::data(compact6);
    out = addr.to_compact_ipv6(out, port);
    EXPECT_EQ(std::size(Compact6), static_cast<size_t>(out - std::data(compact6)));
    EXPECT_EQ(Compact6, compact6);

    /// sockaddr --> compact

    auto [ss, sslen] = addr.to_sockaddr(port);
    std::fill(std::begin(compact6), std::end(compact6), std::byte{});
    out = std::data(compact6);
    out = tr_address::to_compact(out, &ss);
    EXPECT_EQ(out, std::data(compact6) + std::size(compact6));
    EXPECT_EQ(Compact6, compact6);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::from_compact_ipv6(std::data(compact6), std::size(compact6), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().addr);
    EXPECT_EQ(port, pex.front().port);

    // ...serialize that back again too
    std::fill(std::begin(compact6), std::end(compact6), std::byte{});
    out = std::data(compact6);
    out = tr_pex::to_compact_ipv6(out, std::data(pex), std::size(pex));
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

TEST_F(NetTest, globalIPv6)
{
    auto const addr = tr_globalIPv6();
    EXPECT_TRUE(!addr || addr->is_global_unicast_address());
}
