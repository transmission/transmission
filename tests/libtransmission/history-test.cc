// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/history.h>

#include "gtest/gtest.h"

TEST(History, recentHistory)
{
    auto h = tr_recentHistory<size_t, 60>{};

    h.add(10000, 1);
    EXPECT_EQ(0U, h.count(12000, 1000));
    EXPECT_EQ(1U, h.count(12000, 3000));
    EXPECT_EQ(1U, h.count(12000, 5000));
    h.add(20000, 1);
    EXPECT_EQ(0U, h.count(22000, 1000));
    EXPECT_EQ(1U, h.count(22000, 3000));
    EXPECT_EQ(2U, h.count(22000, 15000));
    EXPECT_EQ(2U, h.count(22000, 20000));
}
