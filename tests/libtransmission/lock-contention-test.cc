// Regression test: empirically measures whether tr_torrentStat() blocks on
// the calling thread while the session thread holds session_->unique_lock()
// for an extended period (simulating a slow synchronous disk write happening
// inside tr_peerIo::can_read_wrapper()), and that tr_torrentTryStat() does not.

#include <array>
#include <chrono>
#include <future>
#include <thread>

#include <fmt/format.h>
#include <gtest/gtest.h>

#include <libtransmission/transmission.h>
#include <libtransmission/session.h> // tr_session::unique_lock()

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{

class LockContentionTest : public SessionTest
{
};

TEST_F(LockContentionTest, TrTorrentStatBlocksWhileSessionLockIsHeld)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    static constexpr auto SimulatedWriteDelay = 300ms;

    auto lock_acquired = std::promise<void>{};
    auto lock_acquired_future = lock_acquired.get_future();

    auto writer = std::thread(
        [this, &lock_acquired]()
        {
            auto const lock = session_->unique_lock();
            lock_acquired.set_value();
            std::this_thread::sleep_for(SimulatedWriteDelay);
        });

    // The promise only fires after the writer's unique_lock() has already
    // succeeded, and the writer holds it continuously through its sleep, so
    // the lock is guaranteed held here already -- no extra wall-clock head
    // start needed (or safe to rely on) beyond this synchronization point.
    lock_acquired_future.wait();

    auto const t0 = std::chrono::steady_clock::now();
    auto const stat = tr_torrentStat(tor);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    writer.join();

    (void)stat;

    fmt::print(
        "tr_torrentStat() took {:d}ms while another thread held session_->unique_lock() for {:d}ms\n",
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(),
        std::chrono::duration_cast<std::chrono::milliseconds>(SimulatedWriteDelay).count());

    EXPECT_GE(elapsed, SimulatedWriteDelay - 50ms) << "tr_torrentStat() did NOT block on the held session lock -- "
                                                   << "either the lock requirement is gone, or something else changed.";
}

TEST_F(LockContentionTest, TrTorrentStatIsFastWithNoContention)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    auto const t0 = std::chrono::steady_clock::now();
    auto const stat = tr_torrentStat(tor);
    auto const elapsed = std::chrono::steady_clock::now() - t0;
    (void)stat;

    fmt::print(
        "tr_torrentStat() took {:d}us with no lock contention (baseline)\n",
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());

    EXPECT_LT(elapsed, 50ms);
}

// Candidate fix: tr_torrentTryStat() should return immediately (false, stat
// left untouched) instead of blocking when the session thread holds the
// lock, e.g. mid disk-write.
TEST_F(LockContentionTest, TrTorrentTryStatDoesNotBlockWhileSessionLockIsHeld)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    static constexpr auto SimulatedWriteDelay = 300ms;

    auto lock_acquired = std::promise<void>{};
    auto lock_acquired_future = lock_acquired.get_future();

    auto writer = std::thread(
        [this, &lock_acquired]()
        {
            auto const lock = session_->unique_lock();
            lock_acquired.set_value();
            std::this_thread::sleep_for(SimulatedWriteDelay);
        });

    // See the comment on the equivalent wait() in
    // TrTorrentStatBlocksWhileSessionLockIsHeld above: the lock is already
    // guaranteed held by this point, so no extra sleep is needed.
    lock_acquired_future.wait();

    auto stat = tr_stat{};
    auto const t0 = std::chrono::steady_clock::now();
    auto const got_stat = tr_torrentTryStat(tor, &stat);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    writer.join();

    fmt::print(
        "tr_torrentTryStat() took {:d}us (returned {:s}) while another thread held session_->unique_lock() for {:d}ms\n",
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
        got_stat ? "true" : "false",
        std::chrono::duration_cast<std::chrono::milliseconds>(SimulatedWriteDelay).count());

    EXPECT_FALSE(got_stat) << "expected the lock to still be held by the writer thread";
    EXPECT_LT(elapsed, 50ms) << "tr_torrentTryStat() blocked instead of returning immediately";
}

TEST_F(LockContentionTest, TrTorrentTryStatSucceedsWithNoContention)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    auto stat = tr_stat{};
    auto const t0 = std::chrono::steady_clock::now();
    auto const got_stat = tr_torrentTryStat(tor, &stat);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    fmt::print(
        "tr_torrentTryStat() took {:d}us (returned {:s}) with no lock contention (baseline)\n",
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
        got_stat ? "true" : "false");

    EXPECT_TRUE(got_stat);
    EXPECT_LT(elapsed, 50ms);
}

// The Mac client's Torrent+updateTorrents: calls the *batch* tr_torrentStat()
// overload, which takes the session lock once for the whole torrent list.
// Confirm the batch tr_torrentTryStat() counterpart doesn't block either.
TEST_F(LockContentionTest, BatchTrTorrentTryStatDoesNotBlockWhileSessionLockIsHeld)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);
    auto const ids = std::array<tr_torrent_id_t, 1>{ tr_torrentId(tor) };

    static constexpr auto SimulatedWriteDelay = 300ms;

    auto lock_acquired = std::promise<void>{};
    auto lock_acquired_future = lock_acquired.get_future();

    auto writer = std::thread(
        [this, &lock_acquired]()
        {
            auto const lock = session_->unique_lock();
            lock_acquired.set_value();
            std::this_thread::sleep_for(SimulatedWriteDelay);
        });

    // Same reasoning as the other tests in this file: the promise guarantees
    // the lock is already held, so no extra sleep is needed here.
    lock_acquired_future.wait();

    auto stats = std::array<tr_stat, 1>{};
    auto const t0 = std::chrono::steady_clock::now();
    auto const got_stats = tr_torrentTryStat(session_, ids, std::data(stats));
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    writer.join();

    fmt::print(
        "batch tr_torrentTryStat() took {:d}us (returned {:s}) while another thread held session_->unique_lock() for {:d}ms\n",
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count(),
        got_stats ? "true" : "false",
        std::chrono::duration_cast<std::chrono::milliseconds>(SimulatedWriteDelay).count());

    EXPECT_FALSE(got_stats);
    EXPECT_LT(elapsed, 50ms) << "batch tr_torrentTryStat() blocked instead of returning immediately";
}

// Regression test: a failure partway through the batch (here, an id that
// doesn't resolve to any torrent) must leave every setme entry untouched,
// including ones for ids earlier in the list that resolved just fine --
// per this function's documented all-or-nothing contract.
TEST_F(LockContentionTest, BatchTrTorrentTryStatLeavesSetmeUntouchedOnInvalidId)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    // A bogus id that will never resolve, after a valid one. If the
    // implementation wrote setme[0] before validating ids[1], this would
    // (wrongly) still leave setme[0] populated despite returning false.
    static constexpr auto BogusId = tr_torrent_id_t{ -12345 };
    auto const ids = std::array<tr_torrent_id_t, 2>{ tr_torrentId(tor), BogusId };

    static constexpr auto SentinelId = tr_torrent_id_t{ -99999 };
    auto stats = std::array<tr_stat, 2>{};
    for (auto& stat : stats)
    {
        stat.id = SentinelId;
    }

    auto const got_stats = tr_torrentTryStat(session_, ids, std::data(stats));

    EXPECT_FALSE(got_stats);
    for (auto const& stat : stats)
    {
        EXPECT_EQ(stat.id, SentinelId) << "setme was modified even though tr_torrentTryStat() returned false";
    }
}

} // namespace tr::test
