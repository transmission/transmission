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
#include <string_view>

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

TEST_F(QuarkTest, newQuarkByStringView)
{
    auto constexpr UniqueString = std::string_view{ "this string is not a predefined quark" };
    auto const q = tr_quark_new(UniqueString);
    auto len = size_t{};
    EXPECT_EQ(UniqueString, tr_quark_get_string(q, &len));
    EXPECT_EQ(std::size(UniqueString), len);
}
