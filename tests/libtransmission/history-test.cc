/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "history.h"

#include "gtest/gtest.h"

TEST(History, recentHistory)
{
    auto h = tr_recentHistory {};

    tr_historyAdd(&h, 10000, 1);
    EXPECT_EQ(0, tr_historyGet(&h, 12000, 1000));
    EXPECT_EQ(1, tr_historyGet(&h, 12000, 3000));
    EXPECT_EQ(1, tr_historyGet(&h, 12000, 5000));
    tr_historyAdd(&h, 20000, 1);
    EXPECT_EQ(0, tr_historyGet(&h, 22000, 1000));
    EXPECT_EQ(1, tr_historyGet(&h, 22000, 3000));
    EXPECT_EQ(2, tr_historyGet(&h, 22000, 15000));
    EXPECT_EQ(2, tr_historyGet(&h, 22000, 20000));
}
