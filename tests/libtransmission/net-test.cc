// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "net.h"

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
