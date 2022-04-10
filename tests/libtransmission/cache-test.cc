// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <map>
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

        for (auto block = span.begin; block < span.end; ++block)
        {
            auto const key = makeKey(tor_id, block);
            auto const iter = blocks_.find(key);
            if (iter != std::end(blocks_))
            {
                std::copy_n(std::data(iter->second), std::size(iter->second), setme);
                return true;
            }
        }

        return false;
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_span_t span) final
    {
        prefetches_.emplace_back(tor_id, span);
    }

    using Operation = std::pair<tr_torrent_id_t, tr_block_span_t>;

    std::map<key_t, std::vector<uint8_t>> blocks_;
    std::vector<Operation> gets_;
    std::vector<Operation> puts_;
    std::vector<Operation> prefetches_;
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
    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const block_index = 0;
    auto const block_data = makeRandomBlock(block_info.blockSize(block_index));
    auto ok = cache->put(tor_id, { block_index, block_index + 1 }, std::data(block_data));
    EXPECT_TRUE(ok);

    // read it back again
    auto cached_block_data = std::vector<uint8_t>(block_info.blockSize(block_index));
    ok = cache->get(tor_id, { block_index, block_index + 1 }, std::data(cached_block_data));
    EXPECT_TRUE(ok);
    EXPECT_EQ(block_data, cached_block_data);

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
    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const block = tr_block_index_t{};
    auto const block_data = makeRandomBlock(block_info.blockSize(block));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the block
    auto const span = tr_block_span_t{ block, block + 1 };
    EXPECT_TRUE(cache->put(tor_id, span, std::data(block_data)));

    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the block was saved during cache destruction
    auto expected_puts = std::vector<MockTorrentIo::Operation>{};
    expected_puts.emplace_back(tor_id, span);
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}

TEST_F(CacheTest, blocksAreFoldedIntoSpan)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const span = tr_block_span_t{ 0, 3 };
    auto block_data = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    block_data.try_emplace(0U, makeRandomBlock(block_info.blockSize(0U)));
    block_data.try_emplace(1U, makeRandomBlock(block_info.blockSize(1U)));
    block_data.try_emplace(2U, makeRandomBlock(block_info.blockSize(2U)));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the span in a non-consecutive order
    EXPECT_TRUE(cache->put(tor_id, { 1U, 1U + 1 }, std::data(block_data[1U])));
    EXPECT_TRUE(cache->put(tor_id, { 0U, 0U + 1 }, std::data(block_data[0U])));
    EXPECT_TRUE(cache->put(tor_id, { 2U, 2U + 1 }, std::data(block_data[2U])));

    // cache should be holding it all, so mio_ should be untouched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the span was put to mio in a single operation
    auto expected_puts = std::vector<MockTorrentIo::Operation>{};
    expected_puts.emplace_back(tor_id, span);
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    EXPECT_EQ(block_data[0U], mio.blocks_[mio.makeKey(tor_id, 0U)]);
    EXPECT_EQ(block_data[1U], mio.blocks_[mio.makeKey(tor_id, 1U)]);
    EXPECT_EQ(block_data[2U], mio.blocks_[mio.makeKey(tor_id, 2U)]);
}

TEST_F(CacheTest, destructorSavesSpan)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const span = tr_block_span_t{ 0, 3 };
    auto block_data = std::map<tr_block_index_t, std::vector<uint8_t>>{};
    block_data.try_emplace(0U, makeRandomBlock(block_info.blockSize(0U)));
    block_data.try_emplace(1U, makeRandomBlock(block_info.blockSize(1U)));
    block_data.try_emplace(2U, makeRandomBlock(block_info.blockSize(2U)));

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // cache the span
    auto buf = std::vector<uint8_t>{};
    buf.insert(std::end(buf), std::begin(block_data[0U]), std::end(block_data[0U]));
    buf.insert(std::end(buf), std::begin(block_data[1U]), std::end(block_data[1U]));
    buf.insert(std::end(buf), std::begin(block_data[2U]), std::end(block_data[2U]));
    EXPECT_TRUE(cache->put(tor_id, span, std::data(buf)));

    // cache should be holding it all, so mio_ should be untouched
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    cache.reset();

    // confirm the span was saved during cache destruction
    auto expected_puts = std::vector<MockTorrentIo::Operation>{};
    expected_puts.emplace_back(tor_id, span);
    EXPECT_EQ(expected_puts, mio.puts_);
    EXPECT_TRUE(std::empty(mio.gets_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
    EXPECT_EQ(block_data[0U], mio.blocks_[mio.makeKey(tor_id, 0U)]);
    EXPECT_EQ(block_data[1U], mio.blocks_[mio.makeKey(tor_id, 1U)]);
    EXPECT_EQ(block_data[2U], mio.blocks_[mio.makeKey(tor_id, 2U)]);
}

TEST_F(CacheTest, uncachedGetAsksWrappedIo)
{
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    // create block data
    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const block = tr_block_index_t{};
    auto const block_data = makeRandomBlock(block_info.blockSize(block));

    // save that block to the mio
    auto const span = tr_block_span_t{ block, block + 1 };
    auto ok = mio.put(tor_id, span, std::data(block_data));
    EXPECT_TRUE(ok);
    mio.puts_.clear();

    auto cache = std::shared_ptr<tr_write_cache>(tr_writeCacheNew(mio, MaxBytes));

    // read that block from the cache
    auto cached_block_data = std::vector<uint8_t>(block_info.blockSize(block));
    ok = cache->get(tor_id, span, std::data(cached_block_data));
    EXPECT_TRUE(ok);
    EXPECT_EQ(block_data, cached_block_data);

    // confirm that cache got it from mio
    auto expected_gets = std::vector<MockTorrentIo::Operation>{};
    expected_gets.emplace_back(tor_id, span);
    EXPECT_EQ(expected_gets, mio.gets_);
    EXPECT_TRUE(std::empty(mio.puts_));
    EXPECT_TRUE(std::empty(mio.prefetches_));
}
