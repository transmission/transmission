// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>
#include <libtransmission/values.h>

#include "gtest/gtest.h"

using namespace libtransmission::Values;

using ValuesTest = ::testing::Test;

TEST_F(ValuesTest, value)
{
    auto val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ("1.00 MB/s", val.to_string());
    EXPECT_EQ(1000000UL, val.base_quantity());
    EXPECT_NEAR(1000U, val.count(Speed::Units::KByps), 0.0001);
    EXPECT_NEAR(1U, val.count(Speed::Units::MByps), 0.0001);
    EXPECT_NEAR(0.001, val.count(Speed::Units::GByps), 0.0001);

    val = Speed{ 1, Speed::Units::Byps };
    EXPECT_EQ(1U, val.base_quantity());
    EXPECT_EQ("1 B/s", val.to_string());

    val = Speed{ 10, Speed::Units::KByps };
    EXPECT_EQ("10.00 kB/s", val.to_string());

    val = Speed{ 999, Speed::Units::KByps };
    EXPECT_EQ("999.0 kB/s", val.to_string());
}

TEST_F(ValuesTest, valueHonorsFormatterInit)
{
    tr_formatter_speed_init(1024, "KayBeePerEss", "EmmBeePerEss", "GeeBeePerEss", "TeeBeePerEss");

    auto const val = Speed{ 1, Speed::Units::MByps };
    EXPECT_EQ("1.00 EmmBeePerEss", val.to_string());
    EXPECT_EQ(1048576U, val.base_quantity());
}
