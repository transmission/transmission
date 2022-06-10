// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib> /* qsort() */
#include <ctime>
#include <limits> // for std::numeric_limits<size_t>::max()

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
#include "utils.h"

Cache::CacheBlock::CacheBlock(tr_torrent* _tor, tr_block_info::Location _loc, uint32_t _length)
    : tor(_tor)
    , loc(_loc)
    , length(_length)
    , evbuf(evbuffer_new())
{
}

// return a count of how many contiguous blocks there are starting at this pos
size_t Cache::getBlockRun(size_t pos, RunInfo* info) const
{
    size_t const n = std::size(this->blocks_);
    CacheBlock const& ref = this->blocks_[pos];
    auto block = ref.loc.block;
    size_t len = 0;

    for (auto i = pos; i < n; ++i)
    {
        CacheBlock const& b = this->blocks_[i];

        if (b.loc.block != block)
        {
            break;
        }

        if (b.tor != ref.tor)
        {
            break;
        }

        ++block;
        ++len;
    }

    if (info != nullptr)
    {
        CacheBlock const& b = this->blocks_[pos + len - 1];
        info->last_block_time = b.time;
        info->is_piece_done = b.tor->hasPiece(b.loc.piece);
        info->is_multi_piece = b.loc.piece != ref.loc.piece;
        info->len = len;
        info->pos = pos;
    }

    return len;
}

// Calculate runs
//   - Stale runs, runs sitting in cache for a long time or runs not growing, get priority.
//     Returns number of runs.
size_t Cache::calcRuns(std::vector<RunInfo>& runs) const
{
    size_t const n = std::size(this->blocks_);
    size_t n_runs = 0;
    time_t const now = tr_time();

    for (size_t pos = 0; pos < n; pos += runs[n_runs++].len)
    {
        size_t rank = getBlockRun(pos, &runs[n_runs]);

        // This adds ~2 to the relative length of a run for every minute it has
        // languished in the cache.
        rank += (now - runs[n_runs].last_block_time) / 32;

        // Flushing stale blocks should be a top priority as the probability of them
        // growing is very small, for blocks on piece boundaries, and nonexistant for
        // blocks inside pieces.
        rank |= runs[n_runs].is_piece_done ? DONEFLAG : 0;

        // Move the multi piece runs higher
        rank |= runs[n_runs].is_multi_piece ? MULTIFLAG : 0;

        runs[n_runs].rank = rank;
    }

    std::sort(std::begin(runs), std::begin(runs) + static_cast<ssize_t>(n_runs), compareRuns);
    return n_runs;
}

int Cache::flushContiguous(size_t pos, size_t n)
{
    int err = 0;
    auto* const buf = tr_new(uint8_t, n * tr_block_info::BlockSize);
    auto* walk = buf;

    auto& b = this->blocks_[pos];
    auto* const tor = b.tor;
    auto const loc = b.loc;

    for (size_t i = 0; i < n; ++i)
    {
        b = this->blocks_[pos + i];
        evbuffer_copyout(b.evbuf, walk, b.length);
        walk += b.length;
        evbuffer_free(b.evbuf);
    }

    auto range_begin = std::begin(this->blocks_) + static_cast<ssize_t>(pos);
    this->blocks_.erase(range_begin, range_begin + static_cast<ssize_t>(n));

    err = tr_ioWrite(tor, loc, walk - buf, buf);
    tr_free(buf);

    ++this->disk_writes_;
    this->disk_write_bytes_ += walk - buf;
    return err;
}

int Cache::flushRuns(std::vector<RunInfo>& runs, size_t n)
{
    int err = 0;

    for (size_t i = 0; err == 0 && i < n; i++)
    {
        err = flushContiguous(runs[i].pos, runs[i].len);

        for (size_t j = i + 1; j < n; j++)
        {
            if (runs[j].pos > runs[i].pos)
            {
                TR_ASSERT(runs[j].pos >= runs[i].len);
                runs[j].pos -= runs[i].len;
            }
        }
    }

    return err;
}

int Cache::cacheTrim()
{
    int err = 0;
    size_t blocks_size = std::size(this->blocks_);

    if (blocks_size > this->max_blocks_)
    {
        // Amount of cache that should be removed by the flush. This influences how large
        // runs can grow as well as how often flushes will happen.
        size_t const cacheCutoff = 1 + this->max_blocks_ / 4;
        std::vector<RunInfo> runs(blocks_size);
        runs.resize(blocks_size);

        size_t i = 0;
        size_t j = 0;

        calcRuns(runs);

        while (j < cacheCutoff)
        {
            j += runs[i++].len;
        }

        err = flushRuns(runs, i);
    }

    return err;
}

/***
****
***/

size_t Cache::getMaxBlocks(int64_t max_bytes)
{
    return std::lldiv(max_bytes, tr_block_info::BlockSize).quot;
}

int Cache::setLimit(int64_t new_limit)
{
    this->max_bytes_ = new_limit;
    this->max_blocks_ = getMaxBlocks(new_limit);

    tr_logAddDebug(
        fmt::format("Maximum cache size set to {} ({} blocks)", tr_formatter_mem_B(this->max_bytes_), this->max_blocks_));

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

bool Cache::cacheBlockCompare(CacheBlock const& a, CacheBlock const& b)
{
    // The comparer should return whether a is < b, but for descending (reverse) order we invert the comparisons
    // primary key: torrent id
    if (a.tor->uniqueId != b.tor->uniqueId)
    {
        return a.tor->uniqueId > b.tor->uniqueId;
    }

    // secondary key: block #
    return a.loc.block > b.loc.block;
}

// Non-const variant returns a mutable iterator
std::vector<Cache::CacheBlock>::iterator Cache::findBlock(tr_torrent* torrent, tr_block_info::Location loc)
{
    auto key = CacheBlock{};
    key.tor = torrent;
    key.loc = loc;

    auto result = std::equal_range(std::begin(this->blocks_), std::end(this->blocks_), key, cacheBlockCompare);

    if (std::distance(result.first, result.second) > 0)
    {
        // Found one or more
        return result.first;
    }

    return this->blocks_.end();
}

int Cache::writeBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t length, struct evbuffer* writeme)
{
    TR_ASSERT(tr_amInEventThread(torrent->session));
    TR_ASSERT(loc.block_offset == 0);
    TR_ASSERT(torrent->blockSize(loc.block) == length);
    TR_ASSERT(torrent->blockSize(loc.block) <= evbuffer_get_length(writeme));

    auto cb = findBlock(torrent, loc);
    if (cb == this->blocks_.end())
    {
        CacheBlock new_block(torrent, loc, length);
        auto const insert_point = std::upper_bound(this->blocks_.begin(), this->blocks_.end(), new_block, cacheBlockCompare);
        cb = this->blocks_.insert(insert_point, new_block);
    }

    TR_ASSERT(cb->length == length);

    cb->time = tr_time();

    evbuffer_drain(cb->evbuf, evbuffer_get_length(cb->evbuf));
    evbuffer_remove_buffer(writeme, cb->evbuf, cb->length);

    this->cache_writes_++;
    this->cache_write_bytes_ += cb->length;

    return cacheTrim();
}

int Cache::readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme)
{
    int err = 0;

    if (auto const cb = findBlock(torrent, loc); cb != this->blocks_.end())
    {
        evbuffer_copyout(cb->evbuf, setme, len);
    }
    else
    {
        err = tr_ioRead(torrent, loc, len, setme);
    }

    return err;
}

int Cache::prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len)
{
    int err = 0;

    if (auto const cb = findBlock(torrent, loc); cb == this->blocks_.end())
    {
        err = tr_ioPrefetch(torrent, loc, len);
    }

    return err;
}

/***
****
***/

// Returns max value for size_t (0xFFFF...) if not found
size_t Cache::findBlockPos(tr_torrent* torrent, tr_block_info::Location loc) const
{
    auto key = CacheBlock{};
    key.tor = torrent;
    key.loc = loc;

    auto const result = std::equal_range(std::begin(this->blocks_), std::end(this->blocks_), key, cacheBlockCompare);

    if (std::distance(result.first, result.second) > 0)
    {
        // Found one or more
        return result.first - this->blocks_.begin();
    }

    return std::numeric_limits<size_t>::max(); // return 0xFFFF.... if not found
}

int Cache::flushDone()
{
    int err = 0;

    if (!std::empty(this->blocks_))
    {
        size_t blocks_size = std::size(this->blocks_);
        std::vector<RunInfo> runs(blocks_size);
        runs.resize(blocks_size);

        size_t i = 0;
        size_t const n = calcRuns(runs);

        while (i < n && (runs[i].is_piece_done || runs[i].is_multi_piece))
        {
            runs[i++].rank |= SESSIONFLAG;
        }

        err = flushRuns(runs, i);
    }

    return err;
}

int Cache::flushFile(tr_torrent* torrent, tr_file_index_t i)
{
    auto const [begin, end] = tr_torGetFileBlockSpan(torrent, i);

    size_t const pos = findBlockPos(torrent, torrent->blockLoc(begin));

    tr_logAddTrace(fmt::format("flushing file {} from cache to disk: blocks [{}...{})", i, begin, end));

    /* flush out all the blocks in that file */
    int err = 0;
    while (err == 0 && pos < std::size(this->blocks_))
    {
        auto const& b = this->blocks_[pos];

        if (b.tor != torrent)
        {
            break;
        }

        if (b.loc.block < begin || b.loc.block >= end)
        {
            break;
        }

        err = flushContiguous(pos, getBlockRun(pos, nullptr));
    }

    return err;
}

int Cache::flushTorrent(tr_torrent* torrent)
{
    int err = 0;
    size_t const pos = findBlockPos(torrent, {});

    // flush out all the blocks in that torrent
    while (err == 0 && pos < std::size(this->blocks_))
    {
        auto const& b = this->blocks_[pos];

        if (b.tor != torrent)
        {
            break;
        }

        err = flushContiguous(pos, getBlockRun(pos, nullptr));
    }

    return err;
}
