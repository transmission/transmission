// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // for size_t
#include <cstdint> // for intX_t, uintX_t
#include <memory> // for std::unique_ptr
#include <utility> // for std::pair
#include <vector>

#include <small/vector.hpp>

#include "libtransmission/transmission.h"

#include "libtransmission/block-info.h"
#include "libtransmission/values.h"

class tr_torrents;
struct tr_torrent;

class Cache
{
public:
    using BlockData = small::max_size_vector<uint8_t, tr_block_info::BlockSize>;
    using Memory = libtransmission::Values::Memory;

    Cache(tr_torrents const& torrents, Memory max_size);

    int set_limit(Memory max_size);

    // @return any error code from cacheTrim()
    int write_block(tr_torrent_id_t tor, tr_block_index_t block, std::unique_ptr<BlockData> writeme);

    int read_block(tr_torrent const& tor, tr_block_info::Location const& loc, size_t len, uint8_t* setme);
    int prefetch_block(tr_torrent const& tor, tr_block_info::Location const& loc, size_t len);
    int flush_torrent(tr_torrent_id_t tor_id);
    int flush_file(tr_torrent const& tor, tr_file_index_t file);

private:
    using Key = std::pair<tr_torrent_id_t, tr_block_index_t>;

    struct CacheBlock
    {
        Key key;
        std::unique_ptr<BlockData> buf;
    };

    using Blocks = std::vector<CacheBlock>;
    using CIter = Blocks::const_iterator;

    [[nodiscard]] static Key make_key(tr_torrent const& tor, tr_block_info::Location loc) noexcept;

    [[nodiscard]] static std::pair<CIter, CIter> find_biggest_span(CIter begin, CIter end) noexcept;

    [[nodiscard]] static CIter find_span_end(CIter span_begin, CIter end) noexcept;

    // @return any error code from tr_ioWrite()
    [[nodiscard]] int write_contiguous(CIter begin, CIter end) const;

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_span(CIter begin, CIter end);

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_biggest();

    // @return any error code from writeContiguous()
    [[nodiscard]] int cache_trim();

    [[nodiscard]] static constexpr size_t get_max_blocks(Memory const max_size) noexcept
    {
        return max_size.base_quantity() / tr_block_info::BlockSize;
    }

    [[nodiscard]] CIter get_block(tr_torrent const& tor, tr_block_info::Location const& loc) noexcept;

    tr_torrents const& torrents_;

    Blocks blocks_ = {};
    size_t max_blocks_ = 0;

    mutable size_t disk_writes_ = 0;
    mutable size_t disk_write_bytes_ = 0;
    mutable size_t cache_writes_ = 0;
    mutable size_t cache_write_bytes_ = 0;

    static constexpr struct
    {
        [[nodiscard]] constexpr bool operator()(Key const& key, CacheBlock const& block)
        {
            return key < block.key;
        }
        [[nodiscard]] constexpr bool operator()(CacheBlock const& block, Key const& key)
        {
            return block.key < key;
        }
    } CompareCacheBlockByKey{};
};
