// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

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

    // the cache is not thread-safe, so all calls run in the session thread

    int blockingWriteBlock(tr_torrent_id_t const tor_id, tr_block_index_t const block)
    {
        auto err = int{ -1 };
        auto done = false;
        session_->run_in_session_thread(
            [this, tor_id, block, &err, &done]()
            {
                auto buf = std::make_unique<Cache::BlockData>(tr_block_info::BlockSize);
                std::fill_n(std::data(*buf), tr_block_info::BlockSize, '\0');
                err = session_->cache->write_block(tor_id, block, std::move(buf));
                done = true;
            });
        EXPECT_TRUE(waitFor([&done]() { return done; }, MaxWaitMsec));
        return err;
    }

    void setCacheLimitBlocks(size_t const n_blocks)
    {
        auto done = false;
        session_->run_in_session_thread(
            [this, n_blocks, &done]()
            {
                (void)session_->cache->set_limit(
                    Cache::Memory{ n_blocks * tr_block_info::BlockSize, Cache::Memory::Units::Bytes });
                done = true;
            });
        EXPECT_TRUE(waitFor([&done]() { return done; }, MaxWaitMsec));
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

    // a torrent that can still write must not be starved by them
    auto const [begin, end] = tor->block_span_for_piece(0U);
    EXPECT_EQ(0, blockingWriteBlock(tor->id(), begin));
    EXPECT_EQ(TR_STAT_OK, tr_torrentStat(tor)->error);
}

TEST_F(CacheTest, failedFlushErrorsOnlyItsOwnTorrent)
{
    auto* const tor_bad = zeroTorrentInit(ZeroTorrentState::Complete);
    auto* const tor_ok = torrentInitFromFile("Android-x86 8.1 r6 iso.torrent");
    ASSERT_NE(nullptr, tor_ok);
    setCacheLimitBlocks(2U);

    // make tor_bad's first file unwritable by replacing it with a directory
    auto const path = tr_torrentFindFile(tor_bad, 0U);
    ASSERT_FALSE(std::empty(path));
    ASSERT_TRUE(tr_sys_path_remove(path.c_str()));
    ASSERT_TRUE(tr_sys_dir_create(path.c_str(), 0, 0700));

    // drop the file descriptors that verify left open, so that the next
    // write has to reopen the now-unwritable path
    {
        auto done = false;
        session_->run_in_session_thread(
            [this, tor_bad, &done]()
            {
                session_->openFiles().close_torrent(tor_bad->id());
                done = true;
            });
        ASSERT_TRUE(waitFor([&done]() { return done; }, MaxWaitMsec));
    }

    // Three blocks overflow the two-block limit, forcing a flush that
    // fails. That must put tor_bad into a local error state...
    auto const [begin, end] = tor_bad->block_span_for_file(0U);
    auto last_err = int{};
    for (tr_block_index_t block = begin; block < begin + 3U; ++block)
    {
        last_err = blockingWriteBlock(tor_bad->id(), block);
    }
    EXPECT_NE(0, last_err);
    EXPECT_TRUE(waitFor([tor_bad]() { return tr_torrentStat(tor_bad)->error == TR_STAT_LOCAL_ERROR; }, MaxWaitMsec));

    // ...while other torrents keep writing unharmed instead of being
    // starved by tor_bad's unflushable blocks
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 0U));
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 1U));
    EXPECT_EQ(0, blockingWriteBlock(tor_ok->id(), 2U));
    EXPECT_EQ(TR_STAT_OK, tr_torrentStat(tor_ok)->error);
}

} // namespace libtransmission::test
