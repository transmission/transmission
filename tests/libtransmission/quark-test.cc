/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "quark.h"

#include "gtest/gtest.h"

#include <cstring>
#include <string>

class QuarkTest : public ::testing::Test
{
protected:
    template<typename T>
    std::string quarkGetString(T i)
    {
        size_t len;
        char const* const str = tr_quark_get_string(tr_quark(i), &len);
        EXPECT_EQ(strlen(str), len);
        return std::string(str, len);
    }
};

TEST_F(QuarkTest, allPredefinedKeysCanBeLookedUp)
{
    for (int i = 0; i < TR_N_KEYS; i++)
    {
        auto const str = quarkGetString(i);

        tr_quark q;
        EXPECT_TRUE(tr_quark_lookup(str.data(), str.size(), &q));
        EXPECT_EQ(i, q);
    }
}

TEST_F(QuarkTest, allPredefinedKeysAreSorted)
{
    for (int i = 0; i + 1 < TR_N_KEYS; i++)
    {
        auto const str1 = quarkGetString(i);
        auto const str2 = quarkGetString(i + 1);
        EXPECT_LT(str1, str2);
    }
}

TEST_F(QuarkTest, newEmptyQuarkReturnsNone)
{
    auto const q = tr_quark_new(nullptr, TR_BAD_SIZE);
    EXPECT_EQ(TR_KEY_NONE, q);
    EXPECT_EQ(std::string{ "" }, quarkGetString(q));
}
