// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_VARIANT_MODULE

#include <cstdint> // int64_t
#include <locale>
#include <optional>
#include <stdexcept> // std::runtime_error
#include <string>
#include <string_view>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"

using namespace std::literals;

class JSONTest : public ::testing::TestWithParam<char const*>
{
protected:
    void SetUp() override
    {
        auto const* locale_str = GetParam();
        try
        {
            old_locale_ = std::locale::global(std::locale{ {}, new std::numpunct_byname<char>{ locale_str } });
        }
        catch (std::runtime_error const&)
        {
            GTEST_SKIP();
        }
    }

    void TearDown() override
    {
        if (old_locale_)
        {
            std::locale::global(*old_locale_);
        }
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

    auto serde = tr_variant_serde::json();
    serde.use_input_inplace();
    auto top = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));

    auto sv = std::string_view{};
    auto key = tr_quark_new("string"sv);
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key, &sv));
    EXPECT_EQ("hello world"sv, sv);

    EXPECT_TRUE(tr_variantDictFindStrView(&top, tr_quark_new("escaped"sv), &sv));
    EXPECT_EQ("bell \b formfeed \f linefeed \n carriage return \r tab \t"sv, sv);

    auto i = int64_t{};
    EXPECT_TRUE(tr_variantDictFindInt(&top, tr_quark_new("int"sv), &i));
    EXPECT_EQ(5, i);

    auto d = double{};
    EXPECT_TRUE(tr_variantDictFindReal(&top, tr_quark_new("float"sv), &d));
    EXPECT_EQ(65, int(d * 10));

    auto f = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&top, tr_quark_new("true"sv), &f));
    EXPECT_TRUE(f);

    EXPECT_TRUE(tr_variantDictFindBool(&top, tr_quark_new("false"sv), &f));
    EXPECT_FALSE(f);

    EXPECT_TRUE(tr_variantDictFindStrView(&top, tr_quark_new("null"sv), &sv));
    EXPECT_EQ(""sv, sv);

    tr_variantClear(&top);
}

TEST_P(JSONTest, testUtf8)
{
    auto in = "{ \"key\": \"Letöltések\" }"sv;
    auto sv = std::string_view{};
    tr_quark const key = tr_quark_new("key"sv);

    auto serde = tr_variant_serde::json();
    serde.use_input_inplace();
    auto top = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);
    tr_variantClear(&top);

    in = R"({ "key": "\u005C" })"sv;
    top = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key, &sv));
    EXPECT_EQ("\\"sv, sv);
    tr_variantClear(&top);

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is JSON-escaped.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = R"({ "key": "Let\u00f6lt\u00e9sek" })"sv;
    top = serde.parse(in).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);
    auto json = serde.to_string(top);
    tr_variantClear(&top);

    EXPECT_FALSE(std::empty(json));
    EXPECT_NE(std::string::npos, json.find("\\u00f6"));
    EXPECT_NE(std::string::npos, json.find("\\u00e9"));
    top = serde.parse(json).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStrView(&top, key, &sv));
    EXPECT_EQ("Letöltések"sv, sv);
    tr_variantClear(&top);
}

TEST_P(JSONTest, testUtf16Surrogates)
{
    static auto constexpr ThinkingFaceEmojiUtf8 = "\xf0\x9f\xa4\x94"sv;
    auto top = tr_variant{};
    tr_variantInitDict(&top, 1);
    auto const key = tr_quark_new("key"sv);
    tr_variantDictAddStr(&top, key, ThinkingFaceEmojiUtf8);

    auto serde = tr_variant_serde::json();
    serde.compact_ = true;
    auto const json = serde.to_string(top);
    EXPECT_NE(std::string::npos, json.find("ud83e"));
    EXPECT_NE(std::string::npos, json.find("udd14"));
    tr_variantClear(&top);

    auto parsed = serde.parse(json).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&parsed));
    auto value = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&parsed, key, &value));
    EXPECT_EQ(ThinkingFaceEmojiUtf8, value);
    tr_variantClear(&parsed);
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
    auto top = serde.parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));

    auto sv = std::string_view{};
    auto i = int64_t{};
    auto* headers = tr_variantDictFind(&top, tr_quark_new("headers"sv));
    EXPECT_NE(nullptr, headers);
    EXPECT_TRUE(tr_variantIsDict(headers));
    EXPECT_TRUE(tr_variantDictFindStrView(headers, tr_quark_new("type"sv), &sv));
    EXPECT_EQ("request"sv, sv);
    EXPECT_TRUE(tr_variantDictFindInt(headers, TR_KEY_tag, &i));
    EXPECT_EQ(666, i);
    auto* body = tr_variantDictFind(&top, tr_quark_new("body"sv));
    EXPECT_NE(nullptr, body);
    EXPECT_TRUE(tr_variantDictFindStrView(body, TR_KEY_name, &sv));
    EXPECT_EQ("torrent-info"sv, sv);
    auto* args = tr_variantDictFind(body, tr_quark_new("arguments"sv));
    EXPECT_NE(nullptr, args);
    EXPECT_TRUE(tr_variantIsDict(args));
    auto* ids = tr_variantDictFind(args, TR_KEY_ids);
    EXPECT_NE(nullptr, ids);
    EXPECT_TRUE(tr_variantIsList(ids));
    EXPECT_EQ(2U, tr_variantListSize(ids));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 0), &i));
    EXPECT_EQ(7, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 1), &i));
    EXPECT_EQ(10, i);

    tr_variantClear(&top);
}

TEST_P(JSONTest, test2)
{
    static auto constexpr Input = " "sv;
    auto serde = tr_variant_serde::json();
    serde.use_input_inplace();
    auto top = serde.parse(Input);
    EXPECT_FALSE(top.has_value());
}

TEST_P(JSONTest, test3)
{
    static auto constexpr Input =
        "{ \"error\": 2,"
        "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
        "  \"eta\": 262792,"
        "  \"id\": 25,"
        "  \"leftUntilDone\": 2275655680 }"sv;

    auto serde = tr_variant_serde::json();
    serde.use_input_inplace();
    auto top = serde.parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));

    auto sv = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&top, TR_KEY_errorString, &sv));
    EXPECT_EQ("torrent not registered with this tracker 6UHsVW'*C"sv, sv);

    tr_variantClear(&top);
}

TEST_P(JSONTest, unescape)
{
    static auto constexpr Input = R"({ "string-1": "\/usr\/lib" })"sv;

    auto serde = tr_variant_serde::json();
    serde.use_input_inplace();
    auto top = serde.parse(Input).value_or(tr_variant{});
    EXPECT_TRUE(tr_variantIsDict(&top));

    auto sv = std::string_view{};
    EXPECT_TRUE(tr_variantDictFindStrView(&top, tr_quark_new("string-1"sv), &sv));
    EXPECT_EQ("/usr/lib"sv, sv);
    tr_variantClear(&top);
}

INSTANTIATE_TEST_SUITE_P( //
    JSON,
    JSONTest,
    ::testing::Values( //
        "C",
        "da_DK.UTF-8",
        "fr_FR.UTF-8",
        "ru_RU.UTF-8"));
