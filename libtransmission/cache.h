// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint> // for intX_t, uintX_t
#include <ctime>
#include <memory> // for std::unique_ptr
#include <utility> // for std::pair
#include <vector>

#include <small/vector.hpp>

#include "transmission.h"

#include "block-info.h"

class tr_torrents;
struct tr_torrent;

class Cache
{
public:
    using BlockData = small::max_size_vector<uint8_t, tr_block_info::BlockSize>;

    Cache(tr_torrents& torrents, size_t max_bytes);

    int set_limit(size_t new_limit);

    [[nodiscard]] constexpr auto get_limit() const noexcept
    {
        return max_bytes_;
    }

    // @return any error code from cacheTrim()
    int write_block(tr_torrent_id_t tor, tr_block_index_t block, std::unique_ptr<BlockData> writeme);

    int read_block(tr_torrent* torrent, tr_block_info::Location const& loc, uint32_t len, uint8_t* setme);
    int prefetch_block(tr_torrent* torrent, tr_block_info::Location const& loc, uint32_t len);
    int flush_torrent(tr_torrent const* torrent);
    int flush_file(tr_torrent const* torrent, tr_file_index_t file);

private:
    using Key = std::pair<tr_torrent_id_t, tr_block_index_t>;

    struct CacheBlock
    {
        Key key;
        std::unique_ptr<BlockData> buf;
        time_t time_added = {};
    };

    using Blocks = std::vector<CacheBlock>;
    using CIter = Blocks::const_iterator;

    struct CompareCacheBlockByKey
    {
        [[nodiscard]] constexpr bool operator()(Key const& key, CacheBlock const& block)
        {
            return key < block.key;
        }
        [[nodiscard]] constexpr bool operator()(CacheBlock const& block, Key const& key)
        {
            return block.key < key;
        }
    };

    [[nodiscard]] static Key make_key(tr_torrent const* torrent, tr_block_info::Location loc) noexcept;

    [[nodiscard]] static std::pair<CIter, CIter> find_contiguous(CIter const begin, CIter const end, CIter const iter) noexcept;

    // @return any error code from tr_ioWrite()
    [[nodiscard]] int write_contiguous(CIter const begin, CIter const end) const;

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_span(CIter const begin, CIter const end);

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_oldest();

    // @return any error code from writeContiguous()
    [[nodiscard]] int cache_trim();

    [[nodiscard]] static size_t get_max_blocks(size_t max_bytes) noexcept;

    [[nodiscard]] CIter get_block(tr_torrent const* torrent, tr_block_info::Location const& loc) noexcept;

    tr_torrents& torrents_;

    Blocks blocks_ = {};
    size_t max_blocks_ = 0;
    size_t max_bytes_ = 0;

    mutable size_t disk_writes_ = 0;
    mutable size_t disk_write_bytes_ = 0;
    mutable size_t cache_writes_ = 0;
    mutable size_t cache_write_bytes_ = 0;
};
