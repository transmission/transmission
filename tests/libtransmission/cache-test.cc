// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "transmission.h"

#include "cache-new.h"
#include "crypto-utils.h"

#include <gtest/gtest.h>

using CacheTest = ::testing::Test;
using namespace std::literals;

static bool operator==(tr_block_span_t lhs, tr_block_span_t rhs)
{
    return lhs.begin == rhs.begin && lhs.end == rhs.end;
}

static constexpr auto singleBlockSpan(tr_block_index_t block)
{
    return tr_block_span_t{ block, block + 1 };
}

using BlockMap = std::map<tr_block_index_t, std::vector<uint8_t>>;

static auto makeBlocksFromSpan(tr_block_info const& block_info, tr_block_span_t span)
{
    auto blocks = BlockMap{};
    for (auto block = span.begin; block < span.end; ++block)
    {
        auto const n_bytes = block_info.blockSize(block);
        blocks.try_emplace(block, std::vector<uint8_t>(n_bytes, static_cast<char>('A' + block)));
    }
    return blocks;
}

static auto getContentsOfSpan(BlockMap const& blocks, tr_block_span_t span)
{
    auto ret = BlockMap::mapped_type{};
    for (auto block = span.begin; block < span.end; ++block)
    {
        auto data = blocks.at(block);
        ret.insert(std::end(ret), std::begin(data), std::end(data));
    }
    return ret;
}

struct BasicTestData
{
    tr_torrent_id_t tor_id;
    tr_block_info block_info;
    BlockMap blocks;
    size_t max_blocks;
};

static auto makeBasicTestData(
    tr_torrent_id_t tor_id = 1,
    uint64_t total_size = tr_block_info::BlockSize * 8U + 1U,
    uint64_t piece_size = tr_block_info::BlockSize * 4U,
    size_t max_blocks = size_t{ 7 })
{
    auto ret = BasicTestData{};
    ret.tor_id = tor_id;
    ret.block_info = tr_block_info{ total_size, piece_size };
    ret.blocks = makeBlocksFromSpan(ret.block_info, { 0U, ret.block_info.blockCount() });
    ret.max_blocks = max_blocks;
    return ret;
}

class MockTorrentIo final : public tr_torrent_io
{
public:
    tr_block_info const& block_info_;

    explicit MockTorrentIo(tr_block_info const& block_info)
        : block_info_{ block_info }
    {
    }

    using key_t = std::pair<tr_torrent_id_t, tr_block_index_t>;

    [[nodiscard]] static constexpr auto makeKey(tr_torrent_id_t tor_id, tr_block_index_t block_index)
    {
        return std::make_pair(tor_id, block_index);
    }

    [[nodiscard]] uint32_t blockSize(tr_block_index_t block) const noexcept final
    {
        return block_info_.blockSize(block);
    }

    bool put(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t const* block_data) final
    {
        puts_.emplace_back(tor_id, span);

        for (auto block = span.begin; block < span.end; ++block)
        {
            auto const n = blockSize(block);
            std::copy_n(block_data, n, std::back_inserter(blocks_[makeKey(tor_id, block)]));
            block_data += n;
        }

        return true;
    }

    bool get(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t* setme) final
    {
        gets_.emplace_back(tor_id, span);

        for (auto block = span.begin; block < span.end; ++block)
        {
            auto const key = makeKey(tor_id, block);
            auto const iter = blocks_.find(key);
            if (iter == std::end(blocks_))
            {
                return false;
            }

            setme = std::copy_n(std::data(iter->second), std::size(iter->second), setme);
        }

        return true;
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_span_t span) final
    {
        prefetches_.emplace_back(tor_id, span);
    }

    void populate(tr_torrent_id_t tor_id, BlockMap const& block_map)
    {
        for (auto const& block : block_map)
        {
            blocks_.try_emplace(makeKey(tor_id, block.first), block.second);
        }
    }

    using Operation = std::pair<tr_torrent_id_t, tr_block_span_t>;
    using Operations_t = std::vector<Operation>;

    std::map<key_t, std::vector<uint8_t>> blocks_;

    Operations_t gets_;
    Operations_t puts_;
    Operations_t prefetches_;
};

TEST_F(CacheTest, constructorMaxBlocks)
{
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();
    auto mio = MockTorrentIo{ block_info };

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    EXPECT_EQ(max_blocks, cache->maxBlocks());
}

TEST_F(CacheTest, putBlockDoesCache)
{
    auto constexpr Block = tr_block_index_t{ 0U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make an unpopulated cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    // cache a block
    EXPECT_TRUE(cache->put(tor_id, Block, std::data(contents.at(Block))));

    // read it back again
    auto got = std::vector<uint8_t>(block_info.blockSize(Block));

    // confirm that the block's contents are what we expected
    EXPECT_TRUE(cache->get(tor_id, Block, std::data(got)));
    EXPECT_EQ(contents.at(0), got);

    // confirm that cache didn't touch mio since the block was cached
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, destructorSavesOne)
{
    auto constexpr Block = tr_block_index_t{ 0U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make an unpopulated cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    // cache the block
    EXPECT_TRUE(cache->put(tor_id, Block, std::data(contents.at(Block))));

    // confirm that mio wasn't touched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    // destroy the cache
    cache.reset();

    // confirm the block was saved during cache destruction
    auto const expected_puts = MockTorrentIo::Operations_t{ { tor_id, singleBlockSpan(Block) } };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, blocksAreFoldedIntoSpan)
{
    auto constexpr const Span = tr_block_span_t{ 0U, 5U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make an unpopulated cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    // cache the span in a non-consecutive order
    EXPECT_TRUE(cache->put(tor_id, 1U, std::data(contents.at(1U))));
    EXPECT_TRUE(cache->put(tor_id, 4U, std::data(contents.at(4U))));
    EXPECT_TRUE(cache->put(tor_id, 0U, std::data(contents.at(0U))));
    EXPECT_TRUE(cache->put(tor_id, 2U, std::data(contents.at(2U))));
    EXPECT_TRUE(cache->put(tor_id, 3U, std::data(contents.at(3U))));

    // cache should be holding it all, so mio_ should be untouched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    // destroy the cache
    cache.reset();

    // confirm the mio.put() was called with a single span
    auto const expected_puts = MockTorrentIo::Operations_t{ { tor_id, Span } };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        EXPECT_EQ(contents.at(block), mio.blocks_[mio.makeKey(tor_id, block)]);
    }
}

TEST_F(CacheTest, prefetchUncached)
{
    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make an unpopulated cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    cache->prefetch(tor_id, Span);

    // since the cache didn't have any of the blocks,
    // it should have passed the entire request onto
    // the mock as a single span.
    auto const expected_prefetches = MockTorrentIo::Operations_t{ { tor_id, Span } };
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}

TEST_F(CacheTest, prefetchBisectedSpan)
{
    // Similar to CacheTest.prefetchUncached but in this variation,
    // `cache` has the one of the blocks and so only the rest should
    // be requested from mio.prefetech()

    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto constexpr MidpointBlock = tr_block_index_t{ 2U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make a cache, populated with a single block
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    EXPECT_TRUE(cache->put(tor_id, MidpointBlock, std::data(contents.at(MidpointBlock))));

    // call prefetch
    cache->prefetch(tor_id, Span);

    // since the cache had one of the blocks, the spans
    // to either side of that block should have been passed
    // to mio.prefetch()
    auto const expected_prefetches = MockTorrentIo::Operations_t{
        { tor_id, { Span.begin, MidpointBlock } },
        { tor_id, { MidpointBlock + 1, Span.end } },
    };
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}

TEST_F(CacheTest, getUncached)
{
    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // create and populate the io mock
    auto mio = MockTorrentIo{ block_info };
    mio.populate(tor_id, contents);

    // create the (unpopulated) cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));

    // get the full span
    auto buf = std::vector<uint8_t>(block_info.blockSize(0) * (Span.end - Span.begin));
    EXPECT_TRUE(cache->get(tor_id, Span, std::data(buf)));

    // since the cache has none of the blocks, expect that
    // it passed the entire span on to mio.get()
    auto const expected_gets = MockTorrentIo::Operations_t{
        { tor_id, Span },
    };
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    EXPECT_EQ(getContentsOfSpan(contents, Span), buf);
}

TEST_F(CacheTest, getPartialCache)
{
    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto constexpr MidpointBlock = Span.begin + (Span.end - Span.begin) / 2U;
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // create and populate the io mock
    auto mio = MockTorrentIo{ block_info };
    mio.populate(tor_id, contents);

    // create the cache, populated with some of the blocks
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    auto const cached_span = tr_block_span_t{ Span.begin, MidpointBlock };
    EXPECT_TRUE(cache->put(tor_id, cached_span, std::data(getContentsOfSpan(contents, cached_span))));

    // ask cache for the full span
    auto buf = std::vector<uint8_t>(block_info.blockSize(0) * (Span.end - Span.begin));
    EXPECT_TRUE(cache->get(tor_id, Span, std::data(buf)));

    // since the cache has only the first part of the block,
    // expect the rest to have been requested from mio.get()
    // in a single span
    auto const expected_gets = MockTorrentIo::Operations_t{
        { tor_id, { MidpointBlock, Span.end } },
    };
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    EXPECT_EQ(getContentsOfSpan(contents, Span), buf);
}

TEST_F(CacheTest, getBisectedCache)
{
    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto constexpr MidpointBlock = Span.begin + (Span.end - Span.begin) / 2U;
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // create and populate the io mock
    auto mio = MockTorrentIo{ block_info };
    mio.populate(tor_id, contents);

    // create the cache, populated with one block in the middle
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    auto const cached_span = singleBlockSpan(MidpointBlock);
    EXPECT_TRUE(cache->put(tor_id, cached_span, std::data(getContentsOfSpan(contents, cached_span))));

    // ask cache for the full span
    auto buf = std::vector<uint8_t>(block_info.blockSize(0) * (Span.end - Span.begin));
    EXPECT_TRUE(cache->get(tor_id, Span, std::data(buf)));

    // since the cache has only the first part of the block,
    // expect the rest to have been requested from mio.get()
    // in a single span
    auto const expected_gets = MockTorrentIo::Operations_t{
        { tor_id, { Span.begin, MidpointBlock } },
        { tor_id, { MidpointBlock + 1U, Span.end } },
    };
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    EXPECT_EQ(getContentsOfSpan(contents, Span), buf);
}

TEST_F(CacheTest, honorsBlockLimitWhenReduced)
{
    auto constexpr Span1 = tr_block_span_t{ 4U, 7U };
    auto constexpr Span2 = tr_block_span_t{ 0U, 3U };
    auto constexpr Span3 = tr_block_span_t{ 8U, 9U };
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData();

    // create the unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // create a cache and fill it up to capacity
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    EXPECT_TRUE(cache->put(tor_id, Span1, std::data(getContentsOfSpan(contents, Span1))));
    EXPECT_TRUE(cache->put(tor_id, Span2, std::data(getContentsOfSpan(contents, Span2))));
    EXPECT_TRUE(cache->put(tor_id, Span3, std::data(getContentsOfSpan(contents, Span3))));
    EXPECT_EQ(MockTorrentIo::Operations_t{}, mio.puts_);
    EXPECT_EQ((Span1.end - Span1.begin) + (Span2.end - Span2.begin) + (Span3.end - Span3.begin), cache->maxBlocks());

    // Reduce the max block count by one.
    // When capacity is exceeded, the cache responds by flushing
    // the span with the oldest block.
    cache->setMaxBlocks(cache->maxBlocks() - 1);
    auto expected_puts = MockTorrentIo::Operations_t{};
    expected_puts.emplace_back(tor_id, Span1);
    EXPECT_EQ(expected_puts, mio.puts_);

    // cache's maxBlocks is 6 and it has 4 blocks [0..3) and [8..9),
    // so it won't be another call to mio.put() until we reduce maxBlocks
    // by another three blocks.
    cache->setMaxBlocks(cache->maxBlocks() - 1);
    EXPECT_EQ(expected_puts, mio.puts_);
    cache->setMaxBlocks(cache->maxBlocks() - 1);
    EXPECT_EQ(expected_puts, mio.puts_);
    cache->setMaxBlocks(cache->maxBlocks() - 1);
    expected_puts.emplace_back(tor_id, Span2);
    EXPECT_EQ(expected_puts, mio.puts_);
}

TEST_F(CacheTest, honorsBlockLimitWhenExceeded)
{
    auto constexpr NumBlocks = tr_block_index_t{ 9U };
    auto constexpr LastBlockSize = uint32_t{ 1U };
    auto constexpr MaxBlocks = size_t{ 2 };
    auto constexpr TotalSize = tr_block_info::BlockSize * (NumBlocks - 1U) + LastBlockSize;
    auto constexpr PieceSize = tr_block_info::BlockSize * 4U;
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData(1, TotalSize, PieceSize);
    EXPECT_EQ(NumBlocks, block_info.blockCount());
    EXPECT_EQ(LastBlockSize, block_info.blockSize(NumBlocks - 1));

    // create an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // create a cache and fill it up to capacity
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBlocks));
    for (auto block = tr_block_index_t{ 0U }; block < NumBlocks; ++block)
    {
        EXPECT_TRUE(cache->put(tor_id, block, std::data(contents.at(block))));
    }

    // When full, the cache tries to flush the span with the oldest block.
    // Since we limited the total capacity to two blocks, that means after
    // putting the third block, all three would be flushed, leaving an
    // empty cache. Next, [4..6) will be flushed for the same reasons.
    auto const expected_puts = MockTorrentIo::Operations_t{
        { tor_id, { 0U, 3U } },
        { tor_id, { 3U, 6U } },
        { tor_id, { 6U, 9U } },
    };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, saveTorrent)
{
    auto constexpr Span = tr_block_span_t{ 1U, 3U };
    auto const [tor_id_1, block_info, contents, max_blocks] = makeBasicTestData(1);
    auto const tor_id_2 = tor_id_1 + 1;
    auto const tor_id_3 = tor_id_2 + 1;

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make a cache, populated with multiple torrents
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    EXPECT_TRUE(cache->put(tor_id_1, Span, std::data(getContentsOfSpan(contents, Span))));
    EXPECT_TRUE(cache->put(tor_id_2, Span, std::data(getContentsOfSpan(contents, Span))));
    EXPECT_TRUE(cache->put(tor_id_3, Span, std::data(getContentsOfSpan(contents, Span))));

    // we haven't exceeded the cache's capacity yet, so mio should still be idle
    auto const no_ops = MockTorrentIo::Operations_t{};
    EXPECT_EQ(no_ops, mio.puts_);
    EXPECT_EQ(no_ops, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);

    // save torrent #2
    cache->saveTorrent(tor_id_2);

    // cache should have written torrent #2's blocks to mio
    auto const expected_puts = MockTorrentIo::Operations_t{ { tor_id_2, Span } };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_EQ(no_ops, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);

    // ..and since they're no longer cached, requesting from
    // the cache should delegate to mio.get()
    auto buf = std::vector<uint8_t>(tr_block_info::BlockSize * (Span.end - Span.begin));
    EXPECT_TRUE(cache->get(tor_id_2, Span, std::data(buf)));
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_EQ(expected_puts, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);
    EXPECT_EQ(getContentsOfSpan(contents, Span), buf);
}

TEST_F(CacheTest, saveSpan)
{
    auto const [tor_id, block_info, contents, max_blocks] = makeBasicTestData(1);
    auto const span = tr_block_span_t{ 0U, static_cast<tr_block_index_t>(max_blocks) };
    auto const save_span = tr_block_span_t{ span.begin, span.end / 2U };

    // make an unpopulated io mock
    auto mio = MockTorrentIo{ block_info };

    // make a fully-stocked cache
    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, max_blocks));
    EXPECT_TRUE(cache->put(tor_id, span, std::data(getContentsOfSpan(contents, span))));

    // we haven't exceeded the cache's capacity yet, so mio should still be idle
    auto const no_ops = MockTorrentIo::Operations_t{};
    EXPECT_EQ(no_ops, mio.puts_);
    EXPECT_EQ(no_ops, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);

    // save a span
    cache->saveSpan(tor_id, save_span);

    // cache should have written that span to mio
    auto const expected_puts = MockTorrentIo::Operations_t{ { tor_id, save_span } };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_EQ(no_ops, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);

    // ..and since they're no longer cached, requesting from
    // the cache should delegate to mio.get()
    auto buf = std::vector<uint8_t>(tr_block_info::BlockSize * (save_span.end - save_span.begin));
    EXPECT_TRUE(cache->get(tor_id, save_span, std::data(buf)));
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_EQ(expected_puts, mio.gets_);
    EXPECT_EQ(no_ops, mio.prefetches_);
    EXPECT_EQ(getContentsOfSpan(contents, save_span), buf);
}

TEST_F(CacheTest, getReturnsFalseOnError)
{
}

TEST_F(CacheTest, setReturnsFalseOnError)
{
}
