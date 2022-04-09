// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::copy_n
#include <array>
#include <map>
#include <utility> // std::pair

#include "transmission.h"

#include "block-info.h"
#include "cache-new.h"

/****
*****
****/

namespace
{

auto constexpr BlockSize = tr_block_info::BlockSize;

struct cache_block
{
    uint32_t length;
    time_t time;
    std::array<uint8_t, BlockSize> data;
};

using torrent_id_t = tr_torrent_io::tr_torrent_id_t;

using cache_key_t = std::pair<torrent_id_t, tr_block_index_t>;

constexpr cache_key_t makeKey(torrent_id_t tor_id, tr_block_index_t block_index)
{
    return std::make_pair(tor_id, block_index);
}

using block_map_t = std::map<cache_key_t, cache_block>;

}

/****
*****
****/

class tr_write_cache_impl final: public tr_write_cache
{
public:
    tr_write_cache_impl(tr_torrent_io& torrent_io, tr_block_info const& block_info, size_t max_bytes)
        : block_info_{ block_info }
        , io_{ torrent_io }
    {
        setMaxBytes(max_bytes);
    }

    ~tr_write_cache_impl() final
    {
        setMaxBytes(0);
    }

    bool putBlock(torrent_id_t tor_id, tr_block_index_t block_index, uint8_t const* block_data) override
    {
        auto& entry = blocks_[makeKey(tor_id, block_index)];
        entry.length = block_info_.blockSize(block_index);
        std::copy_n(block_data, entry.length, std::data(entry.data));
        trim();
        return true;
    }

    bool getBlock(torrent_id_t tor_id, tr_block_index_t block, uint8_t* block_data) override
    {
        auto const iter = blocks_.find(makeKey(tor_id, block));
        if (iter == std::end(blocks_))
        {
            return io_.getBlock(tor_id, block, block_data);
        }

        auto const& [key, rec] = *iter;
        std::copy_n(std::data(rec.data), rec.length, block_data);
        return true;
    }

    void prefetch(torrent_id_t tor_id, tr_block_index_t block) override
    {
        auto const iter = blocks_.find(makeKey(tor_id, block));
        if (iter == std::end(blocks_))
        {
            return;
        }

        io_.prefetch(tor_id, block);
    }

    void setMaxBytes(size_t max_bytes) override
    {
        max_bytes_ = max_bytes;
        max_blocks_ = max_bytes > 0U ? max_bytes_ / BlockSize: 0U;
        trim();
    }

    [[nodiscard]] size_t maxBytes() const override
    {
        return max_bytes_;
    }

    void flushCompletePieces() override
    {
        // FIXME
    }

    void flushTorrent(torrent_id_t /*tor_id*/) override
    {
        // FIXME
    }

    void flushFile(torrent_id_t /*tor_id*/,  tr_file_index_t /*file*/) override
    {
        // FIXME
    }

    void trim()
    {
        while (std::size(blocks_) > max_blocks_)
        {
            /// FIXME
        }
    }

private:
    tr_block_info const& block_info_;
    block_map_t blocks_ = {};

    tr_torrent_io& io_;

    size_t max_blocks_ = {};
    size_t max_bytes_ = {};

    // size_t disk_writes = {};
    // size_t disk_write_bytes = {};
    // size_t cache_writes = {};
    // size_t cache_write_bytes = {};
};

/****
*****
****/

tr_write_cache* tr_writeCacheNew(tr_torrent_io& torrent_io, tr_block_info const& info, size_t max_bytes)
{
    return new tr_write_cache_impl(torrent_io, info, max_bytes);
}
