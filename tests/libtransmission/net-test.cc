// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "net.h"
#include "peer-mgr.h"

#include "test-fixtures.h"

using NetTest = ::testing::Test;
using namespace std::literals;

TEST_F(NetTest, conversionsIPv4)
{
    auto constexpr Port = tr_port::fromHost(80);
    auto constexpr AddrStr = "127.0.0.1"sv;

    auto addr = tr_address::fromString(AddrStr);
    EXPECT_TRUE(addr);
    EXPECT_EQ(AddrStr, addr->readable());

    auto [ss, sslen] = addr->toSockaddr(Port);
    EXPECT_EQ(AF_INET, ss.ss_family);
    EXPECT_EQ(Port.network(), reinterpret_cast<sockaddr_in const*>(&ss)->sin_port);

    auto addrport = tr_address::fromSockaddr(reinterpret_cast<sockaddr const*>(&ss));
    EXPECT_TRUE(addrport);
    EXPECT_EQ(addr, addrport->first);
    EXPECT_EQ(Port, addrport->second);
}

TEST_F(NetTest, trAddress)
{
    EXPECT_EQ("0.0.0.0", tr_address::AnyIPv4().readable());
    EXPECT_EQ("::", tr_address::AnyIPv6().readable());
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
    std::tie(addr, in) = tr_address::fromCompact4(in);
    std::tie(port, in) = tr_port::fromCompact(in);
    EXPECT_EQ(std::data(Compact4) + std::size(Compact4), in);
    EXPECT_EQ(ExpectedReadable, addr.readable());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto compact4 = std::array<std::byte, 6>{};
    auto out = std::data(compact4);
    out = addr.toCompact4(out, port);
    EXPECT_EQ(std::size(Compact4), static_cast<size_t>(out - std::data(compact4)));
    EXPECT_EQ(Compact4, compact4);

    /// sockaddr --> compact

    auto [ss, sslen] = addr.toSockaddr(port);
    std::fill(std::begin(compact4), std::end(compact4), std::byte{});
    out = std::data(compact4);
    out = tr_address::toCompact(out, &ss);
    EXPECT_EQ(out, std::data(compact4) + std::size(compact4));
    EXPECT_EQ(Compact4, compact4);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::fromCompact4(std::data(compact4), std::size(compact4), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().addr);
    EXPECT_EQ(port, pex.front().port);

    // ...serialize that back again too
    std::fill(std::begin(compact4), std::end(compact4), std::byte{});
    out = std::data(compact4);
    out = tr_pex::toCompact4(out, std::data(pex), std::size(pex));
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
    std::tie(addr, in) = tr_address::fromCompact6(in);
    std::tie(port, in) = tr_port::fromCompact(in);
    EXPECT_EQ(std::data(Compact6) + std::size(Compact6), in);
    EXPECT_EQ(ExpectedReadable, addr.readable());
    EXPECT_EQ(ExpectedPort, port);

    // ...serialize it back again
    auto compact6 = std::array<std::byte, 18>{};
    auto out = std::data(compact6);
    out = addr.toCompact6(out, port);
    EXPECT_EQ(std::size(Compact6), static_cast<size_t>(out - std::data(compact6)));
    EXPECT_EQ(Compact6, compact6);

    /// sockaddr --> compact

    auto [ss, sslen] = addr.toSockaddr(port);
    std::fill(std::begin(compact6), std::end(compact6), std::byte{});
    out = std::data(compact6);
    out = tr_address::toCompact(out, &ss);
    EXPECT_EQ(out, std::data(compact6) + std::size(compact6));
    EXPECT_EQ(Compact6, compact6);

    /// compact <--> tr_pex

    // extract them into a tr_pex struct...
    auto const pex = tr_pex::fromCompact6(std::data(compact6), std::size(compact6), nullptr, 0U);
    ASSERT_EQ(1U, std::size(pex));
    EXPECT_EQ(addr, pex.front().addr);
    EXPECT_EQ(port, pex.front().port);

    // ...serialize that back again too
    std::fill(std::begin(compact6), std::end(compact6), std::byte{});
    out = std::data(compact6);
    out = tr_pex::toCompact6(out, std::data(pex), std::size(pex));
    EXPECT_EQ(std::data(compact6) + std::size(compact6), out);
    EXPECT_EQ(Compact6, compact6);
}
