/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"
#include "utils.h" /* tr_free */
#include "variant-common.h"
#include "variant.h"

#include <algorithm>
#include <array>
#include <cmath> // lrint()
#include <cctype> // isspace()
#include <string>

#include "gtest/gtest.h"

class VariantTest : public ::testing::Test
{
protected:
    std::string stripWhitespace(std::string const& in)
    {
        auto s = in;
        s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), ::isspace));
        s.erase(std::find_if_not(s.rbegin(), s.rend(), ::isspace).base(), s.end());
        return s;
    }

    auto bencParseInt(std::string const& in, uint8_t const** end, int64_t* val)
    {
        return tr_bencParseInt(in.data(),
            in.data() + in.size(),
            end, val);
    }
};

#ifndef _WIN32
# define STACK_SMASH_DEPTH (1 * 1000 * 1000)
#else
# define STACK_SMASH_DEPTH (100 * 1000)
#endif

TEST_F(VariantTest, parseInt)
{
    auto const in = std::string { "i64e" };
    auto constexpr InitVal = int64_t { 888 };
    auto constexpr ExpectVal = int64_t { 64 };

    uint8_t const* end = {};
    auto val = int64_t { InitVal };
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(0, err);
    EXPECT_EQ(ExpectVal, val);
    EXPECT_EQ(reinterpret_cast<decltype(end)>(in.data() + in.size()), end);
}

TEST_F(VariantTest, parseIntWithMissingEnd)
{
    auto const in = std::string { "i64" };
    auto constexpr InitVal = int64_t { 888 };

    uint8_t const* end = {};
    auto val = int64_t { InitVal };
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(EILSEQ, err);
    EXPECT_EQ(InitVal, val);
    EXPECT_EQ(nullptr, end);
}

TEST_F(VariantTest, parseIntEmptyBuffer)
{
    auto const in = std::string {};
    auto constexpr InitVal = int64_t { 888 };

    uint8_t const* end = {};
    auto val = int64_t { InitVal };
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(EILSEQ, err);
    EXPECT_EQ(InitVal, val);
    EXPECT_EQ(nullptr, end);
}

TEST_F(VariantTest, parseIntWithBadDigits)
{
    auto const in = std::string { "i6z4e" };
    auto constexpr InitVal = int64_t { 888 };

    uint8_t const* end = {};
    auto val = int64_t { InitVal };
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(EILSEQ, err);
    EXPECT_EQ(InitVal, val);
    EXPECT_EQ(nullptr, end);
}

TEST_F(VariantTest, parseNegativeInt)
{
    auto const in = std::string { "i-3e" };

    uint8_t const* end = {};
    auto val = int64_t {};
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(0, err);
    EXPECT_EQ(-3, val);
    EXPECT_EQ(reinterpret_cast<decltype(end)>(in.data() + in.size()), end);
}

TEST_F(VariantTest, parseIntZero)
{
    auto const in = std::string { "i0e" };

    uint8_t const* end = {};
    auto val = int64_t {};
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(0, err);
    EXPECT_EQ(0, val);
    EXPECT_EQ(reinterpret_cast<decltype(end)>(in.data() + in.size()), end);
}

TEST_F(VariantTest, parseIntWithLeadingZero)
{
    auto const in = std::string { "i04e" };
    auto constexpr InitVal = int64_t { 888 };

    uint8_t const* end = {};
    auto val = int64_t { InitVal };
    auto const err = bencParseInt(in, &end, &val);
    EXPECT_EQ(EILSEQ, err); // no leading zeroes allowed
    EXPECT_EQ(InitVal, val);
    EXPECT_EQ(nullptr, end);
}

TEST_F(VariantTest, str)
{
    auto buf = std::array<uint8_t, 128>{};
    int err;
    int n;
    uint8_t const* end;
    uint8_t const* str;
    size_t len;

    // string len is designed to overflow
    n = tr_snprintf(buf.data(), buf.size(), "%zu:boat", size_t(SIZE_MAX - 2));
    err = tr_bencParseStr(&buf[0], &buf[n], &end, &str, &len);
    EXPECT_EQ(EILSEQ, err);
    EXPECT_EQ(size_t{}, len);
    EXPECT_EQ(nullptr, str);
    EXPECT_EQ(nullptr, end);

    // good string
    n = tr_snprintf(buf.data(), buf.size(), "4:boat");
    err = tr_bencParseStr(&buf[0], &buf[n], &end, &str, &len);
    EXPECT_EQ(0, err);
    EXPECT_EQ(size_t{ 4 }, len);
    EXPECT_EQ(0, memcmp("boat", str, len));
    EXPECT_EQ(buf.data() + n, end);
    str = nullptr;
    end = nullptr;
    len = 0;

    // string goes past end of buffer
    err = tr_bencParseStr(&buf[0], &buf[n - 1], &end, &str, &len);
    EXPECT_EQ(EILSEQ, err);
    EXPECT_EQ(size_t{}, len);
    EXPECT_EQ(nullptr, str);
    EXPECT_EQ(nullptr, end);

    // empty string
    n = tr_snprintf(buf.data(), buf.size(), "0:");
    err = tr_bencParseStr(&buf[0], &buf[n], &end, &str, &len);
    EXPECT_EQ(0, err);
    EXPECT_EQ(size_t{}, len);
    EXPECT_EQ('\0', *str);
    EXPECT_EQ(buf.data() + n, end);
    str = nullptr;
    end = nullptr;
    len = 0;

    // short string
    n = tr_snprintf(buf.data(), buf.size(), "3:boat");
    err = tr_bencParseStr(&buf[0], &buf[n], &end, &str, &len);
    EXPECT_EQ(0, err);
    EXPECT_EQ(size_t{ 3 }, len);
    EXPECT_EQ(0, memcmp("boa", str, len));
    EXPECT_EQ(buf.data() + 5, end);
    str = nullptr;
    end = nullptr;
    len = 0;
}

TEST_F(VariantTest, parse)
{
    auto buf = std::array<uint8_t, 512>{};
    int64_t i;

    tr_variant val;
    char const* end;
    auto n = tr_snprintf(buf.data(), buf.size(), "i64e");
    auto err = tr_variantFromBencFull(&val, buf.data(), n, nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantGetInt(&val, &i));
    EXPECT_EQ(int64_t(64), i);
    EXPECT_EQ(reinterpret_cast<char const*>(buf.data()) + n, end);
    tr_variantFree(&val);

    n = tr_snprintf(buf.data(), buf.size(), "li64ei32ei16ee");
    err = tr_variantFromBencFull(&val, buf.data(), n, nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_EQ(reinterpret_cast<char const*>(&buf[n]), end);
    EXPECT_EQ(size_t{ 3 }, tr_variantListSize(&val));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 0), &i));
    EXPECT_EQ(64, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 1), &i));
    EXPECT_EQ(32, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(&val, 2), &i));
    EXPECT_EQ(16, i);

    size_t len;
    auto* saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &len);
    EXPECT_EQ(static_cast<size_t>(n), len);
    EXPECT_STREQ(reinterpret_cast<char const*>(buf.data()), saved);
    tr_free(saved);

    tr_variantFree(&val);
    end = nullptr;

    n = tr_snprintf(buf.data(), buf.size(), "lllee");
    err = tr_variantFromBencFull(&val, buf.data(), n, nullptr, &end);
    EXPECT_NE(0, err);
    EXPECT_EQ(nullptr, end);

    end = nullptr;
    n = tr_snprintf(buf.data(), buf.size(), "le");
    err = tr_variantFromBencFull(&val, buf.data(), n, nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_EQ(reinterpret_cast<char const*>(&buf[n]), end);

    saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &len);
    EXPECT_EQ(static_cast<size_t>(n), len);
    EXPECT_STREQ("le", saved);
    tr_free(saved);
    tr_variantFree(&val);
}

TEST_F(VariantTest, bencParseAndReencode) {
    struct Test
    {
        std::string benc;
        bool is_good;
    };

    auto const tests = std::array<Test, 9>{
        Test{ "llleee", true },
        { "d3:cow3:moo4:spam4:eggse", true },
        { "d4:spaml1:a1:bee", true },
        { "d5:greenli1ei2ei3ee4:spamd1:ai123e3:keyi214eee", true },
        { "d9:publisher3:bob17:publisher-webpage15:www.example.com18:publisher.location4:homee", true },
        { "d8:completei1e8:intervali1800e12:min intervali1800e5:peers0:e", true },
        { "d1:ai0e1:be", false }, // odd number of children
        { "", false },
        { " ", false }
    };

    for (const auto& test : tests)
    {
        tr_variant val;
        char const* end = nullptr;
        auto const err = tr_variantFromBencFull(&val, test.benc.data(), test.benc.size(), nullptr, &end);
        if (!test.is_good)
        {
            EXPECT_NE(0, err);
        }
        else
        {
            EXPECT_EQ(0, err);
            EXPECT_EQ(test.benc.data() + test.benc.size(), end);
            auto saved_len = size_t{};
            auto* saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &saved_len);
            EXPECT_EQ(test.benc, std::string(saved, saved_len));
            tr_free(saved);
            tr_variantFree(&val);
        }
    }
}

TEST_F(VariantTest, bencSortWhenSerializing)
{
    auto const in = std::string { "lld1:bi32e1:ai64eeee" };
    auto const expected_out = std::string { "lld1:ai64e1:bi32eeee" };

    tr_variant val;
    char const* end;
    auto const err = tr_variantFromBencFull(&val, in.data(), in.size(), nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_EQ(reinterpret_cast<decltype(end)>(in.data() + in.size()), end);

    auto len = size_t{};
    auto* saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &len);
    EXPECT_EQ(expected_out, std::string(saved, len));
    tr_free(saved);

    tr_variantFree(&val);
}

TEST_F(VariantTest, bencMalformedTooManyEndings)
{
    auto const in = std::string { "leee" };
    auto const expected_out = std::string { "le" };

    tr_variant val;
    char const* end;
    auto const err = tr_variantFromBencFull(&val, in.data(), in.size(), nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_EQ(in.data() + expected_out.size(), end);

    auto len = size_t{};
    auto* saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &len);
    EXPECT_EQ(expected_out, std::string(saved, len));
    tr_free(saved);

    tr_variantFree(&val);
}

TEST_F(VariantTest, bencMalformedNoEnding)
{
    auto const in = std::string { "l1:a1:b1:c" };
    tr_variant val;
    EXPECT_EQ(EILSEQ, tr_variantFromBenc(&val, in.data(), in.size()));
}

TEST_F(VariantTest, bencMalformedIncompleteString)
{
    auto const in = std::string { "1:" };
    tr_variant val;
    EXPECT_EQ(EILSEQ, tr_variantFromBenc(&val, in.data(), in.size()));
}

TEST_F(VariantTest, bencToJson)
{
    struct Test
    {
        std::string benc;
        std::string expected;
    };

    auto const tests = std::array<Test, 5>{
        Test{ "i6e", "6" },
        { "d5:helloi1e5:worldi2ee", R"({"hello":1,"world":2})" },
        { "d5:helloi1e5:worldi2e3:fooli1ei2ei3eee", R"({"foo":[1,2,3],"hello":1,"world":2})" },
        { "d5:helloi1e5:worldi2e3:fooli1ei2ei3ed1:ai0eeee", R"({"foo":[1,2,3,{"a":0}],"hello":1,"world":2})" },
        { "d4:argsd6:statusle7:status2lee6:result7:successe", R"({"args":{"status":[],"status2":[]},"result":"success"})" }
    };

    for (auto const& test : tests)
    {
        tr_variant top;
        tr_variantFromBenc(&top, test.benc.data(), test.benc.size());

        auto len = size_t{};
        auto* str = tr_variantToStr(&top, TR_VARIANT_FMT_JSON_LEAN, &len);
        EXPECT_EQ(test.expected, stripWhitespace(std::string(str, len)));
        tr_free(str);
        tr_variantFree(&top);
    }
}

TEST_F(VariantTest, merge)
{
    auto const i1 = tr_quark_new("i1", 2);
    auto const i2 = tr_quark_new("i2", 2);
    auto const i3 = tr_quark_new("i3", 2);
    auto const i4 = tr_quark_new("i4", 2);
    auto const s5 = tr_quark_new("s5", 2);
    auto const s6 = tr_quark_new("s6", 2);
    auto const s7 = tr_quark_new("s7", 2);
    auto const s8 = tr_quark_new("s8", 2);

    /* initial dictionary (default values) */
    tr_variant dest;
    tr_variantInitDict(&dest, 10);
    tr_variantDictAddInt(&dest, i1, 1);
    tr_variantDictAddInt(&dest, i2, 2);
    tr_variantDictAddInt(&dest, i4, -35); /* remains untouched */
    tr_variantDictAddStr(&dest, s5, "abc");
    tr_variantDictAddStr(&dest, s6, "def");
    tr_variantDictAddStr(&dest, s7, "127.0.0.1"); /* remains untouched */

    /* new dictionary, will overwrite items in dest */
    tr_variant src;
    tr_variantInitDict(&src, 10);
    tr_variantDictAddInt(&src, i1, 1); /* same value */
    tr_variantDictAddInt(&src, i2, 4); /* new value */
    tr_variantDictAddInt(&src, i3, 3); /* new key:value */
    tr_variantDictAddStr(&src, s5, "abc"); /* same value */
    tr_variantDictAddStr(&src, s6, "xyz"); /* new value */
    tr_variantDictAddStr(&src, s8, "ghi"); /* new key:value */

    tr_variantMergeDicts(&dest, /*const*/ &src);

    int64_t i;
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i1, &i));
    EXPECT_EQ(1, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i2, &i));
    EXPECT_EQ(4, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i3, &i));
    EXPECT_EQ(3, i);
    EXPECT_TRUE(tr_variantDictFindInt(&dest, i4, &i));
    EXPECT_EQ(-35, i);
    size_t len;
    char const* s;
    EXPECT_TRUE(tr_variantDictFindStr(&dest, s5, &s, &len));
    EXPECT_EQ(size_t{ 3 }, len);
    EXPECT_STREQ("abc", s);
    EXPECT_TRUE(tr_variantDictFindStr(&dest, s6, &s, &len));
    EXPECT_EQ(size_t{ 3 }, len);
    EXPECT_STREQ("xyz", s);
    EXPECT_TRUE(tr_variantDictFindStr(&dest, s7, &s, &len));
    EXPECT_EQ(size_t{ 9 }, len);
    EXPECT_STREQ("127.0.0.1", s);
    EXPECT_TRUE(tr_variantDictFindStr(&dest, s8, &s, &len));
    EXPECT_EQ(size_t{ 3 }, len);
    EXPECT_STREQ("ghi", s);

    tr_variantFree(&dest);
    tr_variantFree(&src);
}

TEST_F(VariantTest, stackSmash)
{
    // make a nested list of list of lists.
    int constexpr Depth = STACK_SMASH_DEPTH;
    std::string const in = std::string(Depth, 'l') + std::string(Depth, 'e');

    // confirm that it parses
    char const* end;
    tr_variant val;
    auto err = tr_variantFromBencFull(&val, in.data(), in.size(), nullptr, &end);
    EXPECT_EQ(0, err);
    EXPECT_EQ(in.data() + in.size(), end);

    // confirm that we can serialize it back again
    size_t len;
    auto* saved = tr_variantToStr(&val, TR_VARIANT_FMT_BENC, &len);
    EXPECT_EQ(in, std::string(saved, len));
    tr_free(saved);

    tr_variantFree(&val);
}

TEST_F(VariantTest, boolAndIntRecast)
{
    auto const key1 = tr_quark_new("key1", 4);
    auto const key2 = tr_quark_new("key2", 4);
    auto const key3 = tr_quark_new("key3", 4);
    auto const key4 = tr_quark_new("key4", 4);

    tr_variant top;
    tr_variantInitDict(&top, 10);
    tr_variantDictAddBool(&top, key1, false);
    tr_variantDictAddBool(&top, key2, 0); // NOLINT modernize-use-bool-literals
    tr_variantDictAddInt(&top, key3, true);
    tr_variantDictAddInt(&top, key4, 1);

    // confirm we can read both bools and ints as bools
    bool b;
    EXPECT_TRUE(tr_variantDictFindBool(&top, key1, &b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key2, &b));
    EXPECT_FALSE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key3, &b));
    EXPECT_TRUE(b);
    EXPECT_TRUE(tr_variantDictFindBool(&top, key4, &b));
    EXPECT_TRUE(b);

    // confirm we can read both bools and ints as ints
    int64_t i;
    EXPECT_TRUE(tr_variantDictFindInt(&top, key1, &i));
    EXPECT_EQ(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key2, &i));
    EXPECT_EQ(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key3, &i));
    EXPECT_NE(0, i);
    EXPECT_TRUE(tr_variantDictFindInt(&top, key4, &i));
    EXPECT_NE(0, i);

    tr_variantFree(&top);
}

TEST_F(VariantTest, dictFindType)
{
    auto const expected_str = std::string { "this-is-a-string" };
    auto const expected_bool = bool{ true };
    auto const expected_int = int{ 1234 };
    auto const expected_real = double{ 0.3 };

    auto const key_bool = tr_quark_new("this-is-a-bool", TR_BAD_SIZE);
    auto const key_real = tr_quark_new("this-is-a-real", TR_BAD_SIZE);
    auto const key_int = tr_quark_new("this-is-an-int", TR_BAD_SIZE);
    auto const key_str = tr_quark_new("this-is-a-string", TR_BAD_SIZE);

    // populate a dict
    tr_variant top;
    tr_variantInitDict(&top, 0);
    tr_variantDictAddBool(&top, key_bool, expected_bool);
    tr_variantDictAddInt(&top, key_int, expected_int);
    tr_variantDictAddReal(&top, key_real, expected_real);
    tr_variantDictAddStr(&top, key_str, expected_str.data());

    // look up the keys as strings
    char const* str = {};
    auto len = size_t{};
    EXPECT_FALSE(tr_variantDictFindStr(&top, key_bool, &str, &len));
    EXPECT_FALSE(tr_variantDictFindStr(&top, key_real, &str, &len));
    EXPECT_FALSE(tr_variantDictFindStr(&top, key_int, &str, &len));
    EXPECT_TRUE(tr_variantDictFindStr(&top, key_str, &str, &len));
    EXPECT_EQ(expected_str, std::string(str, len));

    // look up the keys as bools
    auto b = bool{};
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_int, &b));
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_real, &b));
    EXPECT_FALSE(tr_variantDictFindBool(&top, key_str, &b));
    EXPECT_TRUE(tr_variantDictFindBool(&top, key_bool, &b));
    EXPECT_EQ(expected_bool, b);

    // look up the keys as doubles
    auto d = double{};
    EXPECT_FALSE(tr_variantDictFindReal(&top, key_bool, &d));
    EXPECT_TRUE(tr_variantDictFindReal(&top, key_int, &d));
    EXPECT_EQ(expected_int, std::lrint(d));
    EXPECT_FALSE(tr_variantDictFindReal(&top, key_str, &d));
    EXPECT_TRUE(tr_variantDictFindReal(&top, key_real, &d));
    EXPECT_EQ(std::lrint(expected_real * 100), std::lrint(d * 100));

    // look up the keys as ints
    auto i = int64_t {};
    EXPECT_TRUE(tr_variantDictFindInt(&top, key_bool, &i));
    EXPECT_EQ(expected_bool ? 1 : 0, i);
    EXPECT_FALSE(tr_variantDictFindInt(&top, key_real, &i));
    EXPECT_FALSE(tr_variantDictFindInt(&top, key_str, &i));
    EXPECT_TRUE(tr_variantDictFindInt(&top, key_int, &i));
    EXPECT_EQ(expected_int, i);

    tr_variantFree(&top);
}
