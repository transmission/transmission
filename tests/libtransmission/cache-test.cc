// This file Copyright (C) 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <map>
#include <utility>

#include "transmission.h"

#include "cache-new.h"

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

    bool putBlock(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t const* block_data) final
    {
        writes_.push_back(makeKey(tor_id, block));
        std::copy_n(block_data, block_info_.blockSize(block), std::data(blocks_[makeKey(tor_id, block)]));
        return true;
    }

    bool getBlock(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t* setme) final
    {
        reads_.push_back(makeKey(tor_id, block));

        auto it = blocks_.find(makeKey(tor_id, block));
        if (it != std::end(blocks_))
        {
            auto const& [key, data] = *it;
            std::copy_n(std::data(data), block_info_.blockSize(block), setme);
            return true;
        }

        return false;
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_index_t block) final
    {
        prefetches_.push_back(makeKey(tor_id, block));
    }

    std::map<key_t, std::array<uint8_t, tr_torrent_io::BlockSize>> blocks_;
    std::vector<key_t> reads_;
    std::vector<key_t> writes_;
    std::vector<key_t> prefetches_;
};

} // anonymous namespace

TEST_F(CacheTest, constructorMaxBytes)
{
    auto constexpr TotalSize = tr_block_info::BlockSize * 16 + 1;
    auto constexpr PieceSize = tr_block_info::BlockSize * 8;
    auto constexpr MaxBytes = tr_block_info::BlockSize * 8;
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    auto mio = MockTorrentIo{ block_info };
    auto* cache = tr_writeCacheNew(mio, block_info, MaxBytes);
    EXPECT_EQ(MaxBytes, cache->maxBytes());
    delete cache;
}
