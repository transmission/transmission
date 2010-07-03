/*
 * This file Copyright (C) 2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include "transmission.h"
#include "cache.h"
#include "inout.h"
#include "peer-common.h" /* MAX_BLOCK_SIZE */
#include "ptrarray.h"
#include "torrent.h"
#include "utils.h"

#define MY_NAME "Cache"

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, MY_NAME, __VA_ARGS__ ); \
    } while( 0 )


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

    uint8_t * buf;
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
  time_t  last_block_time;
  tr_bool is_aligned;
};


/* return a count of how many contiguous blocks there are starting at this pos */
static int
getBlockRun( const tr_cache * cache, int pos, struct run_info * info )
{
    int i;
    const int n = tr_ptrArraySize( &cache->blocks );
    const struct cache_block ** blocks = (const struct cache_block**) tr_ptrArrayBase( &cache->blocks );
    const struct cache_block * ref = blocks[pos];
    tr_block_index_t block = ref->block;

    for( i=pos; i<n; ++i, ++block ) {
        const struct cache_block * b = blocks[i];
        if( b->block != block ) break;
        if( b->tor != ref->tor ) break;
//fprintf( stderr, "pos %d tor %d block %zu time %zu\n", i, b->tor->uniqueId, (size_t)b->block, (size_t)b->time );
    }

//fprintf( stderr, "run is %d long from [%d to %d)\n", (int)(i-pos), i, (int)pos );
    if( info != NULL ) {
        info->last_block_time = blocks[i-1]->time;
        info->is_aligned = (blocks[pos]->block % 2 == 0) ? TRUE : FALSE;
    }

    return i-pos;
}

/* return the starting index of the longest contiguous run of blocks BUT:
 *   - Run should begin with a even block and length of run should be even.
 *   - Oldest run is preferred.
 */
static int
findChunk2Discard( tr_cache * cache, int * setme_n )
{
    const int n = tr_ptrArraySize( &cache->blocks );
    int pos;
    int bestpos = 0;
    unsigned bestlen = 1;
    unsigned jump;
    time_t oldest_time = tr_time() + 1;
    struct run_info run;

    for( pos=0; pos<n; pos+=jump )
    {
        unsigned len = getBlockRun( cache, pos, &run );
        jump = len;

        /* shortcut */
        if( len < bestlen )
            continue;

        /* check alignment */
        if( len % 2 == 0 ) {
            if( !run.is_aligned ) {
                /* Let it grow. Otherwise we contribute to fragmentation */
                if( bestlen == 1) {
                    /* But if all other blocks are non-contiguous, we prefer this one */
                    bestpos = pos;
                    oldest_time = cache->max_blocks - len;
                }
                continue;
            }
        } else {
            if( len != 1 ) {
                --len;
                if( !run.is_aligned ) {
                    ++pos;
                    --jump;
                }
            }
        }

        if( bestlen < len ) {
            bestlen = len;
            bestpos = pos;
            oldest_time = run.last_block_time;
        } else if( (bestlen == len) && (oldest_time > run.last_block_time) ) {
            bestpos = pos;
            oldest_time = run.last_block_time;
        }
    }

//fprintf( stderr, "DISCARDED run is %d long from [%d to %d)\n", bestlen, bestpos, bestpos+bestlen );
    *setme_n = bestlen;
    return bestpos;
}

static int
flushContiguous( tr_cache * cache, int pos, int n )
{
    int i;
    int err = 0;
    uint8_t * buf = tr_new( uint8_t, n * MAX_BLOCK_SIZE );
    uint8_t * walk = buf;
    struct cache_block ** blocks = (struct cache_block**) tr_ptrArrayBase( &cache->blocks );

    struct cache_block * b = blocks[pos];
    tr_torrent * tor             = b->tor;
    const tr_piece_index_t piece = b->piece;
    const uint32_t offset        = b->offset;

//fprintf( stderr, "flushing %d contiguous blocks [%d-%d) from cache to disk\n", n, pos, n+pos );

    for( i=pos; i<pos+n; ++i ) {
        b = blocks[i];
        memcpy( walk, b->buf, b->length );
        walk += b->length;
        tr_free( b->buf );
        tr_free( b );
    }
    tr_ptrArrayErase( &cache->blocks, pos, pos+n );

    tr_tordbg( tor, "Writing to disk piece %d, offset %d, len %d", (int)piece, (int)offset, (int)(walk-buf) );
    tr_ndbg( MY_NAME, "Removing %d blocks from cache; %d left", n, tr_ptrArraySize(&cache->blocks) );
    //fprintf( stderr, "%s - Writing to disk piece %d, offset %d, len %d\n", tr_torrentName(tor), (int)piece, (int)offset, (int)(walk-buf) );
    //fprintf( stderr, "%s - Removing %d blocks from cache; %d left\n", MY_NAME, n, tr_ptrArraySize(&cache->blocks) );

    err = tr_ioWrite( tor, piece, offset, walk-buf, buf );
    tr_free( buf );

    ++cache->disk_writes;
    cache->disk_write_bytes += walk-buf;
    return err;
}

static int
cacheTrim( tr_cache * cache )
{
    int err = 0;

    while( !err && ( tr_ptrArraySize( &cache->blocks ) > cache->max_blocks ) )
    {
        int n;
        const int i = findChunk2Discard( cache, &n );
        err = flushContiguous( cache, i, n );
    }

    return err;
}

/***
****
***/

static int
getMaxBlocks( int64_t max_bytes )
{
    return max_bytes / (double)MAX_BLOCK_SIZE;
}

int
tr_cacheSetLimit( tr_cache * cache, int64_t max_bytes )
{
    char buf[128];

    cache->max_bytes = max_bytes;
    cache->max_blocks = getMaxBlocks( max_bytes );

    tr_formatter_mem( buf, cache->max_bytes, sizeof( buf ) );
    tr_ndbg( MY_NAME, "Maximum cache size set to %s (%d blocks)", buf, cache->max_blocks );

    return cacheTrim( cache );
}

int64_t
tr_cacheGetLimit( const tr_cache * cache )
{
    return cache->max_bytes;
}

tr_cache *
tr_cacheNew( int64_t max_bytes )
{
    tr_cache * cache = tr_new0( tr_cache, 1 );
    cache->blocks = TR_PTR_ARRAY_INIT;
    cache->max_bytes = max_bytes;
    cache->max_blocks = getMaxBlocks( max_bytes );
    return cache;
}

void
tr_cacheFree( tr_cache * cache )
{
    assert( tr_ptrArrayEmpty( &cache->blocks ) );
    tr_ptrArrayDestruct( &cache->blocks, NULL );
    tr_free( cache );
}

/***
****
***/

static int
cache_block_compare( const void * va, const void * vb )
{
    const struct cache_block * a = va;
    const struct cache_block * b = vb;

    /* primary key: torrent id */
    if( a->tor->uniqueId != b->tor->uniqueId )
        return a->tor->uniqueId < b->tor->uniqueId ? -1 : 1;

    /* secondary key: block # */
    if( a->block != b->block )
        return a->block < b->block ? -1 : 1;

    /* they're equal */
    return 0;
}

static struct cache_block *
findBlock( tr_cache           * cache,
           tr_torrent         * torrent, 
           tr_piece_index_t     piece,
           uint32_t             offset )
{
    struct cache_block key;
    key.tor = torrent;
    key.block = _tr_block( torrent, piece, offset );
    return tr_ptrArrayFindSorted( &cache->blocks, &key, cache_block_compare );
}

int
tr_cacheWriteBlock( tr_cache         * cache,
                    tr_torrent       * torrent,
                    tr_piece_index_t   piece,
                    uint32_t           offset,
                    uint32_t           length,
                    const uint8_t    * writeme )
{
    struct cache_block * cb = findBlock( cache, torrent, piece, offset );

    if( cb == NULL )
    {
        cb = tr_new( struct cache_block, 1 );
        cb->tor = torrent;
        cb->piece = piece;
        cb->offset = offset;
        cb->length = length;
        cb->block = _tr_block( torrent, piece, offset );
        cb->buf = NULL;
        tr_ptrArrayInsertSorted( &cache->blocks, cb, cache_block_compare );
    }

    cb->time = tr_time();

    tr_free( cb->buf );
    cb->buf = tr_memdup( writeme, cb->length );

    ++cache->cache_writes;
    cache->cache_write_bytes += cb->length;

    return cacheTrim( cache );
}

int
tr_cacheReadBlock( tr_cache         * cache,
                   tr_torrent       * torrent,
                   tr_piece_index_t   piece,
                   uint32_t           offset,
                   uint32_t           len,
                   uint8_t          * setme )
{
    int err = 0;
    struct cache_block * cb = findBlock( cache, torrent, piece, offset );

    if( cb )
        memcpy( setme, cb->buf, len );
    else
        err = tr_ioRead( torrent, piece, offset, len, setme );

    return err;
}

int
tr_cachePrefetchBlock( tr_cache         * cache,
                       tr_torrent       * torrent,
                       tr_piece_index_t   piece,
                       uint32_t           offset,
                       uint32_t           len )
{
    int err = 0;
    struct cache_block * cb = findBlock( cache, torrent, piece, offset );

    if( cb == NULL )
        err = tr_ioPrefetch( torrent, piece, offset, len );

    return err;
}

/***
****
***/

static int
findPiece( tr_cache * cache, tr_torrent * torrent, tr_piece_index_t piece )
{
    struct cache_block key;
    key.tor = torrent;
    key.block = tr_torPieceFirstBlock( torrent, piece );
    return tr_ptrArrayLowerBound( &cache->blocks, &key, cache_block_compare, NULL );
}

int
tr_cacheFlushFile( tr_cache * cache, tr_torrent * torrent, tr_file_index_t i )
{
    int err = 0;
    const tr_file * file = &torrent->info.files[i];
    const tr_block_index_t begin = tr_torPieceFirstBlock( torrent, file->firstPiece );
    const tr_block_index_t end  = tr_torPieceFirstBlock( torrent, file->lastPiece ) + tr_torPieceCountBlocks( torrent, file->lastPiece );
    const int pos = findPiece( cache, torrent, file->firstPiece );
    dbgmsg( "flushing file %d from cache to disk: blocks [%zu...%zu)", (int)i, (size_t)begin, (size_t)end );

    /* flush out all the blocks in that file */
    while( !err && ( pos < tr_ptrArraySize( &cache->blocks ) ) )
    {
        const struct cache_block * b = tr_ptrArrayNth( &cache->blocks, pos );
        if( b->tor != torrent ) break;
        if( ( b->block < begin ) || ( b->block >= end ) ) break;
        err = flushContiguous( cache, pos, getBlockRun( cache, pos, NULL ) );
    }

    return err;
}

int
tr_cacheFlushTorrent( tr_cache * cache, tr_torrent * torrent )
{
    int err = 0;
    const int pos = findPiece( cache, torrent, 0 );

    /* flush out all the blocks in that torrent */
    while( !err && ( pos < tr_ptrArraySize( &cache->blocks ) ) )
    {
        const struct cache_block * b = tr_ptrArrayNth( &cache->blocks, pos );
        if( b->tor != torrent ) break;
        err = flushContiguous( cache, pos, getBlockRun( cache, pos, NULL ) );
    }

    return err;
}
