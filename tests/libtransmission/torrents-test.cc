// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "torrent.h"
#include "torrents.h"

#include "gtest/gtest.h"

#include <cstring>

using TorrentsTest = ::testing::Test;

TEST_F(TorrentsTest, simpleTests)
{
    auto constexpr* const TorrentFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/Android-x86 8.1 r6 iso.torrent";
    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(TorrentFile));
    auto* tor = new tr_torrent(std::move(tm));
    EXPECT_NE(nullptr, tor);

    auto torrents = tr_torrents{};
    EXPECT_TRUE(std::empty(torrents));
    EXPECT_EQ(0U, std::size(torrents));

    auto const id = torrents.add(tor);
    tor->uniqueId = id;

    EXPECT_TRUE(std::empty(torrents.removedSince(0)));
    EXPECT_FALSE(std::empty(torrents));
    EXPECT_EQ(1U, std::size(torrents));

    EXPECT_EQ(tor, torrents.fromId(id));
    EXPECT_EQ(tor, torrents.fromHash(tor->infoHash()));
    EXPECT_EQ(tor, torrents.fromMagnet(tor->magnet()));

    tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(TorrentFile));
    EXPECT_EQ(tor, torrents.fromMetainfo(tm));

    // cleanup
    torrents.remove(tor, time(nullptr));
    delete tor;
}

TEST_F(TorrentsTest, rangedLoop)
{
}

TEST_F(TorrentsTest, removedSince)
{
}
