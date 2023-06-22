// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <libtransmission/transmission.h>

#include <libtransmission/tr-buffer.h>

#include "test-fixtures.h"

using BufferTest = ::testing::Test;
using namespace std::literals;
using Buffer = libtransmission::SmallBuffer<1024>;

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

TEST_F(BufferTest, Numbers)
{
    for (auto i = 0; i < 100; ++i)
    {
        auto const expected_u8 = tr_rand_obj<uint8_t>();
        auto const expected_u16 = tr_rand_obj<uint16_t>();
        auto const expected_u32 = tr_rand_obj<uint32_t>();
        auto const expected_u64 = tr_rand_obj<uint64_t>();

        auto buf = Buffer{};

        buf.add_uint8(expected_u8);
        buf.add_uint16(expected_u16);
        buf.add_uint32(expected_u32);
        buf.add_uint64(expected_u64);

        EXPECT_EQ(expected_u8, buf.to_uint8());
        EXPECT_EQ(expected_u16, buf.to_uint16());
        EXPECT_EQ(expected_u32, buf.to_uint32());
        EXPECT_EQ(expected_u64, buf.to_uint64());

        buf.add_uint64(expected_u64);
        buf.add_uint32(expected_u32);
        buf.add_uint16(expected_u16);
        buf.add_uint8(expected_u8);

        EXPECT_EQ(expected_u64, buf.to_uint64());
        EXPECT_EQ(expected_u32, buf.to_uint32());
        EXPECT_EQ(expected_u16, buf.to_uint16());
        EXPECT_EQ(expected_u8, buf.to_uint8());
    }
}

#if 0
TEST_F(BufferTest, NonBufferWriter)
{
    auto constexpr Hello = "Hello, "sv;
    auto constexpr World = "World"sv;
    auto constexpr Bang = "!"sv;

    auto out1 = Buffer{};
    auto out2 = libtransmission::SmallBuffer<1024>{};

    out1.add_uint8(1);
    out2.add_uint8(1);

    out1.add_uint16(1);
    out2.add_uint16(1);

    out1.add_uint32(1);
    out2.add_uint32(1);

    out1.add(Hello);
    out2.add(Hello);

    out1.add(World);
    out2.add(World);

    out1.add(Bang);
    out2.add(Bang);

    auto const result1 = out1.to_string_view();
    auto const result2 = out2.to_string();
    EXPECT_EQ(result1, result2);
}
#endif
