/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>

#include "transmission.h"

#include "error.h"
#include "metainfo.h"
#include "torrent-metainfo.h"
#include "torrent.h"
#include "utils.h"

#include "gtest/gtest.h"

using namespace std::literals;

TEST(TorrentMetainfo, magnetLink)
{
    // background info @ http://wiki.theory.org/BitTorrent_Magnet-URI_Webseeding
    char const constexpr* const MagnetLink =
        "magnet:?"
        "xt=urn:btih:14ffe5dd23188fd5cb53a1d47f1289db70abf31e"
        "&dn=ubuntu_12_04_1_desktop_32_bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http%3A%2F%2Ftransmissionbt.com";

    auto metainfo = tr_torrent_metainfo{};
    EXPECT_TRUE(metainfo.parseMagnet(MagnetLink));
    EXPECT_EQ(0, std::size(metainfo.files())); // because it's a magnet link
    EXPECT_EQ(2, std::size(metainfo.announceList()));
    EXPECT_EQ(MagnetLink, metainfo.magnet());
}

#define BEFORE_PATH \
    "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH \
    "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

// FIXME: split these into parameterized tests?
TEST(TorrentMetainfo, bucket)
{
    struct LocalTest
    {
        std::string_view benc;
        bool expected_parse_result;
    };

    auto const tests = std::array<LocalTest, 9>{ {
        { BEFORE_PATH "5:a.txt" AFTER_PATH, true },
        // allow empty components, but not =all= empty components, see bug #5517
        { BEFORE_PATH "0:5:a.txt" AFTER_PATH, true },
        { BEFORE_PATH "0:0:" AFTER_PATH, false },
        // allow path separators in a filename (replaced with '_')
        { BEFORE_PATH "7:a/a.txt" AFTER_PATH, true },
        // allow "." components (skipped)
        { BEFORE_PATH "1:.5:a.txt" AFTER_PATH, true },
        { BEFORE_PATH "5:a.txt1:." AFTER_PATH, true },
        // allow ".." components (replaced with "__")
        { BEFORE_PATH "2:..5:a.txt" AFTER_PATH, true },
        { BEFORE_PATH "5:a.txt2:.." AFTER_PATH, true },
        // fail on empty string
        { "", false },
    } };

    tr_logSetLevel(TR_LOG_SILENT);

    for (auto const& test : tests)
    {
        auto metainfo = tr_torrent_metainfo{};
        EXPECT_EQ(test.expected_parse_result, metainfo.parseBenc(test.benc));
    }
}

TEST(TorrentMetainfo, sanitize)
{
    struct LocalTest
    {
        std::string_view input;
        std::string_view expected_output;
        bool expected_is_adjusted;
    };

    auto const tests = std::array<LocalTest, 29>{
        // skipped
        LocalTest{ ""sv, ""sv, false },
        { "."sv, ""sv, true },
        { ".."sv, ""sv, true },
        { "....."sv, ""sv, true },
        { "  "sv, ""sv, true },
        { " . "sv, ""sv, true },
        { ". . ."sv, ""sv, true },
        // replaced with '_'
        { "/"sv, "_"sv, true },
        { "////"sv, "____"sv, true },
        { "\\\\"sv, "__"sv, true },
        { "/../"sv, "_.._"sv, true },
        { "foo<bar:baz/boo"sv, "foo_bar_baz_boo"sv, true },
        { "t\0e\x01s\tt\ri\nn\fg"sv, "t_e_s_t_i_n_g"sv, true },
        // appended with '_'
        { "con"sv, "con_"sv, true },
        { "cOm4"sv, "cOm4_"sv, true },
        { "LPt9.txt"sv, "LPt9_.txt"sv, true },
        { "NUL.tar.gz"sv, "NUL_.tar.gz"sv, true },
        // trimmed
        { " foo"sv, "foo"sv, true },
        { "foo "sv, "foo"sv, true },
        { " foo "sv, "foo"sv, true },
        { "foo."sv, "foo"sv, true },
        { "foo..."sv, "foo"sv, true },
        { " foo... "sv, "foo"sv, true },
        // unmodified
        { "foo"sv, "foo"sv, false },
        { ".foo"sv, ".foo"sv, false },
        { "..foo"sv, "..foo"sv, false },
        { "foo.bar.baz"sv, "foo.bar.baz"sv, false },
        { "null"sv, "null"sv, false },
        { "compass"sv, "compass"sv, false },
    };

    auto out = std::string{};
    auto is_adjusted = bool{};
    for (auto const& test : tests)
    {
        out.clear();
        auto const success = tr_metainfoAppendSanitizedPathComponent(out, test.input, &is_adjusted);
        EXPECT_EQ(!std::empty(out), success);
        EXPECT_EQ(test.expected_output, out);
        EXPECT_EQ(test.expected_is_adjusted, is_adjusted);
    }
}

TEST(TorrentMetainfo, AndroidTorrent)
{
    auto const filename = tr_strvJoin(LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv);

    auto* ctor = tr_ctorNew(nullptr);
    auto const err = tr_ctorSetMetainfoFromFile(ctor, filename.c_str());
    EXPECT_EQ(0, err);
    tr_ctorFree(ctor);
}

TEST(TorrentMetainfo, ctorSaveContents)
{
    auto const src_filename = tr_strvJoin(LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv);
    auto const tgt_filename = tr_strvJoin(::testing::TempDir(), "save-contents-test.torrent");

    // try saving without passing any metainfo.
    auto* ctor = tr_ctorNew(nullptr);
    tr_error* error = nullptr;
    EXPECT_FALSE(tr_ctorSaveContents(ctor, tgt_filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    if (error != nullptr)
    {
        EXPECT_EQ(EINVAL, error->code);
        tr_error_clear(&error);
    }

    // now try saving _with_ metainfo
    EXPECT_EQ(0, tr_ctorSetMetainfoFromFile(ctor, src_filename.c_str()));
    EXPECT_TRUE(tr_ctorSaveContents(ctor, tgt_filename.c_str(), &error));
    EXPECT_EQ(nullptr, error);

    // the saved contents should match the source file's contents
    auto src_contents = std::vector<char>{};
    EXPECT_TRUE(tr_loadFile(src_contents, src_filename.c_str(), &error));
    auto tgt_contents = std::vector<char>{};
    EXPECT_TRUE(tr_loadFile(tgt_contents, tgt_filename.c_str(), &error));
    EXPECT_EQ(src_contents, tgt_contents);

    // cleanup
    EXPECT_TRUE(tr_sys_path_remove(tgt_filename.c_str(), &error));
    EXPECT_EQ(nullptr, error);
    tr_error_clear(&error);
    tr_ctorFree(ctor);
}
