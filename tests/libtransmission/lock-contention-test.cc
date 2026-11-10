// Regression test: empirically measures whether tr_torrentStat() blocks on
// the calling thread while the session thread holds session_->unique_lock()
// for an extended period (simulating a slow synchronous disk write happening
// inside tr_peerIo::can_read_wrapper()), and that tr_torrentTryPeers() does not.

#include <chrono>
#include <future>
#include <thread>
#include <vector>

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

// The Mac inspector's Peers tab refreshes tr_torrentPeers() once a second
// while it is visible; it takes the same lock as tr_torrentStat().
TEST_F(LockContentionTest, TrTorrentTryPeersDoesNotBlockWhileSessionLockIsHeld)
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

    lock_acquired_future.wait();

    auto peers = std::vector<tr_peer_stat>{ tr_peer_stat{} };
    auto const t0 = std::chrono::steady_clock::now();
    auto const got_peers = tr_torrentTryPeers(session_, tr_torrentId(tor), &peers);
    auto const elapsed = std::chrono::steady_clock::now() - t0;

    writer.join();

    EXPECT_FALSE(got_peers) << "expected the lock to still be held by the writer thread";
    EXPECT_EQ(std::size(peers), 1U) << "setme was modified even though tr_torrentTryPeers() returned false";
    EXPECT_LT(elapsed, 50ms) << "tr_torrentTryPeers() blocked instead of returning immediately";
}

TEST_F(LockContentionTest, TrTorrentTryPeersSucceedsWithNoContention)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    ASSERT_NE(tor, nullptr);

    auto peers = std::vector<tr_peer_stat>{ tr_peer_stat{} };
    EXPECT_TRUE(tr_torrentTryPeers(session_, tr_torrentId(tor), &peers));
    EXPECT_TRUE(std::empty(peers)) << "a torrent with no peers should yield an empty list";
    EXPECT_FALSE(tr_torrentTryPeers(session_, tr_torrent_id_t{ 0 }, &peers)) << "id 0 names no torrent";
}

} // namespace tr::test
