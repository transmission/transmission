// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_VARIANT_MODULE

#include <cstdint> // int64_t
#include <locale>
#include <optional>
#include <string>
#include <string_view>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/quark.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"

using namespace std::literals;

class JSONTest : public ::testing::TestWithParam<char const*>
{
protected:
    void SetUp() override
    {
        ::testing::TestWithParam<char const*>::SetUp();

        auto const* locale_str = GetParam();
        old_locale_ = tr_locale_set_global(locale_str);
        if (!old_locale_)
        {
            GTEST_SKIP();
        }
    }

    void TearDown() override
    {
        if (old_locale_)
        {
            tr_locale_set_global(*old_locale_);
        }

        ::testing::TestWithParam<char const*>::TearDown();
    }

private:
    std::optional<std::locale> old_locale_;
};

TEST_P(JSONTest, testElements)
{
    auto const in = std::string{
        "{ \"string\": \"hello world\","
        "  \"escaped\": \"bell \\b formfeed \\f linefeed \\n carriage return \\r tab \\t\","
        "  \"int\": 5, "
        "  \"float\": 6.5, "
        "  \"true\": true, "
        "  \"false\": false, "
        "  \"null\": null }"
    };

    auto var = tr_variant_serde::json().inplace().parse(in).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());

    auto sv = std::string_view{};
    auto key = tr_quark_new("string"sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("hello world"sv, sv);

    EXPECT_TRUE(tr_variantDictFindStrView(&var, tr_quark_new("escaped"sv), &sv));
    EXPECT_EQ("bell \b formfeed \f linefeed \n carriage return \r tab \t"sv, sv);

    auto i = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&var, tr_quark_new("int"sv), &i));
    EXPECT_EQ(5, i);

    auto d = double{};
    EXPECT_TRUE(tr_variantDictFindReal(&var, tr_quark_new("float"sv), &d));
    EXPECT_EQ(65, int(d * 10));

    auto f = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&var, tr_quark_new("true"sv), &f));
    EXPECT_TRUE(f);

    EXPECT_TRUE(tr_variantDictFindBool(&var, tr_quark_new("false"sv), &f));
    EXPECT_FALSE(f);

    EXPECT_TRUE(tr_variantDictFindStrView(&var, tr_quark_new("null"sv), &sv));
    EXPECT_EQ(""sv, sv);
}

TEST_P(JSONTest, testUtf8)
{
    auto in = "{ \"key\": \"Letöltések\" }"sv;
    auto sv = std::string_view{};
    tr_quark const key = tr_quark_new("key"sv);

    auto serde = tr_variant_serde::json().inplace().compact();
    auto var = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);
    var.clear();

    in = R"({ "key": "\u005C" })"sv;
    var = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("\\"sv, sv);
    var.clear();

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is UTF-8.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = R"({ "key": "Let\u00f6lt\u00e9sek" })"sv;
    var = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);
    auto json = serde.to_string(var);
    var.clear();

    EXPECT_FALSE(std::empty(json));
    EXPECT_EQ(R"({"key":"Letöltések"})"sv, json);
    var = serde.parse(json).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);

    // Test string known to be prone to locale issues
    // https://github.com/transmission/transmission/issues/5967
    var.clear();
    tr_variantInitDict(&var, 1U);
    tr_variantDictAddStr(&var, key, "Дыскаграфія"sv);
    json = serde.to_string(var);
    EXPECT_EQ(R"({"key":"Дыскаграфія"})"sv, json);
    var = serde.parse(json).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("Дыскаграфія"sv, sv);

    // Thinking emoji 🤔
    var.clear();
    tr_variantInitDict(&var, 1U);
    tr_variantDictAddStr(&var, key, "\xf0\x9f\xa4\x94"sv);
    json = serde.to_string(var);
    EXPECT_EQ("{\"key\":\"\xf0\x9f\xa4\x94\"}"sv, json);
    var = serde.parse(json).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(&var, key, &sv));
    EXPECT_EQ("\xf0\x9f\xa4\x94"sv, sv);
}

TEST_P(JSONTest, test1)
{
    static auto constexpr Input =
        "{\n"
        "    \"headers\": {\n"
        "        \"type\": \"request\",\n"
        "        \"tag\": 666\n"
        "    },\n"
        "    \"body\": {\n"
        "        \"name\": \"torrent-info\",\n"
        "        \"arguments\": {\n"
        "            \"ids\": [ 7, 10 ]\n"
        "        }\n"
        "    }\n"
        "}\n"sv;

    auto serde = tr_variant_serde::json();
    auto var = serde.inplace().parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());

    auto sv = std::string_view{};
    auto i = int64_t{};
    auto* headers = tr_variantDictFind(&var, tr_quark_new("headers"sv));
    EXPECT_NE(nullptr, headers);
    EXPECT_TRUE(headers->holds_alternative<tr_variant::Map>());
    EXPECT_TRUE(tr_variantDictFindStrView(headers, tr_quark_new("type"sv), &sv));
    EXPECT_EQ("request"sv, sv);
    EXPECT_TRUE(tr_variantDictFindInt(headers, TR_KEY_tag, &i));
    EXPECT_EQ(666, i);
    auto* body = tr_variantDictFind(&var, tr_quark_new("body"sv));
    EXPECT_NE(nullptr, body);
    EXPECT_TRUE(tr_variantDictFindStrView(body, TR_KEY_name, &sv));
    EXPECT_EQ("torrent-info"sv, sv);
    auto* args = tr_variantDictFind(body, tr_quark_new("arguments"sv));
    EXPECT_NE(nullptr, args);
    EXPECT_TRUE(args->holds_alternative<tr_variant::Map>());
    auto* ids = tr_variantDictFind(args, TR_KEY_ids);
    ASSERT_NE(nullptr, ids);
    EXPECT_TRUE(ids->holds_alternative<tr_variant::Vector>());
    EXPECT_EQ(2U, tr_variantListSize(ids));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 0), &i));
    EXPECT_EQ(7, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 1), &i));
    EXPECT_EQ(10, i);
}

TEST_P(JSONTest, test2)
{
    static auto constexpr Input = " "sv;
    auto var = tr_variant_serde::json().inplace().parse(Input);
    EXPECT_FALSE(var.has_value());
}

TEST_P(JSONTest, test3)
{
    static auto constexpr Input =
        "{ \"error\": 2,"
        "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
        "  \"eta\": 262792,"
        "  \"id\": 25,"
        "  \"leftUntilDone\": 2275655680 }"sv;

    auto var = tr_variant_serde::json().inplace().parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());

    auto sv = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, TR_KEY_errorString, &sv));
    EXPECT_EQ("torrent not registered with this tracker 6UHsVW'*C"sv, sv);
}

TEST_P(JSONTest, unescape)
{
    static auto constexpr Input = R"({ "string-1": "\/usr\/lib" })"sv;

    auto var = tr_variant_serde::json().inplace().parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(var.holds_alternative<tr_variant::Map>());

    auto sv = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&var, tr_quark_new("string-1"sv), &sv));
    EXPECT_EQ("/usr/lib"sv, sv);
}

TEST_P(JSONTest, parseJsonFuzz)
{
    auto serde = tr_variant_serde::json().inplace();

    auto var = serde.parse({ nullptr, 0U });
    EXPECT_FALSE(var);

    auto buf = std::vector<char>{};
    for (size_t i = 0; i < 100000U; ++i)
    {
        buf.resize(tr_rand_int(1024U));
        tr_rand_buffer(std::data(buf), std::size(buf));

        (void)serde.parse({ std::data(buf), std::size(buf) });
    }
}

INSTANTIATE_TEST_SUITE_P( //
    JSON,
    JSONTest,
    ::testing::Values( //
        "C",
        "da_DK.UTF-8",
        "fr_FR.UTF-8",
        "ru_RU.UTF-8"));
