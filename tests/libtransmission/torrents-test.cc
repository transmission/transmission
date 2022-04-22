// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring>
#include <set>
#include <string_view>

#include "transmission.h"

#include "torrent.h"
#include "torrents.h"
#include "utils.h"

#include "gtest/gtest.h"

using namespace std::literals;

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
    EXPECT_GT(id, 0);
    tor->uniqueId = id;

    EXPECT_TRUE(std::empty(torrents.removedSince(0)));
    EXPECT_FALSE(std::empty(torrents));
    EXPECT_EQ(1U, std::size(torrents));

    EXPECT_EQ(tor, torrents.get(id));
    EXPECT_EQ(tor, torrents.get(tor->infoHash()));
    EXPECT_EQ(tor, torrents.get(tor->magnet()));

    tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(TorrentFile));
    EXPECT_EQ(tor, torrents.get(tm));

    // cleanup
    torrents.remove(tor, time(nullptr));
    delete tor;
}

TEST_F(TorrentsTest, rangedLoop)
{
    auto constexpr Filenames = std::array<std::string_view, 4>{ "Android-x86 8.1 r6 iso.torrent"sv,
                                                                "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
                                                                "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
                                                                "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv };

    auto torrents = tr_torrents{};
    auto torrents_set = std::set<tr_torrent const*>{};

    for (auto const& name : Filenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parseTorrentFile(path));
        auto* const tor = new tr_torrent{ std::move(tm) };
        tor->uniqueId = torrents.add(tor);
        EXPECT_EQ(tor, torrents.get(tor->uniqueId));
        torrents_set.insert(tor);
    }

    for (auto* const tor : torrents)
    {
        EXPECT_EQ(1U, torrents_set.erase(tor));
        delete tor;
    }
    EXPECT_EQ(0U, std::size(torrents_set));
    EXPECT_EQ(0U, std::size(torrents_set));
}

TEST_F(TorrentsTest, removedSince)
{
    auto constexpr Filenames = std::array<std::string_view, 4>{ "Android-x86 8.1 r6 iso.torrent"sv,
                                                                "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
                                                                "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
                                                                "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv };

    auto torrents = tr_torrents{};
    auto torrents_v = std::vector<tr_torrent const*>{};
    torrents_v.reserve(std::size(Filenames));

    // setup: add the torrents
    for (auto const& name : Filenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        auto* const tor = new tr_torrent{ std::move(tm) };
        tor->uniqueId = torrents.add(tor);
        torrents_v.push_back(tor);
    }

    // setup: remove them at the given timestamp
    auto constexpr TimeRemoved = std::array<time_t, 4>{ 100, 200, 200, 300 };
    for (size_t i = 0; i < 4; ++i)
    {
        auto* const tor = torrents_v[i];
        EXPECT_EQ(tor, torrents.get(tor->uniqueId));
        torrents.remove(torrents_v[i], TimeRemoved[i]);
        EXPECT_EQ(nullptr, torrents.get(tor->uniqueId));
    }

    auto remove = std::vector<int>{};
    remove = { torrents_v[3]->uniqueId };
    EXPECT_EQ(remove, torrents.removedSince(300));
    EXPECT_EQ(remove, torrents.removedSince(201));
    remove = { torrents_v[1]->uniqueId, torrents_v[2]->uniqueId, torrents_v[3]->uniqueId };
    EXPECT_EQ(remove, torrents.removedSince(200));
    remove = { torrents_v[0]->uniqueId, torrents_v[1]->uniqueId, torrents_v[2]->uniqueId, torrents_v[3]->uniqueId };
    EXPECT_EQ(remove, torrents.removedSince(50));

    std::for_each(std::begin(torrents_v), std::end(torrents_v), [](auto* tor) { delete tor; });
}
