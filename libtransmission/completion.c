/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include "transmission.h"
#include "completion.h"

struct tr_completion_s
{
    tr_torrent_t      * tor;
    tr_bitfield_t     * blockBitfield;
    uint8_t           * blockDownloaders;
    tr_bitfield_t     * pieceBitfield;

    /* a block is missing if we don't have it AND there's not a request pending */
    int * missingBlocks;

    /* a block is complete if and only if we have it */
    int * completeBlocks;

    /* rather than calculating these over and over again in loops,
       just calculate them once */
    int nBlocksInPiece;
    int nBlocksInLastPiece;
};

#define tr_cpCountBlocks(cp,piece) \
    (piece==cp->tor->info.pieceCount-1 ? cp->nBlocksInLastPiece : cp->nBlocksInPiece)

tr_completion_t * tr_cpInit( tr_torrent_t * tor )
{
    tr_completion_t * cp;

    cp                   = tr_new( tr_completion_t, 1 );
    cp->tor              = tor;
    cp->blockBitfield    = tr_bitfieldNew( tor->blockCount );
    cp->blockDownloaders = tr_new( uint8_t, tor->blockCount );
    cp->pieceBitfield    = tr_bitfieldNew( tor->info.pieceCount );
    cp->missingBlocks    = tr_new( int, tor->info.pieceCount );
    cp->completeBlocks   = tr_new( int, tor->info.pieceCount );

    cp->nBlocksInLastPiece = tr_pieceCountBlocks ( tor->info.pieceCount - 1 );
    cp->nBlocksInPiece = tor->info.pieceCount==1 ? cp->nBlocksInLastPiece
                                                : tr_pieceCountBlocks( 0 );

    tr_cpReset( cp );

    return cp;
}

void tr_cpClose( tr_completion_t * cp )
{
    tr_free(         cp->completeBlocks );
    tr_free(         cp->missingBlocks );
    tr_bitfieldFree( cp->pieceBitfield );
    tr_free(         cp->blockDownloaders );
    tr_bitfieldFree( cp->blockBitfield );
    tr_free(         cp );
}

void tr_cpReset( tr_completion_t * cp )
{
    tr_torrent_t * tor = cp->tor;
    int i;

    tr_bitfieldClear( cp->blockBitfield );
    memset( cp->blockDownloaders, 0, tor->blockCount );
    tr_bitfieldClear( cp->pieceBitfield );
    for( i = 0; i < tor->info.pieceCount; ++i ) {
        cp->missingBlocks[i] = tr_cpCountBlocks( cp, i );
        cp->completeBlocks[i] = 0;
    }
}

int tr_cpPieceHasAllBlocks( const tr_completion_t * cp, int piece )
{
    return tr_cpPieceIsComplete( cp, piece );
}

int tr_cpPieceIsComplete( const tr_completion_t * cp, int piece )
{
    const int total = tr_cpCountBlocks( cp, piece );
    const int have = cp->completeBlocks[piece];
    assert( have <= total );
    return have == total;
}

const tr_bitfield_t * tr_cpPieceBitfield( const tr_completion_t * cp )
{
    return cp->pieceBitfield;
}

void tr_cpPieceAdd( tr_completion_t * cp, int piece )
{
    int i;
    const tr_torrent_t * tor = cp->tor;
    const int n_blocks = tr_cpCountBlocks( cp, piece );
    const int startBlock = tr_pieceStartBlock( piece );
    const int endBlock   = startBlock + n_blocks;

    cp->completeBlocks[piece] = n_blocks;

    for( i=startBlock; i<endBlock; ++i )
        if( !cp->blockDownloaders[i] )
            --cp->missingBlocks[piece];

    tr_bitfieldAddRange( cp->blockBitfield, startBlock, endBlock-1 );

    tr_bitfieldAdd( cp->pieceBitfield, piece );
}

void tr_cpPieceRem( tr_completion_t * cp, int piece )
{
    int i;
    const tr_torrent_t * tor = cp->tor;
    const int n_blocks = tr_cpCountBlocks( cp, piece );
    const int startBlock = tr_pieceStartBlock( piece );
    const int endBlock   = startBlock + n_blocks;

    cp->completeBlocks[piece] = 0;

    for( i=startBlock; i<endBlock; ++i )
        if( !cp->blockDownloaders[i] )
            ++cp->missingBlocks[piece];

    tr_bitfieldRemRange ( cp->blockBitfield, startBlock, endBlock-1 );

    tr_bitfieldRem( cp->pieceBitfield, piece );
}

/* Blocks */
void tr_cpDownloaderAdd( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor = cp->tor;
    if( !cp->blockDownloaders[block] && !tr_cpBlockIsComplete( cp, block ) )
    {
        cp->missingBlocks[tr_blockPiece(block)]--;
    }
    (cp->blockDownloaders[block])++;
}

void tr_cpDownloaderRem( tr_completion_t * cp, int block )
{
    tr_torrent_t * tor;

    assert( cp != NULL );
    assert( cp->tor != NULL );
    assert( 0 <= block );

    tor = cp->tor;
    (cp->blockDownloaders[block])--;
    if( !cp->blockDownloaders[block] && !tr_cpBlockIsComplete( cp, block ) )
    {
        cp->missingBlocks[tr_blockPiece(block)]++;
    }
}

int tr_cpBlockIsComplete( const tr_completion_t * cp, int block )
{
    assert( cp != NULL );
    assert( 0 <= block );

    return tr_bitfieldHas( cp->blockBitfield, block );
}

void tr_cpBlockAdd( tr_completion_t * cp, int block )
{
    const tr_torrent_t * tor;

    assert( cp != NULL );
    assert( cp->tor != NULL );
    assert( 0 <= block );

    tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const int piece = tr_blockPiece( block );
        ++cp->completeBlocks[piece];

        if( cp->completeBlocks[piece] == tr_cpCountBlocks( cp, piece ) )
            tr_bitfieldAdd( cp->pieceBitfield, piece );

        if( !cp->blockDownloaders[block] )
            cp->missingBlocks[piece]--;

        tr_bitfieldAdd( cp->blockBitfield, block );
    }
}

const tr_bitfield_t * tr_cpBlockBitfield( const tr_completion_t * cp )
{
    assert( cp != NULL );

    return cp->blockBitfield;
}

void
tr_cpBlockBitfieldSet( tr_completion_t * cp, tr_bitfield_t * bitfield )
{
    int i;

    assert( cp != NULL );
    assert( bitfield != NULL );

    tr_cpReset( cp );

    for( i=0; i < cp->tor->blockCount; ++i )
        if( tr_bitfieldHas( bitfield, i ) )
            tr_cpBlockAdd( cp, i );
}

float tr_cpPercentBlocksInPiece( const tr_completion_t * cp, int piece )
{
    assert( cp != NULL );

    return cp->completeBlocks[piece] / (double)tr_cpCountBlocks( cp, piece );
}

int tr_cpMissingBlockInPiece( const tr_completion_t * cp, int piece )
{
    int i;
    const tr_torrent_t * tor = cp->tor;
    const int start = tr_pieceStartBlock( piece );
    const int end   = start + tr_cpCountBlocks( cp, piece );

    for( i = start; i < end; ++i )
        if( !tr_cpBlockIsComplete( cp, i ) && !cp->blockDownloaders[i] )
            return i;

    return -1;
}

int tr_cpMostMissingBlockInPiece( const tr_completion_t * cp,
                                  int                     piece,
                                  int                   * downloaders )
{
    tr_torrent_t * tor = cp->tor;
    int start, count, end, i;
    int * pool, poolSize, min, ret;

    start = tr_pieceStartBlock( piece );
    count = tr_cpCountBlocks( cp, piece );
    end   = start + count;

    pool     = tr_new( int, count );
    poolSize = 0;
    min      = 255;

    for( i = start; i < end; i++ )
    {
        if( tr_cpBlockIsComplete( cp, i ) || cp->blockDownloaders[i] > min )
        {
            continue;
        }
        if( cp->blockDownloaders[i] < min )
        {
            min      = cp->blockDownloaders[i];
            poolSize = 0;
        }
        if( cp->blockDownloaders[i] <= min )
        {
            pool[poolSize++] = i;
        }
    }

    if( poolSize > 0 )
    {
        ret = pool[0];
        *downloaders = min;
    }
    else
    {
        ret = -1;
    }

    tr_free( pool );
    return ret;
}


int
tr_cpMissingBlocksForPiece( const tr_completion_t * cp, int piece )
{
    assert( cp != NULL );

    return cp->missingBlocks[piece];
}

/***
****
***/

cp_status_t
tr_cpGetStatus ( const tr_completion_t * cp )
{
    int i;
    int ret = TR_CP_COMPLETE;
    const tr_info_t * info;

    assert( cp != NULL );
    assert( cp->tor != NULL );

    info = &cp->tor->info;
    for( i=0; i<info->pieceCount; ++i ) {
        if( tr_cpPieceIsComplete( cp, i ) )
            continue;
        if( info->pieces[i].priority != TR_PRI_DND)
            return TR_CP_INCOMPLETE;
        ret = TR_CP_DONE;
    }

    return ret;
}

uint64_t
tr_cpLeftUntilComplete ( const tr_completion_t * cp )
{
    int i;
    uint64_t b=0;
    const tr_torrent_t * tor;
    const tr_info_t * info;

    assert( cp != NULL );
    assert( cp->tor != NULL );

    tor = cp->tor;
    info = &tor->info;
    for( i=0; i<info->pieceCount; ++i )
        if( !tr_cpPieceIsComplete( cp, i ) )
            b += ( tr_cpCountBlocks( cp, i ) - cp->completeBlocks[ i ] );

    return b * tor->blockSize;;
}

uint64_t
tr_cpLeftUntilDone ( const tr_completion_t * cp )
{
    int i;
    uint64_t b=0;
    const tr_torrent_t * tor;
    const tr_info_t * info;

    assert( cp != NULL );
    assert( cp->tor != NULL );

    tor = cp->tor;
    info = &tor->info;

    for( i=0; i<info->pieceCount; ++i )
        if( !tr_cpPieceIsComplete( cp, i ) && info->pieces[i].priority != TR_PRI_DND )
            b += tor->blockSize * (tr_cpCountBlocks( cp, i ) - cp->completeBlocks[ i ] );

    return b;
}

float
tr_cpPercentComplete ( const tr_completion_t * cp )
{
    const uint64_t tilComplete = tr_cpLeftUntilComplete( cp );
    const uint64_t total = cp->tor->info.totalSize;
    const float f = 1.0 - (double)tilComplete / total;
    return MAX(0.0, f);
}

float
tr_cpPercentDone( const tr_completion_t * cp )
{
    const uint64_t tilDone = tr_cpLeftUntilDone( cp );
    const uint64_t total = cp->tor->info.totalSize;
    const float f = 1.0 - (double)tilDone / total;
    return MAX(0.0, f);
}

uint64_t
tr_cpDownloadedValid( const tr_completion_t * cp )
{
    int i, n;
    uint64_t b=0;

    for( i=0, n=cp->tor->info.pieceCount; i<n; ++i )
        b += cp->completeBlocks[ i ];

   return b * cp->tor->blockSize;
}
