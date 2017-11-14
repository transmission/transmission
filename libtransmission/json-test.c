/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <string.h> /* strlen() */

#include <locale.h> /* setlocale() */

#define __LIBTRANSMISSION_VARIANT_MODULE__

#include "transmission.h"
#include "utils.h" /* tr_free */
#include "variant.h"
#include "variant-common.h"
#include "libtransmission-test.h"

static int test_elements(void)
{
    char const* in;
    tr_variant top;
    char const* str;
    bool f;
    double d;
    int64_t i;
    int err = 0;
    tr_quark key;

    in =
        "{ \"string\": \"hello world\","
        "  \"escaped\": \"bell \\b formfeed \\f linefeed \\n carriage return \\r tab \\t\","
        "  \"int\": 5, "
        "  \"float\": 6.5, "
        "  \"true\": true, "
        "  \"false\": false, "
        "  \"null\": null }";

    err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    str = NULL;
    key = tr_quark_new("string", 6);
    check(tr_variantDictFindStr(&top, key, &str, NULL));
    check_str(str, ==, "hello world");
    check(tr_variantDictFindStr(&top, tr_quark_new("escaped", 7), &str, NULL));
    check_str(str, ==, "bell \b formfeed \f linefeed \n carriage return \r tab \t");
    i = 0;
    check(tr_variantDictFindInt(&top, tr_quark_new("int", 3), &i));
    check_int(i, ==, 5);
    d = 0;
    check(tr_variantDictFindReal(&top, tr_quark_new("float", 5), &d));
    check_int(((int)(d * 10)), ==, 65);
    f = false;
    check(tr_variantDictFindBool(&top, tr_quark_new("true", 4), &f));
    check_int(f, ==, true);
    check(tr_variantDictFindBool(&top, tr_quark_new("false", 5), &f));
    check_int(f, ==, false);
    check(tr_variantDictFindStr(&top, tr_quark_new("null", 4), &str, NULL));
    check_str(str, ==, "");

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    return 0;
}

static int test_utf8(void)
{
    char const* in = "{ \"key\": \"Letöltések\" }";
    tr_variant top;
    char const* str;
    char* json;
    int err;
    tr_quark const key = tr_quark_new("key", 3);

    err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    check(tr_variantDictFindStr(&top, key, &str, NULL));
    check_str(str, ==, "Letöltések");

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    in = "{ \"key\": \"\\u005C\" }";
    err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    check(tr_variantDictFindStr(&top, key, &str, NULL));
    check_str(str, ==, "\\");

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
    in = "{ \"key\": \"Let\\u00f6lt\\u00e9sek\" }";
    err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    check(tr_variantDictFindStr(&top, key, &str, NULL));
    check_str(str, ==, "Letöltések");
    json = tr_variantToStr(&top, TR_VARIANT_FMT_JSON, NULL);

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    check_ptr(json, !=, NULL);
    check_str(strstr(json, "\\u00f6"), !=, NULL);
    check_str(strstr(json, "\\u00e9"), !=, NULL);
    err = tr_variantFromJson(&top, json, strlen(json));
    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    check(tr_variantDictFindStr(&top, key, &str, NULL));
    check_str(str, ==, "Letöltések");

    if (err == 0)
    {
        tr_variantFree(&top);
    }

    tr_free(json);

    return 0;
}

static int test1(void)
{
    char const* in =
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
        "}\n";
    tr_variant top;
    tr_variant* headers;
    tr_variant* body;
    tr_variant* args;
    tr_variant* ids;
    char const* str;
    int64_t i;
    int const err = tr_variantFromJson(&top, in, strlen(in));

    check_int(err, ==, 0);
    check(tr_variantIsDict(&top));
    check_ptr((headers = tr_variantDictFind(&top, tr_quark_new("headers", 7))), !=, NULL);
    check(tr_variantIsDict(headers));
    check(tr_variantDictFindStr(headers, tr_quark_new("type", 4), &str, NULL));
    check_str(str, ==, "request");
    check(tr_variantDictFindInt(headers, TR_KEY_tag, &i));
    check_int(i, ==, 666);
    check_ptr((body = tr_variantDictFind(&top, tr_quark_new("body", 4))), !=, NULL);
    check(tr_variantDictFindStr(body, TR_KEY_name, &str, NULL));
    check_str(str, ==, "torrent-info");
    check_ptr((args = tr_variantDictFind(body, tr_quark_new("arguments", 9))), !=, NULL);
    check(tr_variantIsDict(args));
    check_ptr((ids = tr_variantDictFind(args, TR_KEY_ids)), !=, NULL);
    check(tr_variantIsList(ids));
    check_uint(tr_variantListSize(ids), ==, 2);
    check(tr_variantGetInt(tr_variantListChild(ids, 0), &i));
    check_int(i, ==, 7);
    check(tr_variantGetInt(tr_variantListChild(ids, 1), &i));
    check_int(i, ==, 10);

    tr_variantFree(&top);
    return 0;
}

static int test2(void)
{
    tr_variant top;
    char const* in = " ";
    int err;

    top.type = 0;
    err = tr_variantFromJson(&top, in, strlen(in));

    check_int(err, !=, 0);
    check(!tr_variantIsDict(&top));

    return 0;
}

static int test3(void)
{
    char const* in =
        "{ \"error\": 2,"
        "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
        "  \"eta\": 262792,"
        "  \"id\": 25,"
        "  \"leftUntilDone\": 2275655680 }";
    tr_variant top;
    char const* str;

    int const err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantDictFindStr(&top, TR_KEY_errorString, &str, NULL));
    check_str(str, ==, "torrent not registered with this tracker 6UHsVW'*C");

    tr_variantFree(&top);
    return 0;
}

static int test_unescape(void)
{
    char const* in = "{ \"string-1\": \"\\/usr\\/lib\" }";
    tr_variant top;
    char const* str;

    int const err = tr_variantFromJson(&top, in, strlen(in));
    check_int(err, ==, 0);
    check(tr_variantDictFindStr(&top, tr_quark_new("string-1", 8), &str, NULL));
    check_str(str, ==, "/usr/lib");

    tr_variantFree(&top);
    return 0;
}

int main(void)
{
    char const* comma_locales[] =
    {
        "da_DK.UTF-8",
        "fr_FR.UTF-8",
        "ru_RU.UTF-8"
    };

    testFunc const tests[] =
    {
        test_elements,
        test_utf8,
        test1,
        test2,
        test3,
        test_unescape
    };

    /* run the tests in a locale with a decimal point of '.' */
    setlocale(LC_NUMERIC, "C");

    int ret = runTests(tests, NUM_TESTS(tests));

    /* run the tests in a locale with a decimal point of ',' */
    bool is_locale_set = false;

    for (size_t i = 0; !is_locale_set && i < TR_N_ELEMENTS(comma_locales); ++i)
    {
        is_locale_set = setlocale(LC_NUMERIC, comma_locales[i]) != NULL;
    }

    if (!is_locale_set)
    {
        fprintf(stderr, "WARNING: unable to run locale-specific json tests. add a locale like %s or %s\n", comma_locales[0],
            comma_locales[1]);
    }
    else
    {
        ret += runTests(tests, NUM_TESTS(tests));
    }

    return ret;
}
