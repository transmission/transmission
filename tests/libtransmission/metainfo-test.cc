/*
 * This file Copyright (C) 2013-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"

#include "error.h"
#include "metainfo.h"
#include "torrent.h"
#include "utils.h"

#include "test-fixtures.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <string_view>

#define BEFORE_PATH \
    "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH \
    "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

using namespace std::literals;

namespace libtransmission
{

namespace test
{

using MetainfoTest = SessionTest;

TEST_F(MetainfoTest, sanitize)
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

TEST_F(MetainfoTest, AndroidTorrent)
{
    auto const filename = tr_strvJoin(LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv);

    auto* ctor = tr_ctorNew(session_);
    EXPECT_TRUE(tr_ctorSetMetainfoFromFile(ctor, filename.c_str(), nullptr));
    tr_ctorFree(ctor);
}

TEST_F(MetainfoTest, ctorSaveContents)
{
    auto const src_filename = tr_strvJoin(LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv);
    auto const tgt_filename = tr_strvJoin(::testing::TempDir(), "save-contents-test.torrent");

    // try saving without passing any metainfo.
    auto* ctor = tr_ctorNew(session_);
    tr_error* error = nullptr;
    EXPECT_FALSE(tr_ctorSaveContents(ctor, tgt_filename.c_str(), &error));
    EXPECT_NE(nullptr, error);
    if (error != nullptr)
    {
        EXPECT_EQ(EINVAL, error->code);
        tr_error_clear(&error);
    }

    // now try saving _with_ metainfo
    EXPECT_TRUE(tr_ctorSetMetainfoFromFile(ctor, src_filename.c_str(), &error));
    EXPECT_EQ(nullptr, error);
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

} // namespace test

} // namespace libtransmission
