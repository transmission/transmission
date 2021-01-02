/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#define LIBTRANSMISSION_VARIANT_MODULE

#include "transmission.h"
#include "utils.h" // tr_free()
#include "variant.h"
#include "variant-common.h"

#include "gtest/gtest.h"

#include <clocale> // setlocale()
#include <cstring> // strlen()
#include <string>

class JSONTest : public ::testing::TestWithParam<char const*>
{
protected:
    void SetUp() override
    {
        auto const* locale_str = GetParam();
        if (setlocale(LC_NUMERIC, locale_str) == nullptr)
        {
            GTEST_SKIP();
        }
    }
};

TEST_P(JSONTest, testElements)
{
    auto const in = std::string {
        "{ \"string\": \"hello world\","
        "  \"escaped\": \"bell \\b formfeed \\f linefeed \\n carriage return \\r tab \\t\","
        "  \"int\": 5, "
        "  \"float\": 6.5, "
        "  \"true\": true, "
        "  \"false\": false, "
        "  \"null\": null }"
    };

    tr_variant top;
    int err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));

    char const* str = {};
    auto key = tr_quark_new("string", 6);
    EXPECT_TRUE(tr_variantDictFindStr(&top, key, &str, nullptr));
    EXPECT_STREQ("hello world", str);

    EXPECT_TRUE(tr_variantDictFindStr(&top, tr_quark_new("escaped", 7), &str, nullptr));
    EXPECT_STREQ("bell \b formfeed \f linefeed \n carriage return \r tab \t", str);

    auto i = int64_t {};
    EXPECT_TRUE(tr_variantDictFindInt(&top, tr_quark_new("int", 3), &i));
    EXPECT_EQ(5, i);

    auto d = double{};
    EXPECT_TRUE(tr_variantDictFindReal(&top, tr_quark_new("float", 5), &d));
    EXPECT_EQ(65, int(d * 10));

    auto f = bool{};
    EXPECT_TRUE(tr_variantDictFindBool(&top, tr_quark_new("true", 4), &f));
    EXPECT_TRUE(f);

    EXPECT_TRUE(tr_variantDictFindBool(&top, tr_quark_new("false", 5), &f));
    EXPECT_FALSE(f);

    EXPECT_TRUE(tr_variantDictFindStr(&top, tr_quark_new("null", 4), &str, nullptr));
    EXPECT_STREQ("", str);

    if (err == 0)
    {
        tr_variantFree(&top);
    }
}

TEST_P(JSONTest, testUtf8)
{
    auto in = std::string { "{ \"key\": \"Letöltések\" }" };
    tr_variant top;
    char const* str;
    char* json;
    int err;
    tr_quark const key = tr_quark_new("key", 3);

    err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStr(&top, key, &str, nullptr));
    EXPECT_STREQ("Letöltések", str);

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    in = std::string { R"({ "key": "\u005C" })" };
    err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStr(&top, key, &str, nullptr));
    EXPECT_STREQ("\\", str);

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is JSON-escaped.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = std::string { R"({ "key": "Let\u00f6lt\u00e9sek" })" };
    err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStr(&top, key, &str, nullptr));
    EXPECT_STREQ("Letöltések", str);
    json = tr_variantToStr(&top, TR_VARIANT_FMT_JSON, nullptr);

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    EXPECT_NE(nullptr, json);
    EXPECT_NE(nullptr, strstr(json, "\\u00f6"));
    EXPECT_NE(nullptr, strstr(json, "\\u00e9"));
    err = tr_variantFromJson(&top, json, strlen(json));
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));
    EXPECT_TRUE(tr_variantDictFindStr(&top, key, &str, nullptr));
    EXPECT_STREQ("Letöltések", str);

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    tr_free(json);
}

TEST_P(JSONTest, test1)
{
    auto const in = std::string {
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
        "}\n"
    };

    tr_variant top;
    auto const err = tr_variantFromJson(&top, in.data(), in.size());

    char const* str;
    int64_t i;
    EXPECT_EQ(0, err);
    EXPECT_TRUE(tr_variantIsDict(&top));
    auto* headers = tr_variantDictFind(&top, tr_quark_new("headers", 7));
    EXPECT_NE(nullptr, headers);
    EXPECT_TRUE(tr_variantIsDict(headers));
    EXPECT_TRUE(tr_variantDictFindStr(headers, tr_quark_new("type", 4), &str, nullptr));
    EXPECT_STREQ("request", str);
    EXPECT_TRUE(tr_variantDictFindInt(headers, TR_KEY_tag, &i));
    EXPECT_EQ(666, i);
    auto* body = tr_variantDictFind(&top, tr_quark_new("body", 4));
    EXPECT_NE(nullptr, body);
    EXPECT_TRUE(tr_variantDictFindStr(body, TR_KEY_name, &str, nullptr));
    EXPECT_STREQ("torrent-info", str);
    auto* args = tr_variantDictFind(body, tr_quark_new("arguments", 9));
    EXPECT_NE(nullptr, args);
    EXPECT_TRUE(tr_variantIsDict(args));
    auto* ids = tr_variantDictFind(args, TR_KEY_ids);
    EXPECT_NE(nullptr, ids);
    EXPECT_TRUE(tr_variantIsList(ids));
    EXPECT_EQ(2, tr_variantListSize(ids));
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 0), &i));
    EXPECT_EQ(7, i);
    EXPECT_TRUE(tr_variantGetInt(tr_variantListChild(ids, 1), &i));
    EXPECT_EQ(10, i);

    tr_variantFree(&top);
}

TEST_P(JSONTest, test2)
{
    tr_variant top;
    auto const in = std::string { " " };

    top.type = 0;
    int err = tr_variantFromJson(&top, in.data(), in.size());

    EXPECT_NE(0, err);
    EXPECT_FALSE(tr_variantIsDict(&top));
}

TEST_P(JSONTest, test3)
{
    auto const in = std::string {
        "{ \"error\": 2,"
        "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
        "  \"eta\": 262792,"
        "  \"id\": 25,"
        "  \"leftUntilDone\": 2275655680 }"
    };

    tr_variant top;
    auto const err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);

    char const* str;
    EXPECT_TRUE(tr_variantDictFindStr(&top, TR_KEY_errorString, &str, nullptr));
    EXPECT_STREQ("torrent not registered with this tracker 6UHsVW'*C", str);

    tr_variantFree(&top);
}

TEST_P(JSONTest, unescape)
{
    tr_variant top;
    auto const in = std::string { R"({ "string-1": "\/usr\/lib" })" };
    int const err = tr_variantFromJson(&top, in.data(), in.size());
    EXPECT_EQ(0, err);

    char const* str;
    EXPECT_TRUE(tr_variantDictFindStr(&top, tr_quark_new("string-1", 8), &str, nullptr));
    EXPECT_STREQ("/usr/lib", str);

    tr_variantFree(&top);
}

INSTANTIATE_TEST_SUITE_P(
    JSON,
    JSONTest,
    ::testing::Values("C", "da_DK.UTF-8", "fr_FR.UTF-8", "ru_RU.UTF-8")
    );
