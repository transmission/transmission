// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <string_view>

#include "transmission.h"

#include "tr-strbuf.h"

#include "test-fixtures.h"

using StrbufTest = ::testing::Test;
using namespace std::literals;

TEST_F(StrbufTest, append)
{
    static auto constexpr Value = "Hello, World!"sv;

    auto buf = tr_pathbuf{};

    buf.append(Value.substr(0, 5));
    EXPECT_EQ(Value.substr(0, 5), buf.sv());

    buf.append(Value.substr(5));
    EXPECT_EQ(Value, buf.sv());
}

TEST_F(StrbufTest, assign)
{
    static auto constexpr Value = "Hello, World!"sv;

    auto buf = tr_pathbuf{};
    buf = Value;
    EXPECT_EQ(Value, buf.sv());
}

TEST_F(StrbufTest, buildPath)
{
    auto buf = tr_pathbuf{};
    buf.buildPath("foo"sv, "bar"sv, "baz"sv);
    EXPECT_EQ("foo/bar/baz", buf.sv());
}

TEST_F(StrbufTest, clear)
{
    static auto constexpr Value = "Hello, World!"sv;
    auto buf = tr_pathbuf{ Value };
    EXPECT_EQ(Value, buf.sv());
    buf.clear();
    EXPECT_TRUE(std::empty(buf));
    EXPECT_EQ(0U, std::size(buf));
    EXPECT_EQ(""sv, buf.sv());
}

TEST_F(StrbufTest, constructorDefault)
{
    auto buf = tr_pathbuf{};
    EXPECT_EQ(0, std::size(buf));
    EXPECT_TRUE(std::empty(buf));
}

TEST_F(StrbufTest, constructorAssign)
{
    static auto constexpr Value = "Hello, World!"sv;

    auto buf = tr_pathbuf{ Value };
    EXPECT_EQ(Value, buf.sv());

    buf = tr_pathbuf{ Value.substr(7, 5), Value.substr(5, 2), Value.substr(0, 5), Value.substr(12, 1) };
    EXPECT_EQ("World, Hello!"sv, buf.sv());

    buf = tr_pathbuf{ "Hello, ", "World!" };
    EXPECT_EQ(Value, buf.sv());
}

TEST_F(StrbufTest, heap)
{
    static auto constexpr Value = "Hello, World!"sv;

    auto buf = tr_strbuf<char, 10>{};
    buf.append(Value.substr(0, 5));
    auto const* const data_stack = std::data(buf);
    buf.append(Value.substr(5));
    auto const* const data_heap = std::data(buf);
    EXPECT_EQ(Value, buf.sv());
    EXPECT_NE(data_stack, data_heap);
}

TEST_F(StrbufTest, indexOperator)
{
    static auto constexpr Value1 = "Hello, World!"sv;
    static auto constexpr Value2 = "Wello, World!"sv;

    // mutable
    {
        auto buf = tr_pathbuf{ Value1 };
        buf[0] = 'W';
        EXPECT_EQ(Value2, buf.sv());
    }

    // const
    {
        auto const buf = tr_pathbuf{ Value1 };
        EXPECT_EQ(Value1.front(), buf[0]);
    }
}

TEST_F(StrbufTest, iterators)
{
    static auto constexpr Value = "Hello, World!"sv;

    // mutable
    {
        auto buf = tr_pathbuf{ Value };
        auto begin = std::begin(buf);
        auto end = std::end(buf);
        EXPECT_EQ(Value.front(), *begin);
        EXPECT_EQ(std::size(Value), std::distance(begin, end));
    }

    // const
    {
        auto const buf = tr_pathbuf{ Value };
        auto const begin = std::begin(buf);
        auto const end = std::end(buf);
        EXPECT_EQ(Value.front(), *begin);
        EXPECT_EQ(std::size(Value), std::distance(begin, end));
    }
}

TEST_F(StrbufTest, sz)
{
    static char const* const Value = "Hello, World!";

    auto buf = tr_pathbuf{ Value };
    EXPECT_STREQ(Value, buf.c_str());
    EXPECT_EQ(strlen(Value), std::size(buf));

    buf = tr_pathbuf{ "H", Value + 1 };
    EXPECT_STREQ(Value, buf.c_str());
    EXPECT_EQ(strlen(Value), std::size(buf));
}
