// This file Copyright (C) 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t, std::byte
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <libtransmission/crypto-utils.h> // tr_rand_buffer()
#include <libtransmission/magnet-metainfo.h>
#include <libtransmission/tr-macros.h>

#include "test-fixtures.h"

using MagnetMetainfoTest = ::tr::test::TransmissionTest;
using namespace std::literals;

TEST_F(MagnetMetainfoTest, magnetParse)
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

    // UriHex again, spelled the way a URI library that insists on an authority
    // writes it.
    auto constexpr UriHexEmptyAuthority =
        "magnet:///?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"sv;

    for (auto const& uri : { UriHex, UriHexWithEmptyValue, UriHexWithJunkValues, UriBase32, UriHexEmptyAuthority })
    {
        auto mm = tr_magnet_metainfo{};

        ASSERT_TRUE(mm.parseMagnet(uri)) << uri;
        EXPECT_EQ(2U, std::size(mm.announce_list()));
        auto it = std::begin(mm.announce_list());
        EXPECT_EQ(0U, it->tier);
        EXPECT_EQ("http://tracker.openbittorrent.com/announce"sv, it->announce.sv());
        EXPECT_EQ("http://tracker.openbittorrent.com/scrape"sv, it->scrape.sv());
        ++it;
        EXPECT_EQ(1U, it->tier);
        EXPECT_EQ("http://tracker.opentracker.org/announce", it->announce.sv());
        EXPECT_EQ("http://tracker.opentracker.org/scrape", it->scrape.sv());
        EXPECT_EQ(1U, mm.webseed_count());
        EXPECT_EQ("http://server.webseed.org/path/to/file"sv, mm.webseed(0));
        EXPECT_EQ("Display Name"sv, mm.name());
        EXPECT_EQ(ExpectedHash, mm.info_hash());
    }

    for (auto const& uri : { "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"sv, "d2354010a3ca4ade5b7427bb093a62a3899ff381"sv })
    {
        auto mm = tr_magnet_metainfo{};

        EXPECT_TRUE(mm.parseMagnet(uri));
        EXPECT_EQ(0U, std::size(mm.announce_list()));
        EXPECT_EQ(0U, mm.webseed_count());
        EXPECT_EQ(ExpectedHash, mm.info_hash());
    }
}

TEST_F(MagnetMetainfoTest, magnetParsePeers)
{
    // https://www.bittorrent.org/beps/bep_0009.html - "x.pe" peer addresses
    auto constexpr Uri =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&x.pe=" // empty
        "&x.pe=192.0.2.1" // no port
        "&x.pe=example.com%3A6881" // hostname, not a literal address
        "&x.pe=not-an-address"
        "&x.pe=0.0.0.0%3A6881" // current network
        "&x.pe=%5B%3A%3A%5D%3A6881" // unspecified
        "&x.pe=224.0.0.1%3A6881" // multicast
        "&x.pe=%5Bfe80%3A%3A1%5D%3A6881" // link-local
        "&x.pe=192.0.2.1%3A6881"
        "&x.pe=192.0.2.1%3A6881" // duplicate of the previous one
        "&x.pe=%5B%3A%3Affff%3A192.0.2.2%5D%3A6881" // ipv4-mapped, kept unmapped
        "&x.pe=127.0.0.1%3A6881" // loopback is usable: the user named it
        "&x.pe=%5B2001%3Adb8%3A%3A1%5D%3A6882"sv;

    auto mm = tr_magnet_metainfo{};
    ASSERT_TRUE(mm.parseMagnet(Uri));

    auto const& peers = mm.peers();
    ASSERT_EQ(4U, std::size(peers));
    EXPECT_EQ("192.0.2.1:6881"sv, peers[0]);
    EXPECT_EQ("192.0.2.2:6881"sv, peers[1]);
    EXPECT_EQ("127.0.0.1:6881"sv, peers[2]);
    EXPECT_EQ("[2001:db8::1]:6882"sv, peers[3]);
}

TEST_F(MagnetMetainfoTest, magnetPeersRoundTrip)
{
    // x.pe has to survive magnet(), since that is what gets saved to the
    // .magnet file and returned by tr_torrentGetMagnetLink()
    auto constexpr Uri =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&x.pe=192.0.2.1%3A6881"
        "&x.pe=%5B2001%3Adb8%3A%3A1%5D%3A6882"sv;

    auto mm = tr_magnet_metainfo{};
    ASSERT_TRUE(mm.parseMagnet(Uri));

    auto round_trip = tr_magnet_metainfo{};
    ASSERT_TRUE(round_trip.parseMagnet(mm.magnet()));
    EXPECT_EQ(mm.peers(), round_trip.peers());
}

TEST_F(MagnetMetainfoTest, magnetParseCapsPeers)
{
    static auto constexpr MaxPeers = size_t{ 200U };

    auto uri = std::string{ "magnet:?xt=urn:btih:d2354010a3ca4ade5b7427bb093a62a3899ff381" };
    for (size_t i = 0; i < MaxPeers + 10U; ++i)
    {
        uri += fmt::format("&x.pe=192.0.2.1%3A{:d}", 1024U + i);
    }

    auto mm = tr_magnet_metainfo{};
    ASSERT_TRUE(mm.parseMagnet(uri));
    EXPECT_EQ(MaxPeers, std::size(mm.peers()));
}

TEST_F(MagnetMetainfoTest, parseMagnetFuzzRegressions)
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

TEST_F(MagnetMetainfoTest, parseMagnetFuzz)
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
