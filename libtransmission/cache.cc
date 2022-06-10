// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib> // std::lldiv()
#include <iterator> // std::distance(), std::next(), std::prev()
#include <limits> // std::numeric_limits<size_t>::max()
#include <numeric> // std::accumulate()
#include <utility> // std::make_pair()

#include <event2/buffer.h>

#include <fmt/core.h>

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "log.h"
#include "torrent.h"
#include "torrents.h"
#include "tr-assert.h"
#include "trevent.h"

Cache::Key Cache::makeKey(tr_torrent const* torrent, tr_block_info::Location loc) noexcept
{
    return std::make_pair(torrent->uniqueId, loc.block);
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
    // join the blocks together into contiguous memory `buf`
    auto buf = std::vector<uint8_t>{};
    auto const buflen = std::accumulate(
        begin,
        end,
        size_t{},
        [](size_t sum, auto const& block) { return sum + std::size(block.buf); });
    buf.reserve(buflen);
    for (auto iter = begin; iter != end; ++iter)
    {
        TR_ASSERT(begin->key.first == iter->key.first);
        TR_ASSERT(begin->key.second + std::distance(begin, iter) == iter->key.second);
        buf.insert(std::end(buf), std::begin(iter->buf), std::end(iter->buf));
    }
    TR_ASSERT(std::size(buf) == buflen);

    // save it
    auto* const tor = torrents_.get(begin->key.first);
    auto const loc = tor->blockLoc(begin->key.second);

    if (auto const err = tr_ioWrite(tor, loc, std::size(buf), std::data(buf)); err != 0)
    {
        return err;
    }

    ++disk_writes_;
    disk_write_bytes_ += std::size(buf);
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

/***
****
***/

int Cache::writeBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t length, struct evbuffer* writeme)
{
    TR_ASSERT(tr_amInEventThread(torrent->session));
    TR_ASSERT(loc.block_offset == 0);
    TR_ASSERT(torrent->blockSize(loc.block) == length);
    TR_ASSERT(torrent->blockSize(loc.block) <= evbuffer_get_length(writeme));

    auto const key = makeKey(torrent, loc);
    auto iter = std::lower_bound(std::begin(blocks_), std::end(blocks_), key, CompareCacheBlockByKey{});
    if (iter == std::end(blocks_) || iter->key != key)
    {
        iter = blocks_.emplace(iter);
        iter->key = key;
    }

    iter->time_added = tr_time();

    iter->buf.resize(length);
    evbuffer_remove(writeme, std::data(iter->buf), std::size(iter->buf));

    ++cache_writes_;
    cache_write_bytes_ += length;

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
        std::copy_n(std::begin(iter->buf), len, setme);
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

/***
****
***/

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

int Cache::flushFile(tr_torrent* torrent, tr_file_index_t i)
{
    auto const compare = CompareCacheBlockByKey{};
    auto const tor_id = torrent->uniqueId;
    auto const [block_begin, block_end] = tr_torGetFileBlockSpan(torrent, i);

    return flushSpan(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_begin), compare),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, block_end), compare));
}

int Cache::flushTorrent(tr_torrent* torrent)
{
    auto const compare = CompareCacheBlockByKey{};
    auto const tor_id = torrent->uniqueId;

    return flushSpan(
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id, 0), compare),
        std::lower_bound(std::begin(blocks_), std::end(blocks_), std::make_pair(tor_id + 1, 0), compare));
}

int Cache::cacheTrim()
{
    while (std::size(blocks_) > max_blocks_)
    {
        auto const oldest_element = std::min_element(
            std::begin(blocks_),
            std::end(blocks_),
            [](auto const& a, auto const& b) { return a.time_added < b.time_added; });

        auto const [begin, end] = findContiguous(std::begin(blocks_), std::end(blocks_), oldest_element);

        if (auto const err = flushSpan(begin, end); err != 0)
        {
            return err;
        }
    }

    return {};
}
