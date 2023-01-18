// This file Copyright (C) 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <string_view>

#include "gtest/gtest.h"

#include <libtransmission/transmission.h>

#include <libtransmission/magnet-metainfo.h>
#include <libtransmission/crypto-utils.h> // tr_rand_buffer()

using namespace std::literals;

TEST(MagnetMetainfo, magnetParse)
{
    auto constexpr ExpectedHash = tr_sha1_digest_t{ std::byte{ 210 }, std::byte{ 53 },  std::byte{ 64 },  std::byte{ 16 },
                                                    std::byte{ 163 }, std::byte{ 202 }, std::byte{ 74 },  std::byte{ 222 },
                                                    std::byte{ 91 },  std::byte{ 116 }, std::byte{ 39 },  std::byte{ 187 },
                                                    std::byte{ 9 },   std::byte{ 58 },  std::byte{ 98 },  std::byte{ 163 },
                                                    std::byte{ 137 }, std::byte{ 159 }, std::byte{ 243 }, std::byte{ 129 } };

    auto constexpr UriHex =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"sv;

    auto constexpr UriHexWithEmptyValue =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&empty"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"sv;

    auto constexpr UriHexWithJunkValues =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&empty"
        "&empty_again"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&empty_again"
        "&="
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"sv;

    auto constexpr UriBase32 =
        "magnet:?xt=urn:btih:"
        "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"sv;

    for (auto const& uri : { UriHex, UriHexWithEmptyValue, UriHexWithJunkValues, UriBase32 })
    {
        auto mm = tr_magnet_metainfo{};

        EXPECT_TRUE(mm.parseMagnet(uri));
        EXPECT_EQ(2U, std::size(mm.announceList()));
        auto it = std::begin(mm.announceList());
        EXPECT_EQ(0U, it->tier);
        EXPECT_EQ("http://tracker.openbittorrent.com/announce"sv, it->announce.sv());
        EXPECT_EQ("http://tracker.openbittorrent.com/scrape"sv, it->scrape.sv());
        ++it;
        EXPECT_EQ(1U, it->tier);
        EXPECT_EQ("http://tracker.opentracker.org/announce", it->announce.sv());
        EXPECT_EQ("http://tracker.opentracker.org/scrape", it->scrape.sv());
        EXPECT_EQ(1U, mm.webseedCount());
        EXPECT_EQ("http://server.webseed.org/path/to/file"sv, mm.webseed(0));
        EXPECT_EQ("Display Name"sv, mm.name());
        EXPECT_EQ(ExpectedHash, mm.infoHash());
    }

    for (auto const& uri : { "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"sv, "d2354010a3ca4ade5b7427bb093a62a3899ff381"sv })
    {
        auto mm = tr_magnet_metainfo{};

        EXPECT_TRUE(mm.parseMagnet(uri));
        EXPECT_EQ(0U, std::size(mm.announceList()));
        EXPECT_EQ(0U, mm.webseedCount());
        EXPECT_EQ(ExpectedHash, mm.infoHash());
    }
}

TEST(WebUtilsTest, parseMagnetFuzzRegressions)
{
    static auto constexpr Tests = std::array<std::string_view, 1>{
        "UICOl7RLjChs/QZZwNH4sSQwuH890UMHuoxoWBmMkr0=",
    };

    for (auto const& test : Tests)
    {
        auto mm = tr_magnet_metainfo{};
        mm.parseMagnet(tr_base64_decode(test));
    }
}

TEST(WebUtilsTest, parseMagnetFuzz)
{
    auto buf = std::array<char, 1024>{};

    for (size_t i = 0; i < 100000; ++i)
    {
        auto const len = static_cast<size_t>(tr_rand_int(1024U));
        tr_rand_buffer(std::data(buf), len);
        auto mm = tr_magnet_metainfo{};
        EXPECT_FALSE(mm.parseMagnet({ std::data(buf), len }));
    }
}
