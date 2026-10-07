// This file Copyright (C) 2026 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <gtest/gtest.h>

#include <libtransmission/types.h>

TEST(Types, byteSpanHelpers)
{
    auto const span = tr_byte_span_t{ .begin = 4U, .end = 10U };
    EXPECT_TRUE(span.is_valid());
    EXPECT_EQ(6U, span.size());

    auto const empty = tr_byte_span_t{ .begin = 7U, .end = 7U };
    EXPECT_TRUE(empty.is_valid());
    EXPECT_EQ(0U, empty.size());

    auto const invalid = tr_byte_span_t{ .begin = 9U, .end = 3U };
    EXPECT_FALSE(invalid.is_valid());
}