// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <future>
#include <memory>
#include <string>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/cache.h>
#include <libtransmission/file.h>
#include <libtransmission/torrent.h>

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

class CacheTest : public SessionTest
{
protected:
    static auto constexpr MaxWaitMsec = 5000;

    // the cache is not thread-safe, so all calls run in the session thread.
    // the promise is shared so that a timed-out wait can't leave the queued
    // lambda holding a dangling reference into this frame.

    int blockingWriteBlock(tr_torrent_id_t const tor_id, tr_block_index_t const block)
    {
        auto const promise = std::make_shared<std::promise<int>>();
        auto future = promise->get_future();
        session_->run_in_session_thread(
            [session = session_, tor_id, block, promise]()
            {
                auto buf = std::make_unique<Cache::BlockData>(tr_block_info::BlockSize);
                promise->set_value(session->cache->write_block(tor_id, block, std::move(buf)));
            });
        if (future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }) != std::future_status::ready)
        {
            ADD_FAILURE() << "timed out waiting for Cache::write_block()";
            return -1;
        }
        return future.get();
    }

    int blockingFlushTorrent(tr_torrent_id_t const tor_id)
    {
        auto const promise = std::make_shared<std::promise<int>>();
        auto future = promise->get_future();
        session_->run_in_session_thread(
            [session = session_, tor_id, promise]() { promise->set_value(session->cache->flush_torrent(tor_id)); });
        if (future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }) != std::future_status::ready)
        {
            ADD_FAILURE() << "timed out waiting for Cache::flush_torrent()";
            return -1;
        }
        return future.get();
    }

    tr_stat_errtype blockingErrorType(tr_torrent const* const tor)
    {
        auto const promise = std::make_shared<std::promise<tr_stat_errtype>>();
        auto future = promise->get_future();
        session_->run_in_session_thread([tor, promise]() { promise->set_value(tor->error().error_type()); });
        if (future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }) != std::future_status::ready)
        {
            ADD_FAILURE() << "timed out waiting for the torrent's error state";
            return TR_STAT_LOCAL_ERROR;
        }
        return future.get();
    }

    void setCacheLimitBlocks(size_t const n_blocks)
    {
        auto const promise = std::make_shared<std::promise<int>>();
        auto future = promise->get_future();
        session_->run_in_session_thread(
            [session = session_, n_blocks, promise]()
            {
                promise->set_value(session->cache->set_limit(
                    Cache::Memory{ n_blocks * tr_block_info::BlockSize, Cache::Memory::Units::Bytes }));
            });
        if (future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }) != std::future_status::ready)
        {
            ADD_FAILURE() << "timed out waiting for Cache::set_limit()";
        }
    }
};

TEST_F(CacheTest, dropsBlocksOfRemovedTorrents)
{
    auto* const tor = zeroTorrentInit(ZeroTorrentState::Partial);
    setCacheLimitBlocks(2U);

    // Cache blocks whose torrent is gone from the session can never be
    // flushed. Three of them overflow the two-block cache limit, so the
    // cache must trim by dropping them.
    auto const ghost_id = static_cast<tr_torrent_id_t>(tor->id() + 1000);
    for (tr_block_index_t block = 0U; block < 3U; ++block)
    {
        (void)blockingWriteBlock(ghost_id, block);
    }

    // the unflushable blocks must be dropped, not kept:
    // flushing the ghost torrent now finds nothing left to write
    EXPECT_EQ(0, blockingFlushTorrent(ghost_id));

    // and a torrent that can still write must not be starved by them
    auto const [begin, end] = tor->block_span_for_piece(0U);
    EXPECT_EQ(0, blockingWriteBlock(tor->id(), begin));
    EXPECT_EQ(TR_STAT_OK, blockingErrorType(tor));
}

TEST_F(CacheTest, failedFlushErrorsOnlyItsOwnTorrent)
{
    auto* const tor_bad = zeroTorrentInit(ZeroTorrentState::Complete);
    auto* const tor_ok = torrentInitFromFile("perfect-pieces.torrent", true /*paused*/);
    ASSERT_NE(nullptr, tor_ok);
    setCacheLimitBlocks(2U);

    // Make tor_bad's files unwritable and uncreatable: remove them and
    // replace their parent directory with a regular file, so that
    // reopening and recreating them both fail on every platform.
    auto const path = tr_torrentFindFile(tor_bad, 0U);
    ASSERT_FALSE(std::empty(path));
    auto const dir = std::string{ tr_sys_path_dirname(path) };
    for (tr_file_index_t i = 0U, n = tor_bad->file_count(); i < n; ++i)
    {
        auto const filename = tr_torrentFindFile(tor_bad, i);
        ASSERT_FALSE(std::empty(filename));
        ASSERT_TRUE(tr_sys_path_remove(filename.c_str()));
    }
    ASSERT_TRUE(tr_sys_path_remove(dir.c_str()));
    createFileWithContents(dir, "not a directory");

    // drop the file descriptors that verify left open, so that the next
    // write has to reopen the now-unwritable path
    {
        auto const promise = std::make_shared<std::promise<void>>();
        auto future = promise->get_future();
        session_->run_in_session_thread(
            [session = session_, tor_id = tor_bad->id(), promise]()
            {
                session->openFiles().close_torrent(tor_id);
                promise->set_value();
            });
        ASSERT_EQ(std::future_status::ready, future.wait_for(std::chrono::milliseconds{ MaxWaitMsec }));
    }

    // Three blocks overflow the two-block limit, forcing a flush that
    // fails. The error must be reported to the torrent that owns the
    // blocks, and the failed write must stop it with a local error.
    auto const [begin, end] = tor_bad->block_span_for_file(0U);
    auto last_err = int{};
    for (tr_block_index_t block = begin; block < begin + 3U; ++block)
    {
        last_err = blockingWriteBlock(tor_bad->id(), block);
    }
    EXPECT_NE(0, last_err);
    EXPECT_EQ(TR_STAT_LOCAL_ERROR, blockingErrorType(tor_bad));

    // the unwritable blocks must be dropped, not kept:
    // flushing tor_bad again now finds nothing left to write
    EXPECT_EQ(0, blockingFlushTorrent(tor_bad->id()));

    // and other torrents keep writing unharmed instead of being
    // starved by tor_bad's unflushable blocks
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 0U));
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 1U));
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 2U));
    EXPECT_EQ(TR_STAT_OK, blockingErrorType(tor_ok));
}

} // namespace libtransmission::test
