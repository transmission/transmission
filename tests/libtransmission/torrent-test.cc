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
#include <string>
#include <utility>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/log.h>
#include <libtransmission/torrent.h>
#include <libtransmission/utils.h>

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

TEST_F(TorrentTest, setAnnounceListReportsSaveFailure)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);

    // the trackers can't be saved into a .torrent file that has no info dict
    auto error = tr_error{};
    EXPECT_TRUE(tr_file_save(tor->torrent_file(), "d7:comment3:fooe", &error)) << error;
    EXPECT_FALSE(tor->set_announce_list("https://a.example.com/announce"));
    EXPECT_EQ(TR_STAT_LOCAL_ERROR, tor->error().error_type());
    EXPECT_NE(std::string::npos, tor->error().errmsg().find("missing 'info' dict"));
}

namespace
{
[[nodiscard]] std::future<std::vector<tr_stat_snapshot>> statAsync(tr_session* const session)
{
    // shared, so that a callback that runs after the caller stops waiting still has its promise
    auto const promise = std::make_shared<std::promise<std::vector<tr_stat_snapshot>>>();
    auto future = promise->get_future();
    tr_torrentStatAsync(
        session,
        [promise](std::vector<tr_stat_snapshot> snapshots) { promise->set_value(std::move(snapshots)); });
    return future;
}
} // namespace

TEST_F(TorrentTest, statAsyncCallsBackOnTheSessionThread)
{
    auto const on_session_thread = std::make_shared<std::promise<bool>>();
    auto future = on_session_thread->get_future();
    tr_torrentStatAsync(
        session_,
        [session = session_, on_session_thread](std::vector<tr_stat_snapshot> const& /*snapshots*/)
        { on_session_thread->set_value(session->am_in_session_thread()); });

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(future.get());
}

TEST_F(TorrentTest, statAsyncRunsAfterQueuedWork)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(nullptr, tor);
    tr_torrentStartNow(tor);
    ASSERT_TRUE(libtransmission::test::waitFor([tor]() { return tr_torrentStat(tor)->activity == TR_STATUS_SEED; }, 5000));

    // Hold the session thread, so that the stop is still queued when the stats are requested.
    auto release = std::promise<void>{};
    session_->queue_session_thread([released = release.get_future().share()] { released.wait(); });
    tr_torrentStop(tor);
    EXPECT_EQ(TR_STATUS_SEED, tr_torrentStat(tor)->activity); // a synchronous stat runs ahead of the stop
    auto future = statAsync(session_);
    release.set_value();

    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto const snapshots = future.get();
    ASSERT_EQ(1U, std::size(snapshots));
    EXPECT_EQ(TR_STATUS_STOPPED, snapshots.front().stat.activity);

    tr_torrentRemove(tor, false, nullptr, nullptr);
}

TEST_F(TorrentTest, statAsyncReportsEveryTorrentById)
{
    auto torrents = std::array<tr_torrent*, TorFilenames.size()>{};
    std::transform(
        TorFilenames.begin(),
        TorFilenames.end(),
        torrents.begin(),
        [this](auto const filename) { return torrentInitFromFile(filename); });

    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto snapshots = future.get();
    ASSERT_EQ(std::size(torrents), std::size(snapshots));
    for (auto const* const tor : torrents)
    {
        auto const iter = std::find_if(
            std::begin(snapshots),
            std::end(snapshots),
            [tor](auto const& snapshot) { return snapshot.stat.id == tor->id(); });
        ASSERT_NE(std::end(snapshots), iter);
        EXPECT_EQ(tor->total_size(), iter->stat.sizeWhenDone); // each torrent has a different size
    }

    // the request queues behind the removal
    auto const removed_id = torrents.front()->id();
    tr_torrentRemove(torrents.front(), false, nullptr, nullptr);
    future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    snapshots = future.get();
    EXPECT_EQ(std::size(torrents) - 1U, std::size(snapshots));
    EXPECT_TRUE(
        std::none_of(
            std::begin(snapshots),
            std::end(snapshots),
            [removed_id](auto const& snapshot) { return snapshot.stat.id == removed_id; }));
}

TEST_F(TorrentTest, statAsyncErrorStringOutlivesTheTorrentsError)
{
    // too long for the small-string optimization, so that a stale pointer into the torrent would really dangle
    static auto constexpr OldError = "a local error whose text is too long to fit in a small-string buffer"sv;
    static auto constexpr NewError = "the error that replaces it, also too long for a small-string buffer"sv;

    auto* const tor = zeroTorrentInit(ZeroTorrentState::NoFiles);
    ASSERT_NE(nullptr, tor);
    {
        auto const lock = tor->unique_lock();
        tor->error().set_local_error(OldError);
    }

    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    auto snapshots = future.get();
    ASSERT_EQ(1U, std::size(snapshots));
    auto& snapshot = snapshots.front();
    EXPECT_EQ(TR_STAT_LOCAL_ERROR, snapshot.stat.error);

    // nothing in the snapshot may borrow from the torrent, which can change its error at any time
    EXPECT_EQ(nullptr, snapshot.stat.errorString);
    {
        auto const lock = tor->unique_lock();
        tor->error().set_local_error(NewError);
    }
    EXPECT_EQ(OldError, snapshot.error_string);

    {
        auto const lock = tor->unique_lock();
        tor->error().clear();
    }
    EXPECT_EQ(OldError, snapshot.error_string);

    // a receiver re-points errorString at its final copy of the text
    auto const owned_text = std::move(snapshot.error_string);
    snapshot.stat.errorString = owned_text.c_str();
    EXPECT_EQ(OldError, snapshot.stat.errorString);
}

TEST_F(TorrentTest, statAsyncIgnoresAnEmptyCallback)
{
    auto const old_level = tr_logGetLevel();
    tr_logFreeQueue(tr_logGetQueue());
    tr_logSetLevel(TR_LOG_WARN);
    tr_logSetQueueEnabled(true);

    tr_torrentStatAsync(session_, {});

    auto found = false;
    tr_log_message* const msgs = tr_logGetQueue();
    for (auto const* walk = msgs; walk != nullptr; walk = walk->next)
    {
        found |= walk->message == "callback != nullptr"sv;
    }
    EXPECT_TRUE(found);
    tr_logFreeQueue(msgs);

    // the session thread survives to answer the next request
    auto future = statAsync(session_);
    ASSERT_EQ(std::future_status::ready, future.wait_for(5s));
    EXPECT_TRUE(std::empty(future.get()));

    tr_logSetQueueEnabled(false);
    tr_logSetLevel(old_level);
}
