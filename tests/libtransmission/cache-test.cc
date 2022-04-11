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

using namespace std::literals;

static bool operator==(tr_block_span_t lhs, tr_block_span_t rhs)
{
    return lhs.begin == rhs.begin && lhs.end == rhs.end;
}

static std::vector<uint8_t> makeRandomBlock(size_t block_size)
{
    auto buf = std::vector<uint8_t>(block_size);
    tr_rand_buffer(std::data(buf), std::size(buf));
    return buf;
}

static constexpr auto singleBlockSpan(tr_block_index_t block)
{
    return tr_block_span_t{ block, block + 1 };
}

static auto makeBlocks(tr_block_info const& block_info, std::set<tr_block_index_t> const& block_indices)
{
    auto blocks = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    for (auto const block : block_indices)
    {
        auto const n_bytes = block_info.blockSize(block);
        blocks.try_emplace(block, std::vector<uint8_t>(n_bytes, static_cast<char>('A' + block)));
    }
    return blocks;
}

class MockTorrentIo final: public tr_torrent_io
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

        std::cerr << __FILE__ << ':' << __LINE__ << " span { begin:" << span.begin << ", end:" << span.end << '}' << std::endl;
        for (auto block = span.begin; block < span.end; ++block)
        {
            std::cerr << __FILE__ << ':' << __LINE__ << " looking for tor_id " << tor_id << " block " << block << std::endl;
            auto const key = makeKey(tor_id, block);
            auto const iter = blocks_.find(key);
            if (iter == std::end(blocks_))
            {
                std::cerr << __FILE__ << ':' << __LINE__ << " could not find; returning false" << std::endl;
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

    using Operation = std::pair<tr_torrent_id_t, tr_block_span_t>;
    using Operations_t = std::vector<Operation>;

    std::map<key_t, std::vector<uint8_t>> blocks_;

    Operations_t gets_;
    Operations_t puts_;
    Operations_t prefetches_;
};

class CacheTest: public ::testing::Test
{
protected:
    static auto constexpr TotalSize = tr_block_info::BlockSize * 16 + 1;
    static auto constexpr PieceSize = tr_block_info::BlockSize * 8;
    static auto constexpr MaxBytes = tr_block_info::BlockSize * 8;

};

TEST_F(CacheTest, constructorMaxBytes)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    EXPECT_EQ(MaxBytes, cache->maxBytes());
}

TEST_F(CacheTest, putBlockDoesCache)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // put a block
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Block = tr_block_index_t { 0U };
    auto const blocks = makeBlocks(block_info, { Block });
    EXPECT_TRUE(cache->put(TorId, Block, std::data(blocks.at(Block))));

    // read it back again
    auto got = std::vector<uint8_t>(block_info.blockSize(Block));
    EXPECT_TRUE(cache->get(TorId, Block, std::data(got)));
    EXPECT_EQ(blocks.at(0), got);

    // confirm that cache didn't touch mio since the block was cached
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, destructorSavesOne)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Block = tr_block_index_t{ 0U };
    auto const blocks = makeBlocks(block_info, { Block });

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the block
    EXPECT_TRUE(cache->put(TorId, Block, std::data(blocks.at(Block))));

    // confirm that mio wasn't touched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the block was saved during cache destruction
    auto const expected_puts = MockTorrentIo::Operations_t{
       { TorId, singleBlockSpan(Block) },
    };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, blocksAreFoldedIntoSpan)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto constexpr const TorId = tr_torrent_id_t{ 1 };
    auto constexpr const Span = tr_block_span_t{ 0U, 5U };
    auto const blocks = makeBlocks(block_info, { 0U, 1U, 2U, 3U, 4U });

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the span in a non-consecutive order
    EXPECT_TRUE(cache->put(TorId, 1U, std::data(blocks.at(1U))));
    EXPECT_TRUE(cache->put(TorId, 4U, std::data(blocks.at(4U))));
    EXPECT_TRUE(cache->put(TorId, 0U, std::data(blocks.at(0U))));
    EXPECT_TRUE(cache->put(TorId, 2U, std::data(blocks.at(2U))));
    EXPECT_TRUE(cache->put(TorId, 3U, std::data(blocks.at(3U))));

    // cache should be holding it all, so mio_ should be untouched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the mio.put() was called with a single span
    auto const expected_puts = MockTorrentIo::Operations_t{
        { TorId, Span },
    };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        EXPECT_EQ(blocks.at(block), mio.blocks_[mio.makeKey(TorId, block)]);
    }
}

TEST_F(CacheTest, destructorSavesSpan)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0, 3 };
    auto const blocks = makeBlocks(block_info, { 0U, 1U, 2U, 3U, 4U });

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the span
    auto buf = std::vector<uint8_t>{};
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        buf.insert(std::end(buf), std::begin(blocks.at(block)), std::end(blocks.at(block)));
    }
    EXPECT_TRUE(cache->put(TorId, Span, std::data(buf)));

    // cache should be holding it all, so mio_ should be untouched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the span was saved during cache destruction
    auto const expected_puts = MockTorrentIo::Operations_t{
        { TorId, Span },
    };
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        EXPECT_EQ(blocks.at(block), mio.blocks_[mio.makeKey(TorId, block)]);
    }
}

TEST_F(CacheTest, uncachedGetAsksWrappedIo)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Block = tr_block_index_t{ 0 };
    auto const blocks = makeBlocks(block_info, { Block });

    // save that block to the mio
    EXPECT_TRUE(mio.put(TorId, singleBlockSpan(Block), std::data(blocks.at(Block))));
    mio.puts_.clear();

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // read that block from the cache
    auto got = std::vector<uint8_t>(block_info.blockSize(Block));
    EXPECT_TRUE(cache->get(TorId, Block, std::data(got)));
    EXPECT_EQ(blocks.at(Block), got);

    // confirm that cache got it from mio
    auto const expected_gets = MockTorrentIo::Operations_t{{
      { TorId, singleBlockSpan(Block) },
    }};
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, prefetchUncached)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0U, 3U };

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    cache->prefetch(TorId, Span);

    auto const expected_prefetches = MockTorrentIo::Operations_t{
      { TorId, Span },
    };
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}

TEST_F(CacheTest, prefetchPartialCache)
{
    // This is similar to CacheTest.prefetchUncached
    // but in this variation, `cache` has the first block
    // so only the rest of the span should be passed to
    // mio.prefetch()

    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0U, 3U };
    auto constexpr MiddleBlock = tr_block_index_t { 1U };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto const blocks = makeBlocks(block_info, { 0U, 1U, 2U });

    // create and populate the mock io

    auto mio = MockTorrentIo{ block_info };
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        mio.blocks_.try_emplace(MockTorrentIo::makeKey(TorId, block), blocks.at(block));
    }

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    // create and populate the cache

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    cache->put(TorId, singleBlockSpan(MiddleBlock), std::data(blocks.at(MiddleBlock)));

    // Now that MiddleBlock is cached, prefetching the entire span should
    // result in cache.prefetch() calling mio.prefetch() on the two subspans
    cache->prefetch(TorId, Span);
    auto const expected_prefetches = MockTorrentIo::Operations_t{
      { TorId, { Span.begin, MiddleBlock } },
      { TorId, { MiddleBlock + 1, Span.end } },
    };
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}

TEST_F(CacheTest, prefetchBiscectedSpan)
{
    // This is similar to CacheTest.prefetchUncached
    // but in this variation, `cache` has the first block
    // so only the rest of the span should be passed to
    // mio.prefetch()

    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto blocks = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    blocks.try_emplace(1U, makeRandomBlock(block_info.blockSize(1U)));

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0U, 4U };

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    cache->put(TorId, tr_block_span_t{ 1U, 2U }, std::data(blocks[1U]));
    cache->prefetch(TorId, Span);

    auto const expected_prefetches = MockTorrentIo::Operations_t{
      { TorId, { 0U, 1U } },
      { TorId, { 2U, 4U } },
    };
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}

TEST_F(CacheTest, getUncachedSpan)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data and populate mio
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0U, 3U };
    auto blocks = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    for (tr_block_index_t block = Span.begin; block != Span.end; ++block)
    {
        blocks.try_emplace(block, std::vector<uint8_t>(block_info.blockSize(block), static_cast<char>('0' + block)));
        mio.blocks_.try_emplace(MockTorrentIo::makeKey(TorId, block), blocks[block]);
    }

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    auto buf = std::vector<uint8_t>(block_info.blockSize(0) * (Span.end - Span.begin));
    cache->get(TorId, Span, std::data(buf));

    auto const expected_gets = MockTorrentIo::Operations_t{
        { TorId, Span },
    };
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto expected_buf = std::vector<uint8_t>{};
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        std::copy(std::begin(blocks[block]), std::end(blocks[block]), std::back_inserter(expected_buf));
    }
    EXPECT_EQ(expected_buf, buf);
}

TEST_F(CacheTest, getPartialCache)
{
    // This is similar to CacheTest.getUncached
    // but in this variation, `cache` has the first block
    // so only the rest of the span should be passed to
    // mio.get()

    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data and populate mio
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr Span = tr_block_span_t{ 0U, 5U };
    auto blocks = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    for (tr_block_index_t block = Span.begin; block != Span.end; ++block)
    {
        blocks.try_emplace(block, std::vector<uint8_t>(block_info.blockSize(block), static_cast<char>('0' + block)));
        mio.blocks_.try_emplace(MockTorrentIo::makeKey(TorId, block), blocks[block]);
    }

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    auto buf = std::vector<uint8_t>(block_info.blockSize(0) * (Span.end - Span.begin));
    EXPECT_TRUE(cache->put(TorId, { 2U, 3U }, std::data(blocks[2U])));
    cache->get(TorId, Span, std::data(buf));

    auto const expected_gets = MockTorrentIo::Operations_t{
        { TorId, { 0U, 2U } },
        { TorId, { 3U, 5U } },
    };
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto expected_buf = std::vector<uint8_t>{};
    for (auto block = Span.begin; block < Span.end; ++block)
    {
        std::copy(std::begin(blocks[block]), std::end(blocks[block]), std::back_inserter(expected_buf));
    }
    EXPECT_EQ(expected_buf, buf);
}

#if 0
TEST_F(CacheTest, getBiscectedSpan)
{
    // This is similar to CacheTest.prefetchUncached
    // but in this variation, `cache` has the first block
    // so only the rest of the span should be passed to
    // mio.prefetch()

    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto blocks = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    blocks.try_emplace(1U, makeRandomBlock(block_info.blockSize(1U)));

    // create block data
    auto constexpr TorId = tr_torrent_id_t{ 1 };
    auto constexpr = tr_block_span_t{ 0U, 4U };

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    cache->put(tor_id, tr_block_span_t{ 1U, 2U }, std::data(blocks[1U]));
    cache->prefetch(tor_id, span);

    auto expected_prefetches = std::vector<MockTorrentIo::Operation>{};
    expected_prefetches.emplace_back(tor_id, tr_block_span_t{ 0U, 1U });
    expected_prefetches.emplace_back(tor_id, tr_block_span_t{ 2U, 4U });
    EXPECT_EQ(expected_prefetches, mio.prefetches_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
}
#endif

TEST_F(CacheTest, honorsByteLimit)
{
}

TEST_F(CacheTest, flushesOldestFirst)
{
}

TEST_F(CacheTest, saveTorrent)
{
}

TEST_F(CacheTest, saveSpan)
{
}

TEST_F(CacheTest, setMaxBytesToLowerValueReducesCacheSize)
{
}

TEST_F(CacheTest, returnsFalseIfGetFails)
{
}
