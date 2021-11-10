/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "magnet-metainfo.h"
#include "utils.h"

#include "gtest/gtest.h"

#include <array>
#include <string_view>

using namespace std::literals;

TEST(MagnetMetainfo, magnetParse)
{
    auto constexpr ExpectedHash = tr_sha1_digest_t{ std::byte(210), std::byte(53),  std::byte(64),  std::byte(16),
                                                    std::byte(163), std::byte(202), std::byte(74),  std::byte(222),
                                                    std::byte(91),  std::byte(116), std::byte(39),  std::byte(187),
                                                    std::byte(9),   std::byte(58),  std::byte(98),  std::byte(163),
                                                    std::byte(137), std::byte(159), std::byte(243), std::byte(129) };

    auto constexpr UriHex =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"sv;

    auto constexpr UriBase32 =
        "magnet:?xt=urn:btih:"
        "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"sv;

    for (auto const& uri : { UriHex, UriBase32 })
    {
        auto mm = tr_magnet_metainfo{};

        EXPECT_TRUE(mm.parseMagnet(uri));
        EXPECT_EQ(2, std::size(mm.trackers));
        auto it = std::begin(mm.trackers);
        EXPECT_EQ(0, it->first);
        EXPECT_EQ("http://tracker.openbittorrent.com/announce"sv, tr_quark_get_string_view(it->second.announce_url));
        EXPECT_EQ("http://tracker.openbittorrent.com/scrape"sv, tr_quark_get_string_view(it->second.scrape_url));
        ++it;
        EXPECT_EQ(1, it->first);
        EXPECT_EQ("http://tracker.opentracker.org/announce", tr_quark_get_string_view(it->second.announce_url));
        EXPECT_EQ("http://tracker.opentracker.org/scrape", tr_quark_get_string_view(it->second.scrape_url));
        EXPECT_EQ(1, std::size(mm.webseed_urls));
        EXPECT_EQ("http://server.webseed.org/path/to/file"sv, mm.webseed_urls.front());
        EXPECT_EQ("Display Name"sv, mm.name);
        EXPECT_EQ(ExpectedHash, mm.info_hash);
    }
}
