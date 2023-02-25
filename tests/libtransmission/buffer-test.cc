// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#include <libtransmission/tr-buffer.h>

#include "test-fixtures.h"

using BufferTest = ::testing::Test;
using namespace std::literals;
using Buffer = libtransmission::Buffer;

TEST_F(BufferTest, startsWithInSingleSegment)
{
    auto constexpr Hello = "Hello, "sv;
    auto constexpr World = "World"sv;
    auto constexpr Bang = "!"sv;

    auto buf = Buffer{};
    buf.add(Hello);
    EXPECT_TRUE(buf.starts_with(Hello));

    buf.add(World);
    EXPECT_TRUE(buf.starts_with(Hello));
    EXPECT_TRUE(buf.starts_with("Hello, Worl"sv));
    EXPECT_TRUE(buf.starts_with("Hello, World"sv));
    EXPECT_FALSE(buf.starts_with("Hello, World!"sv));
    EXPECT_FALSE(buf.starts_with("Hello!"sv));

    buf.add(Bang);
    EXPECT_FALSE(buf.starts_with("Hello!"));
    EXPECT_TRUE(buf.starts_with(Hello));
    EXPECT_TRUE(buf.starts_with("Hello, Worl"sv));
    EXPECT_TRUE(buf.starts_with("Hello, World"sv));
    EXPECT_TRUE(buf.starts_with("Hello, World!"sv));
}
TEST_F(BufferTest, startsWithInMultiSegment)
{
    auto constexpr Hello = "Hello, "sv;
    auto constexpr World = "World"sv;
    auto constexpr Bang = "!"sv;

    auto buf = std::make_unique<Buffer>();
    buf->add(Buffer{ Hello });
    EXPECT_TRUE(buf->starts_with(Hello));

    buf->add(Buffer{ World });
    EXPECT_TRUE(buf->starts_with(Hello));
    EXPECT_TRUE(buf->starts_with("Hello, Worl"sv));
    EXPECT_TRUE(buf->starts_with("Hello, World"sv));
    EXPECT_FALSE(buf->starts_with("Hello, World!"sv));
    EXPECT_FALSE(buf->starts_with("Hello!"sv));

    buf->add(Buffer{ Bang });
    EXPECT_FALSE(buf->starts_with("Hello!"));
    EXPECT_TRUE(buf->starts_with(Hello));
    EXPECT_TRUE(buf->starts_with("Hello, Worl"sv));
    EXPECT_TRUE(buf->starts_with("Hello, World"sv));
    EXPECT_TRUE(buf->starts_with("Hello, World!"sv));
}
