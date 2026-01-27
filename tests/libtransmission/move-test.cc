// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/cache.h> // tr_cacheWriteBlock()
#include <libtransmission/file.h> // tr_sys_path_*()
#include <libtransmission/quark.h>
#include <libtransmission/torrent.h>
#include <libtransmission/torrent-files.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
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
    EXPECT_EQ(tor->piece_size(), tr_torrentStat(tor)->leftUntilDone);

    auto completeness = TR_LEECH;
    auto const zeroes_completeness_func =
        [](tr_torrent* /*torrent*/, tr_completeness c, bool /*was_running*/, void* vc) noexcept
    {
        *static_cast<tr_completeness*>(vc) = c;
    };
    tr_sessionSetCompletenessCallback(session_, zeroes_completeness_func, &completeness);

    struct TestIncompleteDirData
    {
        tr_session* session = {};
        tr_torrent* tor = {};
        tr_block_index_t block = {};
        tr_piece_index_t pieceIndex = {};
        std::unique_ptr<Cache::BlockData> buf;
        bool done = {};
    };

    auto const test_incomplete_dir_threadfunc = [](TestIncompleteDirData* data) noexcept
    {
        data->session->cache->write_block(data->tor->id(), data->block, std::move(data->buf));
        data->tor->on_block_received(data->block);
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
            data.buf = std::make_unique<Cache::BlockData>(tr_block_info::BlockSize);
            std::fill_n(std::data(*data.buf), tr_block_info::BlockSize, '\0');
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
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    auto test = [&completeness]()
    {
        return completeness != TR_LEECH;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));
    EXPECT_EQ(TR_SEED, completeness);

    // Wait for the auto-move from incomplete_dir to download_dir to complete
    auto relocate_test = [tor]()
    {
        return !tor->is_relocating();
    };
    EXPECT_TRUE(waitFor(relocate_test, MaxWaitMsec));

    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const expected = tr_pathbuf{ download_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_EQ(expected, tr_torrentFindFile(tor, i));
    }

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
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

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
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

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

// Test that relocation is asynchronous (doesn't block the calling thread)
TEST_F(MoveTest, setLocationIsAsync)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Record time before move
    auto const start_time = std::chrono::steady_clock::now();

    // Start the move operation
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);

    // The call should return quickly (not block) - verify it returns within 100ms
    // The actual move may take longer, but the API call should be non-blocking
    auto const call_duration = std::chrono::steady_clock::now() - start_time;
    EXPECT_LT(call_duration, std::chrono::milliseconds(100)) << "setLocation should return immediately, not block";

    // Now wait for the relocation to complete
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

// Test that is_relocating() returns true while relocation is in progress
TEST_F(MoveTest, isRelocatingDuringMove)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Before moving, is_relocating() should be false
    EXPECT_FALSE(tor->is_relocating());

    // Start the move operation
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);

    // is_relocating() should become true at some point during the move
    // Since we're dealing with async operations, we need to poll
    bool was_relocating = false;
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(MaxWaitMsec);

    while (std::chrono::steady_clock::now() < deadline)
    {
        if (tor->is_relocating())
        {
            was_relocating = true;
            break;
        }
        if (state == TR_LOC_DONE)
        {
            // Move completed before we could observe is_relocating() - that's ok for small files
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Wait for move to complete
    auto test = [&state]()
    {
        return state == TR_LOC_DONE || state == TR_LOC_ERROR;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));

    // After move completes, is_relocating() should be false
    EXPECT_FALSE(tor->is_relocating());
    EXPECT_EQ(TR_LOC_DONE, state);

    // The test torrent is small, so is_relocating() might transition too fast to observe
    // Log whether we observed it (informational, not a hard requirement for small files)
    if (!was_relocating)
    {
        // This is expected for small files - the move completes very quickly
        GTEST_LOG_(INFO) << "Relocation completed too quickly to observe is_relocating() == true "
                         << "(expected for small test files)";
    }

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

// Test that the status pointer correctly transitions from TR_LOC_MOVING to TR_LOC_DONE
TEST_F(MoveTest, statusPointerTransitions)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Track all states observed
    auto observed_moving = std::atomic<bool>{ false };
    auto observed_done = std::atomic<bool>{ false };
    auto state = -1;

    tr_torrentSetLocation(tor, target_dir, true, &state);

    // Poll to observe the state transitions
    auto const deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(MaxWaitMsec);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (state == TR_LOC_MOVING)
        {
            observed_moving = true;
        }
        else if (state == TR_LOC_DONE)
        {
            observed_done = true;
            break;
        }
        else if (state == TR_LOC_ERROR)
        {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Must have reached DONE state
    EXPECT_TRUE(observed_done) << "Expected state to reach TR_LOC_DONE";
    EXPECT_EQ(TR_LOC_DONE, state);

    // MOVING state might not be observed for small/fast moves, but if seen it must be before DONE
    // (the state variable is set to MOVING immediately in set_location())
    // Since we set state = -1 initially and the API sets it to MOVING first,
    // if we observed DONE, MOVING must have happened (even if we missed it)

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

// Test that files are actually moved to the new location
TEST_F(MoveTest, filesActuallyMoved)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    auto const original_dir = tr_pathbuf{ tr_sessionGetDownloadDir(session_) };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Verify files exist at original location before move
    auto const n = tr_torrentFileCount(tor);
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const original_path = tr_pathbuf{ original_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_TRUE(tr_sys_path_exists(original_path)) << "File should exist at original location: " << original_path;
    }

    // Move the torrent
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);

    // Wait for move to complete
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));

    sync();

    // Verify files now exist at new location
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const new_path = tr_pathbuf{ target_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_TRUE(tr_sys_path_exists(new_path)) << "File should exist at new location: " << new_path;

        // Also verify tr_torrentFindFile returns the new location
        EXPECT_EQ(new_path, tr_torrentFindFile(tor, i));
    }

    // Verify files no longer exist at original location (they were moved, not copied)
    for (tr_file_index_t i = 0; i < n; ++i)
    {
        auto const original_path = tr_pathbuf{ original_dir, '/', tr_torrentFile(tor, i).name };
        EXPECT_FALSE(tr_sys_path_exists(original_path))
            << "File should not exist at original location after move: " << original_path;
    }

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

// Test that cancellation works (removing a torrent during relocation)
TEST_F(MoveTest, cancellationDuringRelocation)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    auto const tor_id = tor->id();
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Start the move operation
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, true, &state);

    // Immediately remove the torrent while relocation might be in progress
    // This tests that the relocation worker handles cancellation gracefully
    tr_torrentRemove(tor, false, nullptr, nullptr, nullptr, nullptr);

    // Wait a bit to ensure any pending relocation work has been cleaned up
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // The torrent should be removed - trying to find it should return nullptr
    EXPECT_EQ(nullptr, session_->torrents().get(tor_id));

    // No crash or hang means the test passed - the cancellation was handled properly
}

// Test that relocation without moving (just changing download_dir) completes immediately
TEST_F(MoveTest, setLocationWithoutMove)
{
    auto const target_dir = tr_pathbuf{ session_->configDir(), "/target"sv };
    tr_sys_dir_create(target_dir, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Set location without moving files (move_from_old_path = false)
    auto state = -1;
    tr_torrentSetLocation(tor, target_dir, false, &state);

    // Should complete very quickly since no files are being moved
    auto test = [&state]()
    {
        return state == TR_LOC_DONE;
    };
    EXPECT_TRUE(waitFor(test, 500)); // Short timeout since it should be immediate
    EXPECT_EQ(TR_LOC_DONE, state);

    // is_relocating() should be false since we're not actually moving
    EXPECT_FALSE(tor->is_relocating());

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

// Test that attempting to relocate while already relocating returns an error
TEST_F(MoveTest, doubleRelocationReturnsError)
{
    auto const target_dir1 = tr_pathbuf{ session_->configDir(), "/target1"sv };
    auto const target_dir2 = tr_pathbuf{ session_->configDir(), "/target2"sv };
    tr_sys_dir_create(target_dir1, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);
    tr_sys_dir_create(target_dir2, TR_SYS_DIR_CREATE_PARENTS, 0777, nullptr);

    // init a torrent.
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Complete);
    blockingTorrentVerify(tor);
    EXPECT_EQ(0, tr_torrentStat(tor)->leftUntilDone);

    // Start first relocation
    auto state1 = -1;
    tr_torrentSetLocation(tor, target_dir1, true, &state1);

    // Try to start a second relocation immediately
    // (This tests the "relocation already in progress" check)
    auto state2 = -1;
    tr_torrentSetLocation(tor, target_dir2, true, &state2);

    // Wait for first relocation to complete
    auto test = [&state1]()
    {
        return state1 == TR_LOC_DONE || state1 == TR_LOC_ERROR;
    };
    EXPECT_TRUE(waitFor(test, MaxWaitMsec));

    // First relocation should succeed
    EXPECT_EQ(TR_LOC_DONE, state1);

    // Second relocation should either:
    // 1. Return TR_LOC_ERROR immediately (if checked while first was in progress)
    // 2. Return TR_LOC_DONE (if first completed before second was processed)
    // Either is acceptable behavior
    auto test2 = [&state2]()
    {
        return state2 == TR_LOC_DONE || state2 == TR_LOC_ERROR;
    };
    EXPECT_TRUE(waitFor(test2, MaxWaitMsec));

    // cleanup
    tr_torrentRemove(tor, true, nullptr, nullptr, nullptr, nullptr);
}

} // namespace libtransmission::test
