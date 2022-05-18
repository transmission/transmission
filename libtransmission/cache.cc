// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib> /* qsort() */
#include <ctime>

#include <event2/buffer.h>

#include <fmt/core.h>

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "log.h"
#include "ptrarray.h"
#include "torrent.h"
#include "tr-assert.h"
#include "trevent.h"
#include "utils.h"

/* return a count of how many contiguous blocks there are starting at this pos */
int tr_cache::getBlockRun(int pos, run_info* info) const
{
    int const n = tr_ptrArraySize(&this->blocks);
    auto const* const* blocks1 = (struct cache_block const* const*)tr_ptrArrayBase(&this->blocks);
    struct cache_block const* ref = blocks1[pos];
    auto block = ref->loc.block;
    int len = 0;

    for (int i = pos; i < n; ++i)
    {
        struct cache_block const* b = blocks1[i];

        if (b->loc.block != block)
        {
            break;
        }

        if (b->tor != ref->tor)
        {
            break;
        }

        ++block;
        ++len;
    }

    if (info != nullptr)
    {
        struct cache_block const* b = blocks1[pos + len - 1];
        info->last_block_time = b->time;
        info->is_piece_done = b->tor->hasPiece(b->loc.piece);
        info->is_multi_piece = b->loc.piece != blocks1[pos]->loc.piece;
        info->len = len;
        info->pos = pos;
    }

    return len;
}

// higher rank comes before lower rank
int tr_cache::compareRuns(void const* va, void const* vb)
{
    auto const* const a = static_cast<run_info const*>(va);
    auto const* const b = static_cast<run_info const*>(vb);
    return b->rank - a->rank;
}

/* Calculate runs
 *   - Stale runs, runs sitting in cache for a long time or runs not growing, get priority.
 *     Returns number of runs.
 */
// TODO: return std::vector
int tr_cache::calcRuns(struct run_info* runs) const
{
    int const n = tr_ptrArraySize(&this->blocks);
    int i = 0;
    time_t const now = tr_time();

    for (int pos = 0; pos < n; pos += runs[i++].len)
    {
        int rank = getBlockRun(pos, &runs[i]);

        /* This adds ~2 to the relative length of a run for every minute it has
         * languished in the cache. */
        rank += (now - runs[i].last_block_time) / 32;

        /* Flushing stale blocks should be a top priority as the probability of them
         * growing is very small, for blocks on piece boundaries, and nonexistant for
         * blocks inside pieces. */
        rank |= runs[i].is_piece_done ? DONEFLAG : 0;

        /* Move the multi piece runs higher */
        rank |= runs[i].is_multi_piece ? MULTIFLAG : 0;

        runs[i].rank = rank;
    }

    std::qsort(runs, i, sizeof(struct run_info), compareRuns);
    return i;
}

int tr_cache::flushContiguous(int pos, int n)
{
    int err = 0;
    auto* const buf = tr_new(uint8_t, n * tr_block_info::BlockSize);
    auto* walk = buf;
    auto** blocks1 = (struct cache_block**)tr_ptrArrayBase(&this->blocks);

    auto* b = blocks1[pos];
    auto* const tor = b->tor;
    auto const loc = b->loc;

    for (int i = 0; i < n; ++i)
    {
        b = blocks1[pos + i];
        evbuffer_copyout(b->evbuf, walk, b->length);
        walk += b->length;
        evbuffer_free(b->evbuf);
        tr_free(b);
    }

    tr_ptrArrayErase(&this->blocks, pos, pos + n);

    err = tr_ioWrite(tor, loc, walk - buf, buf);
    tr_free(buf);

    ++this->disk_writes;
    this->disk_write_bytes += walk - buf;
    return err;
}

int tr_cache::flushRuns(struct run_info* runs, int n)
{
    int err = 0;

    for (int i = 0; err == 0 && i < n; i++)
    {
        err = flushContiguous(runs[i].pos, runs[i].len);

        for (int j = i + 1; j < n; j++)
        {
            if (runs[j].pos > runs[i].pos)
            {
                runs[j].pos -= runs[i].len;
            }
        }
    }

    return err;
}

int tr_cache::cacheTrim()
{
    int err = 0;

    if (tr_ptrArraySize(&this->blocks) > this->max_blocks)
    {
        /* Amount of cache that should be removed by the flush. This influences how large
         * runs can grow as well as how often flushes will happen. */
        int const cacheCutoff = 1 + this->max_blocks / 4;
        auto* const runs = tr_new(struct run_info, tr_ptrArraySize(&this->blocks));
        int i = 0;
        int j = 0;

        calcRuns(runs);

        while (j < cacheCutoff)
        {
            j += runs[i++].len;
        }

        err = flushRuns(runs, i);
        tr_free(runs);
    }

    return err;
}

/***
****
***/

int tr_cache::getMaxBlocks(int64_t max_bytes)
{
    return std::lldiv(max_bytes, tr_block_info::BlockSize).quot;
}

int tr_cache::setLimit(int64_t new_limit)
{
    this->max_bytes = new_limit;
    this->max_blocks = getMaxBlocks(new_limit);

    tr_logAddDebug(
        fmt::format("Maximum cache size set to {} ({} blocks)", tr_formatter_mem_B(this->max_bytes), this->max_blocks));

    return cacheTrim();
}

tr_cache::tr_cache(int64_t max_bytes)
    : max_blocks(getMaxBlocks(max_bytes))
    , max_bytes(max_bytes)
{
    this->blocks = {};
}

tr_cache::~tr_cache()
{
    tr_ptrArrayDestruct(&this->blocks, nullptr);
}

/***
****
***/

int tr_cache::cache_block_compare(void const* va, void const* vb)
{
    auto const* a = static_cast<cache_block const*>(va);
    auto const* b = static_cast<cache_block const*>(vb);

    /* primary key: torrent id */
    if (a->tor->uniqueId != b->tor->uniqueId)
    {
        return a->tor->uniqueId < b->tor->uniqueId ? -1 : 1;
    }

    /* secondary key: block # */
    if (a->loc.block != b->loc.block)
    {
        return a->loc.block < b->loc.block ? -1 : 1;
    }

    /* they're equal */
    return 0;
}

tr_cache::cache_block* tr_cache::findBlock(tr_torrent* torrent, tr_block_info::Location loc)
{
    auto key = cache_block{};
    key.tor = torrent;
    key.loc = loc;
    return static_cast<cache_block*>(tr_ptrArrayFindSorted(&this->blocks, &key, cache_block_compare));
}

int tr_cache::writeBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t length, struct evbuffer* writeme)
{
    TR_ASSERT(tr_amInEventThread(torrent->session));
    TR_ASSERT(loc.block_offset == 0);
    TR_ASSERT(torrent->blockSize(loc.block) == length);
    TR_ASSERT(torrent->blockSize(loc.block) <= evbuffer_get_length(writeme));

    auto* cb = findBlock(torrent, loc);
    if (cb == nullptr)
    {
        cb = tr_new(struct cache_block, 1);
        cb->tor = torrent;
        cb->loc = loc;
        cb->length = length;
        cb->evbuf = evbuffer_new();
        tr_ptrArrayInsertSorted(&this->blocks, cb, cache_block_compare);
    }

    TR_ASSERT(cb->length == length);

    cb->time = tr_time();

    evbuffer_drain(cb->evbuf, evbuffer_get_length(cb->evbuf));
    evbuffer_remove_buffer(writeme, cb->evbuf, cb->length);

    this->cache_writes++;
    this->cache_write_bytes += cb->length;

    return cacheTrim();
}

int tr_cache::readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme)
{
    int err = 0;

    if (auto* const cb = findBlock(torrent, loc); cb != nullptr)
    {
        evbuffer_copyout(cb->evbuf, setme, len);
    }
    else
    {
        err = tr_ioRead(torrent, loc, len, setme);
    }

    return err;
}

int tr_cache::prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len)
{
    int err = 0;

    if (auto const* const cb = findBlock(torrent, loc); cb == nullptr)
    {
        err = tr_ioPrefetch(torrent, loc, len);
    }

    return err;
}

/***
****
***/

int tr_cache::findBlockPos(tr_torrent* torrent, tr_block_info::Location loc) const
{
    cache_block key;
    key.tor = torrent;
    key.loc = loc;
    return tr_ptrArrayLowerBound(&this->blocks, &key, cache_block_compare, nullptr);
}

int tr_cache::flushDone()
{
    int err = 0;

    if (tr_ptrArraySize(&this->blocks) > 0)
    {
        auto* const runs = tr_new(struct run_info, tr_ptrArraySize(&this->blocks));
        int i = 0;
        int const n = calcRuns(runs);

        while (i < n && (runs[i].is_piece_done || runs[i].is_multi_piece))
        {
            runs[i++].rank |= SESSIONFLAG;
        }

        err = flushRuns(runs, i);
        tr_free(runs);
    }

    return err;
}

int tr_cache::flushFile(tr_torrent* torrent, tr_file_index_t i)
{
    auto const [begin, end] = tr_torGetFileBlockSpan(torrent, i);

    int const pos = findBlockPos(torrent, torrent->blockLoc(begin));

    tr_logAddTrace(fmt::format("flushing file {} from cache to disk: blocks [{}...{})", i, begin, end));

    /* flush out all the blocks in that file */
    int err = 0;
    while (err == 0 && pos < tr_ptrArraySize(&this->blocks))
    {
        auto const* b = static_cast<cache_block const*>(tr_ptrArrayNth(&this->blocks, pos));

        if (b->tor != torrent)
        {
            break;
        }

        if (b->loc.block < begin || b->loc.block >= end)
        {
            break;
        }

        err = flushContiguous(pos, getBlockRun(pos, nullptr));
    }

    return err;
}

int tr_cache::flushTorrent(tr_torrent* torrent)
{
    int err = 0;
    int const pos = findBlockPos(torrent, {});

    /* flush out all the blocks in that torrent */
    while (err == 0 && pos < tr_ptrArraySize(&this->blocks))
    {
        auto const* b = static_cast<struct cache_block const*>(tr_ptrArrayNth(&this->blocks, pos));

        if (b->tor != torrent)
        {
            break;
        }

        err = flushContiguous(pos, getBlockRun(pos, nullptr));
    }

    return err;
}
