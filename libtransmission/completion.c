/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
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

#include <assert.h>
#include <string.h>

#include "transmission.h"
#include "completion.h"
#include "torrent.h"
#include "utils.h"

static void
tr_cpReset( tr_completion * cp )
{
    tr_bitfieldClear( &cp->pieceBitfield );
    tr_bitfieldClear( &cp->blockBitfield );
    memset( cp->completeBlocks, 0, sizeof( uint16_t ) * cp->tor->info.pieceCount );
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = 1;
    cp->haveValidIsDirty = 1;
}

tr_completion *
tr_cpConstruct( tr_completion * cp, tr_torrent * tor )
{
    cp->tor = tor;
    cp->completeBlocks  = tr_new( uint16_t, tor->info.pieceCount );
    tr_bitfieldConstruct( &cp->blockBitfield, tor->blockCount );
    tr_bitfieldConstruct( &cp->pieceBitfield, tor->info.pieceCount );
    tr_cpReset( cp );
    return cp;
}

tr_completion*
tr_cpDestruct( tr_completion * cp )
{
    tr_free( cp->completeBlocks );
    tr_bitfieldDestruct( &cp->pieceBitfield );
    tr_bitfieldDestruct( &cp->blockBitfield );
    return cp;
}

void
tr_cpInvalidateDND( tr_completion * cp )
{
    cp->sizeWhenDoneIsDirty = 1;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * ccp )
{
    if( ccp->sizeWhenDoneIsDirty )
    {
        tr_completion *    cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = cp->tor;
        const tr_info *    info = &tor->info;
        tr_piece_index_t   i;
        uint64_t           size = 0;

        for( i = 0; i < info->pieceCount; ++i )
        {
            if( !info->pieces[i].dnd )
            {
                /* we want the piece... */
                size += tr_torPieceCountBytes( tor, i );
            }
            else if( tr_cpPieceIsComplete( cp, i ) )
            {
                /* we have the piece... */
                size += tr_torPieceCountBytes( tor, i );
            }
            else if( cp->completeBlocks[i] )
            {
                /* we have part of the piece... */
                const tr_block_index_t b = tr_torPieceFirstBlock( tor, i );
                const tr_block_index_t e = b + tr_torPieceCountBlocks( tor,
                                                                       i );
                tr_block_index_t       j;
                for( j = b; j < e; ++j )
                    if( tr_cpBlockIsComplete( cp, j ) )
                        size += tr_torBlockCountBytes( tor, j );
            }
        }

        cp->sizeWhenDoneLazy = size;
        cp->sizeWhenDoneIsDirty = 0;
    }

    assert( ccp->sizeWhenDoneLazy <= ccp->tor->info.totalSize );
    assert( ccp->sizeWhenDoneLazy >= ccp->sizeNow );
    return ccp->sizeWhenDoneLazy;
}

void
tr_cpPieceAdd( tr_completion *  cp,
               tr_piece_index_t piece )
{
    const tr_torrent *     tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock( tor, piece );
    const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, piece );
    tr_block_index_t       i;

    for( i = start; i < end; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpPieceRem( tr_completion *  cp,
               tr_piece_index_t piece )
{
    const tr_torrent *     tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock( tor, piece );
    const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, piece );
    tr_block_index_t       block;

    assert( cp );
    assert( piece < tor->info.pieceCount );
    assert( start < tor->blockCount );
    assert( start <= end );
    assert( end <= tor->blockCount );

    for( block = start; block < end; ++block )
        if( tr_cpBlockIsComplete( cp, block ) )
            cp->sizeNow -= tr_torBlockCountBytes( tor, block );

    cp->sizeWhenDoneIsDirty = 1;
    cp->haveValidIsDirty = 1;
    cp->completeBlocks[piece] = 0;
    tr_bitfieldRemRange ( &cp->blockBitfield, start, end );
    tr_bitfieldRem( &cp->pieceBitfield, piece );
}

void
tr_cpBlockAdd( tr_completion *  cp,
               tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const tr_piece_index_t piece = tr_torBlockPiece( tor, block );
        const int              blockSize = tr_torBlockCountBytes( tor,
                                                                  block );

        ++cp->completeBlocks[piece];

        if( tr_cpPieceIsComplete( cp, piece ) )
            tr_bitfieldAdd( &cp->pieceBitfield, piece );

        tr_bitfieldAdd( &cp->blockBitfield, block );

        cp->sizeNow += blockSize;

        cp->haveValidIsDirty = 1;
        cp->sizeWhenDoneIsDirty = 1;
    }
}

#if 0
int
tr_cpBlockBitfieldSet( tr_completion * cp,
                       tr_bitfield *   bitfield )
{
    int success = FALSE;

    assert( cp );
    assert( bitfield );

    if( tr_bitfieldTestFast( bitfield, cp->tor->blockCount - 1 ) )
    {
        tr_block_index_t i;
        tr_cpReset( cp );
        for( i = 0; i < cp->tor->blockCount; ++i )
            if( tr_bitfieldHasFast( bitfield, i ) )
                tr_cpBlockAdd( cp, i );
        success = TRUE;
    }

    return success;
}
#endif

int
tr_cpBlockBitfieldSet( tr_completion * cp, tr_bitfield * blockBitfield )
{
    int success = FALSE;

    assert( cp );
    assert( blockBitfield );

    if(( success = tr_bitfieldTestFast( blockBitfield, cp->tor->blockCount - 1 )))
    {
        tr_piece_index_t p;
        const tr_torrent * tor = cp->tor;

        tr_cpReset( cp );

        for( p=0; p<tor->info.pieceCount; ++p )
        {
            tr_block_index_t i;
            uint16_t completeBlocksInPiece = 0;

            const tr_block_index_t start = tr_torPieceFirstBlock( tor, p );
            const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, p );

            for( i=start; i!=end; ++i ) {
                if( tr_bitfieldTestFast( blockBitfield, i ) ) {
                    ++completeBlocksInPiece;
                    cp->sizeNow += tr_torBlockCountBytes( tor, i );
                }
            }

            cp->completeBlocks[p] = completeBlocksInPiece;

            if( completeBlocksInPiece == end - start )
                tr_bitfieldAdd( &cp->pieceBitfield, p );
        }

        memcpy( cp->blockBitfield->bits, blockBitfield->bits, cp->blockBitfield->byteCount );

        cp->haveValidIsDirty = 1;
        cp->sizeWhenDoneIsDirty = 1;
    }

    return success;
}

/***
****
***/

tr_completeness
tr_cpGetStatus( const tr_completion * cp )
{
    if( cp->sizeNow == cp->tor->info.totalSize ) return TR_SEED;
    if( cp->sizeNow == tr_cpSizeWhenDone( cp ) ) return TR_PARTIAL_SEED;
    return TR_LEECH;
}

static uint64_t
calculateHaveValid( const tr_completion * ccp )
{
    uint64_t                  b = 0;
    tr_piece_index_t          i;
    const tr_torrent        * tor            = ccp->tor;
    const uint64_t            pieceSize      = tor->info.pieceSize;
    const uint64_t            lastPieceSize  = tor->lastPieceSize;
    const tr_piece_index_t    lastPiece      = tor->info.pieceCount - 1;

    for( i=0; i!=lastPiece; ++i )
        if( tr_cpPieceIsComplete( ccp, i ) )
            b += pieceSize;

    if( tr_cpPieceIsComplete( ccp, lastPiece ) )
        b += lastPieceSize;

    return b;
}

uint64_t
tr_cpHaveValid( const tr_completion * ccp )
{
    if( ccp->haveValidIsDirty )
    {
        tr_completion * cp = (tr_completion *) ccp; /* mutable */
        cp->haveValidLazy = calculateHaveValid( ccp );
        cp->haveValidIsDirty = 0;
    }

    return ccp->haveValidLazy;
}

void
tr_cpGetAmountDone( const tr_completion * cp,
                    float *               tab,
                    int                   tabCount )
{
    int                i;
    const tr_torrent * tor = cp->tor;
    const float        interval = tor->info.pieceCount / (float)tabCount;
    const int          isSeed = tr_cpGetStatus( cp ) == TR_SEED;

    for( i = 0; i < tabCount; ++i )
    {
        const tr_piece_index_t piece = i * interval;

        if( tor == NULL )
            tab[i] = 0.0f;
        else if( isSeed || tr_cpPieceIsComplete( cp, piece ) )
            tab[i] = 1.0f;
        else
            tab[i] = (float)cp->completeBlocks[piece] /
                     tr_torPieceCountBlocks( tor, piece );
    }
}

int
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    return tr_torPieceCountBlocks( cp->tor, piece ) - cp->completeBlocks[piece];
}


int
tr_cpPieceIsComplete( const tr_completion * cp, tr_piece_index_t piece )
{
    return cp->completeBlocks[piece] == tr_torPieceCountBlocks( cp->tor, piece );
}
