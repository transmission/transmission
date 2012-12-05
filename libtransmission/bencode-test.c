#include <ctype.h> /* isspace () */
#include <errno.h> /* EILSEQ */
#include <string.h> /* strlen (), strncmp () */

#include <event2/buffer.h>

#include "transmission.h"
#include "bencode.h"
#include "utils.h" /* tr_free */

// #define VERBOSE
#include "libtransmission-test.h"

#ifndef WIN32
#define STACK_SMASH_DEPTH (1 * 1000 * 1000)
#else
#define STACK_SMASH_DEPTH ( 100 * 1000)
#endif

static int
testInt (void)
{
    uint8_t         buf[128];
    int64_t         val;
    int             err;
    const uint8_t * end;

    /* good int string */
    tr_snprintf ((char*)buf, sizeof (buf), "i64e");
    err = tr_bencParseInt (buf, buf + 4, &end, &val);
    check_int_eq (0, err);
    check_int_eq (64, val);
    check ((buf + 4) ==  end);

    /* missing 'e' */
    end = NULL;
    val = 888;
    err = tr_bencParseInt (buf, buf + 3, &end, &val);
    check_int_eq (EILSEQ, err);
    check_int_eq (888, val);
    check (end == NULL);

    /* empty buffer */
    err = tr_bencParseInt (buf, buf + 0, &end, &val);
    check_int_eq (EILSEQ, err);
    check_int_eq (888, val);
    check (end == NULL);

    /* bad number */
    tr_snprintf ((char*)buf, sizeof (buf), "i6z4e");
    err = tr_bencParseInt (buf, buf + 5, &end, &val);
    check_int_eq (EILSEQ, err);
    check_int_eq (888, val);
    check (end == NULL);

    /* negative number */
    tr_snprintf ((char*)buf, sizeof (buf), "i-3e");
    err = tr_bencParseInt (buf, buf + 4, &end, &val);
    check_int_eq (0, err);
    check_int_eq (-3, val);
    check ((buf + 4) == end);

    /* zero */
    tr_snprintf ((char*)buf, sizeof (buf), "i0e");
    err = tr_bencParseInt (buf, buf + 4, &end, &val);
    check_int_eq (0, err);
    check_int_eq (0, val);
    check ((buf + 3) == end);

    /* no leading zeroes allowed */
    val = 0;
    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "i04e");
    err = tr_bencParseInt (buf, buf + 4, &end, &val);
    check_int_eq (EILSEQ, err);
    check_int_eq (0, val);
    check (NULL == end);

    return 0;
}

static int
testStr (void)
{
    uint8_t         buf[128];
    int             err;
    const uint8_t * end;
    const uint8_t * str;
    size_t          len;

    /* good string */
    tr_snprintf ((char*)buf, sizeof (buf), "4:boat");
    err = tr_bencParseStr (buf, buf + 6, &end, &str, &len);
    check_int_eq (0, err);
    check_int_eq (4, len);
    check (!strncmp ((char*)str, "boat", len));
    check (end == buf + 6);
    str = NULL;
    end = NULL;
    len = 0;

    /* string goes past end of buffer */
    err = tr_bencParseStr (buf, buf + 5, &end, &str, &len);
    check_int_eq (EILSEQ, err);
    check_int_eq (0, len);
    check (str == NULL);
    check (end == NULL);
    check (!len);

    /* empty string */
    tr_snprintf ((char*)buf, sizeof (buf), "0:");
    err = tr_bencParseStr (buf, buf + 2, &end, &str, &len);
    check_int_eq (0, err);
    check_int_eq (0, len);
    check (!*str);
    check (end == buf + 2);
    str = NULL;
    end = NULL;
    len = 0;

    /* short string */
    tr_snprintf ((char*)buf, sizeof (buf), "3:boat");
    err = tr_bencParseStr (buf, buf + 6, &end, &str, &len);
    check_int_eq (0, err);
    check_int_eq (3, len);
    check (!strncmp ((char*)str, "boa", len));
    check (end == buf + 5);
    str = NULL;
    end = NULL;
    len = 0;

    return 0;
}

static int
testString (const char * str,
            int          isGood)
{
    tr_benc         val;
    const uint8_t * end = NULL;
    char *          saved;
    const size_t    len = strlen (str);
    int             savedLen;
    int             err = tr_bencParse (str, str + len, &val, &end);

    if (!isGood)
    {
        check (err);
    }
    else
    {
        check (!err);
#if 0
        fprintf (stderr, "in: [%s]\n", str);
        fprintf (stderr, "out:\n%s", tr_bencToStr (&val, TR_FMT_JSON, NULL));
#endif
        check (end == (const uint8_t*)str + len);
        saved = tr_bencToStr (&val, TR_FMT_BENC, &savedLen);
        check_streq (str, saved);
        check_int_eq (savedLen, len);
        tr_free (saved);
        tr_bencFree (&val);
    }
    return 0;
}

static int
testParse (void)
{
    tr_benc         val;
    tr_benc *       child;
    tr_benc *       child2;
    uint8_t         buf[512];
    const uint8_t * end;
    int             err;
    int             len;
    int64_t         i;
    char *          saved;

    tr_snprintf ((char*)buf, sizeof (buf), "i64e");
    err = tr_bencParse (buf, buf + sizeof (buf), &val, &end);
    check (!err);
    check (tr_bencGetInt (&val, &i));
    check_int_eq (64, i);
    check (end == buf + 4);
    tr_bencFree (&val);

    tr_snprintf ((char*)buf, sizeof (buf), "li64ei32ei16ee");
    err = tr_bencParse (buf, buf + sizeof (buf), &val, &end);
    check (!err);
    check (end == buf + strlen ((char*)buf));
    check (val.val.l.count == 3);
    check (tr_bencGetInt (&val.val.l.vals[0], &i));
    check_int_eq (64, i);
    check (tr_bencGetInt (&val.val.l.vals[1], &i));
    check_int_eq (32, i);
    check (tr_bencGetInt (&val.val.l.vals[2], &i));
    check_int_eq (16, i);
    saved = tr_bencToStr (&val, TR_FMT_BENC, &len);
    check_streq ((char*)buf, saved);
    tr_free (saved);
    tr_bencFree (&val);

    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "lllee");
    err = tr_bencParse (buf, buf + strlen ((char*)buf), &val, &end);
    check (err);
    check (end == NULL);

    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "le");
    err = tr_bencParse (buf, buf + sizeof (buf), &val, &end);
    check (!err);
    check (end == buf + 2);
    saved = tr_bencToStr (&val, TR_FMT_BENC, &len);
    check_streq ("le", saved);
    tr_free (saved);
    tr_bencFree (&val);

    if ((err = testString ("llleee", true)))
        return err;
    if ((err = testString ("d3:cow3:moo4:spam4:eggse", true)))
        return err;
    if ((err = testString ("d4:spaml1:a1:bee", true)))
        return err;
    if ((err =
             testString ("d5:greenli1ei2ei3ee4:spamd1:ai123e3:keyi214eee",
                         true)))
        return err;
    if ((err =
             testString (
                 "d9:publisher3:bob17:publisher-webpage15:www.example.com18:publisher.location4:homee",
                 true)))
        return err;
    if ((err =
             testString (
                 "d8:completei1e8:intervali1800e12:min intervali1800e5:peers0:e",
                 true)))
        return err;
    if ((err = testString ("d1:ai0e1:be", false))) /* odd number of children
                                                         */
        return err;
    if ((err = testString ("", false)))
        return err;
    if ((err = testString (" ", false)))
        return err;

    /* nested containers
     * parse an unsorted dict
     * save as a sorted dict */
    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "lld1:bi32e1:ai64eeee");
    err = tr_bencParse (buf, buf + sizeof (buf), &val, &end);
    check (!err);
    check (end == buf + strlen ((const char*)buf));
    check ((child = tr_bencListChild (&val, 0)));
    check ((child2 = tr_bencListChild (child, 0)));
    saved = tr_bencToStr (&val, TR_FMT_BENC, &len);
    check_streq ("lld1:ai64e1:bi32eeee", saved);
    tr_free (saved);
    tr_bencFree (&val);

    /* too many endings */
    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "leee");
    err = tr_bencParse (buf, buf + sizeof (buf), &val, &end);
    check (!err);
    check (end == buf + 2);
    saved = tr_bencToStr (&val, TR_FMT_BENC, &len);
    check_streq ("le", saved);
    tr_free (saved);
    tr_bencFree (&val);

    /* no ending */
    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "l1:a1:b1:c");
    err = tr_bencParse (buf, buf + strlen ((char*)buf), &val, &end);
    check (err);

    /* incomplete string */
    end = NULL;
    tr_snprintf ((char*)buf, sizeof (buf), "1:");
    err = tr_bencParse (buf, buf + strlen ((char*)buf), &val, &end);
    check (err);

    return 0;
}

static void
stripWhitespace (char * in)
{
    char * out;

    for (out = in; *in; ++in)
        if (!isspace (*in))
            *out++ = *in;
    *out = '\0';
}

static int
testJSONSnippet (const char * benc_str,
                 const char * expected)
{
    tr_benc top;
    char * serialized;
    struct evbuffer * buf;

    tr_bencLoad (benc_str, strlen (benc_str), &top, NULL);
    buf = tr_bencToBuf (&top, TR_FMT_JSON);
    serialized = (char*) evbuffer_pullup (buf, -1);
    stripWhitespace (serialized);
#if 0
    fprintf (stderr, "benc: %s\n", benc_str);
    fprintf (stderr, "json: %s\n", serialized);
    fprintf (stderr, "want: %s\n", expected);
#endif
    check_streq (expected, serialized);
    tr_bencFree (&top);
    evbuffer_free (buf);
    return 0;
}

static int
testJSON (void)
{
    int          val;
    const char * benc_str;
    const char * expected;

    benc_str = "i6e";
    expected = "6";
    if ((val = testJSONSnippet (benc_str, expected)))
        return val;

    benc_str = "d5:helloi1e5:worldi2ee";
    expected = "{\"hello\":1,\"world\":2}";
    if ((val = testJSONSnippet (benc_str, expected)))
        return val;

    benc_str = "d5:helloi1e5:worldi2e3:fooli1ei2ei3eee";
    expected = "{\"foo\":[1,2,3],\"hello\":1,\"world\":2}";
    if ((val = testJSONSnippet (benc_str, expected)))
        return val;

    benc_str = "d5:helloi1e5:worldi2e3:fooli1ei2ei3ed1:ai0eeee";
    expected = "{\"foo\":[1,2,3,{\"a\":0}],\"hello\":1,\"world\":2}";
    if ((val = testJSONSnippet (benc_str, expected)))
        return val;

    benc_str = "d4:argsd6:statusle7:status2lee6:result7:successe";
    expected =
        "{\"args\":{\"status\":[],\"status2\":[]},\"result\":\"success\"}";
    if ((val = testJSONSnippet (benc_str, expected)))
        return val;

    return 0;
}

static int
testMerge (void)
{
    tr_benc dest, src;
    int64_t i;
    const char * s;

    /* initial dictionary (default values)  */
    tr_bencInitDict (&dest, 10);
    tr_bencDictAddInt (&dest, "i1", 1);
    tr_bencDictAddInt (&dest, "i2", 2);
    tr_bencDictAddInt (&dest, "i4", -35); /* remains untouched */
    tr_bencDictAddStr (&dest, "s5", "abc");
    tr_bencDictAddStr (&dest, "s6", "def");
    tr_bencDictAddStr (&dest, "s7", "127.0.0.1"); /* remains untouched */

    /* new dictionary, will overwrite items in dest  */
    tr_bencInitDict (&src, 10);
    tr_bencDictAddInt (&src, "i1", 1);     /* same value */
    tr_bencDictAddInt (&src, "i2", 4);     /* new value */
    tr_bencDictAddInt (&src, "i3", 3);     /* new key:value */
    tr_bencDictAddStr (&src, "s5", "abc"); /* same value */
    tr_bencDictAddStr (&src, "s6", "xyz"); /* new value */
    tr_bencDictAddStr (&src, "s8", "ghi"); /* new key:value */

    tr_bencMergeDicts (&dest, /*const*/ &src);

    check (tr_bencDictFindInt (&dest, "i1", &i));
    check_int_eq (1, i);
    check (tr_bencDictFindInt (&dest, "i2", &i));
    check_int_eq (4, i);
    check (tr_bencDictFindInt (&dest, "i3", &i));
    check_int_eq (3, i);
    check (tr_bencDictFindInt (&dest, "i4", &i));
    check_int_eq (-35, i);
    check (tr_bencDictFindStr (&dest, "s5", &s));
    check_streq ("abc", s);
    check (tr_bencDictFindStr (&dest, "s6", &s));
    check_streq ("xyz", s);
    check (tr_bencDictFindStr (&dest, "s7", &s));
    check_streq ("127.0.0.1", s);
    check (tr_bencDictFindStr (&dest, "s8", &s));
    check_streq ("ghi", s);

    tr_bencFree (&dest);
    tr_bencFree (&src);
    return 0;
}

static int
testStackSmash (void)
{
    int             i;
    int             len;
    int             err;
    uint8_t *       in;
    const uint8_t * end;
    tr_benc         val;
    char *          saved;
    const int       depth = STACK_SMASH_DEPTH;

    in = tr_new (uint8_t, depth * 2 + 1);
    for (i = 0; i < depth; ++i)
    {
        in[i] = 'l';
        in[depth + i] = 'e';
    }
    in[depth * 2] = '\0';
    err = tr_bencParse (in, in + (depth * 2), &val, &end);
    check (!err);
    check (end == in + (depth * 2));
    saved = tr_bencToStr (&val, TR_FMT_BENC, &len);
    check_streq ((char*)in, saved);
    tr_free (in);
    tr_free (saved);
    tr_bencFree (&val);

    return 0;
}

static int
testBool (void)
{
    tr_benc top;
    int64_t intVal;
    bool boolVal;

    tr_bencInitDict (&top, 0);

    tr_bencDictAddBool (&top, "key1", false);
    tr_bencDictAddBool (&top, "key2", 0);
    tr_bencDictAddInt (&top, "key3", true);
    tr_bencDictAddInt (&top, "key4", 1);
    check (tr_bencDictFindBool (&top, "key1", &boolVal));
    check (!boolVal);
    check (tr_bencDictFindBool (&top, "key2", &boolVal));
    check (!boolVal);
    check (tr_bencDictFindBool (&top, "key3", &boolVal));
    check (boolVal);
    check (tr_bencDictFindBool (&top, "key4", &boolVal));
    check (boolVal);
    check (tr_bencDictFindInt (&top, "key1", &intVal));
    check (!intVal);
    check (tr_bencDictFindInt (&top, "key2", &intVal));
    check (!intVal);
    check (tr_bencDictFindInt (&top, "key3", &intVal));
    check (intVal);
    check (tr_bencDictFindInt (&top, "key4", &intVal));
    check (intVal);

    tr_bencFree (&top);
    return 0;
}

static int
testParse2 (void)
{
    tr_benc top;
    tr_benc top2;
    int64_t intVal;
    const char * strVal;
    double realVal;
    bool boolVal;
    int len;
    char * benc;
    const uint8_t * end;

    tr_bencInitDict (&top, 0);
    tr_bencDictAddBool (&top, "this-is-a-bool", true);
    tr_bencDictAddInt (&top, "this-is-an-int", 1234);
    tr_bencDictAddReal (&top, "this-is-a-real", 0.5);
    tr_bencDictAddStr (&top, "this-is-a-string", "this-is-a-string");

    benc = tr_bencToStr (&top, TR_FMT_BENC, &len);
    check_streq ("d14:this-is-a-booli1e14:this-is-a-real8:0.50000016:this-is-a-string16:this-is-a-string14:this-is-an-inti1234ee", benc);
    check (!tr_bencParse (benc, benc+len, &top2, &end));
    check ((char*)end == benc + len);
    check (tr_bencIsDict (&top2));
    check (tr_bencDictFindInt (&top, "this-is-an-int", &intVal));
    check_int_eq (1234, intVal);
    check (tr_bencDictFindBool (&top, "this-is-a-bool", &boolVal));
    check (boolVal == true);
    check (tr_bencDictFindStr (&top, "this-is-a-string", &strVal));
    check_streq ("this-is-a-string", strVal);
    check (tr_bencDictFindReal (&top, "this-is-a-real", &realVal));
    check_int_eq (50, (int)(realVal*100));

    tr_bencFree (&top2);
    tr_free (benc);
    tr_bencFree (&top);

    return 0;
}

int
main (void)
{
    static const testFunc tests[] = {
	testInt, testStr, testParse, testJSON, testMerge, testBool,
	testParse2, testStackSmash,
    };

    return runTests (tests, NUM_TESTS (tests));
}
