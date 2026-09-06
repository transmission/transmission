// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <atomic>
#include <cstddef>
#include <future>
#include <ranges>
#include <thread>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent.h>

#include "test-fixtures.h"

using TorrentTest = tr::test::SessionTest;

namespace
{
auto constexpr TorFilenames = std::array{
    "Android-x86 8.1 r6 iso.torrent"sv,
    "debian-11.2.0-amd64-DVD-1.iso.torrent"sv,
    "ubuntu-18.04.6-desktop-amd64.iso.torrent"sv,
    "ubuntu-20.04.4-desktop-amd64.iso.torrent"sv,
};
}

TEST_F(TorrentTest, queueMoveUp)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 1, 3, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_up(move_torrents);

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
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_down(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, queueMoveTop)
{
    static constexpr auto ExpectedQueuePosition = std::array{ 0, 3, 1, 2 };
    auto ctor = tr_ctor{ session_ };
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[2], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_top(move_torrents);

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
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });
    auto const move_torrents = std::array{ torrents[0], torrents[1], torrents[3] };

    // Pre-test sanity checks
    for (size_t i = 0; i < torrents.size(); ++i)
    {
        ASSERT_EQ(i, torrents[i]->queue_position());
        ASSERT_EQ(i + 1U, torrents[i]->id());
    }

    tr_torrent::queue_move_bottom(move_torrents);

    for (size_t i = 0; i < ExpectedQueuePosition.size(); ++i)
    {
        EXPECT_EQ(ExpectedQueuePosition[i], torrents[i]->queue_position()) << i;
    }
}

TEST_F(TorrentTest, statDoesNotBlockWhenSessionLockIsContended)
{
    auto* const tor = torrentInitFromFile(TorFilenames[0]);

    // refresh the snapshot with an uncontended call
    auto const baseline = tr_torrentStat(tor);

    // hold the session lock on another thread, simulating a session
    // thread that's stuck in slow disk I/O
    auto lock_held = std::atomic<bool>{ false };
    auto locked = std::promise<void>{};
    auto release = std::promise<void>{};
    auto blocker = std::thread(
        [tor, &lock_held, &locked, &release]()
        {
            auto const lock = tor->unique_lock();
            lock_held = true;
            locked.set_value();
            release.get_future().wait();
            lock_held = false;
        });
    locked.get_future().wait();

    // `lock_held` can only be cleared after `release` is set, so if these
    // calls had blocked on the lock, it would still read true afterwards
    auto const stats = tr_torrentStat(tor);
    EXPECT_TRUE(lock_held) << "tr_torrentStat() blocked until the session lock was released";
    EXPECT_EQ(baseline.id, stats.id);
    EXPECT_EQ(baseline.activity, stats.activity);

    auto const batched = tr_torrentStat(&tor, 1U);
    EXPECT_TRUE(lock_held) << "batched tr_torrentStat() blocked until the session lock was released";
    ASSERT_EQ(1U, std::size(batched));
    EXPECT_EQ(baseline.id, batched.front().id);

    release.set_value();
    blocker.join();

    auto const fresh = tr_torrentStat(tor);
    EXPECT_EQ(baseline.id, fresh.id);
}
