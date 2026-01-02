// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cstddef> // size_t
#include <string>
#include <string_view>

#include <libtransmission/quark.h>

#include "test-fixtures.h"

using QuarkTest = ::libtransmission::test::TransmissionTest;

TEST_F(QuarkTest, allKnownKeysCanBeLookedUp){
#define TEST_KNOWN_KEY(_key, _str) \
    { \
        auto const str = tr_quark_get_string_view(_key); \
        auto const key = tr_quark_lookup(str); \
        ASSERT_TRUE(key.has_value()); \
        EXPECT_EQ(_str, key->sv()); \
    }
    KNOWN_KEYS(TEST_KNOWN_KEY)
#undef TEST_KNOWN_KEY
}

TEST_F(QuarkTest, newQuarkByStringView)
{
    auto constexpr UniqueString = std::string_view{ "this string is not a predefined quark" };
    auto const q = tr_quark_new(UniqueString);
    EXPECT_EQ(UniqueString, tr_quark_get_string_view(q));
}
