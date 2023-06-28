// This file Copyright © 2010-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // for size_t
#include <cstdint> // for intX_t, uintX_t
#include <memory> // for std::unique_ptr
#include <utility> // for std::pair
#include <vector>

#include "transmission.h"

#include "block-info.h"

class tr_torrents;
struct tr_torrent;

class Cache
{
public:
    Cache(tr_torrents& torrents, int64_t max_bytes);

    int setLimit(int64_t new_limit);

    [[nodiscard]] constexpr auto getLimit() const noexcept
    {
        return max_bytes_;
    }

    // @return any error code from cacheTrim()
    int writeBlock(tr_torrent_id_t tor, tr_block_index_t block, std::unique_ptr<std::vector<uint8_t>> writeme);

    int readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme);
    int prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len);
    int flushTorrent(tr_torrent const* torrent);
    int flushFile(tr_torrent const* torrent, tr_file_index_t file);

private:
    using Key = std::pair<tr_torrent_id_t, tr_block_index_t>;

    struct CacheBlock
    {
        Key key;
        std::unique_ptr<std::vector<uint8_t>> buf;
    };

    using Blocks = std::vector<CacheBlock>;
    using CIter = Blocks::const_iterator;

    [[nodiscard]] static Key make_key(tr_torrent const* torrent, tr_block_info::Location loc) noexcept;

    [[nodiscard]] static std::pair<CIter, CIter> find_biggest_span(CIter begin, CIter end) noexcept;

    [[nodiscard]] static CIter find_span_end(CIter span_begin, CIter end) noexcept;

    // @return any error code from tr_ioWrite()
    [[nodiscard]] int write_contiguous(CIter begin, CIter end) const;

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_span(CIter begin, CIter end);

    // @return any error code from writeContiguous()
    [[nodiscard]] int flush_biggest();

    // @return any error code from writeContiguous()
    [[nodiscard]] int cacheTrim();

    [[nodiscard]] static size_t getMaxBlocks(int64_t max_bytes) noexcept;

    [[nodiscard]] CIter getBlock(tr_torrent const* torrent, tr_block_info::Location loc) noexcept;

    tr_torrents& torrents_;

    Blocks blocks_ = {};
    size_t max_blocks_ = 0;
    size_t max_bytes_ = 0;

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
