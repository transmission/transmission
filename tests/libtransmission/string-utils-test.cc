// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "lib/base/string-utils.h"

#include "libtransmission/crypto-utils.h" // tr_rand_int()

#include "test-fixtures.h"

using UtilsTest = ::tr::test::TransmissionTest;
using namespace std::literals;

TEST_F(UtilsTest, trStrvContains)
{
    EXPECT_FALSE(tr_strv_contains("a test is this"sv, "TEST"sv));
    EXPECT_FALSE(tr_strv_contains("test"sv, "testt"sv));
    EXPECT_FALSE(tr_strv_contains("test"sv, "this is a test"sv));
    EXPECT_TRUE(tr_strv_contains(" test "sv, "tes"sv));
    EXPECT_TRUE(tr_strv_contains(" test"sv, "test"sv));
    EXPECT_TRUE(tr_strv_contains("a test is this"sv, "test"sv));
    EXPECT_TRUE(tr_strv_contains("test "sv, "test"sv));
    EXPECT_TRUE(tr_strv_contains("test"sv, ""sv));
    EXPECT_TRUE(tr_strv_contains("test"sv, "t"sv));
    EXPECT_TRUE(tr_strv_contains("test"sv, "te"sv));
    EXPECT_TRUE(tr_strv_contains("test"sv, "test"sv));
    EXPECT_TRUE(tr_strv_contains("this is a test"sv, "test"sv));
    EXPECT_TRUE(tr_strv_contains(""sv, ""sv));
}

TEST_F(UtilsTest, trStrvStartsWith)
{
    EXPECT_FALSE(tr_strv_starts_with(""sv, "this is a string"sv));
    EXPECT_FALSE(tr_strv_starts_with("this is a strin"sv, "this is a string"sv));
    EXPECT_FALSE(tr_strv_starts_with("this is a strin"sv, "this is a string"sv));
    EXPECT_FALSE(tr_strv_starts_with("this is a string"sv, " his is a string"sv));
    EXPECT_FALSE(tr_strv_starts_with("this is a string"sv, "his is a string"sv));
    EXPECT_FALSE(tr_strv_starts_with("this is a string"sv, "string"sv));
    EXPECT_TRUE(tr_strv_starts_with(""sv, ""sv));
    EXPECT_TRUE(tr_strv_starts_with("this is a string"sv, ""sv));
    EXPECT_TRUE(tr_strv_starts_with("this is a string"sv, "this "sv));
    EXPECT_TRUE(tr_strv_starts_with("this is a string"sv, "this is"sv));
    EXPECT_TRUE(tr_strv_starts_with("this is a string"sv, "this"sv));
}

TEST_F(UtilsTest, trStrvEndsWith)
{
    EXPECT_FALSE(tr_strv_ends_with(""sv, "string"sv));
    EXPECT_FALSE(tr_strv_ends_with("this is a string"sv, "alphabet"sv));
    EXPECT_FALSE(tr_strv_ends_with("this is a string"sv, "strin"sv));
    EXPECT_FALSE(tr_strv_ends_with("this is a string"sv, "this is"sv));
    EXPECT_FALSE(tr_strv_ends_with("this is a string"sv, "this"sv));
    EXPECT_FALSE(tr_strv_ends_with("tring"sv, "string"sv));
    EXPECT_TRUE(tr_strv_ends_with(""sv, ""sv));
    EXPECT_TRUE(tr_strv_ends_with("this is a string"sv, " string"sv));
    EXPECT_TRUE(tr_strv_ends_with("this is a string"sv, ""sv));
    EXPECT_TRUE(tr_strv_ends_with("this is a string"sv, "a string"sv));
    EXPECT_TRUE(tr_strv_ends_with("this is a string"sv, "g"sv));
    EXPECT_TRUE(tr_strv_ends_with("this is a string"sv, "string"sv));
}

TEST_F(UtilsTest, trStrvSep)
{
    static auto constexpr CommaCh = ',';
    static auto constexpr SemiColonCh = ';';
    static auto constexpr CommaSemiColonSv = ",;"sv;
    static auto constexpr CommaSemiColonArr = std::array{ ',', ';' };

    auto sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ("token2"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaCh));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2,token3"sv, tr_strv_sep(&sv, SemiColonCh));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, SemiColonCh));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1"sv, tr_strv_sep(&sv, CommaSemiColonSv));
    EXPECT_EQ("token2"sv, tr_strv_sep(&sv, CommaSemiColonSv));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, CommaSemiColonSv));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaSemiColonSv));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1"sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));
    EXPECT_EQ("token2"sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1"sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));
    EXPECT_EQ("token2"sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2"sv, tr_strv_sep(&sv, CommaCh, 7));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaCh));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2,token3"sv, tr_strv_sep(&sv, SemiColonCh, 7));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, SemiColonCh));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2"sv, tr_strv_sep(&sv, CommaSemiColonSv, 7));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, CommaSemiColonSv));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaSemiColonSv));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2"sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 7, std::size(CommaSemiColonArr)));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, std::data(CommaSemiColonArr), 0, std::size(CommaSemiColonArr)));

    sv = "token1,token2,token3"sv;
    EXPECT_EQ("token1,token2"sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv), 7));
    EXPECT_EQ("token3"sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, std::data(CommaSemiColonSv)));

    sv = " token1,token2"sv;
    EXPECT_EQ(" token1"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ("token2"sv, tr_strv_sep(&sv, CommaCh));

    sv = "token1;token2"sv;
    EXPECT_EQ("token1;token2"sv, tr_strv_sep(&sv, CommaCh));
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaCh));

    sv = ""sv;
    EXPECT_EQ(""sv, tr_strv_sep(&sv, CommaCh));
}

TEST_F(UtilsTest, trStrvStrip)
{
    EXPECT_EQ(""sv, tr_strv_strip("              "sv));
    EXPECT_EQ("test test"sv, tr_strv_strip("    test test     "sv));
    EXPECT_EQ("test"sv, tr_strv_strip("   test     "sv));
    EXPECT_EQ("test"sv, tr_strv_strip("   test "sv));
    EXPECT_EQ("test"sv, tr_strv_strip(" test       "sv));
    EXPECT_EQ("test"sv, tr_strv_strip(" test "sv));
    EXPECT_EQ("test"sv, tr_strv_strip("test"sv));
}

TEST_F(UtilsTest, strvReplaceInvalid)
{
    auto in = "hello world"sv;
    auto out = tr_strv_replace_invalid(in);
    EXPECT_EQ(in, out);

    in = "hello world"sv;
    out = tr_strv_replace_invalid(in.substr(0, 5));
    EXPECT_EQ("hello"sv, out);

    // this version is not utf-8 (but cp866)
    in = "\x92\xE0\xE3\xA4\xAD\xAE \xA1\xEB\xE2\xEC \x81\xAE\xA3\xAE\xAC"sv;
    out = tr_strv_replace_invalid(in, '?');
    EXPECT_EQ(17U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));

    // same string, but utf-8 clean
    in = "Трудно быть Богом"sv;
    out = tr_strv_replace_invalid(in);
    EXPECT_NE(0U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));
    EXPECT_EQ(in, out);

    // https://trac.transmissionbt.com/ticket/6064
    // This was a fuzzer-generated string that crashed Transmission.
    // Even invalid strings shouldn't cause a crash.
    in = "\xF4\x00\x81\x82"sv;
    out = tr_strv_replace_invalid(in);
    EXPECT_NE(0U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));

    in = "\xF4\x33\x81\x82"sv;
    out = tr_strv_replace_invalid(in, '?');
    EXPECT_NE(nullptr, out.data());
    EXPECT_EQ(4U, std::size(out));
    EXPECT_EQ(out, tr_strv_replace_invalid(out));
}

TEST_F(UtilsTest, strvConvertUtf8Fuzz)
{
    auto buf = std::vector<char>{};
    for (size_t i = 0; i < 1000; ++i)
    {
        buf.resize(tr_rand_int(4096U));
        tr_rand_buffer(std::data(buf), std::size(buf));
        auto const out = tr_strv_to_utf8_string({ std::data(buf), std::size(buf) });
        EXPECT_EQ(out, tr_strv_to_utf8_string(out));
    }
}

TEST_F(UtilsTest, trStrlower)
{
    EXPECT_EQ(""sv, tr_strlower(""sv));
    EXPECT_EQ("apple"sv, tr_strlower("APPLE"sv));
    EXPECT_EQ("apple"sv, tr_strlower("Apple"sv));
    EXPECT_EQ("apple"sv, tr_strlower("aPPLe"sv));
    EXPECT_EQ("apple"sv, tr_strlower("applE"sv));
    EXPECT_EQ("hello"sv, tr_strlower("HELLO"sv));
    EXPECT_EQ("hello"sv, tr_strlower("hello"sv));
}
