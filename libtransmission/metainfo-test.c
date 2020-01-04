/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "libtransmission-test.h"

#include "transmission.h"
#include "metainfo.h"
#include "utils.h"

#include <errno.h>

static int test_magnet_link(void)
{
    tr_info inf;
    tr_ctor* ctor;
    char const* magnet_link;
    tr_parse_result parse_result;

    /* background info @ http://wiki.theory.org/BitTorrent_Magnet-URI_Webseeding */
    magnet_link =
        "magnet:?"
        "xt=urn:btih:14FFE5DD23188FD5CB53A1D47F1289DB70ABF31E"
        "&dn=ubuntu+12+04+1+desktop+32+bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http://transmissionbt.com ";
    ctor = tr_ctorNew(NULL);
    tr_ctorSetMetainfoFromMagnetLink(ctor, magnet_link);
    parse_result = tr_torrentParse(ctor, &inf);
    check_int(inf.fileCount, ==, 0); /* cos it's a magnet link */
    check_int(parse_result, ==, TR_PARSE_OK);
    check_int(inf.trackerCount, ==, 2);
    check_str(inf.trackers[0].announce, ==, "http://tracker.publicbt.com/announce");
    check_str(inf.trackers[1].announce, ==, "udp://tracker.publicbt.com:80");
    check_int(inf.webseedCount, ==, 1);
    check_str(inf.webseeds[0], ==, "http://transmissionbt.com");

    /* cleanup */
    tr_metainfoFree(&inf);
    tr_ctorFree(ctor);
    return 0;
}

#define BEFORE_PATH \
    "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH \
    "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

static int test_metainfo(void)
{
    struct
    {
        int expected_benc_err;
        int expected_parse_result;
        void const* benc;
    }
    const metainfo[] =
    {
        { 0, TR_PARSE_OK, BEFORE_PATH "5:a.txt" AFTER_PATH },

        /* allow empty components, but not =all= empty components, see bug #5517 */
        { 0, TR_PARSE_OK, BEFORE_PATH "0:5:a.txt" AFTER_PATH },
        { 0, TR_PARSE_ERR, BEFORE_PATH "0:0:" AFTER_PATH },

        /* allow path separators in a filename (replaced with '_') */
        { 0, TR_PARSE_OK, BEFORE_PATH "7:a/a.txt" AFTER_PATH },

        /* allow "." components (skipped) */
        { 0, TR_PARSE_OK, BEFORE_PATH "1:.5:a.txt" AFTER_PATH },
        { 0, TR_PARSE_OK, BEFORE_PATH "5:a.txt1:." AFTER_PATH },

        /* allow ".." components (replaced with "__") */
        { 0, TR_PARSE_OK, BEFORE_PATH "2:..5:a.txt" AFTER_PATH },
        { 0, TR_PARSE_OK, BEFORE_PATH "5:a.txt2:.." AFTER_PATH },

        /* fail on empty string */
        { EILSEQ, TR_PARSE_ERR, "" }
    };

    tr_logSetLevel(0); /* yes, we already know these will generate errors, thank you... */

    for (size_t i = 0; i < TR_N_ELEMENTS(metainfo); i++)
    {
        tr_ctor* ctor = tr_ctorNew(NULL);
        int const err = tr_ctorSetMetainfo(ctor, metainfo[i].benc, strlen(metainfo[i].benc));
        check_int(err, ==, metainfo[i].expected_benc_err);

        if (err == 0)
        {
            tr_parse_result const parse_result = tr_torrentParse(ctor, NULL);
            check_int(parse_result, ==, metainfo[i].expected_parse_result);
        }

        tr_ctorFree(ctor);
    }

    return 0;
}

static int test_sanitize(void)
{
    struct
    {
        char const* str;
        size_t len;
        char const* expected_result;
        bool expected_is_adjusted;
    }
    const test_data[] =
    {
        /* skipped */
        { "", 0, NULL, false },
        { ".", 1, NULL, false },
        { "..", 2, NULL, true },
        { ".....", 5, NULL, false },
        { "  ", 2, NULL, false },
        { " . ", 3, NULL, false },
        { ". . .", 5, NULL, false },
        /* replaced with '_'  */
        { "/", 1, "_", true },
        { "////", 4, "____", true },
        { "\\\\", 2, "__", true },
        { "/../", 4, "_.._", true },
        { "foo<bar:baz/boo", 15, "foo_bar_baz_boo", true },
        { "t\0e\x01s\tt\ri\nn\fg", 13, "t_e_s_t_i_n_g", true },
        /* appended with '_' */
        { "con", 3, "con_", true },
        { "cOm4", 4, "cOm4_", true },
        { "LPt9.txt", 8, "LPt9_.txt", true },
        { "NUL.tar.gz", 10, "NUL_.tar.gz", true },
        /* trimmed */
        { " foo", 4, "foo", true },
        { "foo ", 4, "foo", true },
        { " foo ", 5, "foo", true },
        { "foo.", 4, "foo", true },
        { "foo...", 6, "foo", true },
        { " foo... ", 8, "foo", true },
        /* unmodified */
        { "foo", 3, "foo", false },
        { ".foo", 4, ".foo", false },
        { "..foo", 5, "..foo", false },
        { "foo.bar.baz", 11, "foo.bar.baz", false },
        { "null", 4, "null", false },
        { "compass", 7, "compass", false }
    };

    for (size_t i = 0; i < TR_N_ELEMENTS(test_data); ++i)
    {
        bool is_adjusted;
        char* const result = tr_metainfo_sanitize_path_component(test_data[i].str, test_data[i].len, &is_adjusted);

        check_str(result, ==, test_data[i].expected_result);

        if (test_data[i].expected_result != NULL)
        {
            check_bool(is_adjusted, ==, test_data[i].expected_is_adjusted);
        }

        tr_free(result);
    }

    return 0;
}

int main(void)
{
    testFunc const tests[] =
    {
        test_magnet_link,
        test_metainfo,
        test_sanitize
    };

    return runTests(tests, NUM_TESTS(tests));
}
