/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include "transmission.h"
#include "magnet.h"
#include "utils.h"

#include "gtest/gtest.h"

#include <array>
#include <string_view>

using namespace std::literals;

#include <iostream>

TEST(Magnet, magnetParse)
{
    auto constexpr ExpectedHash = std::array<uint8_t, SHA_DIGEST_LENGTH>{
        210, 53,  64, 16, 163, 202, 74,  222, 91,  116, //
        39,  187, 9,  58, 98,  163, 137, 159, 243, 129, //
    };

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
        std::cerr << __FILE__ << ':' << __LINE__ << " uri [" << uri << ']' << std::endl;

        auto* info = tr_magnetParse(uri);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_NE(nullptr, info);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_EQ(2, info->trackerCount);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_STREQ("http://tracker.openbittorrent.com/announce", info->trackers[0]);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_STREQ("http://tracker.opentracker.org/announce", info->trackers[1]);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_EQ(1, info->webseedCount);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_STREQ("http://server.webseed.org/path/to/file", info->webseeds[0]);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_STREQ("Display Name", info->displayName);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_EQ(std::size(ExpectedHash), sizeof(info->hash));
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        EXPECT_EQ(0, memcmp(info->hash, std::data(ExpectedHash), std::size(ExpectedHash)));
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
        tr_magnetFree(info);
        std::cerr << __FILE__ << ':' << __LINE__ << std::endl;
    }
}
