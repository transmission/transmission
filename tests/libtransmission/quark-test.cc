// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cstddef> // size_t
#include <string>
#include <string_view>

#include <libtransmission/quark.h>

#include "gtest/gtest.h"

using namespace std::literals;

using QuarkTest = ::testing::Test;

TEST_F(QuarkTest, allPredefinedKeysCanBeLookedUp)
{
    for (size_t i = 0; i < TR_N_STATIC_KEYS; ++i)
    {
        auto const str = tr_quark_get_string_view(i);
        auto const key = tr_quark_lookup(str);
        EXPECT_EQ(i, key.value_or(~i));
    }
}

TEST_F(QuarkTest, stringsMatchExpectedValues)
{
    // can lookup public keys
    EXPECT_EQ(""sv, tr_quark_get_string_view(TR_KEY_NONE));
    EXPECT_EQ("wanted"sv, tr_quark_get_string_view(TR_KEY_wanted));

    // can lookup private keys
    using namespace libtransmission::api_compat::detail;
    EXPECT_EQ("files-added", tr_quark_get_string_view(TR_KEY_files_added_kebab));
}

TEST_F(QuarkTest, newQuarkByStringView)
{
    auto constexpr UniqueString = std::string_view{ "this string is not a predefined quark" };
    auto const q = tr_quark_new(UniqueString);
    EXPECT_EQ(UniqueString, tr_quark_get_string_view(q));
}
