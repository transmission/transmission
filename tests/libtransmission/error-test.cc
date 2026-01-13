// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include <libtransmission/error.h>

#include "test-fixtures.h"

#include "gtest/gtest.h"

using namespace std::literals;

TEST(Error, errorSet)
{
    auto error = tr_error{};

    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(error.has_value()) << error;
    error.set(2, "oops"sv);
    EXPECT_TRUE(error);
    EXPECT_TRUE(error.has_value()) << error;
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("oops"sv, error.message());

    error.prefix_message("error: ");
    EXPECT_TRUE(error);
    EXPECT_TRUE(error.has_value()) << error;
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("error: oops"sv, error.message());

    error = {};
    EXPECT_FALSE(error) << error;
    EXPECT_FALSE(error.has_value()) << error;
}
