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
    tr_torrent_t * tor;

    /* number of peers from whom we've requested this block */
    uint8_t * blockDownloaders;

    /* do we have this block? */
    tr_bitfield_t * blockBitfield;

    /* do we have this piece? */
    tr_bitfield_t * pieceBitfield;

    /* a block is complete if and only if we have it */
    int * completeBlocks;
};

tr_completion_t * tr_cpInit( tr_torrent_t * tor )
{
    tr_completion_t * cp;

    cp                   = tr_new( tr_completion_t, 1 );
    cp->tor              = tor;
    cp->blockBitfield    = tr_bitfieldNew( tor->blockCount );
    cp->blockDownloaders = tr_new( uint8_t, tor->blockCount );
    cp->pieceBitfield    = tr_bitfieldNew( tor->info.pieceCount );
    cp->completeBlocks   = tr_new( int, tor->info.pieceCount );

    tr_cpReset( cp );

    return cp;
}

void tr_cpClose( tr_completion_t * cp )
{
    tr_free(         cp->completeBlocks );
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
    for( i = 0; i < tor->info.pieceCount; ++i )
        cp->completeBlocks[i] = 0;
}

int tr_cpPieceHasAllBlocks( const tr_completion_t * cp, int piece )
{
    return tr_cpPieceIsComplete( cp, piece );
}

int tr_cpPieceIsComplete( const tr_completion_t * cp, int piece )
{
    return cp->completeBlocks[piece] >= TR_BLOCKS_IN_PIECE(cp->tor,piece);
}

const tr_bitfield_t * tr_cpPieceBitfield( const tr_completion_t * cp )
{
    return cp->pieceBitfield;
}

void tr_cpPieceAdd( tr_completion_t * cp, int piece )
{
    const tr_torrent_t * tor = cp->tor;
    const int n_blocks = TR_BLOCKS_IN_PIECE(tor,piece);
    const int startBlock = TOR_PIECE_FIRST_BLOCK(tor,piece);
    const int endBlock   = startBlock + n_blocks;

    cp->completeBlocks[piece] = n_blocks;
    tr_bitfieldAddRange( cp->blockBitfield, startBlock, endBlock );
    tr_bitfieldAdd( cp->pieceBitfield, piece );
}

void tr_cpPieceRem( tr_completion_t * cp, int piece )
{
    const tr_torrent_t * tor = cp->tor;
    const int n_blocks = TR_BLOCKS_IN_PIECE(tor,piece);
    const int startBlock = TOR_PIECE_FIRST_BLOCK(tor,piece);
    const int endBlock   = startBlock + n_blocks;

    assert( cp != NULL );
    assert( 0 <= piece );
    assert( piece < tor->info.pieceCount );
    assert( 0 <= startBlock );
    assert( startBlock < tor->blockCount );
    assert( startBlock <= endBlock );
    assert( endBlock <= tor->blockCount );

    cp->completeBlocks[piece] = 0;
    tr_bitfieldRemRange ( cp->blockBitfield, startBlock, endBlock );
    tr_bitfieldRem( cp->pieceBitfield, piece );
}

/* Blocks */
void tr_cpDownloaderAdd( tr_completion_t * cp, int block )
{
    ++cp->blockDownloaders[block];
}

void tr_cpDownloaderRem( tr_completion_t * cp, int block )
{
    --cp->blockDownloaders[block];
}

int tr_cpBlockIsComplete( const tr_completion_t * cp, int block )
{
    return tr_bitfieldHas( cp->blockBitfield, block );
}

void tr_cpBlockAdd( tr_completion_t * cp, int block )
{
    const tr_torrent_t * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const int piece = TOR_BLOCK_PIECE(tor, block);

        ++cp->completeBlocks[piece];

        if( cp->completeBlocks[piece] == TR_BLOCKS_IN_PIECE(tor,piece) )
            tr_bitfieldAdd( cp->pieceBitfield, piece );

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

    return cp->completeBlocks[piece] / (double)TR_BLOCKS_IN_PIECE(cp->tor,piece);
}

int
tr_cpMissingBlocksForPiece( const tr_completion_t * cp, int piece )
{
    int i;
    int n;
    const tr_torrent_t * tor = cp->tor;
    const int start = TOR_PIECE_FIRST_BLOCK(tor,piece);
    const int end   = start + TR_BLOCKS_IN_PIECE(tor,piece);

    n = 0;
    for( i = start; i < end; ++i )
        if( !tr_cpBlockIsComplete( cp, i ) && !cp->blockDownloaders[i] )
            ++n;

    return n;
}

int tr_cpMissingBlockInPiece( const tr_completion_t * cp, int piece )
{
    int i;
    const tr_torrent_t * tor = cp->tor;
    const int start = TOR_PIECE_FIRST_BLOCK(tor,piece);
    const int end   = start + TR_BLOCKS_IN_PIECE(tor,piece);

    for( i = start; i < end; ++i )
        if( !tr_cpBlockIsComplete( cp, i ) && !cp->blockDownloaders[i] )
            return i;

    return -1;
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
        if( !info->pieces[i].dnd )
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
            b += ( TR_BLOCKS_IN_PIECE(tor,i) - cp->completeBlocks[ i ] );

    b *= tor->blockSize;

    if( tor->blockCount && !tr_cpBlockIsComplete( cp, tor->blockCount - 1 ) )
          b -= (tor->blockSize - (tor->info.totalSize % tor->blockSize));

    return b;
}

void
tr_cpDoneStats( const tr_completion_t  * cp ,
                uint64_t               * setmeHaveBytes,
                uint64_t               * setmeTotalBytes )
{
    const tr_torrent_t * tor = cp->tor;
    const tr_info_t * info = &tor->info;
    uint64_t have=0, total=0;
    int i;

    for( i=0; i<info->pieceCount; ++i ) {
        if( !info->pieces[i].dnd ) {
            total += info->pieceSize;
            have += cp->completeBlocks[ i ];
        }
    }

    have *= tor->blockSize;

    /* the last piece/block is probably smaller than the others */
    if( !info->pieces[info->pieceCount-1].dnd ) {
        total -= ( info->pieceSize - ( info->totalSize % info->pieceSize ) );
        if( tr_cpBlockIsComplete( cp, tor->blockCount-1 ) )
            have -= ( tor->blockSize - ( info->totalSize % tor->blockSize ) );
    }

    assert( have < total );
    assert( total <= info->totalSize );

    *setmeHaveBytes = have;
    *setmeTotalBytes = total;
}

float
tr_cpPercentComplete ( const tr_completion_t * cp )
{
    const uint64_t tilComplete = tr_cpLeftUntilComplete( cp );
    const uint64_t total = cp->tor->info.totalSize;
    const float f = 1.0 - (double)tilComplete / total;
    return MAX(0.0, f);
}

uint64_t
tr_cpDownloadedValid( const tr_completion_t * cp )
{
    uint64_t b = 0;
    const tr_torrent_t * tor = cp->tor;
    const tr_info_t * info = &tor->info;
    int i;

    for( i=0; i<info->pieceCount; ++i )
        if( tr_cpPieceIsComplete( cp, i ) )
            ++b;

    b *= tor->blockSize;

    if( tor->blockCount && tr_bitfieldHas( cp->blockBitfield, tor->blockCount - 1 ) )
        b -= (tor->blockSize - (tor->info.totalSize % tor->blockSize));

   return b;
}
