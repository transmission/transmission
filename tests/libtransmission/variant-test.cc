// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_VARIANT_MODULE

#include <libtransmission/transmission.h>

#include <libtransmission/benc.h>
#include <libtransmission/crypto-utils.h> // tr_rand_buffer(), tr_rand_int()
#include <libtransmission/error.h>
#include <libtransmission/variant-common.h>
#include <libtransmission/variant.h>

#include <algorithm>
#include <array>
#include <cmath> // lrint()
#include <cctype> // isspace()
#include <string>
#include <string_view>

#include "gtest/gtest.h"

using namespace std::literals;

class VariantTest : public ::testing::Test
{
protected:
    static std::string stripWhitespace(std::string const& in)
    {
        auto s = in;
        s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), ::isspace));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), ::isspace).base(), s.end());
        return s;
    }
};

#ifndef _WIN32
#define STACK_SMASH_DEPTH (1 * 1000 * 1000)
#else
#define STACK_SMASH_DEPTH (100 * 1000)
#endif

TEST_F(VariantTest, getType)
{
    auto i = int64_t{};
    auto b = bool{};
    auto d = double{};
    auto sv = std::string_view{};
    auto v = tr_variant{};

    tr_variantInitInt(&v, 30);
    EXPECT_TRUE(tr_variantGetInt(&v, &i));
    EXPECT_EQ(30, i);
    EXPECT_TRUE(tr_variantGetReal(&v, &d));
    EXPECT_EQ(30, int(d));
    EXPECT_FALSE(tr_variantGetBool(&v, &b));
    EXPECT_FALSE(tr_variantGetStrView(&v, &sv));

    auto strkey = "foo"sv;
    tr_variantInitStr(&v, strkey);
    EXPECT_FALSE(tr_variantGetBool(&v, &b));
    EXPECT_TRUE(tr_variantGetStrView(&v, &sv));
    EXPECT_EQ(strkey, sv);
    EXPECT_NE(std::data(strkey), std::data(sv));

    strkey = "anything"sv;
    tr_variantInitStrView(&v, strkey);
    EXPECT_TRUE(tr_variantGetStrView(&v, &sv));
    EXPECT_EQ(strkey, sv);
    EXPECT_EQ(std::data(strkey), std::data(sv)); // literally the same memory
    EXPECT_EQ(std::size(strkey), std::size(sv));

    strkey = "true"sv;
    tr_variantInitStr(&v, strkey);
    EXPECT_TRUE(tr_variantGetBool(&v, &b));
    EXPECT_TRUE(b);
    EXPECT_TRUE(tr_variantGetStrView(&v, &sv));
    EXPECT_EQ(strkey, sv);

    strkey = "false"sv;
    tr_variantInitStr(&v, strkey);
    EXPECT_TRUE(tr_variantGetBool(&v, &b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(tr_variantGetStrView(&v, &sv));
    EXPECT_EQ(strkey, sv);
}

TEST_F(VariantTest, parseInt)
{
    auto constexpr Benc = "i64e"sv;
    auto constexpr ExpectVal = int64_t{ 64 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ(ExpectVal, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithMissingEnd)
{
    auto constexpr Benc = "i64"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntEmptyBuffer)
{
    auto constexpr Benc = ""sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithBadDigits)
{
    auto constexpr Benc = "i6z4e"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseNegativeInt)
{
    auto constexpr Benc = "i-3e"sv;
    auto constexpr Expected = int64_t{ -3 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ(Expected, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseNegativeWithLeadingZero)
{
    auto constexpr Benc = "i-03e"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntZero)
{
    auto constexpr Benc = "i0e"sv;
    auto constexpr Expected = int64_t{ 0 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ(Expected, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithLeadingZero)
{
    auto constexpr Benc = "i04e"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, str)
{
    using namespace transmission::benc::impl;

    // string len is designed to overflow
    auto benc = "99999999999999999999:boat"sv;
    auto inout = benc;
    auto value = ParseString(&inout);
    EXPECT_FALSE(value);
    EXPECT_EQ(benc, inout);

    // good string
    inout = benc = "4:boat";
    value = ParseString(&inout);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ("boat"sv, *value);
    EXPECT_EQ(std::data(benc) + std::size(benc), std::data(inout));

    // string goes past end of buffer
    inout = benc = "4:boa"sv;
    value = ParseString(&inout);
    EXPECT_FALSE(value);
    EXPECT_EQ(benc, inout);

    // empty string
    inout = benc = "0:"sv;
    value = ParseString(&inout);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ(""sv, *value);
    EXPECT_EQ(std::data(benc) + std::size(benc), std::data(inout));

    // short string
    inout = benc = "3:boat";
    value = ParseString(&inout);
    EXPECT_TRUE(value.has_value());
    assert(value.has_value());
    EXPECT_EQ("boa"sv, *value);
    EXPECT_EQ(std::data(benc) + benc.find('t'), std::data(inout));
}

TEST_F(VariantTest, parse)
{
    auto benc = "i64e"sv;
    auto i = int64_t{};
    auto val = tr_variant{};
    char const* end = nullptr;
    auto ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, &end);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(tr_variantGetInt(&val, &i));
    EXPECT_EQ(int64_t(64), i);
    EXPECT_EQ(std::data(benc) + std::size(benc), end);
    tr_variantClear(&val);

    benc = "li64ei32ei16ee"sv;
    ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, &end);
    EXPECT_TRUE(ok);
    EXPECT_EQ(std::data(benc) + std::size(benc), end);
    EXPECT_EQ(size_t{ 3 }, tr_variantListSize(&val));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 0), &i));
    EXPECT_EQ(64, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 1), &i));
    EXPECT_EQ(32, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 2), &i));
    EXPECT_EQ(16, i);
    EXPECT_EQ(benc, tr_variantToStr(&val, TR_VARIANT_FMT_BENC));

    tr_variantClear(&val);
    end = nullptr;

    benc = "lllee"sv;
    ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, &end);
    EXPECT_FALSE(ok);
    EXPECT_EQ(nullptr, end);

    benc = "le"sv;
    EXPECT_TRUE(tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, &end));
    EXPECT_EQ(std::data(benc) + std::size(benc), end);
    EXPECT_EQ(benc, tr_variantToStr(&val, TR_VARIANT_FMT_BENC));
    tr_variantClear(&val);

    benc = "d20:"sv;
    end = nullptr;
    ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, benc, &end);
    EXPECT_FALSE(ok);
    EXPECT_EQ(nullptr, end);
}

TEST_F(VariantTest, bencParseAndReencode)
{
    struct LocalTest
    {
        std::string_view benc;
        bool is_good;
    };

    auto constexpr Tests = std::array<LocalTest, 9>{ {
        { "llleee"sv, true },
        { "d3:cow3:moo4:spam4:eggse"sv, true },
        { "d4:spaml1:a1:bee"sv, true },
        { "d5:greenli1ei2ei3ee4:spamd1:ai123e3:keyi214eee"sv, true },
        { "d9:publisher3:bob17:publisher-webpage15:www.example.com18:publisher.location4:homee"sv, true },
        { "d8:completei1e8:intervali1800e12:min intervali1800e5:peers0:e"sv, true },
        { "d1:ai0e1:be"sv, false }, // odd number of children
        { ""sv, false },
        { " "sv, false },
    } };

    for (auto const& test : Tests)
    {
        tr_variant val;
        char const* end = nullptr;
        auto const is_good = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, test.benc, &end);

        EXPECT_EQ(test.is_good, is_good);
        if (is_good)
        {
            EXPECT_EQ(test.benc.data() + test.benc.size(), end);
            EXPECT_EQ(test.benc, tr_variantToStr(&val, TR_VARIANT_FMT_BENC));
            tr_variantClear(&val);
        }
    }
}

TEST_F(VariantTest, bencSortWhenSerializing)
{
    auto constexpr In = "lld1:bi32e1:ai64eeee"sv;
    auto constexpr ExpectedOut = "lld1:ai64e1:bi32eeee"sv;

    tr_variant val;
    char const* end = nullptr;
    auto const ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, In, &end);
    EXPECT_TRUE(ok);
    EXPECT_EQ(std::data(In) + std::size(In), end);
    EXPECT_EQ(ExpectedOut, tr_variantToStr(&val, TR_VARIANT_FMT_BENC));

    tr_variantClear(&val);
}

TEST_F(VariantTest, bencMalformedTooManyEndings)
{
    auto constexpr In = "leee"sv;
    auto constexpr ExpectedOut = "le"sv;

    tr_variant val;
    char const* end = nullptr;
    auto const ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, In, &end);
    EXPECT_TRUE(ok);
    EXPECT_EQ(std::data(In) + std::size(ExpectedOut), end);
    EXPECT_EQ(ExpectedOut, tr_variantToStr(&val, TR_VARIANT_FMT_BENC));

    tr_variantClear(&val);
}

TEST_F(VariantTest, bencMalformedNoEnding)
{
    auto constexpr In = "l1:a1:b1:c"sv;
    tr_variant val;
    EXPECT_FALSE(tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, In));
}

TEST_F(VariantTest, bencMalformedIncompleteString)
{
    auto constexpr In = "1:"sv;
    tr_variant val;
    EXPECT_FALSE(tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, In));
}

TEST_F(VariantTest, bencToJson)
{
    struct LocalTest
    {
        std::string_view benc;
        std::string_view expected;
    };

    auto constexpr Tests = std::array<LocalTest, 5>{
        { { "i6e"sv, "6"sv },
          { "d5:helloi1e5:worldi2ee"sv, R"({"hello":1,"world":2})"sv },
          { "d5:helloi1e5:worldi2e3:fooli1ei2ei3eee"sv, R"({"foo":[1,2,3],"hello":1,"world":2})"sv },
          { "d5:helloi1e5:worldi2e3:fooli1ei2ei3ed1:ai0eeee"sv, R"({"foo":[1,2,3,{"a":0}],"hello":1,"world":2})"sv },
          { "d4:argsd6:statusle7:status2lee6:result7:successe"sv,
            R"({"args":{"status":[],"status2":[]},"result":"success"})"sv } }
    };

    for (auto const& test : Tests)
    {
        tr_variant top;
        tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, test.benc);

        auto const str = tr_variantToStr(&top, TR_VARIANT_FMT_JSON_LEAN);
        EXPECT_EQ(test.expected, stripWhitespace(str));
        tr_variantClear(&top);
    }
}

TEST_F(VariantTest, merge)
{
    auto const i1 = tr_quark_new("i1"sv);
    auto const i2 = tr_quark_new("i2"sv);
    auto const i3 = tr_quark_new("i3"sv);
    auto const i4 = tr_quark_new("i4"sv);
    auto const s5 = tr_quark_new("s5"sv);
    auto const s6 = tr_quark_new("s6"sv);
    auto const s7 = tr_quark_new("s7"sv);
    auto const s8 = tr_quark_new("s8"sv);

    /* initial dictionary (default values) */
    tr_variant dest;
    tr_variantInitDict(&dest, 10);
    tr_variantDictAddInt(&dest, i1, 1);
    tr_variantDictAddInt(&dest, i2, 2);
    tr_variantDictAddInt(&dest, i4, -35); /* remains untouched */
    tr_variantDictAddStrView(&dest, s5, "abc");
    tr_variantDictAddStrView(&dest, s6, "def");
    tr_variantDictAddStrView(&dest, s7, "127.0.0.1"); /* remains untouched */

    /* new dictionary, will overwrite items in dest */
    tr_variant src;
    tr_variantInitDict(&src, 10);
    tr_variantDictAddInt(&src, i1, 1); /* same value */
    tr_variantDictAddInt(&src, i2, 4); /* new value */
    tr_variantDictAddInt(&src, i3, 3); /* new key:value */
    tr_variantDictAddStrView(&src, s5, "abc"); /* same value */
    tr_variantDictAddStrView(&src, s6, "xyz"); /* new value */
    tr_variantDictAddStrView(&src, s8, "ghi"); /* new key:value */

    tr_variantMergeDicts(&dest, /*const*/ &src);

    auto i = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i1, &i));
    EXPECT_EQ(1, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i2, &i));
    EXPECT_EQ(4, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i3, &i));
    EXPECT_EQ(3, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i4, &i));
    EXPECT_EQ(-35, i);
    auto sv = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&dest, s5, &sv));
    EXPECT_EQ("abc"sv, sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&dest, s6, &sv));
    EXPECT_EQ("xyz"sv, sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&dest, s7, &sv));
    EXPECT_EQ("127.0.0.1"sv, sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&dest, s8, &sv));
    EXPECT_EQ("ghi"sv, sv);

    tr_variantClear(&dest);
    tr_variantClear(&src);
}

TEST_F(VariantTest, stackSmash)
{
    // make a nested list of list of lists.
    int constexpr Depth = STACK_SMASH_DEPTH;
    std::string const in = std::string(Depth, 'l') + std::string(Depth, 'e');

    // confirm that it fails instead of crashing
    char const* end = nullptr;
    tr_variant val;
    tr_error* error = nullptr;
    auto ok = tr_variantFromBuf(&val, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, in, &end, &error);
    EXPECT_NE(nullptr, error);
    EXPECT_EQ(E2BIG, error->code);
    EXPECT_FALSE(ok);

    tr_error_clear(&error);
}

TEST_F(VariantTest, boolAndIntRecast)
{
    auto const key1 = tr_quark_new("key1"sv);
    auto const key2 = tr_quark_new("key2"sv);
    auto const key3 = tr_quark_new("key3"sv);
    auto const key4 = tr_quark_new("key4"sv);

    auto top = tr_variant{};
    tr_variantInitDict(&top, 10);
    tr_variantDictAddBool(&top, key1, false);
    tr_variantDictAddBool(&top, key2, 0); // NOLINT modernize-use-bool-literals
    tr_variantDictAddInt(&top, key3, true); // NOLINT readability-implicit-bool-conversion
    tr_variantDictAddInt(&top, key4, 1);

    // confirm we can read both bools and ints as bools
    auto b = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&top, key1, &b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key2, &b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key3, &b));
    EXPECT_TRUE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key4, &b));
    EXPECT_TRUE(b);

    // confirm we can read both bools and ints as ints
    auto i = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&top, key1, &i));
    EXPECT_EQ(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key2, &i));
    EXPECT_EQ(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key3, &i));
    EXPECT_NE(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key4, &i));
    EXPECT_NE(0, i);

    tr_variantClear(&top);
}

TEST_F(VariantTest, dictFindType)
{
    auto constexpr ExpectedStr = "this-is-a-string"sv;
    auto constexpr ExpectedBool = bool{ true };
    auto constexpr ExpectedInt = int{ 1234 };
    auto constexpr ExpectedReal = double{ 0.3 };

    auto const key_bool = tr_quark_new("this-is-a-bool"sv);
    auto const key_real = tr_quark_new("this-is-a-real"sv);
    auto const key_int = tr_quark_new("this-is-an-int"sv);
    auto const key_str = tr_quark_new("this-is-a-string"sv);
    auto const key_unknown = tr_quark_new("this-is-a-missing-entry"sv);

    // populate a dict
    tr_variant top;
    tr_variantInitDict(&top, 0);
    tr_variantDictAddBool(&top, key_bool, ExpectedBool);
    tr_variantDictAddInt(&top, key_int, ExpectedInt);
    tr_variantDictAddReal(&top, key_real, ExpectedReal);
    tr_variantDictAddStr(&top, key_str, ExpectedStr.data());

    // look up the keys as strings
    auto sv = std::string_view{};
    EXPECT_FALSE(tr_variantDictFindStrView(&top, key_bool, &sv));
    EXPECT_FALSE(tr_variantDictFindStrView(&top, key_real, &sv));
    EXPECT_FALSE(tr_variantDictFindStrView(&top, key_int, &sv));
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key_str, &sv));
    EXPECT_EQ(ExpectedStr, sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key_str, &sv));
    EXPECT_EQ(ExpectedStr, sv);
    EXPECT_FALSE(tr_variantDictFindStrView(&top, key_unknown, &sv));
    EXPECT_FALSE(tr_variantDictFindStrView(&top, key_unknown, &sv));

    // look up the keys as bools
    auto b = bool{};
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_int, &b));
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_real, &b));
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_str, &b));
    EXPECT_TRUE(tr_variantDictFindBool(&top, key_bool, &b));
    EXPECT_EQ(ExpectedBool, b);

    // look up the keys as doubles
    auto d = double{};
    EXPECT_FALSE(tr_variantDictFindReal(&top, key_bool, &d));
    EXPECT_TRUE(tr_variantDictFindReal(&top, key_int, &d));
    EXPECT_EQ(ExpectedInt, std::lrint(d));
    EXPECT_FALSE(tr_variantDictFindReal(&top, key_str, &d));
    EXPECT_TRUE(tr_variantDictFindReal(&top, key_real, &d));
    EXPECT_EQ(std::lrint(ExpectedReal * 100), std::lrint(d * 100));

    // look up the keys as ints
    auto i = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&top, key_bool, &i));
    EXPECT_EQ(ExpectedBool ? 1 : 0, i);
    EXPECT_FALSE(tr_variantDictFindInt(&top, key_real, &i));
    EXPECT_FALSE(tr_variantDictFindInt(&top, key_str, &i));
    EXPECT_TRUE(tr_variantDictFindInt(&top, key_int, &i));
    EXPECT_EQ(ExpectedInt, i);

    tr_variantClear(&top);
}

TEST_F(VariantTest, variantFromBufFuzz)
{
    auto buf = std::vector<char>{};

    for (size_t i = 0; i < 100000; ++i)
    {
        buf.resize(tr_rand_int(4096U));
        tr_rand_buffer(std::data(buf), std::size(buf));
        // std::cerr << '[' << tr_base64_encode({ std::data(buf), std::size(buf) }) << ']' << std::endl;

        if (auto top = tr_variant{};
            tr_variantFromBuf(&top, TR_VARIANT_PARSE_JSON | TR_VARIANT_PARSE_INPLACE, buf, nullptr, nullptr))
        {
            tr_variantClear(&top);
        }

        if (auto top = tr_variant{};
            tr_variantFromBuf(&top, TR_VARIANT_PARSE_BENC | TR_VARIANT_PARSE_INPLACE, buf, nullptr, nullptr))
        {
            tr_variantClear(&top);
        }
    }
}
