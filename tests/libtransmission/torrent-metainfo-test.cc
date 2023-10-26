// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h>
#include <libtransmission/error.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/utils.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

using TorrentMetainfoTest = SessionTest;

TEST_F(TorrentMetainfoTest, magnetLink)
{
    // background info @ http://wiki.theory.org/BitTorrent_Magnet-URI_Webseeding
    auto constexpr MagnetLink =
        "magnet:?"
        "xt=urn:btih:14ffe5dd23188fd5cb53a1d47f1289db70abf31e"
        "&dn=ubuntu_12_04_1_desktop_32_bit"
        "&tr=http%3A%2F%2Ftracker.publicbt.com%2Fannounce"
        "&tr=udp%3A%2F%2Ftracker.publicbt.com%3A80"
        "&ws=http%3A%2F%2Ftransmissionbt.com"sv;

    auto metainfo = tr_torrent_metainfo{};
    EXPECT_TRUE(metainfo.parseMagnet(MagnetLink));
    EXPECT_EQ(0U, metainfo.fileCount()); // because it's a magnet link
    EXPECT_EQ(2U, std::size(metainfo.announceList()));
    EXPECT_EQ(MagnetLink, metainfo.magnet().sv());
}

#define BEFORE_PATH \
    "d10:created by25:Transmission/2.82 (14160)13:creation datei1402280218e8:encoding5:UTF-84:infod5:filesld6:lengthi2e4:pathl"
#define AFTER_PATH \
    "eed6:lengthi2e4:pathl5:b.txteee4:name3:foo12:piece lengthi32768e6:pieces20:ÞÉ`âMs¡Å;Ëº¬.åÂà7:privatei0eee"

// FIXME: split these into parameterized tests?
TEST_F(TorrentMetainfoTest, bucket)
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

    tr_logSetLevel(TR_LOG_OFF);

    for (auto const& test : tests)
    {
        auto metainfo = tr_torrent_metainfo{};
        EXPECT_EQ(test.expected_parse_result, metainfo.parseBenc(test.benc));
    }
}

TEST_F(TorrentMetainfoTest, parseBencFuzzRegressions)
{
    static auto constexpr Tests = std::array<std::string_view, 1>{
        "ZC/veNSVW0Ss+KGfMqH4DQqtYXzgmVi5oBi0XlxviLytlwwjf7MLanOcnS73eSB/iye83hVyvSWg27tPl5oqWdNEZ0euMbo7E8FH/xgTvUEOnBVgvPno50CyI7c5F2QTw16avUB7dvGzx5xIjzJ2qkD2BsNtOoiZI3skC6XwSifsDfJUN8NxHFiwvWxmZRLq2eQlE2wGxAW5aLj6U1MHDzPZ83+2o81pRyMr11bHmWFcNorTGLeOpHBd9veduHpNOKNwOatoXeb57jZCy1Zmu9y/wCuUx6DP3I5FGQ/t3AYh7w028Z/zgIlvWat6QjqSPp7j1nEbl6SNZNl1doGmusl9hvRsbaCq9b1XHpTDtQSJ8Owj07fph0p0ZVu5kJpQBfOGsHLh6ALVrTepptIvcnNW9+nauE+NJa2z+9Yla7780sCdBsGYZZA6HUr0J9GXES7+uRPPBwAl2YB1qWhCsOCClixTiAlwrsBl1bJ/a4FV04aU5jXDEYrpJMzdSAEoypDWMsn3Fc5umLqJ1jtqPqykKY0HjPrCkVAMmvmacauBzIj5Eg/uw0xtZp+wXdLQv8qyuXgsJs7dExZbgTgfPY4niTBpftM6YFQrCx/IxiMshYp7tMolykoed/8gZMm6yyWizzml4BlvnvY3+J2eVKRvS7QToRKxN5eFP9l/pflrK+8cHbwVnjQ1pE3hTQACmNIQHRTY2QoOGwG+HTwo48akfbJnjJ3F0iN6miy7lvv5u0p1rpbM2On5FJ3G98OYnzGIxf8BomHvVp/3eX6QJZUMZKsUTpgbRqg0AJH9FjiERQ9v6B25Va+Q0yV8z5DmiA5AgyIwkIzlSBAl0PYsNaw+rH06a93yBhAfK6EPSArYLjMI6o/1kF4UxNyfE+F79xbdCAKRAX3iJ7DH1GncFoIQ1fZd/uZaF9tXjViQ7P/sHuKdZvfLpvJq88JV5Pcdsfdlle86QAF4weB+k/k8f/xgvxRNbbcAfjLvEHhDBzfEvHkgFrW19WvLHyAqjjUovpecIu3KeCqwyOr1dHViUVelxqc5BklyGQ+Asd6GnWPSzO5Hamj4rYrapgogEup5PKm1j2CgL2HH2tySWwjgtOWbooGhsdBnCeQOsapCxwc6ALtudG4Q9RBu6A6pLUfFE3rm1RuvNGoJNHiEQ4BAFiqLpYJd4lR7V2fI6EIKug0dB3SpHpUeNCQbG67IM+kVe0I+vP3cECGOGXo="sv,
    };

    for (auto const& test : Tests)
    {
        auto tm = tr_torrent_metainfo{};
        tm.parseBenc(tr_base64_decode(test));
    }
}

TEST_F(TorrentMetainfoTest, parseBencFuzz)
{
    auto buf = std::vector<char>{};

    for (size_t i = 0; i < 100000U; ++i)
    {
        buf.resize(tr_rand_int(1024U));
        tr_rand_buffer(std::data(buf), std::size(buf));
        // std::cerr << '[' << tr_base64_encode({ std::data(buf), std::size(buf) }) << ']' << std::endl;

        auto tm = tr_torrent_metainfo{};
        tm.parseBenc({ std::data(buf), std::size(buf) });
    }
}

#if 0
TEST_F(TorrentMetainfoTest, sanitize)
{
    struct LocalTest
    {
        std::string_view input;
        std::string_view expected_output;
    };

    auto const tests = std::array<LocalTest, 29>{
        // skipped
        LocalTest{ ""sv, ""sv },
        { "."sv, ""sv },
        { ".."sv, ""sv },
        { "....."sv, ""sv },
        { "  "sv, ""sv },
        { " . "sv, ""sv },
        { ". . ."sv, ""sv },
        // replaced with '_'
        { "/"sv, "_"sv },
        { "////"sv, "____"sv },
        { "\\\\"sv, "__"sv },
        { "/../"sv, "_.._"sv },
        { "foo<bar:baz/boo"sv, "foo_bar_baz_boo"sv },
        { "t\0e\x01s\tt\ri\nn\fg"sv, "t_e_s_t_i_n_g"sv },
        // appended with '_'
        { "con"sv, "con_"sv },
        { "cOm4"sv, "cOm4_"sv },
        { "LPt9.txt"sv, "LPt9_.txt"sv },
        { "NUL.tar.gz"sv, "NUL_.tar.gz"sv },
        // trimmed
        { " foo"sv, "foo"sv },
        { "foo "sv, "foo"sv },
        { " foo "sv, "foo"sv },
        { "foo."sv, "foo"sv },
        { "foo..."sv, "foo"sv },
        { " foo... "sv, "foo"sv },
        // unmodified
        { "foo"sv, "foo"sv },
        { ".foo"sv, ".foo"sv },
        { "..foo"sv, "..foo"sv },
        { "foo.bar.baz"sv, "foo.bar.baz"sv },
        { "null"sv, "null"sv },
        { "compass"sv, "compass"sv },
    };

    auto out = std::string{};
    for (auto const& test : tests)
    {
        out.clear();
        auto const success = tr_metainfoAppendSanitizedPathComponent(out, test.input);
        EXPECT_EQ(!std::empty(out), success);
        EXPECT_EQ(test.expected_output, out);
    }
}
#endif

TEST_F(TorrentMetainfoTest, AndroidTorrent)
{
    auto const filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv };

    auto* ctor = tr_ctorNew(session_);
    tr_error* error = nullptr;
    EXPECT_TRUE(tr_ctorSetMetainfoFromFile(ctor, filename.c_str(), &error));
    EXPECT_EQ(nullptr, error) << *error;
    auto const* const metainfo = tr_ctorGetMetainfo(ctor);
    EXPECT_NE(nullptr, metainfo);
    EXPECT_EQ(336, metainfo->infoDictOffset());
    EXPECT_EQ(26583, metainfo->infoDictSize());
    EXPECT_EQ(592, metainfo->piecesOffset());
    tr_ctorFree(ctor);
}

TEST_F(TorrentMetainfoTest, ctorSaveContents)
{
    auto const src_filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/Android-x86 8.1 r6 iso.torrent"sv };
    auto const tgt_filename = tr_pathbuf{ ::testing::TempDir(), "save-contents-test.torrent" };

    // try saving without passing any metainfo.
    auto* ctor = tr_ctorNew(session_);
    tr_error* error = nullptr;
    EXPECT_FALSE(tr_ctorSaveContents(ctor, tgt_filename.sv(), &error));
    EXPECT_NE(nullptr, error);
    if (error != nullptr)
    {
        EXPECT_EQ(EINVAL, error->code);
        tr_error_clear(&error);
    }

    // now try saving _with_ metainfo
    EXPECT_TRUE(tr_ctorSetMetainfoFromFile(ctor, src_filename.c_str(), &error));
    EXPECT_EQ(nullptr, error) << *error;
    EXPECT_TRUE(tr_ctorSaveContents(ctor, tgt_filename.sv(), &error));
    EXPECT_EQ(nullptr, error) << *error;

    // the saved contents should match the source file's contents
    auto src_contents = std::vector<char>{};
    EXPECT_TRUE(tr_loadFile(src_filename.sv(), src_contents, &error));
    auto tgt_contents = std::vector<char>{};
    EXPECT_TRUE(tr_loadFile(tgt_filename.sv(), tgt_contents, &error));
    EXPECT_EQ(src_contents, tgt_contents);

    // cleanup
    EXPECT_TRUE(tr_sys_path_remove(tgt_filename, &error));
    EXPECT_EQ(nullptr, error) << *error;
    tr_error_clear(&error);
    tr_ctorFree(ctor);
}

TEST_F(TorrentMetainfoTest, magnetInfoHash)
{
    // compatibility with magnet torrents created by Transmission <= 3.0
    auto const src_filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/gimp-2.10.32-1-arm64.dmg.torrent"sv };
    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(src_filename));
}

TEST_F(TorrentMetainfoTest, HoffmanStyleWebseeds)
{
    auto const src_filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/debian-11.2.0-amd64-DVD-1.iso.torrent"sv };
    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(src_filename));
    EXPECT_EQ(size_t{ 2 }, tm.webseedCount());
    EXPECT_EQ(
        "https://cdimage.debian.org/cdimage/release/11.2.0//srv/cdbuilder.debian.org/dst/deb-cd/weekly-builds/amd64/iso-dvd/debian-11.2.0-amd64-DVD-1.iso"sv,
        tm.webseed(0));
    EXPECT_EQ(
        "https://cdimage.debian.org/cdimage/archive/11.2.0//srv/cdbuilder.debian.org/dst/deb-cd/weekly-builds/amd64/iso-dvd/debian-11.2.0-amd64-DVD-1.iso"sv,
        tm.webseed(1));
}

TEST_F(TorrentMetainfoTest, GetRightStyleWebseedList)
{
    auto const src_filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/webseed-getright-list.torrent"sv };
    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(src_filename));
    EXPECT_EQ(size_t{ 2 }, tm.webseedCount());
    EXPECT_EQ("http://www.webseed-one.com/"sv, tm.webseed(0));
    EXPECT_EQ("http://webseed-two.com/"sv, tm.webseed(1));
}

TEST_F(TorrentMetainfoTest, GetRightStyleWebseedString)
{
    auto const src_filename = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, "/webseed-getright-string.torrent"sv };
    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(src_filename));
    EXPECT_EQ(size_t{ 1 }, tm.webseedCount());
    EXPECT_EQ("http://www.webseed-one.com/"sv, tm.webseed(0));
}

// Test for https://github.com/transmission/transmission/issues/3591
TEST_F(TorrentMetainfoTest, parseBencOOBWrite)
{
    auto tm = tr_torrent_metainfo{};
    EXPECT_FALSE(tm.parseBenc(tr_base64_decode("ZGg0OmluZm9kNjpwaWVjZXMzOkFpzQ==")));
}

} // namespace libtransmission::test
