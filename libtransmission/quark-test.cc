/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cstring>
#include <string_view>

#include "transmission.h"
#include "quark.h"

#include "gtest/gtest.h"

class QuarkTest: public ::testing::Test
{
protected:
    template<typename T>
    std::string_view quark_get_string(T i) {
        size_t len;
        char const* const str = tr_quark_get_string(tr_quark(i), &len);
        EXPECT_EQ(strlen(str), len);
        return std::string_view(str, len);
    }
};

TEST_F(QuarkTest, all_predefined_keys_can_be_looked_up)
{
    for (int i = 0; i < TR_N_KEYS; i++)
    {
        auto const str = quark_get_string(i);

        tr_quark q;
        EXPECT_TRUE(tr_quark_lookup(std::data(str), std::size(str), &q));
        EXPECT_EQ(i, q);
    }
}

TEST_F(QuarkTest, all_predefined_keys_are_sorted)
{
    for (int i = 0; i + 1 < TR_N_KEYS; i++)
    {
        auto const str1 = quark_get_string(i);
        auto const str2 = quark_get_string(i+1);
        EXPECT_LT(str1, str2);
    }
}

TEST_F(QuarkTest, new_empty_quark_returns_none)
{
    auto const q = tr_quark_new(NULL, TR_BAD_SIZE);
    EXPECT_EQ(TR_KEY_NONE, q);
    EXPECT_EQ(std::string_view{""}, quark_get_string(q));
}
