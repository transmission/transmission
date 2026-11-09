// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent.h>

#include "test-fixtures.h"

using TorrentTest = libtransmission::test::SessionTest;

namespace
{
auto constexpr TorFilenames = std::array{
    "Android-x86 8.1 r6 iso.torrent"sv,
    "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
    "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
    "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
};
}

TEST_F(TorrentTest, peersAsyncCallsBackOnTheSessionThread)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);

    auto const on_session_thread = std::make_shared<std::promise<bool>>();
    auto future = on_session_thread->get_future();
    tr_torrentPeersAsync(
        tor,
        [session = session_, on_session_thread](std::vector<tr_peer_stat> const& peers)
        {
            EXPECT_TRUE(std::empty(peers)); // nothing is connected
            on_session_thread->set_value(session->am_in_session_thread());
        });
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());
}

TEST_F(TorrentTest, peersAsyncReportsNoPeersForARemovedTorrent)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);

    // Hold the session thread, so that the removal runs before the peers request.
    auto release = std::promise<void>{};
    session_->queue_session_thread([released = release.get_future().share()] { released.wait(); });
    tr_torrentRemove(tor, false, nullptr, nullptr);

    auto const peer_count = std::make_shared<std::promise<size_t>>();
    auto future = peer_count->get_future();
    tr_torrentPeersAsync(
        tor,
        [peer_count](std::vector<tr_peer_stat> const& peers) { peer_count->set_value(std::size(peers)); });

    release.set_value();
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_EQ(0U, future.get());
}

TEST_F(TorrentTest, queueMoveUp)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 1, 3, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::transform(
        TorFilenames.begin(),
        TorFilenames.end(),
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrentsQueueMoveUp(move_torrents.data(), move_torrents.size());

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveDown)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 0, 2, 3 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::transform(
        TorFilenames.begin(),
        TorFilenames.end(),
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrentsQueueMoveDown(move_torrents.data(), move_torrents.size());

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, useSessionLimitsAffectsBothDirections)
{
    auto* const tor = torrentInitFromFile(TorFilenames[0]);
    ASSERT_NE(nullptr, tor);

    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(TR_UP));
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(TR_DOWN));

    tr_torrentUseSessionLimits(tor, false);
    EXPECT_FALSE(tor->bandwidth().are_parent_limits_honored(TR_UP));
    EXPECT_FALSE(tor->bandwidth().are_parent_limits_honored(TR_DOWN));

    tr_torrentUseSessionLimits(tor, true);
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(TR_UP));
    EXPECT_TRUE(tor->bandwidth().are_parent_limits_honored(TR_DOWN));
}

TEST_F(TorrentTest, queueMoveTop)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 3, 1, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::transform(
        TorFilenames.begin(),
        TorFilenames.end(),
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrentsQueueMoveTop(move_torrents.data(), move_torrents.size());

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveBottom)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 1, 2, 0, 3 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::transform(
        TorFilenames.begin(),
        TorFilenames.end(),
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrentsQueueMoveBottom(move_torrents.data(), move_torrents.size());

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}
