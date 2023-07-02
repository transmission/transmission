// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstdlib> // std::lldiv()
#include <iterator> // std::distance(), std::next(), std::prev()
#include <limits> // std::numeric_limits<size_t>::max()
#include <memory>
#include <numeric> // std::accumulate()
#include <utility> // std::make_pair()
#include <vector>

#include <fmt/core.h>

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "log.h"
#include "torrent.h"
#include "torrents.h"
#include "tr-assert.h"
#include "utils.h" // tr_time(), tr_formatter

Cache::Key Cache::make_key(tr_torrent const* torrent, tr_block_info::Location loc) noexcept
{
    return std::make_pair(torrent->id(), loc.block);
}

Cache::CIter Cache::find_span_end(CIter span_begin, CIter end) noexcept
{
    static constexpr auto NotAdjacent = [](CacheBlock const& block1, CacheBlock const& block2)
    {
        return block1.key.first != block2.key.first || block1.key.second + 1 != block2.key.second;
    };
    auto const span_end = std::adjacent_find(span_begin, end, NotAdjacent);
    return span_end == end ? end : std::next(span_end);
}

std::pair<Cache::CIter, Cache::CIter> Cache::find_biggest_span(CIter const begin, CIter const end) noexcept
{
    auto biggest_begin = begin;
    auto biggest_end = begin;
    auto biggest_len = std::distance(biggest_begin, biggest_end);

    for (auto span_begin = begin; span_begin < end;)
    {
        auto span_end = find_span_end(span_begin, end);
        auto const len = std::distance(span_begin, span_end);

        if (len > biggest_len)
        {
            biggest_begin = span_begin;
            biggest_end = span_end;
            biggest_len = len;
        }

        std::advance(span_begin, len);
    }

    return { biggest_begin, biggest_end };
}

int Cache::write_contiguous(CIter const begin, CIter const end) const
{
    // The most common case without an extra data copy.
    auto const* towrite = begin->buf.get();

    // Contiguous area to join more than one block, if any.
    auto buf = std::vector<uint8_t>{};

    if (end - begin > 1)
    {
        // copy blocks into contiguous memory
        auto const buflen = std::accumulate(
            begin,
            end,
            size_t{},
            [](size_t sum, auto const& block) { return sum + std::size(*block.buf); });
        buf.resize(buflen);
        auto* walk = std::data(buf);
        for (auto iter = begin; iter != end; ++iter)
        {
            TR_ASSERT(begin->key.first == iter->key.first);
            TR_ASSERT(begin->key.second + std::distance(begin, iter) == iter->key.second);
            walk = std::copy_n(std::data(*iter->buf), std::size(*iter->buf), walk);
        }
        TR_ASSERT(std::data(buf) + std::size(buf) == walk);
        towrite = &buf;
    }

    // save it
    auto const& [torrent_id, block] = begin->key;
    auto* const tor = torrents_.get(torrent_id);
    if (tor == nullptr)
    {
        return EINVAL;
    }

    auto const loc = tor->blockLoc(block);

    if (auto const err = tr_ioWrite(tor, loc, std::size(*towrite), std::data(*towrite)); err != 0)
    {
        return err;
    }

    ++disk_writes_;
    disk_write_bytes_ += std::size(*towrite);
    return {};
}

size_t Cache::getMaxBlocks(int64_t max_bytes) noexcept
{
    return std::lldiv(max_bytes, tr_block_info::BlockSize).quot;
}

int Cache::setLimit(int64_t new_limit)
{
    max_bytes_ = new_limit;
    max_blocks_ = getMaxBlocks(new_limit);

    tr_logAddDebug(fmt::format("Maximum cache size set to {} ({} blocks)", tr_formatter_mem_B(max_bytes_), max_blocks_));

    return cacheTrim();
}

Cache::Cache(tr_torrents& torrents, int64_t max_bytes)
    : torrents_{ torrents }
    , max_blocks_(getMaxBlocks(max_bytes))
    , max_bytes_(max_bytes)
{
}

// ---

int Cache::writeBlock(tr_torrent_id_t tor_id, tr_block_index_t block, std::unique_ptr<std::vector<uint8_t>> writeme)
{
    auto const key = Key{ tor_id, block };
    auto iter = std::lower_bound(std::begin(blocks_), std::end(blocks_), key, CompareCacheBlockByKey);
    if (iter == std::end(blocks_) || iter->key != key)
    {
        iter = blocks_.emplace(iter);
        iter->key = key;
    }

    iter->buf = std::move(writeme);

    ++cache_writes_;
    cache_write_bytes_ += std::size(*iter->buf);

    return cacheTrim();
}

Cache::CIter Cache::getBlock(tr_torrent const* torrent, tr_block_info::Location loc) noexcept
{
    if (auto const [begin, end] = std::equal_range(
            std::begin(blocks_),
            std::end(blocks_),
            make_key(torrent, loc),
            CompareCacheBlockByKey);
        begin < end)
    {
        return begin;
    }

    return std::end(blocks_);
}

int Cache::readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme)
{
    if (auto const iter = getBlock(torrent, loc); iter != std::end(blocks_))
    {
        std::copy_n(std::begin(*iter->buf), len, setme);
        return {};
    }

    return tr_ioRead(torrent, loc, len, setme);
}

int Cache::prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len)
{
    if (auto const iter = getBlock(torrent, loc); iter != std::end(blocks_))
    {
        return {}; // already have it
    }

    return tr_ioPrefetch(torrent, loc, len);
}

// ---

int Cache::flush_span(CIter const begin, CIter const end)
{
    for (auto span_begin = begin; span_begin < end;)
    {
        auto const span_end = find_span_end(span_begin, end);

        if (auto const err = write_contiguous(span_begin, span_end); err != 0)
        {
            return err;
        }

        span_begin = span_end;
    }

    blocks_.erase(begin, end);
    return {};
}

int Cache::flushFile(tr_torrent const* torrent, tr_file_index_t file)
{
    auto const tor_id = torrent->id();
    auto const [block_begin, block_end] = tr_torGetFileBlockSpan(torrent, file);

    return flush_span(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_begin), CompareCacheBlockByKey),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_end), CompareCacheBlockByKey));
}

int Cache::flushTorrent(tr_torrent const* torrent)
{
    auto const tor_id = torrent->id();

    return flush_span(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, 0), CompareCacheBlockByKey),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id + 1, 0), CompareCacheBlockByKey));
}

int Cache::flush_biggest()
{
    auto const [begin, end] = find_biggest_span(std::begin(blocks_), std::end(blocks_));

    if (begin == end) // nothing to flush
    {
        return 0;
    }

    if (auto const err = write_contiguous(begin, end); err != 0)
    {
        return err;
    }

    blocks_.erase(begin, end);
    return 0;
}

int Cache::cacheTrim()
{
    while (std::size(blocks_) > max_blocks_)
    {
        if (auto const err = flush_biggest(); err != 0)
        {
            return err;
        }
    }

    return 0;
}
