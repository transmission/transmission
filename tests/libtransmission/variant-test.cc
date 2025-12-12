// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cerrno>
#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <map>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#define LIBTRANSMISSION_VARIANT_MODULE

#include <libtransmission/benc.h>
#include <libtransmission/crypto-utils.h> // tr_rand_buffer(), tr_rand_int()
#include <libtransmission/error.h>
#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace
{

template<class... Ts>
struct Overloaded : Ts...
{
    using Ts::operator()...;
};

template<class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

} // namespace

class VariantTest : public ::testing::Test
{
protected:
    static void expectVariantMatchesQuark(tr_quark key);

    [[nodiscard]] static tr_variant parseJson(std::string_view json)
    {
        auto serde = tr_variant_serde::json();
        serde.inplace();
        if (auto var = serde.parse(json))
        {
            return std::move(*var);
        }

        ADD_FAILURE() << "Failed to parse JSON: " << json;
        return {};
    }
};

#ifndef _WIN32
#define STACK_SMASH_DEPTH (1 * 1000 * 1000)
#else
#define STACK_SMASH_DEPTH (100 * 1000)
#endif

TEST_F(VariantTest, getType)
{
    auto v = tr_variant{};

    v = 30;
    auto i = v.value_if<int64_t>();
    ASSERT_TRUE(i);
    EXPECT_EQ(30, *i);
    auto d = v.value_if<double>();
    ASSERT_TRUE(d);
    EXPECT_EQ(30, static_cast<int>(*d));
    EXPECT_FALSE(v.holds_alternative<bool>());
    EXPECT_FALSE(v.holds_alternative<std::string_view>());

    auto strkey = "foo"sv;
    v = tr_variant{ strkey };
    EXPECT_FALSE(v.holds_alternative<bool>());
    auto sv = v.value_if<std::string_view>();
    ASSERT_TRUE(sv);
    EXPECT_EQ(strkey, *sv);
    EXPECT_NE(std::data(strkey), std::data(*sv));
    EXPECT_EQ(std::size(strkey), std::size(*sv));

    strkey = "anything"sv;
    v = tr_variant::unmanaged_string(strkey);
    sv = v.value_if<std::string_view>();
    ASSERT_TRUE(sv);
    EXPECT_EQ(strkey, *sv);
    EXPECT_EQ(std::data(strkey), std::data(*sv)); // literally the same memory
    EXPECT_EQ(std::size(strkey), std::size(*sv));

    strkey = "true"sv;
    v = tr_variant{ strkey };
    auto b = v.value_if<bool>();
    ASSERT_TRUE(b);
    EXPECT_TRUE(*b);
    sv = v.value_if<std::string_view>();
    ASSERT_TRUE(sv);
    EXPECT_EQ(strkey, *sv);

    strkey = "false"sv;
    v = tr_variant{ strkey };
    b = v.value_if<bool>();
    ASSERT_TRUE(b);
    EXPECT_FALSE(*b);
    sv = v.value_if<std::string_view>();
    ASSERT_TRUE(sv);
    EXPECT_EQ(strkey, *sv);
}

// static
void VariantTest::expectVariantMatchesQuark(tr_quark const key)
{
    auto const key_sv = tr_quark_get_string_view(key);

    auto const var = tr_variant::unmanaged_string(key);
    auto const var_sv = var.value_if<std::string_view>();
    ASSERT_TRUE(var_sv);

    // The strings should not just be equal,
    // but should point to literally the same memory
    EXPECT_EQ(key_sv, *var_sv);
    EXPECT_EQ(std::data(key_sv), std::data(*var_sv));
}

TEST_F(VariantTest, unmanagedStringFromPredefinedQuark)
{
    expectVariantMatchesQuark(TR_KEY_name);
}

TEST_F(VariantTest, unmanagedStringFromNewQuark)
{
    static auto constexpr NewString = "this-string-is-not-already-interned"sv;
    ASSERT_FALSE(tr_quark_lookup(NewString));

    auto const key = tr_quark_new(NewString);
    expectVariantMatchesQuark(key);
}

TEST_F(VariantTest, parseInt)
{
    static auto constexpr Benc = "i64e"sv;
    static auto constexpr ExpectVal = int64_t{ 64 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    ASSERT_TRUE(value);
    EXPECT_EQ(ExpectVal, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithMissingEnd)
{
    static auto constexpr Benc = "i64"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntEmptyBuffer)
{
    static auto constexpr Benc = ""sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithBadDigits)
{
    static auto constexpr Benc = "i6z4e"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseNegativeInt)
{
    static auto constexpr Benc = "i-3e"sv;
    static auto constexpr Expected = int64_t{ -3 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    ASSERT_TRUE(value);
    EXPECT_EQ(Expected, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseNegativeWithLeadingZero)
{
    static auto constexpr Benc = "i-03e"sv;

    auto benc = Benc;
    EXPECT_FALSE(transmission::benc::impl::ParseInt(&benc));
    EXPECT_EQ(std::data(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntZero)
{
    static auto constexpr Benc = "i0e"sv;
    static auto constexpr Expected = int64_t{ 0 };

    auto benc = Benc;
    auto const value = transmission::benc::impl::ParseInt(&benc);
    ASSERT_TRUE(value);
    EXPECT_EQ(Expected, *value);
    EXPECT_EQ(std::data(Benc) + std::size(Benc), std::data(benc));
}

TEST_F(VariantTest, parseIntWithLeadingZero)
{
    static auto constexpr Benc = "i04e"sv;

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
    ASSERT_TRUE(value);
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
    ASSERT_TRUE(value);
    EXPECT_EQ(""sv, *value);
    EXPECT_EQ(std::data(benc) + std::size(benc), std::data(inout));

    // short string
    inout = benc = "3:boat";
    value = ParseString(&inout);
    ASSERT_TRUE(value);
    EXPECT_EQ("boa"sv, *value);
    EXPECT_EQ(std::data(benc) + benc.find('t'), std::data(inout));
}

TEST_F(VariantTest, parse)
{
    auto serde = tr_variant_serde::benc();
    serde.inplace();

    auto benc = "i64e"sv;
    auto var = serde.parse(benc).value_or(tr_variant{});
    auto i = var.value_if<int64_t>();
    ASSERT_TRUE(i);
    EXPECT_EQ(64, *i);
    EXPECT_EQ(std::data(benc) + std::size(benc), serde.end());
    var.clear();

    benc = "li64ei32ei16ee"sv;
    var = serde.parse(benc).value_or(tr_variant{});
    auto* l = var.get_if<tr_variant::Vector>();
    ASSERT_NE(l, nullptr);
    EXPECT_EQ(std::data(benc) + std::size(benc), serde.end());
    ASSERT_EQ(3, std::size(*l));
    i = (*l)[0].value_if<int64_t>();
    ASSERT_TRUE(i);
    EXPECT_EQ(64, *i);
    i = (*l)[1].value_if<int64_t>();
    ASSERT_TRUE(i);
    EXPECT_EQ(32, *i);
    i = (*l)[2].value_if<int64_t>();
    ASSERT_TRUE(i);
    EXPECT_EQ(16, *i);
    EXPECT_EQ(benc, serde.to_string(var));
    var.clear();

    benc = "lllee"sv;
    var = serde.parse(benc).value_or(tr_variant{});
    EXPECT_FALSE(var.has_value());
    EXPECT_EQ(std::data(benc) + std::size(benc), serde.end());
    var.clear();

    benc = "le"sv;
    var = serde.parse(benc).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Vector>());
    EXPECT_EQ(std::data(benc) + std::size(benc), serde.end());
    EXPECT_EQ(benc, serde.to_string(var));
    var.clear();

    benc = "d20:"sv;
    var = serde.parse(benc).value_or(tr_variant{});
    EXPECT_FALSE(var.has_value());
    EXPECT_EQ(std::data(benc) + 1U, serde.end());
}

TEST_F(VariantTest, bencParseAndReencode)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, bool>, 9>{ {
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

    auto serde = tr_variant_serde::benc();
    serde.inplace();

    for (auto const& [benc, is_good] : Tests)
    {
        auto var = serde.parse(benc);
        EXPECT_EQ(is_good, var.has_value());
        if (var)
        {
            EXPECT_EQ(benc.data() + benc.size(), serde.end());
            EXPECT_EQ(benc, serde.to_string(*var));
        }
    }
}

TEST_F(VariantTest, bencSortWhenSerializing)
{
    static auto constexpr In = "lld1:bi32e1:ai64eeee"sv;
    static auto constexpr ExpectedOut = "lld1:ai64e1:bi32eeee"sv;

    auto serde = tr_variant_serde::benc();
    auto var = serde.inplace().parse(In);
    EXPECT_TRUE(var.has_value());
    EXPECT_EQ(std::data(In) + std::size(In), serde.end());
    EXPECT_EQ(ExpectedOut, serde.to_string(*var));
}

TEST_F(VariantTest, bencMalformedTooManyEndings)
{
    static auto constexpr In = "leee"sv;
    static auto constexpr ExpectedOut = "le"sv;

    auto serde = tr_variant_serde::benc();
    auto var = serde.inplace().parse(In);
    EXPECT_TRUE(var.has_value());
    EXPECT_EQ(std::data(In) + std::size(ExpectedOut), serde.end());
    EXPECT_EQ(ExpectedOut, serde.to_string(*var));
}

TEST_F(VariantTest, bencMalformedNoEnding)
{
    static auto constexpr In = "l1:a1:b1:c"sv;

    auto serde = tr_variant_serde::benc();
    auto const var = serde.inplace().parse(In);
    EXPECT_FALSE(var.has_value());
}

TEST_F(VariantTest, bencMalformedIncompleteString)
{
    static auto constexpr In = "1:"sv;

    auto serde = tr_variant_serde::benc();
    auto const var = serde.inplace().parse(In);
    EXPECT_FALSE(var.has_value());
}

TEST_F(VariantTest, bencToJson)
{
    static auto constexpr Tests = std::array<std::pair<std::string_view, std::string_view>, 5>{
        { { "i6e"sv, "6"sv },
          { "d5:helloi1e5:worldi2ee"sv, R"({"hello":1,"world":2})"sv },
          { "d5:helloi1e5:worldi2e3:fooli1ei2ei3eee"sv, R"({"foo":[1,2,3],"hello":1,"world":2})"sv },
          { "d5:helloi1e5:worldi2e3:fooli1ei2ei3ed1:ai0eeee"sv, R"({"foo":[1,2,3,{"a":0}],"hello":1,"world":2})"sv },
          { "d4:argsd6:statusle7:status2lee6:result7:successe"sv,
            R"({"args":{"status":[],"status2":[]},"result":"success"})"sv } }
    };

    auto benc_serde = tr_variant_serde::benc();
    auto json_serde = tr_variant_serde::json();
    benc_serde.inplace();
    json_serde.compact();

    for (auto const& [benc, expected] : Tests)
    {
        auto top = benc_serde.parse(benc).value_or(tr_variant{});
        EXPECT_EQ(expected, json_serde.to_string(top));
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
    auto dest = tr_variant::make_map(6U);
    auto* map = dest.get_if<tr_variant::Map>();
    map->try_emplace(i1, 1);
    map->try_emplace(i2, 2);
    map->try_emplace(i4, -35); /* remains untouched */
    map->try_emplace(s5, "abc");
    map->try_emplace(s6, "def");
    map->try_emplace(s7, "127.0.0.1"); /* remains untouched */

    /* new dictionary, will overwrite items in dest */
    auto src = tr_variant::make_map(6U);
    map = src.get_if<tr_variant::Map>();
    map->try_emplace(i1, 1); /* same value */
    map->try_emplace(i2, 4); /* new value */
    map->try_emplace(i3, 3); /* new key:value */
    map->try_emplace(s5, "abc"); /* same value */
    map->try_emplace(s6, "xyz"); /* new value */
    map->try_emplace(s8, "ghi"); /* new key:value */

    dest.merge(src);

    map = dest.get_if<tr_variant::Map>();
    auto i = map->value_if<int64_t>(i1);
    ASSERT_TRUE(i);
    EXPECT_EQ(1, *i);
    i = map->value_if<int64_t>(i2);
    ASSERT_TRUE(i);
    EXPECT_EQ(4, *i);
    i = map->value_if<int64_t>(i3);
    ASSERT_TRUE(i);
    EXPECT_EQ(3, *i);
    i = map->value_if<int64_t>(i4);
    ASSERT_TRUE(i);
    EXPECT_EQ(-35, *i);
    auto sv = map->value_if<std::string_view>(s5);
    ASSERT_TRUE(sv);
    EXPECT_EQ("abc"sv, *sv);
    sv = map->value_if<std::string_view>(s6);
    ASSERT_TRUE(sv);
    EXPECT_EQ("xyz"sv, *sv);
    sv = map->value_if<std::string_view>(s7);
    ASSERT_TRUE(sv);
    EXPECT_EQ("127.0.0.1"sv, *sv);
    sv = map->value_if<std::string_view>(s8);
    ASSERT_TRUE(sv);
    EXPECT_EQ("ghi"sv, *sv);
}

TEST_F(VariantTest, mergePromotesScalarToMapViaJson)
{
    auto const key = tr_quark_new("nested"sv);

    auto const src = parseJson(R"({"nested":42})"sv);
    auto tgt = tr_variant{};
    tgt.merge(src);

    auto const* const map = tgt.get_if<tr_variant::Map>();
    ASSERT_NE(map, nullptr);
    auto const value = map->value_if<int64_t>(key);
    ASSERT_TRUE(value);
    EXPECT_EQ(42, *value);
}

TEST_F(VariantTest, mergeVectorsResizesAndRecursesViaJson)
{
    auto const key = tr_quark_new("name"sv);

    auto const src = parseJson(R"([7,{"name":"new"},true])"sv);
    auto tgt = parseJson(R"([1,{"name":"old"}])"sv);
    tgt.merge(src);

    auto const* const vec = tgt.get_if<tr_variant::Vector>();
    ASSERT_NE(vec, nullptr);
    ASSERT_EQ(3U, vec->size());
    auto const first = vec->at(0).value_if<int64_t>();
    ASSERT_TRUE(first);
    EXPECT_EQ(7, *first);
    auto const name = vec->at(1).get_if<tr_variant::Map>()->value_if<std::string_view>(key);
    ASSERT_TRUE(name);
    EXPECT_EQ("new"sv, *name);
    auto const flag = vec->at(2).value_if<bool>();
    ASSERT_TRUE(flag);
    EXPECT_TRUE(*flag);
}

TEST_F(VariantTest, mergeScalarsOverwritesRegardlessOfTypeViaJson)
{
    auto tgt = parseJson(R"(true)"sv);

    tgt.merge(parseJson(R"(123)"sv));
    EXPECT_EQ(123, *tgt.value_if<int64_t>());

    tgt.merge(parseJson(R"(4.5)"sv));
    EXPECT_DOUBLE_EQ(4.5, *tgt.value_if<double>());

    tgt.merge(parseJson(R"("foo")"sv));
    auto const sv = tgt.value_if<std::string_view>();
    ASSERT_TRUE(sv);
    EXPECT_EQ("foo"sv, *sv);

    tgt.merge(parseJson(R"(null)"sv));
    EXPECT_TRUE(tgt.holds_alternative<std::nullptr_t>());

    tgt.merge(parseJson(R"({})"sv));
    EXPECT_TRUE(tgt.holds_alternative<tr_variant::Map>());
}

TEST_F(VariantTest, mergeNestedMapsPreservesExistingEntriesViaJson)
{
    auto const config = tr_quark_new("config"sv);

    auto const src = parseJson(R"({"config":{"enabled":false,"address":"localhost"}})"sv);
    auto tgt = parseJson(R"({"config":{"port":51413,"enabled":true}})"sv);
    tgt.merge(src);

    auto const* const cfg = tgt.get_if<tr_variant::Map>()->find_if<tr_variant::Map>(config);
    ASSERT_NE(cfg, nullptr);
    auto const port = cfg->value_if<int64_t>(tr_quark_new("port"sv));
    ASSERT_TRUE(port);
    EXPECT_EQ(51413, *port);
    auto const enabled = cfg->value_if<bool>(tr_quark_new("enabled"sv));
    ASSERT_TRUE(enabled);
    EXPECT_FALSE(*enabled);
    auto const address = cfg->value_if<std::string_view>(tr_quark_new("address"sv));
    ASSERT_TRUE(address);
    EXPECT_EQ("localhost"sv, *address);
}

TEST_F(VariantTest, mergeStringsUpdatesOwnershipViaJson)
{
    // src holds one unmanaged string
    auto constexpr SrcSv = "src"sv;
    auto const src = tr_variant::unmanaged_string(SrcSv);
    ASSERT_EQ(std::data(SrcSv), std::data(src.value_if<std::string_view>().value_or(""sv)));

    // tgt holds a different unmanaged string
    auto constexpr TgtSv = "tgt"sv;
    auto tgt = tr_variant::unmanaged_string(TgtSv);
    ASSERT_EQ(std::data(TgtSv), std::data(tgt.value_if<std::string_view>().value_or(""sv)));

    tgt.merge(src);

    // tgt should now be holding an allocated copy of `SrcSv`.
    // tgt needs its own copy in case it outlives `src`.
    auto const actual = tgt.value_if<std::string_view>().value_or(""sv);
    ASSERT_EQ(SrcSv, actual);
    ASSERT_NE(std::data(SrcSv), std::data(actual));
    ASSERT_NE(std::data(TgtSv), std::data(actual));
}

TEST_F(VariantTest, stackSmash)
{
    // make a nested list of list of lists.
    static int constexpr Depth = STACK_SMASH_DEPTH;
    std::string const in = std::string(Depth, 'l') + std::string(Depth, 'e');

    // confirm that it fails instead of crashing
    auto serde = tr_variant_serde::benc();
    auto var = serde.inplace().parse(in);
    EXPECT_FALSE(var.has_value());
    EXPECT_TRUE(serde.error_);
    EXPECT_EQ(E2BIG, serde.error_.code());
}

TEST_F(VariantTest, boolAndIntRecast)
{
    auto const key1 = tr_quark_new("key1"sv);
    auto const key2 = tr_quark_new("key2"sv);
    auto const key3 = tr_quark_new("key3"sv);
    auto const key4 = tr_quark_new("key4"sv);

    auto top = tr_variant::make_map(4U);
    auto* map = top.get_if<tr_variant::Map>();
    map->try_emplace(key1, false);
    map->try_emplace(key2, 0);
    map->try_emplace(key3, true);
    map->try_emplace(key4, 1);

    // confirm we can read both bools and ints as bools
    auto b = map->value_if<bool>(key1);
    ASSERT_TRUE(b);
    EXPECT_FALSE(*b);
    b = map->value_if<bool>(key2);
    ASSERT_TRUE(b);
    EXPECT_FALSE(*b);
    b = map->value_if<bool>(key3);
    ASSERT_TRUE(b);
    EXPECT_TRUE(*b);
    b = map->value_if<bool>(key4);
    ASSERT_TRUE(b);
    EXPECT_TRUE(*b);

    // confirm we can read both bools and ints as ints
    auto i = map->value_if<int64_t>(key1);
    ASSERT_TRUE(i);
    EXPECT_EQ(0, *i);
    i = map->value_if<int64_t>(key2);
    ASSERT_TRUE(i);
    EXPECT_EQ(0, *i);
    i = map->value_if<int64_t>(key3);
    ASSERT_TRUE(i);
    EXPECT_NE(0, *i);
    i = map->value_if<int64_t>(key4);
    ASSERT_TRUE(i);
    EXPECT_NE(0, *i);
}

TEST_F(VariantTest, dictFindType)
{
    static auto constexpr ExpectedStr = "this-is-a-string"sv;
    static auto constexpr ExpectedBool = true;
    static auto constexpr ExpectedInt = 1234;
    static auto constexpr ExpectedReal = 0.3;

    auto const key_bool = tr_quark_new("this-is-a-bool"sv);
    auto const key_real = tr_quark_new("this-is-a-real"sv);
    auto const key_int = tr_quark_new("this-is-an-int"sv);
    auto const key_str = tr_quark_new("this-is-a-string"sv);
    auto const key_unknown = tr_quark_new("this-is-a-missing-entry"sv);

    // populate a dict
    auto top = tr_variant::make_map(4U);
    auto* map = top.get_if<tr_variant::Map>();
    map->try_emplace(key_bool, ExpectedBool);
    map->try_emplace(key_int, ExpectedInt);
    map->try_emplace(key_real, ExpectedReal);
    map->try_emplace(key_str, ExpectedStr);

    // look up the keys as strings
    EXPECT_FALSE(map->value_if<std::string_view>(key_bool));
    EXPECT_FALSE(map->value_if<std::string_view>(key_real));
    EXPECT_FALSE(map->value_if<std::string_view>(key_int));
    auto sv = map->value_if<std::string_view>(key_str);
    ASSERT_TRUE(sv);
    EXPECT_EQ(ExpectedStr, *sv);
    EXPECT_FALSE(map->value_if<std::string_view>(key_unknown));

    // look up the keys as bools
    EXPECT_FALSE(map->value_if<bool>(key_int));
    EXPECT_FALSE(map->value_if<bool>(key_real));
    EXPECT_FALSE(map->value_if<bool>(key_str));
    auto b = map->value_if<bool>(key_bool);
    ASSERT_TRUE(b);
    EXPECT_EQ(ExpectedBool, b);
    EXPECT_FALSE(map->value_if<bool>(key_unknown));

    // look up the keys as doubles
    EXPECT_FALSE(map->value_if<double>(key_bool));
    auto d = map->value_if<double>(key_int);
    ASSERT_TRUE(d);
    EXPECT_EQ(static_cast<double>(ExpectedInt), *d);
    EXPECT_FALSE(map->value_if<double>(key_str));
    d = map->value_if<double>(key_real);
    ASSERT_TRUE(d);
    EXPECT_EQ(ExpectedReal, *d);

    // look up the keys as ints
    auto i = map->value_if<int64_t>(key_bool);
    ASSERT_TRUE(i);
    EXPECT_EQ(ExpectedBool ? 1 : 0, *i);
    EXPECT_FALSE(map->value_if<int64_t>(key_real));
    EXPECT_FALSE(map->value_if<int64_t>(key_str));
    i = map->value_if<int64_t>(key_int);
    ASSERT_TRUE(i);
    EXPECT_EQ(ExpectedInt, *i);
}

TEST_F(VariantTest, mapContains)
{
    auto const key_bool = tr_quark_new("contains-bool"sv);
    auto const key_int = tr_quark_new("contains-int"sv);
    auto const key_double = tr_quark_new("contains-double"sv);
    auto const key_string = tr_quark_new("contains-string"sv);
    auto const key_vector = tr_quark_new("contains-vector"sv);
    auto const key_map = tr_quark_new("contains-map"sv);
    auto const key_missing = tr_quark_new("contains-missing"sv);
    auto const nested_key = tr_quark_new("contains-nested"sv);

    // populate a test map

    auto top = tr_variant::make_map(6U);
    auto* const map = top.get_if<tr_variant::Map>();
    ASSERT_NE(map, nullptr);

    map->try_emplace(key_bool, true);
    map->try_emplace(key_int, int64_t{ 42 });
    map->try_emplace(key_double, 4.2);
    map->try_emplace(key_string, "needle"sv);

    auto vec = tr_variant::Vector{};
    vec.emplace_back(true);
    vec.emplace_back(int64_t{ 7 });
    map->try_emplace(key_vector, std::move(vec));

    auto nested = tr_variant::make_map(1U);
    auto* nested_map = nested.get_if<tr_variant::Map>();
    ASSERT_NE(nested_map, nullptr);
    nested_map->try_emplace(nested_key, "nested"sv);
    map->try_emplace(key_map, std::move(nested));

    // ---

    // test: returns true for entries that exist
    EXPECT_TRUE(map->contains(key_bool));
    EXPECT_TRUE(map->contains(key_double));
    EXPECT_TRUE(map->contains(key_int));
    EXPECT_TRUE(map->contains(key_map));
    EXPECT_TRUE(map->contains(key_string));
    EXPECT_TRUE(map->contains(key_vector));

    // test: returns false for entries that never existed
    EXPECT_FALSE(map->contains(key_missing));

    // test: returns false for entries that were removed
    EXPECT_EQ(1U, map->erase(key_vector));
    EXPECT_FALSE(map->contains(key_vector));
}

TEST_F(VariantTest, mapReplaceKey)
{
    auto constexpr IntVal = int64_t{ 73 };
    auto const key_bool = tr_quark_new("replace-bool"sv);
    auto const key_int = tr_quark_new("replace-int"sv);
    auto const key_double = tr_quark_new("replace-double"sv);
    auto const key_string = tr_quark_new("replace-string"sv);
    auto const key_vector = tr_quark_new("replace-vector"sv);
    auto const key_map = tr_quark_new("replace-map"sv);
    auto const key_duplicate = tr_quark_new("replace-duplicate"sv);
    auto const key_missing_src = tr_quark_new("replace-missing-src"sv);
    auto const key_missing_tgt = tr_quark_new("replace-missing-tgt"sv);
    auto const key_replacement = tr_quark_new("replace-string-new"sv);
    auto const key_nested = tr_quark_new("replace-nested"sv);

    // populate a sample map

    auto top = tr_variant::make_map(7U);
    auto* const map = top.get_if<tr_variant::Map>();
    ASSERT_NE(map, nullptr);

    map->try_emplace(key_bool, true);
    map->try_emplace(key_int, IntVal);
    map->try_emplace(key_double, 7.3);
    map->try_emplace(key_string, "string"sv);

    auto vec = tr_variant::Vector{};
    vec.emplace_back(false);
    vec.emplace_back(int64_t{ 99 });
    map->try_emplace(key_vector, std::move(vec));

    auto nested = tr_variant::make_map(1U);
    auto* nested_map = nested.get_if<tr_variant::Map>();
    ASSERT_NE(nested_map, nullptr);
    nested_map->try_emplace(key_nested, "nested"sv);
    map->try_emplace(key_map, std::move(nested));

    map->try_emplace(key_duplicate, "occupied"sv);

    // ---

    // test: neither src nor tgt exist
    auto const serde = tr_variant_serde::json();
    auto expected = serde.to_string(top);
    EXPECT_FALSE(map->contains(key_missing_src));
    EXPECT_FALSE(map->contains(key_missing_tgt));
    EXPECT_FALSE(map->replace_key(key_missing_src, key_missing_tgt));
    EXPECT_FALSE(map->contains(key_missing_src));
    EXPECT_FALSE(map->contains(key_missing_tgt));
    auto actual = serde.to_string(top);
    EXPECT_EQ(expected, actual); // confirm variant is unchanged

    // test: src doesn't exist
    expected = serde.to_string(top);
    EXPECT_FALSE(map->contains(key_missing_src));
    EXPECT_EQ(IntVal, map->value_if<int64_t>(key_int).value_or(!IntVal));
    EXPECT_FALSE(map->replace_key(key_missing_src, key_int));
    EXPECT_FALSE(map->contains(key_missing_src));
    EXPECT_EQ(IntVal, map->value_if<int64_t>(key_int).value_or(!IntVal));
    actual = serde.to_string(top);
    EXPECT_EQ(expected, actual); // confirm variant is unchanged

    // test: tgt already exists
    expected = serde.to_string(top);
    EXPECT_TRUE(map->contains(key_int));
    EXPECT_TRUE(map->contains(key_string));
    EXPECT_FALSE(map->replace_key(key_int, key_string));
    EXPECT_TRUE(map->contains(key_int));
    EXPECT_TRUE(map->contains(key_string));
    actual = serde.to_string(top);
    EXPECT_EQ(expected, actual); // confirm variant is unchanged

    // test: successful replacement
    EXPECT_TRUE(map->contains(key_int));
    EXPECT_FALSE(map->contains(key_replacement));
    EXPECT_TRUE(map->replace_key(key_int, key_replacement));
    EXPECT_FALSE(map->contains(key_int));
    EXPECT_TRUE(map->contains(key_replacement));
    EXPECT_EQ(IntVal, map->value_if<int64_t>(key_replacement).value_or(!IntVal));
}

TEST_F(VariantTest, visitStringExposesStringView)
{
    auto const text = "visit-string"sv;
    auto var = tr_variant{ std::string{ text } };
    auto called = false;

    var.visit(
        Overloaded{ [&](std::string_view sv)
                    {
                        called = true;
                        EXPECT_EQ(text, sv);
                    },
                    [](auto&&)
                    {
                        FAIL();
                    } });

    EXPECT_TRUE(called);
}

TEST_F(VariantTest, visitConstVariant)
{
    auto var = tr_variant::make_vector(1U);
    auto* vec = var.get_if<tr_variant::Vector>();
    ASSERT_NE(vec, nullptr);
    vec->emplace_back(int64_t{ 99 });

    auto const result = std::as_const(var).visit(
        Overloaded{ [](tr_variant::Vector const& values) -> int64_t
                    {
                        EXPECT_EQ(1U, std::size(values));
                        return values[0].value_if<int64_t>().value_or(-1);
                    },
                    [](auto&&) -> int64_t
                    {
                        ADD_FAILURE() << "unexpected alternative";
                        return -1;
                    } });

    EXPECT_EQ(99, result);
}

namespace
{

[[nodiscard]] std::string summarizeVariant(tr_variant const& var)
{
    auto const summarize = [&](auto const& self, tr_variant const& cur) -> std::string
    {
        return cur.visit(
            Overloaded{ [&](tr_variant::Map const& map)
                        {
                            auto out = std::string{ "{" };
                            auto first = true;
                            for (auto const& [key, child] : map)
                            {
                                if (!first)
                                {
                                    out += ',';
                                }
                                first = false;
                                out += tr_quark_get_string_view(key);
                                out += ':';
                                out += self(self, child);
                            }
                            out += '}';
                            return out;
                        },
                        [&](tr_variant::Vector const& vec)
                        {
                            auto out = std::string{ "[" };
                            for (size_t i = 0; i < vec.size(); ++i)
                            {
                                if (i != 0U)
                                {
                                    out += ',';
                                }
                                out += self(self, vec[i]);
                            }
                            out += ']';
                            return out;
                        },
                        [&](std::string_view const val) { return std::string{ val }; },
                        [&](bool const val) { return std::string{ val ? "true" : "false" }; },
                        [&](int64_t const val) { return std::to_string(val); },
                        [&](double const val) { return std::to_string(val); },
                        [&](std::nullptr_t) { return std::string{ "null" }; },
                        [&](std::monostate) { return std::string{ "<unset>" }; },
                        [&](auto&&)
                        {
                            return std::string{ "?" };
                        } });
    };

    return summarize(summarize, var);
}

} // namespace

TEST_F(VariantTest, visitsNestedObjects)
{
    auto const var = parseJson(R"({"outer":{"inner":{"flag":true,"name":"transmission"}}})"sv);
    auto const summary = summarizeVariant(var);
    auto constexpr Expected = "{outer:{inner:{flag:true,name:transmission}}}"sv;
    EXPECT_EQ(Expected, summary);
}

TEST_F(VariantTest, visitsNestedArrays)
{
    auto const var = parseJson(R"([[1],[2,3]])"sv);
    auto const summary = summarizeVariant(var);
    auto constexpr Expected = "[[1],[2,3]]"sv;
    EXPECT_EQ(Expected, summary);
}

TEST_F(VariantTest, visitsScalarValues)
{
    using TestCase = std::pair<tr_variant, std::string_view>;
    auto const TestCases = std::array<TestCase, 5U>{ {
        { tr_variant{ "string" }, "string" },
        { tr_variant::unmanaged_string("unmanaged"), "unmanaged" },
        { tr_variant{ 999 }, "999" },
        { tr_variant{ false }, "false" },
        { tr_variant{ nullptr }, "null" },
    } };

    for (auto const& [var, expected] : TestCases)
    {
        EXPECT_EQ(expected, summarizeVariant(var));
    }
}

TEST_F(VariantTest, visitsMixedTypes)
{
    auto const var = parseJson(R"({"outer":{"flag":false,"list":[null,"x"]}})"sv);
    auto const summary = summarizeVariant(var);
    auto constexpr Expected = "{outer:{flag:false,list:[null,x]}}"sv;
    EXPECT_EQ(Expected, summary);
}

TEST_F(VariantTest, visitsNodesDepthFirst)
{
    auto const var = parseJson(R"({
        "files": [
            { "name": "file1", "size": 5, "pieces": [1, 2] },
            { "name": "file2", "size": 7, "pieces": [] }
        ],
        "meta": { "active": true }
    })"sv);

    auto visited_counts = std::map<size_t, size_t>{};

    auto flattened = tr_variant::Vector{};
    flattened.reserve(64U);

    auto flatten = [&](tr_variant const& node, auto const& self) -> void
    {
        ++visited_counts[node.index()];

        node.visit(
            [&](auto const& val)
            {
                using ValueType = std::decay_t<decltype(val)>;

                if constexpr (
                    std::is_same_v<ValueType, bool> || //
                    std::is_same_v<ValueType, double> || //
                    std::is_same_v<ValueType, int64_t> || //
                    std::is_same_v<ValueType, std::monostate> || //
                    std::is_same_v<ValueType, std::nullptr_t> || //
                    std::is_same_v<ValueType, std::string_view>)
                {
                    flattened.emplace_back(val);
                }
                else if constexpr (std::is_same_v<ValueType, tr_variant::Vector>)
                {
                    for (auto const& child : val)
                    {
                        self(child, self);
                    }
                }
                else if constexpr (std::is_same_v<ValueType, tr_variant::Map>)
                {
                    for (auto const& [key, child] : val)
                    {
                        flattened.emplace_back(tr_variant::unmanaged_string(key));
                        self(child, self);
                    }
                }
            });
    };

    flatten(var, flatten);

    // confirm the nodes were visited depth-first
    auto serde = tr_variant_serde::json();
    serde.compact();
    auto const actual = serde.to_string({ std::move(flattened) });
    auto constexpr Expected =
        R"(["files","name","file1","size",5,"pieces",1,2,"name","file2","size",7,"pieces","meta","active",true])"sv;
    EXPECT_EQ(Expected, actual);

    // confirm the expected number of nodes were visited
    auto const expected_visited_count = std::map<size_t, size_t>{
        { tr_variant::BoolIndex, 1U }, //
        { tr_variant::IntIndex, 4U }, //
        { tr_variant::MapIndex, 4U }, //
        { tr_variant::StringIndex, 2U }, //
        { tr_variant::VectorIndex, 3U }, //
    };
    EXPECT_EQ(expected_visited_count, visited_counts);
}

TEST_F(VariantTest, variantFromBufFuzz)
{
    auto benc_serde = tr_variant_serde::json();
    auto json_serde = tr_variant_serde::json();
    auto buf = std::vector<char>{};

    for (size_t i = 0; i < 100000; ++i)
    {
        buf.resize(tr_rand_int(4096U));
        tr_rand_buffer(std::data(buf), std::size(buf));

        (void)benc_serde.inplace().parse(buf);
        (void)json_serde.inplace().parse(buf);
    }
}

TEST_F(VariantTest, serdeEnd)
{
    static auto constexpr TestsJson = std::array{
        std::tuple{ R"({ "json1": 1 }{ "json2": 2 })"sv, '{', 14U },
        std::tuple{ R"({ "json1": 1 })"sv, '\0', 14U },
    };
    static auto constexpr TestsBenc = std::array{
        std::tuple{ "d5:benc1i1eed5:benc2i2ee"sv, 'd', 12U },
        std::tuple{ "d5:benc1i1ee"sv, '\0', 12U },
    };

    for (auto [in, c, pos] : TestsJson)
    {
        auto json_serde = tr_variant_serde::json().inplace();
        auto json_var = json_serde.parse(in).value_or(tr_variant{});
        EXPECT_TRUE(json_var.holds_alternative<tr_variant::Map>()) << json_serde.error_;
        EXPECT_EQ(*json_serde.end(), c);
        EXPECT_EQ(json_serde.end() - std::data(in), pos);
    }

    for (auto [in, c, pos] : TestsBenc)
    {
        auto benc_serde = tr_variant_serde::benc().inplace();
        auto benc_var = benc_serde.parse(in).value_or(tr_variant{});
        EXPECT_TRUE(benc_var.holds_alternative<tr_variant::Map>()) << benc_serde.error_;
        EXPECT_EQ(*benc_serde.end(), c);
        EXPECT_EQ(benc_serde.end() - std::data(in), pos);
    }
}
