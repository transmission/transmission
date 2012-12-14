#include <string.h> /* strlen () */
#include "transmission.h"
#include "utils.h" /* tr_free */
#include "variant.h"

#include "libtransmission-test.h"

static int
test_elements (void)
{
    const char * in;
    tr_variant top;
    const char * str;
    bool f;
    double d;
    int64_t i;
    int err = 0;

    in = "{ \"string\": \"hello world\","
         "  \"escaped\": \"bell \\b formfeed \\f linefeed \\n carriage return \\r tab \\t\","
         "  \"int\": 5, "
         "  \"float\": 6.5, "
         "  \"true\": true, "
         "  \"false\": false, "
         "  \"null\": null }";

    err = tr_variantFromJson (&top, in, strlen(in));
    check_int_eq (0, err);
    check (tr_variantIsDict (&top));
    str = NULL;
    check (tr_variantDictFindStr (&top, "string", &str, NULL));
    check_streq ("hello world", str);
    check (tr_variantDictFindStr (&top, "escaped", &str, NULL));
    check_streq ("bell \b formfeed \f linefeed \n carriage return \r tab \t", str);
    i = 0;
    check (tr_variantDictFindInt (&top, "int", &i));
    check_int_eq (5, i);
    d = 0;
    check (tr_variantDictFindReal (&top, "float", &d));
    check_int_eq (65, ((int)(d*10)));
    f = false;
    check (tr_variantDictFindBool (&top, "true", &f));
    check_int_eq (true, f);
    check (tr_variantDictFindBool (&top, "false", &f));
    check_int_eq (false, f);
    check (tr_variantDictFindStr (&top, "null", &str, NULL));
    check_streq ("", str);

    if (!err)
        tr_variantFree (&top);

    return 0;
}
static int
test_utf8 (void)
{
    const char      * in = "{ \"key\": \"Letöltések\" }";
    tr_variant           top;
    const char      * str;
    char            * json;
    int               err;

    err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, "key", &str, NULL));
    check_streq ("Letöltések", str);
    if (!err)
        tr_variantFree (&top);

    in = "{ \"key\": \"\\u005C\" }";
    err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, "key", &str, NULL));
    check_streq ("\\", str);
    if (!err)
        tr_variantFree (&top);

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is JSON-escaped.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = "{ \"key\": \"Let\\u00f6lt\\u00e9sek\" }";
    err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, "key", &str, NULL));
    check_streq ("Letöltések", str);
    json = tr_variantToStr (&top, TR_VARIANT_FMT_JSON, NULL);
    if (!err)
        tr_variantFree (&top);
    check (json);
    check (strstr (json, "\\u00f6") != NULL);
    check (strstr (json, "\\u00e9") != NULL);
    err = tr_variantFromJson (&top, json, strlen(json));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, "key", &str, NULL));
    check_streq ("Letöltések", str);
    if (!err)
        tr_variantFree (&top);
    tr_free (json);

    return 0;
}

static int
test1 (void)
{
    const char * in =
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
    tr_variant      top, *headers, *body, *args, *ids;
    const char * str;
    int64_t      i;
    const int    err = tr_variantFromJson (&top, in, strlen(in));

    check (!err);
    check (tr_variantIsDict (&top));
    check ((headers = tr_variantDictFind (&top, "headers")));
    check (tr_variantIsDict (headers));
    check (tr_variantDictFindStr (headers, "type", &str, NULL));
    check_streq ("request", str);
    check (tr_variantDictFindInt (headers, "tag", &i));
    check_int_eq (666, i);
    check ((body = tr_variantDictFind (&top, "body")));
    check (tr_variantDictFindStr (body, "name", &str, NULL));
    check_streq ("torrent-info", str);
    check ((args = tr_variantDictFind (body, "arguments")));
    check (tr_variantIsDict (args));
    check ((ids = tr_variantDictFind (args, "ids")));
    check (tr_variantIsList (ids));
    check_int_eq (2, tr_variantListSize (ids));
    check (tr_variantGetInt (tr_variantListChild (ids, 0), &i));
    check_int_eq (7, i);
    check (tr_variantGetInt (tr_variantListChild (ids, 1), &i));
    check_int_eq (10, i);

    tr_variantFree (&top);
    return 0;
}

static int
test2 (void)
{
    tr_variant top;
    const char * in = " ";
    int err;

    top.type = 0;
    err = tr_variantFromJson (&top, in, strlen(in));

    check (err);
    check (!tr_variantIsDict (&top));

    return 0;
}

static int
test3 (void)
{
    const char * in = "{ \"error\": 2,"
                      "  \"errorString\": \"torrent not registered with this tracker 6UHsVW'*C\","
                      "  \"eta\": 262792,"
                      "  \"id\": 25,"
                      "  \"leftUntilDone\": 2275655680 }";
    tr_variant top;
    const char * str;

    const int err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantDictFindStr (&top, "errorString", &str, NULL));
    check_streq ("torrent not registered with this tracker 6UHsVW'*C", str);

    tr_variantFree (&top);
    return 0;
}

static int
test_unescape (void)
{
    const char * in = "{ \"string-1\": \"\\/usr\\/lib\" }";
    tr_variant top;
    const char * str;

    const int err = tr_variantFromJson (&top, in, strlen(in));
    check_int_eq (0, err);
    check (tr_variantDictFindStr (&top, "string-1", &str, NULL));
    check_streq ("/usr/lib", str);

    tr_variantFree (&top);
    return 0;
}

int
main (void)
{
    const testFunc tests[] = { test_elements,
                               test_utf8,
                               test1,
                               test2,
                               test3,
                               test_unescape };

    return runTests (tests, NUM_TESTS (tests));
}
