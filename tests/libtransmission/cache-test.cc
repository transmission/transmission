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

using CacheTest = ::testing::Test;
using namespace std::literals;

namespace
{

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
        writes_.emplace_back(tor_id, span);

        for (auto block = span.begin; block < span.end; ++block)
        {
            auto const key = makeKey(tor_id, block);
            auto& entry = blocks_[key];
            std::copy_n(block_data, blockSize(block), std::data(entry));
        }
        return true;
    }

    bool get(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t* setme) final
    {
        reads_.emplace_back(tor_id, span);

        for (auto block = span.begin; block < span.end; ++block)
        {
            auto const key = makeKey(tor_id, block);
            auto const iter = blocks_.find(key);
            if (iter != std::end(blocks_))
            {
                std::copy_n(std::data(iter->second), blockSize(block), setme);
                return true;
            }
        }

        return false;
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_span_t span) final
    {
        prefetches_.emplace_back(tor_id, span);
    }

    struct Operation
    {
        Operation(tr_torrent_id_t id, tr_block_span_t span)
            : id_{ id }
            , span_{ span }
        {
        }

        tr_torrent_id_t id_;
        tr_block_span_t span_;
    };

    std::map<key_t, std::array<uint8_t, tr_torrent_io::BlockSize>> blocks_;
    std::vector<Operation> reads_;
    std::vector<Operation> writes_;
    std::vector<Operation> prefetches_;
};

} // anonymous namespace

TEST_F(CacheTest, constructorMaxBytes)
{
    auto constexpr TotalSize = tr_block_info::BlockSize * 16 + 1;
    auto constexpr PieceSize = tr_block_info::BlockSize * 8;
    auto constexpr MaxBytes = tr_block_info::BlockSize * 8;
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto* cache = tr_writeCacheNew(mio, MaxBytes);

    EXPECT_EQ(MaxBytes, cache->maxBytes());

    delete cache;
}

TEST_F(CacheTest, putBlockDoesCache)
{
    auto constexpr TotalSize = tr_block_info::BlockSize * 16 + 1;
    auto constexpr PieceSize = tr_block_info::BlockSize * 8;
    auto constexpr MaxBytes = tr_block_info::BlockSize * 8;
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto mio = MockTorrentIo{ block_info };

    auto* cache = tr_writeCacheNew(mio, MaxBytes);

    auto const tor_id = tr_torrent_id_t{ 1 };
    auto const block_index = 0;
    auto block_data = std::vector<uint8_t>(block_info.blockSize(block_index));
    tr_rand_buffer(std::data(block_data), std::size(block_data));
    auto ok = cache->put(tor_id, { block_index, block_index + 1 }, std::data(block_data));
    EXPECT_TRUE(ok);

    auto cached_block_data = std::vector<uint8_t>(block_info.blockSize(block_index));
    ok = cache->get(tor_id, { block_index, block_index + 1 }, std::data(cached_block_data));
    EXPECT_TRUE(ok);
    EXPECT_EQ(block_data, cached_block_data);

    EXPECT_TRUE(std::empty(mio.reads_));
    EXPECT_TRUE(std::empty(mio.writes_));
    EXPECT_TRUE(std::empty(mio.prefetches_));

    delete cache;
}
