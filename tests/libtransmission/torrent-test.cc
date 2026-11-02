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
#include <ranges>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/log.h>
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

[[nodiscard]] std::future<std::vector<tr_stat>> statAsync(tr_session* const session)
{
    // shared, so that a callback that runs after the caller stops waiting still has its promise
    auto const promise = std::make_shared<std::promise<std::vector<tr_stat>>>();
    auto future = promise->get_future();
    tr_torrentStatAsync(session, [promise](std::vector<tr_stat> stats) { promise->set_value(std::move(stats)); });
    return future;
}
} // namespace

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

TEST_F(TorrentTest, statAsyncCallsBackOnTheSessionThread)
{
    auto const on_session_thread = std::make_shared<std::promise<bool>>();
    auto future = on_session_thread->get_future();
    tr_torrentStatAsync(
        session_,
        [session = session_, on_session_thread](std::vector<tr_stat> const& /*stats*/)
        { on_session_thread->set_value(session->am_in_session_thread()); });

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());
}

TEST_F(TorrentTest, statAsyncRunsAfterQueuedWork)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    tr_torrentStartNow(tor);
    ASSERT_EQ(TR_STATUS_SEED, tr_torrentStat(tor).activity);

    // Hold the session thread, so that the stop is still queued when the stats are requested.
    auto release = std::promise<void>{};
    session_->queue_session_thread([released = release.get_future().share()] { released.wait(); });
    tr_torrentStop(tor);
    EXPECT_EQ(TR_STATUS_SEED, tr_torrentStat(tor).activity); // a synchronous stat runs ahead of the stop
    auto future = statAsync(session_);
    release.set_value();

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto const stats = future.get();
    ASSERT_EQ(1U, std::size(stats));
    EXPECT_EQ(TR_STATUS_STOPPED, stats.front().activity);

    tr_torrentRemove(tor, false);
}

TEST_F(TorrentTest, statAsyncReportsEveryTorrentById)
{
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::ranges::transform(
        TorFilenames,
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });

    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto stats = future.get();
    ASSERT_EQ(std::size(torrents), std::size(stats));
    for (auto const* const tor : torrents)
    {
        auto const iter = std::ranges::find(stats, tor->id(), &tr_stat::id);
        ASSERT_NE(std::end(stats), iter);
        EXPECT_EQ(tor->total_size(), iter->size_when_done); // each torrent has a different size
    }

    // the request queues behind the removal
    auto const removed_id = torrents.front()->id();
    tr_torrentRemove(torrents.front(), false);
    future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    stats = future.get();
    EXPECT_EQ(std::size(torrents) - 1U, std::size(stats));
    EXPECT_EQ(std::end(stats), std::ranges::find(stats, removed_id, &tr_stat::id));
}

TEST_F(TorrentTest, statAsyncIgnoresAnEmptyCallback)
{
    auto const old_level = tr_logGetLevel();
    tr_logClearQueue();
    tr_logSetLevel(TR_LOG_WARN);
    tr_logSetQueueEnabled(true);

    tr_torrentStatAsync(session_, {});
    auto const msgs = tr_logGetQueue();
    EXPECT_NE(std::end(msgs), std::ranges::find(msgs, "callback != nullptr"sv, &tr_log_message::message));

    // the session thread survives to answer the next request
    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(std::empty(future.get()));

    tr_logSetQueueEnabled(false);
    tr_logSetLevel(old_level);
}
