// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>
#include <libtransmission/error.h>

#include "test-fixtures.h"

#include "gtest/gtest.h"

using namespace std::literals;

TEST(Error, errorSet)
{
    auto error = tr_error{};

    tr_error_prefix(&error, "error: ");
    EXPECT_FALSE(error.is_set()) << error;

    tr_error_set(&error, 2, "oops"sv);
    EXPECT_TRUE(error.is_set());
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("oops", error.message());

    tr_error_prefix(&error, "error: ");
    EXPECT_TRUE(error.is_set());
    EXPECT_EQ(2, error.code());
    EXPECT_EQ("error: oops", error.message());
}

TEST(Error, propagate)
{
    static auto constexpr Code = int{ 1 };

    auto error = tr_error{};
    auto error2 = tr_error{};

    tr_error_set(&error, Code, "oops"sv);
    EXPECT_TRUE(error.is_set());
    EXPECT_EQ(Code, error.code());
    EXPECT_EQ("oops", error.message());

    tr_error_propagate(&error2, std::move(error));
    EXPECT_TRUE(error2.is_set());
    EXPECT_EQ(Code, error2.code());
    EXPECT_EQ("oops", error2.message());
}
