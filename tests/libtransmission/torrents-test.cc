// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <set>
#include <string_view>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent.h>
#include <libtransmission/torrents.h>

#include "gtest/gtest.h"

using namespace std::literals;

using TorrentsTest = ::testing::Test;

TEST_F(TorrentsTest, simpleTests)
{
    auto constexpr* const TorrentFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/Android-x86 8.1 r6 iso.torrent";

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};

    auto tm = tr_torrent_metainfo{};
    EXPECT_TRUE(tm.parseTorrentFile(TorrentFile));
    owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));
    auto* const tor = owned.back().get();

    auto torrents = tr_torrents{};
    EXPECT_TRUE(std::empty(torrents));
    EXPECT_EQ(0U, std::size(torrents));

    auto const id = torrents.add(tor);
    EXPECT_GT(id, 0);
    tor->unique_id_ = id;

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
}

TEST_F(TorrentsTest, rangedLoop)
{
    auto constexpr Filenames = std::array<std::string_view, 4>{ "Android-x86 8.1 r6 iso.torrent"sv,
                                                                "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
                                                                "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
                                                                "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    auto torrents = tr_torrents{};
    auto torrents_set = std::set<tr_torrent const*>{};

    for (auto const& name : Filenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parseTorrentFile(path));
        owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));

        auto* const tor = owned.back().get();
        tor->unique_id_ = torrents.add(tor);
        EXPECT_EQ(tor, torrents.get(tor->id()));
        torrents_set.insert(tor);
    }

    for (auto* const tor : torrents)
    {
        EXPECT_EQ(1U, torrents_set.erase(tor));
    }
    EXPECT_EQ(0U, std::size(torrents_set));
}

TEST_F(TorrentsTest, removedSince)
{
    auto constexpr Filenames = std::array<std::string_view, 4>{ "Android-x86 8.1 r6 iso.torrent"sv,
                                                                "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
                                                                "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
                                                                "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv };

    auto owned = std::vector<std::unique_ptr<tr_torrent>>{};
    auto torrents = tr_torrents{};
    auto torrents_v = std::vector<tr_torrent const*>{};
    torrents_v.reserve(std::size(Filenames));

    // setup: add the torrents
    for (auto const& name : Filenames)
    {
        auto const path = tr_pathbuf{ LIBTRANSMISSION_TEST_ASSETS_DIR, '/', name };
        auto tm = tr_torrent_metainfo{};
        EXPECT_TRUE(tm.parseTorrentFile(path));
        owned.emplace_back(std::make_unique<tr_torrent>(std::move(tm)));

        auto* const tor = owned.back().get();
        tor->unique_id_ = torrents.add(tor);
        torrents_v.push_back(tor);
    }

    // setup: remove them at the given timestamp
    auto constexpr TimeRemoved = std::array<time_t, 4>{ 100, 200, 200, 300 };
    for (size_t i = 0; i < 4; ++i)
    {
        auto* const tor = torrents_v[i];
        EXPECT_EQ(tor, torrents.get(tor->id()));
        torrents.remove(torrents_v[i], TimeRemoved[i]);
        EXPECT_EQ(nullptr, torrents.get(tor->id()));
    }

    auto remove = std::vector<tr_torrent_id_t>{};
    remove = { torrents_v[3]->id() };
    EXPECT_EQ(remove, torrents.removedSince(300));
    EXPECT_EQ(remove, torrents.removedSince(201));
    remove = { torrents_v[1]->id(), torrents_v[2]->id(), torrents_v[3]->id() };
    EXPECT_EQ(remove, torrents.removedSince(200));
    remove = { torrents_v[0]->id(), torrents_v[1]->id(), torrents_v[2]->id(), torrents_v[3]->id() };
    EXPECT_EQ(remove, torrents.removedSince(50));
}
