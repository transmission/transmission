// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstddef> // std::byte
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/error.h>
#include <libtransmission/file.h> // tr_sys_path_*()
#include <libtransmission/inout.h>
#include <libtransmission/move.h>
#include <libtransmission/quark.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/torrent.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/types.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace tr::test
{

auto constexpr MaxWaitMsec = 5000;

class IncompleteDirTest
    : public SessionTest
    , public ::testing::WithParamInterface<std::pair<std::string, std::string>>
{
protected:
    void SetUp() override
    {
        if (auto* map = settings()->get_if<tr_variant::Map>(); map != nullptr)
        {
            auto const download_dir = GetParam().second;
            map->insert_or_assign(TR_KEY_download_dir, download_dir);
            auto const incomplete_dir = GetParam().first;
            map->insert_or_assign(TR_KEY_incomplete_dir, incomplete_dir);
            map->insert_or_assign(TR_KEY_incomplete_dir_enabled, true);
        }

        SessionTest::SetUp();
    }

    static auto constexpr MaxWaitMsec = 3000;
};

TEST_P(IncompleteDirTest, incompleteDir)
{
    std::string const download_dir = tr_sessionGetDownloadDir(session_);
    std::string const incomplete_dir = tr_sessionGetIncompleteDir(session_);

    // init an incomplete torrent.
    // the test zero_torrent will be missing its first piece.
    tr_sessionSetIncompleteFileNamingEnabled(session_, true);
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    auto path = tr_pathbuf{};

    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 0).name, tr_torrent_files::PartialFileSuffix);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 0));
    path.assign(incomplete_dir, '/', tr_torrentFile(tor, 1).name);
    EXPECT_EQ(path, tr_torrentFindFile(tor, 1));
    EXPECT_EQ(tor->piece_size(), tr_torrentStat(tor).left_until_done);

    auto completeness = TR_LEECH;
    tr_sessionSetCompletenessCallback(
        session_,
        [&completeness](tr_torrent_id_t const /*tor_id*/, tr_completeness const c, bool const /*was_running*/) noexcept
        { completeness = c; });

    struct TestIncompleteDirData
    {
        tr_session* session = {};
        tr_torrent* tor = {};
        tr_block_index_t block = {};
        tr_piece_index_t pieceIndex = {};
        std::vector<uint8_t> buf;
        bool done = {};
    };

    auto const test_incomplete_dir_threadfunc = [](TestIncompleteDirData* data) noexcept
    {
        auto& tor = *data->tor;
        if (tr_ioWrite(tor, data->session->openFiles(), tor.block_loc(data->block), data->buf) == 0)
        {
            data->tor->on_block_received(data->block);
        }

        data->done = true;
    };

    // now finish writing it
    {
        auto data = TestIncompleteDirData{};
        data.session = session_;
        data.tor = tor;

        auto const [begin, end] = tor->block_span_for_piece(data.pieceIndex);

        for (tr_block_index_t block_index = begin; block_index < end; ++block_index)
        {
            data.buf.resize(tr_block_info::BlockSize);
            std::ranges::fill(data.buf, '\0');
            data.block = block_index;
            data.done = false;
            session_->run_in_session_thread(test_incomplete_dir_threadfunc, &data);

            auto const test = [&data]()
            {
                return data.done;
            };
            EXPECT_TRUE(waitFor(test, MaxWaitMsec));
        }
    }

    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto test = [&completeness]()
    {
        return completeness != TR_LEECH;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_SEED, completeness);

    // the files are relocated to the download dir on a background thread,
    // so wait for the move to finish before checking their final location
    auto const n = tr_torrentFileCount(tor);
    auto const files_moved = [tor, &download_dir, n]()
    {
        for (tr_file_index_t i = 0; i < n; ++i)
        {
            auto const expected = tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name };
            if (expected != tr_torrentFindFile(tor, i))
            {
                return false;
            }
        }
        return true;
    };
    EXPECT_TRUE(waitFor(files_moved, MaxWaitMsec));

    // cleanup
    tr_torrentRemove(tor, true);
}

INSTANTIATE_TEST_SUITE_P(
    IncompleteDir,
    IncompleteDirTest,
    ::testing::Values(
        // what happens when incompleteDir is a subdir of downloadDir
        std::make_pair(std::string{ "Downloads/Incomplete" }, std::string{ "Downloads" }),
        // test what happens when downloadDir is a subdir of incompleteDir
        std::make_pair(std::string{ "Downloads" }, std::string{ "Downloads/Complete" }),
        // test what happens when downloadDir and incompleteDir are siblings
        std::make_pair(std::string{ "Incomplete" }, std::string{ "Downloads" })));

/***
****
***/

using MoveTest = SessionTest;

TEST_F(MoveTest, setLocation)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // now move it
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the torrent is still complete after being moved
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // confirm the files really got moved
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // cleanup
    tr_torrentRemove(tor, true);
}

// The relocation runs on a background thread and is expected to create the
// destination directory tree if it does not already exist.
TEST_F(MoveTest, setLocationCreatesDestination)
{
    // a deliberately not-yet-existing, nested destination
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/new/nested/target"sv };
    EXPECT_FALSE(tr_sys_path_exists(target_dir));

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    // move it; the background move thread should create the destination
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    auto const moved = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(moved, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // confirm the files ended up at the new location...
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    // ...and that the torrent is still complete
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    tr_torrentRemove(tor, true);
}

// Two relocations of the same torrent are serialized through the move worker;
// the second one must observe the bookkeeping left by the first.
TEST_F(MoveTest, setLocationMoveBackAndForth)
{
    auto const original_dir = tr_pathbuf{ tr_sessionGetDownloadDir(session_) };
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/elsewhere"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto const n = tr_torrentFileCount(tor);
    auto const files_in = [tor, n](tr_pathbuf const& dir)
    {
        for (tr_file_index_t i = 0; i < n; ++i)
        {
            if (tr_pathbuf{ dir, '/', tr_torrentFile(tor, i).name } != tr_torrentFindFile(tor, i))
            {
                return false;
            }
        }
        return true;
    };

    // move out...
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    sync();
    EXPECT_TRUE(files_in(target_dir));

    // ...and back again
    state = -1;
    tr_torrentSetLocation(tor, original_dir, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    sync();
    EXPECT_TRUE(files_in(original_dir));

    // the round trip preserved the data
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    tr_torrentRemove(tor, true);
}

// When move_from_old_path is false the files are left in place and only the
// torrent's download dir is repointed.
TEST_F(MoveTest, setLocationWithoutMovingUpdatesDownloadDir)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/already-there"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);

    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, false, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_DONE; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_DONE, state);

    // the torrent now points at the new dir even though nothing was relocated
    EXPECT_EQ(target_dir.sv(), tr_torrentGetDownloadDir(tor));

    tr_torrentRemove(tor, true);
}

// When the relocation fails, the torrent must end up in the error state, be
// stopped, keep its files where they were, and report a message that names the
// directory the move actually started from (not a drifted current_dir()).
TEST_F(MoveTest, setLocationFailureReportsErrorAndStops)
{
    // make the destination impossible to create: drop a regular file exactly
    // where the destination directory would have to be created. This fails
    // deterministically regardless of the uid the tests run as (so it's stable
    // even under a root CI), unlike chmod-based permission tricks.
    auto const blocker = tr_pathbuf{ session_->configDir(), "/blocked"sv };
    createFileWithContents(blocker.sv(), "not a directory"sv);
    EXPECT_TRUE(tr_sys_path_exists(blocker));

    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor).left_until_done);

    auto const source_dir = std::string{ tr_torrentGetDownloadDir(tor) };

    auto state = -1;
    tr_torrentSetLocation(tor, blocker, true, &state);
    EXPECT_TRUE(waitFor([&state]() { return state == TR_LOC_ERROR; }, MaxWaitMsec));
    EXPECT_EQ(TR_LOC_ERROR, state);

    // the failed move must surface as a local error on the torrent...
    auto const stat = tr_torrentStat(tor);
    EXPECT_EQ(tr_stat::Error::LocalError, stat.error);
    // ...whose message names the real source directory, not current_dir()
    EXPECT_NE(std::string::npos, stat.error_string.find(source_dir)) << "error was: " << stat.error_string;

    // ...and the data must still be at its original location, untouched
    sync();
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ source_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

    tr_torrentRemove(tor, true);
}

/***
****  tr_move_worker unit tests
****
****  These exercise the background mover in isolation with a mock Mediator, so
****  the failure/exception/teardown paths can be tested deterministically
****  without touching the filesystem or a live session.
***/

namespace
{

[[nodiscard]] tr_sha1_digest_t make_info_hash(uint8_t const seed)
{
    auto hash = tr_sha1_digest_t{};
    hash[0] = std::byte{ seed };
    return hash;
}

// Shared, thread-safe record of what the worker did. Must outlive the worker
// (the worker owns the mediators and may touch them on its own thread), so in
// every test it is declared before the tr_move_worker.
struct Recorder
{
    std::mutex mutex;
    std::vector<std::string> events;

    std::atomic<int> queued = 0;
    std::atomic<int> started = 0;
    std::atomic<int> moved = 0;
    std::atomic<int> done = 0;
    std::atomic<int> ok_count = 0;
    std::atomic<int> fail_count = 0;

    std::atomic<int> active = 0; // moves currently executing
    std::atomic<int> max_active = 0; // high-water mark; must stay 1 (serialized)

    std::atomic<bool> gate_open = true; // gated mediators block in move() until true

    void push(std::string event)
    {
        auto const lock = std::scoped_lock{ mutex };
        events.push_back(std::move(event));
    }

    void enter_move()
    {
        auto const n = ++active;
        auto prev = max_active.load();
        while (prev < n && !max_active.compare_exchange_weak(prev, n))
        {
        }
    }

    void leave_move()
    {
        --active;
    }

    [[nodiscard]] std::vector<std::string> started_tags()
    {
        auto const lock = std::scoped_lock{ mutex };
        auto tags = std::vector<std::string>{};
        for (auto const& event : events)
        {
            if (auto constexpr Prefix = "started:"sv; event.starts_with(Prefix))
            {
                tags.emplace_back(event.substr(std::size(Prefix)));
            }
        }
        return tags;
    }
};

class MockMediator final : public tr_move_worker::Mediator
{
public:
    enum class Fault : uint8_t
    {
        None,
        MoveReturnsFalse,
        ThrowInStarted,
        ThrowInMove,
        ThrowInDone,
    };

    MockMediator(uint8_t const seed, Recorder* const rec, Fault const fault = Fault::None, bool const gated = false)
        : hash_{ make_info_hash(seed) }
        , tag_{ std::to_string(seed) }
        , rec_{ rec }
        , fault_{ fault }
        , gated_{ gated }
    {
    }

    [[nodiscard]] tr_sha1_digest_t const& info_hash() const override
    {
        return hash_;
    }

    void on_move_queued() override
    {
        ++rec_->queued;
        rec_->push("queued:" + tag_);
    }

    void on_move_started() override
    {
        ++rec_->started;
        rec_->push("started:" + tag_);
        if (fault_ == Fault::ThrowInStarted)
        {
            throw std::runtime_error{ "on_move_started boom" };
        }
    }

    bool move(tr_error* const error) override
    {
        rec_->push("move:" + tag_);
        ++rec_->moved;

        rec_->enter_move();
        // make sure the decrement happens even if we throw below
        auto const guard = MoveGuard{ rec_ };

        if (gated_)
        {
            auto const deadline = std::chrono::steady_clock::now() + 5s;
            while (!rec_->gate_open.load() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::sleep_for(1ms);
            }
        }

        if (fault_ == Fault::ThrowInMove)
        {
            throw std::runtime_error{ "move boom" };
        }

        if (fault_ == Fault::MoveReturnsFalse)
        {
            if (error != nullptr)
            {
                error->set(EACCES, "mock move failure"sv);
            }
            return false;
        }

        return true;
    }

    void on_move_done(bool const ok, tr_error const* const /*error*/) override
    {
        ++rec_->done;
        if (ok)
        {
            ++rec_->ok_count;
        }
        else
        {
            ++rec_->fail_count;
        }
        rec_->push("done:" + tag_ + (ok ? ":ok" : ":fail"));

        if (fault_ == Fault::ThrowInDone)
        {
            throw std::runtime_error{ "on_move_done boom" };
        }
    }

private:
    struct MoveGuard
    {
        Recorder* rec;
        ~MoveGuard()
        {
            rec->leave_move();
        }
    };

    tr_sha1_digest_t const hash_;
    std::string const tag_;
    Recorder* const rec_;
    Fault const fault_;
    bool const gated_;
};

} // namespace

using namespace std::chrono_literals;

using MoveWorkerTest = TransmissionTest;

TEST_F(MoveWorkerTest, runsSingleJobToCompletion)
{
    auto rec = Recorder{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec));
        EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 1; }, MaxWaitMsec));
    }

    EXPECT_EQ(1, rec.queued.load());
    EXPECT_EQ(1, rec.started.load());
    EXPECT_EQ(1, rec.moved.load());
    EXPECT_EQ(1, rec.done.load());
    EXPECT_EQ(1, rec.ok_count.load());
    EXPECT_EQ((std::vector<std::string>{ "queued:1", "started:1", "move:1", "done:1:ok" }), rec.events);
}

TEST_F(MoveWorkerTest, queuesAndRunsInFifoOrder)
{
    auto rec = Recorder{};
    rec.gate_open.store(false);
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::None, /*gated=*/true));
        worker.add(std::make_unique<MockMediator>(2, &rec));
        worker.add(std::make_unique<MockMediator>(3, &rec));

        // job 1 is parked in move(); 2 and 3 must be sitting in the queue
        EXPECT_TRUE(waitFor([&rec]() { return rec.moved.load() == 1; }, MaxWaitMsec));
        EXPECT_EQ(3, rec.queued.load());
        EXPECT_EQ(1, rec.started.load());
        EXPECT_EQ(0, rec.done.load());

        rec.gate_open.store(true);
        EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 3; }, MaxWaitMsec));
    }

    EXPECT_EQ(3, rec.ok_count.load());
    EXPECT_EQ(1, rec.max_active.load()); // never two moves running at once
    EXPECT_EQ((std::vector<std::string>{ "1", "2", "3" }), rec.started_tags());
}

TEST_F(MoveWorkerTest, failingMoveDoesNotStopTheQueue)
{
    auto rec = Recorder{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::MoveReturnsFalse));
        worker.add(std::make_unique<MockMediator>(2, &rec));
        EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 2; }, MaxWaitMsec));
    }

    EXPECT_EQ(2, rec.done.load());
    EXPECT_EQ(1, rec.ok_count.load()); // only job 2 succeeded
    EXPECT_EQ(1, rec.fail_count.load()); // job 1 reported failure but didn't stall the queue
}

// The headline robustness test: a move() that throws must NOT terminate the
// process or wedge the worker. Before the fix this crashed (std::terminate) or
// left every future relocation queued forever.
TEST_F(MoveWorkerTest, throwingMoveDoesNotWedgeWorker)
{
    auto rec = Recorder{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::ThrowInMove));
        worker.add(std::make_unique<MockMediator>(2, &rec));
        // job 2 must still run to completion even though job 1 threw
        EXPECT_TRUE(waitFor([&rec]() { return rec.ok_count.load() == 1; }, MaxWaitMsec));
    }

    EXPECT_EQ(2, rec.started.load()); // both jobs were picked up
    EXPECT_EQ(1, rec.ok_count.load()); // job 2 finished cleanly
}

TEST_F(MoveWorkerTest, throwingOnMoveStartedDoesNotWedgeWorker)
{
    auto rec = Recorder{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::ThrowInStarted));
        worker.add(std::make_unique<MockMediator>(2, &rec));
        EXPECT_TRUE(waitFor([&rec]() { return rec.ok_count.load() == 1; }, MaxWaitMsec));
    }

    EXPECT_EQ(2, rec.started.load());
    EXPECT_EQ(1, rec.moved.load()); // job 1 threw before its move() ran
    EXPECT_EQ(1, rec.ok_count.load());
}

TEST_F(MoveWorkerTest, throwingOnMoveDoneDoesNotWedgeWorker)
{
    auto rec = Recorder{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::ThrowInDone));
        worker.add(std::make_unique<MockMediator>(2, &rec));
        EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 2; }, MaxWaitMsec));
    }

    EXPECT_EQ(2, rec.done.load());
    EXPECT_EQ(2, rec.ok_count.load()); // both moves succeeded; job 1 just threw afterwards
}

TEST_F(MoveWorkerTest, removeDropsQueuedJob)
{
    auto rec = Recorder{};
    rec.gate_open.store(false);
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::None, /*gated=*/true));
        worker.add(std::make_unique<MockMediator>(2, &rec));

        // job 1 is running (gated); job 2 is still queued
        EXPECT_TRUE(waitFor([&rec]() { return rec.moved.load() == 1; }, MaxWaitMsec));

        // drop job 2 before it ever starts
        worker.remove(make_info_hash(2));

        rec.gate_open.store(true);
        EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 1; }, MaxWaitMsec));
        std::this_thread::sleep_for(50ms); // give a removed job a chance to (wrongly) run
    }

    EXPECT_EQ(2, rec.queued.load()); // both were enqueued
    EXPECT_EQ(1, rec.started.load()); // but job 2 never started
    EXPECT_EQ(1, rec.done.load());
}

TEST_F(MoveWorkerTest, removeWaitsForInProgressJob)
{
    auto rec = Recorder{};
    rec.gate_open.store(false);

    auto worker = tr_move_worker{};
    worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::None, /*gated=*/true));
    EXPECT_TRUE(waitFor([&rec]() { return rec.moved.load() == 1; }, MaxWaitMsec));

    // removing the in-flight torrent must block until its move finishes
    auto remove_returned = std::atomic<bool>{ false };
    auto remover = std::thread(
        [&worker, &remove_returned]()
        {
            worker.remove(make_info_hash(1));
            remove_returned.store(true);
        });

    std::this_thread::sleep_for(100ms);
    EXPECT_FALSE(remove_returned.load()); // still blocked while the move runs
    EXPECT_EQ(0, rec.done.load());

    rec.gate_open.store(true);
    EXPECT_TRUE(waitFor([&remove_returned]() { return remove_returned.load(); }, MaxWaitMsec));
    EXPECT_EQ(1, rec.done.load()); // the move completed before remove() returned

    remover.join();
}

TEST_F(MoveWorkerTest, destructorDrainsInFlightJob)
{
    auto rec = Recorder{};
    rec.gate_open.store(false);

    auto opener = std::thread{};
    {
        auto worker = tr_move_worker{};
        worker.add(std::make_unique<MockMediator>(1, &rec, MockMediator::Fault::None, /*gated=*/true));
        EXPECT_TRUE(waitFor([&rec]() { return rec.moved.load() == 1; }, MaxWaitMsec));

        // release the gate slightly later so the job is genuinely in-flight as
        // ~tr_move_worker begins draining
        opener = std::thread(
            [&rec]()
            {
                std::this_thread::sleep_for(50ms);
                rec.gate_open.store(true);
            });

        // leaving this scope destroys the worker, which must block until the
        // in-flight move drains rather than abandoning it
    }

    opener.join();
    EXPECT_EQ(1, rec.done.load());
    EXPECT_EQ(1, rec.ok_count.load());
}

TEST_F(MoveWorkerTest, workerRestartsAfterQueueDrains)
{
    auto rec = Recorder{};
    auto worker = tr_move_worker{};

    worker.add(std::make_unique<MockMediator>(1, &rec));
    EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 1; }, MaxWaitMsec));

    // let the worker thread notice the empty queue and exit
    std::this_thread::sleep_for(50ms);

    // a fresh add() must spin the worker back up
    worker.add(std::make_unique<MockMediator>(2, &rec));
    EXPECT_TRUE(waitFor([&rec]() { return rec.done.load() == 2; }, MaxWaitMsec));
    EXPECT_EQ(2, rec.ok_count.load());
}

TEST_F(MoveWorkerTest, removeOnIdleWorkerIsNoop)
{
    auto worker = tr_move_worker{};
    worker.remove(make_info_hash(42)); // must return immediately, not hang or crash
    SUCCEED();
}

} // namespace tr::test
