// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cassert>
#include <cstddef> // size_t
#include <string>
#include <string_view>

#include "lib/base/quark.h"

#include "test-fixtures.h"

using QuarkTest = ::tr::test::TransmissionTest;

TEST_F(QuarkTest, allPredefinedKeysCanBeLookedUp)
{
    for (size_t i = 0; i < TR_N_KEYS; ++i)
    {
        auto const str = tr_quark_get_string_view(i);
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
