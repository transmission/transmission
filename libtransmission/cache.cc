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

Cache::Key Cache::makeKey(tr_torrent const* torrent, tr_block_info::Location loc) noexcept
{
    return std::make_pair(torrent->id(), loc.block);
}

std::pair<Cache::CIter, Cache::CIter> Cache::findContiguous(CIter const begin, CIter const end, CIter const iter) noexcept
{
    if (iter == end)
    {
        return std::make_pair(end, end);
    }

    auto span_begin = iter;
    for (auto key = iter->key;;)
    {
        if (span_begin == begin)
        {
            break;
        }

        --key.second;
        auto const prev = std::prev(span_begin);
        if (prev->key != key)
        {
            break;
        }
    }

    auto span_end = std::next(iter);
    for (auto key = iter->key;;)
    {
        if (span_end == end)
        {
            break;
        }

        ++key.second;
        if (span_end->key != key)
        {
            break;
        }
    }

    return std::make_pair(span_begin, span_end);
}

int Cache::writeContiguous(CIter const begin, CIter const end) const
{
    // The most common case without an extra data copy.
    auto const* towrite = begin->buf.get();

    // Contiguous area to join more than one block, if any.
    auto buf = std::vector<uint8_t>{};

    if (end - begin > 1)
    {
        // Yes, there are.
        auto const buflen = std::accumulate(
            begin,
            end,
            size_t{},
            [](size_t sum, auto const& block) { return sum + std::size(*block.buf); });
        buf.reserve(buflen);
        for (auto iter = begin; iter != end; ++iter)
        {
            TR_ASSERT(begin->key.first == iter->key.first);
            TR_ASSERT(begin->key.second + std::distance(begin, iter) == iter->key.second);
            buf.insert(std::end(buf), std::begin(*iter->buf), std::end(*iter->buf));
        }
        TR_ASSERT(std::size(buf) == buflen);
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

int Cache::writeBlock(tr_torrent_id_t tor_id, tr_block_index_t block, std::unique_ptr<std::vector<uint8_t>>& writeme)
{
    auto const key = Key{ tor_id, block };
    auto iter = std::lower_bound(std::begin(blocks_), std::end(blocks_), key, CompareCacheBlockByKey{});
    if (iter == std::end(blocks_) || iter->key != key)
    {
        iter = blocks_.emplace(iter);
        iter->key = key;
    }

    iter->time_added = tr_time();

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
            makeKey(torrent, loc),
            CompareCacheBlockByKey{});
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

int Cache::flushSpan(CIter const begin, CIter const end)
{
    for (auto walk = begin; walk < end;)
    {
        auto const [contig_begin, contig_end] = findContiguous(begin, end, walk);

        if (auto const err = writeContiguous(contig_begin, contig_end); err != 0)
        {
            return err;
        }

        walk = contig_end;
    }

    blocks_.erase(begin, end);
    return {};
}

int Cache::flushFile(tr_torrent const* torrent, tr_file_index_t file)
{
    auto const compare = CompareCacheBlockByKey{};
    auto const tor_id = torrent->id();
    auto const [block_begin, block_end] = tr_torGetFileBlockSpan(torrent, file);

    return flushSpan(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_begin), compare),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_end), compare));
}

int Cache::flushTorrent(tr_torrent const* torrent)
{
    auto const compare = CompareCacheBlockByKey{};
    auto const tor_id = torrent->id();

    return flushSpan(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, 0), compare),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id + 1, 0), compare));
}

int Cache::flushOldest()
{
    auto const oldest = std::min_element(
        std::begin(blocks_),
        std::end(blocks_),
        [](auto const& a, auto const& b) { return a.time_added < b.time_added; });

    if (oldest == std::end(blocks_)) // nothing to flush
    {
        return 0;
    }

    auto const [begin, end] = findContiguous(std::begin(blocks_), std::end(blocks_), oldest);

    if (auto const err = writeContiguous(begin, end); err != 0)
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
        if (auto const err = flushOldest(); err != 0)
        {
            return err;
        }
    }

    return 0;
}
