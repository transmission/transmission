// This file Copyright 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdlib> /* qsort() */
#include <ctime>

#include <event2/buffer.h>

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "log.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "ptrarray.h"
#include "torrent.h"
#include "tr-assert.h"
#include "trevent.h"
#include "utils.h"

static char constexpr MyName[] = "Cache";

#define dbgmsg(...) tr_logAddDeepNamed(MyName, __VA_ARGS__)

/****
*****
****/

struct cache_block
{
    tr_torrent* tor;

    tr_block_info::Location loc;
    uint32_t length;

    time_t time;

    struct evbuffer* evbuf;
};

struct tr_cache
{
    tr_ptrArray blocks;
    int max_blocks;
    size_t max_bytes;

    size_t disk_writes;
    size_t disk_write_bytes;
    size_t cache_writes;
    size_t cache_write_bytes;
};

/****
*****
****/

struct run_info
{
    int pos;
    int rank;
    time_t last_block_time;
    bool is_multi_piece;
    bool is_piece_done;
    unsigned int len;
};

/* return a count of how many contiguous blocks there are starting at this pos */
static int getBlockRun(tr_cache const* cache, int pos, struct run_info* info)
{
    int const n = tr_ptrArraySize(&cache->blocks);
    auto const* const* blocks = (struct cache_block const* const*)tr_ptrArrayBase(&cache->blocks);
    struct cache_block const* ref = blocks[pos];
    auto block = ref->loc.block;
    int len = 0;

    for (int i = pos; i < n; ++i)
    {
        struct cache_block const* b = blocks[i];

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
        struct cache_block const* b = blocks[pos + len - 1];
        info->last_block_time = b->time;
        info->is_piece_done = b->tor->hasPiece(b->loc.piece);
        info->is_multi_piece = b->loc.piece != blocks[pos]->loc.piece;
        info->len = len;
        info->pos = pos;
    }

    return len;
}

/* higher rank comes before lower rank */
static int compareRuns(void const* va, void const* vb)
{
    auto const* const a = static_cast<struct run_info const*>(va);
    auto const* const b = static_cast<struct run_info const*>(vb);
    return b->rank - a->rank;
}

enum
{
    MULTIFLAG = 0x1000,
    DONEFLAG = 0x2000,
    SESSIONFLAG = 0x4000
};

/* Calculte runs
 *   - Stale runs, runs sitting in cache for a long time or runs not growing, get priority.
 *     Returns number of runs.
 */
// TODO: return std::vector
static int calcRuns(tr_cache const* cache, struct run_info* runs)
{
    int const n = tr_ptrArraySize(&cache->blocks);
    int i = 0;
    time_t const now = tr_time();

    for (int pos = 0; pos < n; pos += runs[i++].len)
    {
        int rank = getBlockRun(cache, pos, &runs[i]);

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

    qsort(runs, i, sizeof(struct run_info), compareRuns);
    return i;
}

static int flushContiguous(tr_cache* cache, int pos, int n)
{
    int err = 0;
    auto* const buf = tr_new(uint8_t, n * MAX_BLOCK_SIZE);
    auto* walk = buf;
    auto** blocks = (struct cache_block**)tr_ptrArrayBase(&cache->blocks);

    auto* b = blocks[pos];
    auto* const tor = b->tor;
    auto const piece = b->loc.piece;
    auto const offset = b->loc.piece_offset;

    for (int i = 0; i < n; ++i)
    {
        b = blocks[pos + i];
        evbuffer_copyout(b->evbuf, walk, b->length);
        walk += b->length;
        evbuffer_free(b->evbuf);
        tr_free(b);
    }

    tr_ptrArrayErase(&cache->blocks, pos, pos + n);

    err = tr_ioWrite(tor, piece, offset, walk - buf, buf);
    tr_free(buf);

    ++cache->disk_writes;
    cache->disk_write_bytes += walk - buf;
    return err;
}

static int flushRuns(tr_cache* cache, struct run_info* runs, int n)
{
    int err = 0;

    for (int i = 0; err == 0 && i < n; i++)
    {
        err = flushContiguous(cache, runs[i].pos, runs[i].len);

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

static int cacheTrim(tr_cache* cache)
{
    int err = 0;

    if (tr_ptrArraySize(&cache->blocks) > cache->max_blocks)
    {
        /* Amount of cache that should be removed by the flush. This influences how large
         * runs can grow as well as how often flushes will happen. */
        int const cacheCutoff = 1 + cache->max_blocks / 4;
        auto* const runs = tr_new(struct run_info, tr_ptrArraySize(&cache->blocks));
        int i = 0;
        int j = 0;

        calcRuns(cache, runs);

        while (j < cacheCutoff)
        {
            j += runs[i++].len;
        }

        err = flushRuns(cache, runs, i);
        tr_free(runs);
    }

    return err;
}

/***
****
***/

static int getMaxBlocks(int64_t max_bytes)
{
    return max_bytes / (double)MAX_BLOCK_SIZE;
}

int tr_cacheSetLimit(tr_cache* cache, int64_t max_bytes)
{
    cache->max_bytes = max_bytes;
    cache->max_blocks = getMaxBlocks(max_bytes);

    tr_logAddNamedDbg(
        MyName,
        "Maximum cache size set to %s (%d blocks)",
        tr_formatter_mem_B(cache->max_bytes).c_str(),
        cache->max_blocks);

    return cacheTrim(cache);
}

int64_t tr_cacheGetLimit(tr_cache const* cache)
{
    return cache->max_bytes;
}

tr_cache* tr_cacheNew(int64_t max_bytes)
{
    auto* const cache = tr_new0(tr_cache, 1);
    cache->blocks = {};
    cache->max_bytes = max_bytes;
    cache->max_blocks = getMaxBlocks(max_bytes);
    return cache;
}

void tr_cacheFree(tr_cache* cache)
{
    tr_ptrArrayDestruct(&cache->blocks, nullptr);
    tr_free(cache);
}

/***
****
***/

static int cache_block_compare(void const* va, void const* vb)
{
    auto const* a = static_cast<struct cache_block const*>(va);
    auto const* b = static_cast<struct cache_block const*>(vb);

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

static struct cache_block* findBlock(tr_cache* cache, tr_torrent* torrent, tr_block_info::Location loc)
{
    auto key = cache_block{};
    key.tor = torrent;
    key.loc = loc;
    return static_cast<struct cache_block*>(tr_ptrArrayFindSorted(&cache->blocks, &key, cache_block_compare));
}

int tr_cacheWriteBlock(
    tr_cache* cache,
    tr_torrent* torrent,
    tr_block_info::Location loc,
    uint32_t length,
    struct evbuffer* writeme)
{
    TR_ASSERT(tr_amInEventThread(torrent->session));

    struct cache_block* cb = findBlock(cache, torrent, loc);

    if (cb == nullptr)
    {
        cb = tr_new(struct cache_block, 1);
        cb->tor = torrent;
        cb->loc = loc;
        cb->length = length;
        cb->evbuf = evbuffer_new();
        tr_ptrArrayInsertSorted(&cache->blocks, cb, cache_block_compare);
    }

    TR_ASSERT(cb->length == length);

    cb->time = tr_time();

    evbuffer_drain(cb->evbuf, evbuffer_get_length(cb->evbuf));
    evbuffer_remove_buffer(writeme, cb->evbuf, cb->length);

    cache->cache_writes++;
    cache->cache_write_bytes += cb->length;

    return cacheTrim(cache);
}

int tr_cacheReadBlock(tr_cache* cache, tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme)
{
    int err = 0;

    if (auto* const cb = findBlock(cache, torrent, loc); cb != nullptr)
    {
        evbuffer_copyout(cb->evbuf, setme, len);
    }
    else
    {
        err = tr_ioRead(torrent, loc.piece, loc.piece_offset, len, setme);
    }

    return err;
}

int tr_cachePrefetchBlock(tr_cache* cache, tr_torrent* torrent, tr_block_info::Location loc, uint32_t len)
{
    int err = 0;

    if (auto const* const cb = findBlock(cache, torrent, loc); cb == nullptr)
    {
        err = tr_ioPrefetch(torrent, loc.piece, loc.piece_offset, len);
    }

    return err;
}

/***
****
***/

static int findBlockPos(tr_cache const* cache, tr_torrent* torrent, tr_block_info::Location loc)
{
    struct cache_block key;
    key.tor = torrent;
    key.loc = loc;
    return tr_ptrArrayLowerBound(&cache->blocks, &key, cache_block_compare, nullptr);
}

int tr_cacheFlushDone(tr_cache* cache)
{
    int err = 0;

    if (tr_ptrArraySize(&cache->blocks) > 0)
    {
        auto* const runs = tr_new(struct run_info, tr_ptrArraySize(&cache->blocks));
        int i = 0;
        int const n = calcRuns(cache, runs);

        while (i < n && (runs[i].is_piece_done || runs[i].is_multi_piece))
        {
            runs[i++].rank |= SESSIONFLAG;
        }

        err = flushRuns(cache, runs, i);
        tr_free(runs);
    }

    return err;
}

int tr_cacheFlushFile(tr_cache* cache, tr_torrent* torrent, tr_file_index_t i)
{
    auto const [begin, end] = tr_torGetFileBlockSpan(torrent, i);

    int pos = findBlockPos(cache, torrent, torrent->blockLoc(begin));
    dbgmsg("flushing file %d from cache to disk: blocks [%zu...%zu)", (int)i, (size_t)begin, (size_t)end);

    /* flush out all the blocks in that file */
    int err = 0;
    while (err == 0 && pos < tr_ptrArraySize(&cache->blocks))
    {
        auto const* b = static_cast<struct cache_block const*>(tr_ptrArrayNth(&cache->blocks, pos));

        if (b->tor != torrent)
        {
            break;
        }

        if (b->loc.block < begin || b->loc.block >= end)
        {
            break;
        }

        err = flushContiguous(cache, pos, getBlockRun(cache, pos, nullptr));
    }

    return err;
}

int tr_cacheFlushTorrent(tr_cache* cache, tr_torrent* torrent)
{
    int err = 0;
    int const pos = findBlockPos(cache, torrent, {});

    /* flush out all the blocks in that torrent */
    while (err == 0 && pos < tr_ptrArraySize(&cache->blocks))
    {
        auto const* b = static_cast<struct cache_block const*>(tr_ptrArrayNth(&cache->blocks, pos));

        if (b->tor != torrent)
        {
            break;
        }

        err = flushContiguous(cache, pos, getBlockRun(cache, pos, nullptr));
    }

    return err;
}
