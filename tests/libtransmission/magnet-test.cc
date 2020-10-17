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

TEST(Magnet, magnetParse)
{
    auto const expected_hash = std::array<uint8_t, SHA_DIGEST_LENGTH>{
        210, 53, 64, 16, 163, 202, 74, 222, 91, 116,
        39, 187, 9, 58, 98, 163, 137, 159, 243, 129
    };

    char const* const uri_hex =
        "magnet:?xt=urn:btih:"
        "d2354010a3ca4ade5b7427bb093a62a3899ff381"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile";

    char const* const uri_base32 =
        "magnet:?xt=urn:btih:"
        "2I2UAEFDZJFN4W3UE65QSOTCUOEZ744B"
        "&dn=Display%20Name"
        "&tr=http%3A%2F%2Ftracker.openbittorrent.com%2Fannounce"
        "&ws=http%3A%2F%2Fserver.webseed.org%2Fpath%2Fto%2Ffile"
        "&tr=http%3A%2F%2Ftracker.opentracker.org%2Fannounce";

    for (auto const& uri : { uri_hex, uri_base32 })
    {
        auto* info = tr_magnetParse(uri);
        EXPECT_NE(nullptr, info);
        EXPECT_EQ(2, info->trackerCount);
        EXPECT_STREQ("http://tracker.openbittorrent.com/announce", info->trackers[0]);
        EXPECT_STREQ("http://tracker.opentracker.org/announce", info->trackers[1]);
        EXPECT_EQ(1, info->webseedCount);
        EXPECT_STREQ("http://server.webseed.org/path/to/file", info->webseeds[0]);
        EXPECT_STREQ("Display Name", info->displayName);
        EXPECT_EQ(expected_hash.size(), sizeof(info->hash));
        EXPECT_EQ(0, memcmp(info->hash, expected_hash.data(), expected_hash.size()));
        tr_magnetFree(info);
    }
}
