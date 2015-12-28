/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <string.h> /* strlen () */

#include <locale.h> /* setlocale() */

#define __LIBTRANSMISSION_VARIANT_MODULE__
#include "transmission.h"
#include "utils.h" /* tr_free */
#include "variant.h"
#include "variant-common.h"
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
    tr_quark key;

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
    key = tr_quark_new ("string", 6);
    check (tr_variantDictFindStr (&top, key, &str, NULL));
    check_streq ("hello world", str);
    check (tr_variantDictFindStr (&top, tr_quark_new("escaped",7), &str, NULL));
    check_streq ("bell \b formfeed \f linefeed \n carriage return \r tab \t", str);
    i = 0;
    check (tr_variantDictFindInt (&top, tr_quark_new("int",3), &i));
    check_int_eq (5, i);
    d = 0;
    check (tr_variantDictFindReal (&top, tr_quark_new("float",5), &d));
    check_int_eq (65, ((int)(d*10)));
    f = false;
    check (tr_variantDictFindBool (&top, tr_quark_new("true",4), &f));
    check_int_eq (true, f);
    check (tr_variantDictFindBool (&top, tr_quark_new("false",5), &f));
    check_int_eq (false, f);
    check (tr_variantDictFindStr (&top, tr_quark_new("null",4), &str, NULL));
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
    const tr_quark key = tr_quark_new ("key", 3);

    err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, key, &str, NULL));
    check_streq ("Letöltések", str);
    if (!err)
        tr_variantFree (&top);

    in = "{ \"key\": \"\\u005C\" }";
    err = tr_variantFromJson (&top, in, strlen(in));
    check (!err);
    check (tr_variantIsDict (&top));
    check (tr_variantDictFindStr (&top, key, &str, NULL));
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
    check (tr_variantDictFindStr (&top, key, &str, NULL));
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
    check (tr_variantDictFindStr (&top, key, &str, NULL));
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
    check ((headers = tr_variantDictFind (&top, tr_quark_new("headers",7))));
    check (tr_variantIsDict (headers));
    check (tr_variantDictFindStr (headers, tr_quark_new("type",4), &str, NULL));
    check_streq ("request", str);
    check (tr_variantDictFindInt (headers, TR_KEY_tag, &i));
    check_int_eq (666, i);
    check ((body = tr_variantDictFind (&top, tr_quark_new("body",4))));
    check (tr_variantDictFindStr (body, TR_KEY_name, &str, NULL));
    check_streq ("torrent-info", str);
    check ((args = tr_variantDictFind (body, tr_quark_new("arguments",9))));
    check (tr_variantIsDict (args));
    check ((ids = tr_variantDictFind (args, TR_KEY_ids)));
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
    check (tr_variantDictFindStr (&top, TR_KEY_errorString, &str, NULL));
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
    check (tr_variantDictFindStr (&top, tr_quark_new("string-1",8), &str, NULL));
    check_streq ("/usr/lib", str);

    tr_variantFree (&top);
    return 0;
}

int
main (void)
{
  int i;
  int n;
  int rv;

  const char * comma_locales[] = { "da_DK.UTF-8",
                                   "fr_FR.UTF-8",
                                   "ru_RU.UTF-8"};

  const testFunc tests[] = { test_elements,
                             test_utf8,
                             test1,
                             test2,
                             test3,
                             test_unescape };

  /* run the tests in a locale with a decimal point of '.' */
  setlocale (LC_NUMERIC, "C");
  if ((rv = runTests (tests, NUM_TESTS (tests))))
    return rv;

  /* run the tests in a locale with a decimal point of ',' */
  n = sizeof(comma_locales) / sizeof(comma_locales[0]);
  for (i=0; i<n; ++i)
    if (setlocale (LC_NUMERIC, comma_locales[i]) != NULL)
      break;
  if (i==n)
    fprintf (stderr, "WARNING: unable to run locale-specific json tests. add a locale like %s or %s\n",
             comma_locales[0],
             comma_locales[1]);
  else if ((rv = runTests (tests, NUM_TESTS(tests))))
    return rv;

  /* success */
  return 0;
}
