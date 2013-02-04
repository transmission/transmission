#include <string.h> /* strlen () */

#include <locale.h> /* setlocale () */

#include "transmission.h"
#include "bencode.h"
#include "json.h"
#include "utils.h" /* tr_free */

#undef VERBOSE
#include "libtransmission-test.h"

static int
test_elements (void)
{
    const char * in;
    tr_benc top;
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

    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check_int_eq (0, err);
    check (tr_bencIsDict (&top));
    str = NULL;
    check (tr_bencDictFindStr (&top, "string", &str));
    check_streq ("hello world", str);
    check (tr_bencDictFindStr (&top, "escaped", &str));
    check_streq ("bell \b formfeed \f linefeed \n carriage return \r tab \t", str);
    i = 0;
    check (tr_bencDictFindInt (&top, "int", &i));
    check_int_eq (5, i);
    d = 0;
    check (tr_bencDictFindReal (&top, "float", &d));
    check_int_eq (65, ((int)(d*10)));
    f = false;
    check (tr_bencDictFindBool (&top, "true", &f));
    check_int_eq (true, f);
    check (tr_bencDictFindBool (&top, "false", &f));
    check_int_eq (false, f);
    check (tr_bencDictFindStr (&top, "null", &str));
    check_streq ("", str);

    if (!err)
        tr_bencFree (&top);

    return 0;
}
static int
test_utf8 (void)
{
    const char      * in = "{ \"key\": \"Letöltések\" }";
    tr_benc           top;
    const char      * str;
    char            * json;
    int               err;

    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check (!err);
    check (tr_bencIsDict (&top));
    check (tr_bencDictFindStr (&top, "key", &str));
    check_streq ("Letöltések", str);
    if (!err)
        tr_bencFree (&top);

    in = "{ \"key\": \"\\u005C\" }";
    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check (!err);
    check (tr_bencIsDict (&top));
    check (tr_bencDictFindStr (&top, "key", &str));
    check_streq ("\\", str);
    if (!err)
        tr_bencFree (&top);

    /**
     * 1. Feed it JSON-escaped nonascii to the JSON decoder.
     * 2. Confirm that the result is UTF-8.
     * 3. Feed the same UTF-8 back into the JSON encoder.
     * 4. Confirm that the result is JSON-escaped.
     * 5. Dogfood that result back into the parser.
     * 6. Confirm that the result is UTF-8.
     */
    in = "{ \"key\": \"Let\\u00f6lt\\u00e9sek\" }";
    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check (!err);
    check (tr_bencIsDict (&top));
    check (tr_bencDictFindStr (&top, "key", &str));
    check_streq ("Letöltések", str);
    json = tr_bencToStr (&top, TR_FMT_JSON, NULL);
    if (!err)
        tr_bencFree (&top);
    check (json);
    check (strstr (json, "\\u00f6") != NULL);
    check (strstr (json, "\\u00e9") != NULL);
    err = tr_jsonParse (NULL, json, strlen (json), &top, NULL);
    check (!err);
    check (tr_bencIsDict (&top));
    check (tr_bencDictFindStr (&top, "key", &str));
    check_streq ("Letöltések", str);
    if (!err)
        tr_bencFree (&top);
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
    tr_benc      top, *headers, *body, *args, *ids;
    const char * str;
    int64_t      i;
    const int    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);

    check (!err);
    check (tr_bencIsDict (&top));
    check ((headers = tr_bencDictFind (&top, "headers")));
    check (tr_bencIsDict (headers));
    check (tr_bencDictFindStr (headers, "type", &str));
    check_streq ("request", str);
    check (tr_bencDictFindInt (headers, "tag", &i));
    check_int_eq (666, i);
    check ((body = tr_bencDictFind (&top, "body")));
    check (tr_bencDictFindStr (body, "name", &str));
    check_streq ("torrent-info", str);
    check ((args = tr_bencDictFind (body, "arguments")));
    check (tr_bencIsDict (args));
    check ((ids = tr_bencDictFind (args, "ids")));
    check (tr_bencIsList (ids));
    check_int_eq (2, tr_bencListSize (ids));
    check (tr_bencGetInt (tr_bencListChild (ids, 0), &i));
    check_int_eq (7, i);
    check (tr_bencGetInt (tr_bencListChild (ids, 1), &i));
    check_int_eq (10, i);

    tr_bencFree (&top);
    return 0;
}

static int
test2 (void)
{
    tr_benc top;
    const char * in = " ";
    int err;

    top.type = 0;
    err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);

    check (err);
    check (!tr_bencIsDict (&top));

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
    tr_benc top;
    const char * str;

    const int err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check (!err);
    check (tr_bencDictFindStr (&top, "errorString", &str));
    check_streq ("torrent not registered with this tracker 6UHsVW'*C", str);

    tr_bencFree (&top);
    return 0;
}

static int
test_unescape (void)
{
    const char * in = "{ \"string-1\": \"\\/usr\\/lib\" }";
    tr_benc top;
    const char * str;

    const int err = tr_jsonParse (NULL, in, strlen (in), &top, NULL);
    check_int_eq (0, err);
    check (tr_bencDictFindStr (&top, "string-1", &str));
    check_streq ("/usr/lib", str);

    tr_bencFree (&top);
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
