/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "metainfo.h"
#include "utils.h"

#include "gtest/gtest.h"

#include <array>
#include <cerrno>
#include <cstring>

TEST(Metainfo, magnetLink)
{
    // background info @ http://wiki.theory.org/BitTorrent_Magnet-URI_Webseeding
    char const constexpr* const MagnetLink =
        "magnet:?"
        "xt=urn:btih:14FFE5DD23188FD5CB53A1D47F1289DB70ABF31E"
        "&dn=ubuntu+12+04+1+desktop+32+bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http://transmissionbt.com ";

    auto* ctor = tr_ctorNew(nullptr);
    tr_ctorSetMetainfoFromMagnetLink(ctor, MagnetLink);
    tr_info inf;
    auto const parse_result = tr_torrentParse(ctor, &inf);
    EXPECT_EQ(TR_PARSE_OK, parse_result);
    EXPECT_EQ(0, inf.fileCount); // because it's a magnet link
    EXPECT_EQ(2, inf.trackerCount);
    EXPECT_STREQ("http://tracker.publicbt.com/announce", inf.trackers[0].announce);
    EXPECT_STREQ("udp://tracker.publicbt.com:80", inf.trackers[1].announce);
    EXPECT_EQ(1, inf.webseedCount);
    EXPECT_STREQ("http://transmissionbt.com", inf.webseeds[0]);

    /* cleanup */
    tr_metainfoFree(&inf);
    tr_ctorFree(ctor);
}

#define BEFORE_PATH \
    "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH \
    "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

// FIXME: split these into parameterized tests?
TEST(Metainfo, bucket)
{
    struct Test
    {
        int expected_benc_err;
        int expected_parse_result;
        void const* benc;
    };

    auto constexpr Tests = std::array<Test, 9>{
        Test{ 0, TR_PARSE_OK, BEFORE_PATH "5:a.txt" AFTER_PATH },

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

    tr_logSetLevel(TR_LOG_SILENT);

    for (auto const& test : Tests)
    {
        auto* ctor = tr_ctorNew(nullptr);
        int const err = tr_ctorSetMetainfo(ctor, test.benc, strlen(static_cast<char const*>(test.benc)));
        EXPECT_EQ(test.expected_benc_err, err);

        if (err == 0)
        {
            tr_parse_result const parse_result = tr_torrentParse(ctor, nullptr);
            EXPECT_EQ(test.expected_parse_result, parse_result);
        }

        tr_ctorFree(ctor);
    }
}

TEST(Metainfo, sanitize)
{
    struct Test
    {
        char const* str;
        size_t len;
        char const* expected_result;
        bool expected_is_adjusted;
    };

    auto constexpr Tests = std::array<Test, 29>{
        // skipped
        Test{ "", 0, nullptr, false },
        { ".", 1, nullptr, false },
        { "..", 2, nullptr, true },
        { ".....", 5, nullptr, false },
        { "  ", 2, nullptr, false },
        { " . ", 3, nullptr, false },
        { ". . .", 5, nullptr, false },
        // replaced with '_'
        { "/", 1, "_", true },
        { "////", 4, "____", true },
        { "\\\\", 2, "__", true },
        { "/../", 4, "_.._", true },
        { "foo<bar:baz/boo", 15, "foo_bar_baz_boo", true },
        { "t\0e\x01s\tt\ri\nn\fg", 13, "t_e_s_t_i_n_g", true },
        // appended with '_'
        { "con", 3, "con_", true },
        { "cOm4", 4, "cOm4_", true },
        { "LPt9.txt", 8, "LPt9_.txt", true },
        { "NUL.tar.gz", 10, "NUL_.tar.gz", true },
        // trimmed
        { " foo", 4, "foo", true },
        { "foo ", 4, "foo", true },
        { " foo ", 5, "foo", true },
        { "foo.", 4, "foo", true },
        { "foo...", 6, "foo", true },
        { " foo... ", 8, "foo", true },
        // unmodified
        { "foo", 3, "foo", false },
        { ".foo", 4, ".foo", false },
        { "..foo", 5, "..foo", false },
        { "foo.bar.baz", 11, "foo.bar.baz", false },
        { "null", 4, "null", false },
        { "compass", 7, "compass", false }
    };

    for (auto const& test : Tests)
    {
        bool is_adjusted;
        char* const result = tr_metainfo_sanitize_path_component(test.str, test.len, &is_adjusted);

        EXPECT_STREQ(test.expected_result, result);

        if (test.expected_result != nullptr)
        {
            EXPECT_EQ(test.expected_is_adjusted, is_adjusted);
        }

        tr_free(result);
    }
}
