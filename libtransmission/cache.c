/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#include <stdlib.h> /* qsort () */

#include <event2/buffer.h>

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "log.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "ptrarray.h"
#include "torrent.h"
#include "trevent.h"
#include "utils.h"

#define MY_NAME "Cache"

#define dbgmsg(...) \
  do \
    { \
      if (tr_logGetDeepEnabled ()) \
        tr_logAddDeep (__FILE__, __LINE__, MY_NAME, __VA_ARGS__); \
    } \
  while (0)

/****
*****
****/

struct cache_block
{
  tr_torrent * tor;

  tr_piece_index_t piece;
  uint32_t offset;
  uint32_t length;

  time_t time;
  tr_block_index_t block;

  struct evbuffer * evbuf;
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
static int
getBlockRun (const tr_cache * cache, int pos, struct run_info * info)
{
  int i;
  const int n = tr_ptrArraySize (&cache->blocks);
  const struct cache_block * const * blocks = (const struct cache_block* const *) tr_ptrArrayBase (&cache->blocks);
  const struct cache_block * ref = blocks[pos];
  tr_block_index_t block = ref->block;

  for (i=pos; i<n; ++i, ++block)
    {
      const struct cache_block * b = blocks[i];
      if (b->block != block)
        break;
      if (b->tor != ref->tor)
        break;
      //fprintf (stderr, "pos %d tor %d block %zu time %zu\n", i, b->tor->uniqueId, (size_t)b->block, (size_t)b->time);
    }

  //fprintf (stderr, "run is %d long from [%d to %d)\n", (int)(i-pos), i, (int)pos);

  if (info != NULL)
    {
      const struct cache_block * b = blocks[i-1];
      info->last_block_time = b->time;
      info->is_piece_done = tr_torrentPieceIsComplete (b->tor, b->piece);
      info->is_multi_piece = b->piece != blocks[pos]->piece;
      info->len = i - pos;
      info->pos = pos;
    }

  return i-pos;
}

/* higher rank comes before lower rank */
static int
compareRuns (const void * va, const void * vb)
{
  const struct run_info * a = va;
  const struct run_info * b = vb;
  return b->rank - a->rank;
}

enum
{
  MULTIFLAG   = 0x1000,
  DONEFLAG    = 0x2000,
  SESSIONFLAG = 0x4000
};

/* Calculte runs
 *   - Stale runs, runs sitting in cache for a long time or runs not growing, get priority.
 *     Returns number of runs.
 */
static int
calcRuns (tr_cache * cache, struct run_info * runs)
{
  const int n = tr_ptrArraySize (&cache->blocks);
  int i = 0, pos;
  const time_t now = tr_time ();

  for (pos = 0; pos < n; pos += runs[i++].len)
    {
      int rank = getBlockRun (cache, pos, &runs[i]);

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

      //fprintf (stderr,"block run at pos %d of length %d and age %ld adjusted +%d\n",runs[i].pos,runs[i].len,now-runs[i].last_block_time,rank-runs[i].len);
    }

  //fprintf (stderr, "%d block runs\n", i);
  qsort (runs, i, sizeof (struct run_info), compareRuns);
  return i;
}

static int
flushContiguous (tr_cache * cache, int pos, int n)
{
  int i;
  int err = 0;
  uint8_t * buf = tr_new (uint8_t, n * MAX_BLOCK_SIZE);
  uint8_t * walk = buf;
  struct cache_block ** blocks = (struct cache_block**) tr_ptrArrayBase (&cache->blocks);

  struct cache_block * b = blocks[pos];
  tr_torrent * tor = b->tor;
  const tr_piece_index_t piece = b->piece;
  const uint32_t offset = b->offset;

  for (i=pos; i<pos+n; ++i)
    {
      b = blocks[i];
      evbuffer_copyout (b->evbuf, walk, b->length);
      walk += b->length;
      evbuffer_free (b->evbuf);
      tr_free (b);
    }
  tr_ptrArrayErase (&cache->blocks, pos, pos+n);

  err = tr_ioWrite (tor, piece, offset, walk-buf, buf);
  tr_free (buf);

  ++cache->disk_writes;
  cache->disk_write_bytes += walk-buf;
  return err;
}

static int
flushRuns (tr_cache * cache, struct run_info * runs, int n)
{
  int i;
  int err = 0;

  for (i=0; !err && i<n; i++)
    {
      int j;

      err = flushContiguous (cache, runs[i].pos, runs[i].len);

      for (j=i+1; j<n; j++)
        if (runs[j].pos > runs[i].pos)
          runs[j].pos -= runs[i].len;
    }

  return err;
}

static int
cacheTrim (tr_cache * cache)
{
  int err = 0;

  if (tr_ptrArraySize (&cache->blocks) > cache->max_blocks)
    {
      /* Amount of cache that should be removed by the flush. This influences how large
       * runs can grow as well as how often flushes will happen. */
      const int cacheCutoff = 1 + cache->max_blocks / 4;
      struct run_info * runs = tr_new (struct run_info, tr_ptrArraySize (&cache->blocks));
      int i=0, j=0;

      calcRuns (cache, runs);
      while (j < cacheCutoff)
        j += runs[i++].len;
      err = flushRuns (cache, runs, i);
      tr_free (runs);
    }

  return err;
}

/***
****
***/

static int
getMaxBlocks (int64_t max_bytes)
{
  return max_bytes / (double)MAX_BLOCK_SIZE;
}

int
tr_cacheSetLimit (tr_cache * cache, int64_t max_bytes)
{
  char buf[128];

  cache->max_bytes = max_bytes;
  cache->max_blocks = getMaxBlocks (max_bytes);

  tr_formatter_mem_B (buf, cache->max_bytes, sizeof (buf));
  tr_logAddNamedDbg (MY_NAME, "Maximum cache size set to %s (%d blocks)", buf, cache->max_blocks);

  return cacheTrim (cache);
}

int64_t
tr_cacheGetLimit (const tr_cache * cache)
{
  return cache->max_bytes;
}

tr_cache *
tr_cacheNew (int64_t max_bytes)
{
  tr_cache * cache = tr_new0 (tr_cache, 1);
  cache->blocks = TR_PTR_ARRAY_INIT;
  cache->max_bytes = max_bytes;
  cache->max_blocks = getMaxBlocks (max_bytes);
  return cache;
}

void
tr_cacheFree (tr_cache * cache)
{
  assert (tr_ptrArrayEmpty (&cache->blocks));
  tr_ptrArrayDestruct (&cache->blocks, NULL);
  tr_free (cache);
}

/***
****
***/

static int
cache_block_compare (const void * va, const void * vb)
{
  const struct cache_block * a = va;
  const struct cache_block * b = vb;

  /* primary key: torrent id */
  if (a->tor->uniqueId != b->tor->uniqueId)
    return a->tor->uniqueId < b->tor->uniqueId ? -1 : 1;

  /* secondary key: block # */
  if (a->block != b->block)
    return a->block < b->block ? -1 : 1;

  /* they're equal */
  return 0;
}

static struct cache_block *
findBlock (tr_cache           * cache,
           tr_torrent         * torrent,
           tr_piece_index_t     piece,
           uint32_t             offset)
{
  struct cache_block key;
  key.tor = torrent;
  key.block = _tr_block (torrent, piece, offset);
  return tr_ptrArrayFindSorted (&cache->blocks, &key, cache_block_compare);
}

int
tr_cacheWriteBlock (tr_cache         * cache,
                    tr_torrent       * torrent,
                    tr_piece_index_t   piece,
                    uint32_t           offset,
                    uint32_t           length,
                    struct evbuffer  * writeme)
{
  struct cache_block * cb = findBlock (cache, torrent, piece, offset);

  assert (tr_amInEventThread (torrent->session));

  if (cb == NULL)
    {
      cb = tr_new (struct cache_block, 1);
      cb->tor = torrent;
      cb->piece = piece;
      cb->offset = offset;
      cb->length = length;
      cb->block = _tr_block (torrent, piece, offset);
      cb->evbuf = evbuffer_new ();
      tr_ptrArrayInsertSorted (&cache->blocks, cb, cache_block_compare);
    }

  cb->time = tr_time ();

  assert (cb->length == length);
  evbuffer_drain (cb->evbuf, evbuffer_get_length (cb->evbuf));
  evbuffer_remove_buffer (writeme, cb->evbuf, cb->length);

  cache->cache_writes++;
  cache->cache_write_bytes += cb->length;

  return cacheTrim (cache);
}

int
tr_cacheReadBlock (tr_cache         * cache,
                   tr_torrent       * torrent,
                   tr_piece_index_t   piece,
                   uint32_t           offset,
                   uint32_t           len,
                   uint8_t          * setme)
{
  int err = 0;
  struct cache_block * cb = findBlock (cache, torrent, piece, offset);

  if (cb)
    evbuffer_copyout (cb->evbuf, setme, len);
  else
    err = tr_ioRead (torrent, piece, offset, len, setme);

  return err;
}

int
tr_cachePrefetchBlock (tr_cache         * cache,
                       tr_torrent       * torrent,
                       tr_piece_index_t   piece,
                       uint32_t           offset,
                       uint32_t           len)
{
  int err = 0;
  struct cache_block * cb = findBlock (cache, torrent, piece, offset);

  if (cb == NULL)
    err = tr_ioPrefetch (torrent, piece, offset, len);

  return err;
}

/***
****
***/

static int
findBlockPos (tr_cache * cache, tr_torrent * torrent, tr_piece_index_t block)
{
  struct cache_block key;
  key.tor = torrent;
  key.block = block;
  return tr_ptrArrayLowerBound (&cache->blocks, &key, cache_block_compare, NULL);
}

int tr_cacheFlushDone (tr_cache * cache)
{
  int err = 0;

  if (tr_ptrArraySize (&cache->blocks) > 0)
    {
      int i, n;
      struct run_info * runs;

      runs = tr_new (struct run_info, tr_ptrArraySize (&cache->blocks));
      i = 0;
      n = calcRuns (cache, runs);

      while (i < n && (runs[i].is_piece_done || runs[i].is_multi_piece))
        runs[i++].rank |= SESSIONFLAG;

      err = flushRuns (cache, runs, i);
      tr_free (runs);
    }

  return err;
}

int
tr_cacheFlushFile (tr_cache * cache, tr_torrent * torrent, tr_file_index_t i)
{
  int pos;
  int err = 0;
  tr_block_index_t first;
  tr_block_index_t last;

  tr_torGetFileBlockRange (torrent, i, &first, &last);
  pos = findBlockPos (cache, torrent, first);
  dbgmsg ("flushing file %d from cache to disk: blocks [%zu...%zu]", (int)i, (size_t)first, (size_t)last);

  /* flush out all the blocks in that file */
  while (!err && (pos < tr_ptrArraySize (&cache->blocks)))
    {
      const struct cache_block * b = tr_ptrArrayNth (&cache->blocks, pos);

      if (b->tor != torrent)
        break;
      if ((b->block < first) || (b->block > last))
        break;

      err = flushContiguous (cache, pos, getBlockRun (cache, pos, NULL));
    }

  return err;
}

int
tr_cacheFlushTorrent (tr_cache * cache, tr_torrent * torrent)
{
  int err = 0;
  const int pos = findBlockPos (cache, torrent, 0);

  /* flush out all the blocks in that torrent */
  while (!err && (pos < tr_ptrArraySize (&cache->blocks)))
    {
      const struct cache_block * b = tr_ptrArrayNth (&cache->blocks, pos);

      if (b->tor != torrent)
        break;

      err = flushContiguous (cache, pos, getBlockRun (cache, pos, NULL));
    }

  return err;
}
