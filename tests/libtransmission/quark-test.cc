// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/quark.h>

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
        return std::string{ tr_quark_get_string_view(tr_quark{ i }) };
    }
};

TEST_F(QuarkTest, allPredefinedKeysCanBeLookedUp)
{
    for (size_t i = 0; i < TR_N_KEYS; ++i)
    {
        auto const str = quarkGetString(i);
        auto const q = tr_quark_lookup(str);
        ASSERT_TRUE(q.has_value());
        assert(q.has_value());
        EXPECT_EQ(i, *q);
    }
}

TEST_F(QuarkTest, newQuarkByStringView)
{
    auto constexpr UniqueString = std::string_view{ "this string is not a predefined quark" };
    auto const q = tr_quark_new(UniqueString);
    EXPECT_EQ(UniqueString, tr_quark_get_string_view(q));
}
