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

struct tr_completion
{
    unsigned int sizeWhenDoneIsDirty : 1;

    tr_torrent * tor;

    /* do we have this block? */
    tr_bitfield * blockBitfield;

    /* do we have this piece? */
    tr_bitfield * pieceBitfield;

    /* a block is complete if and only if we have it */
    uint16_t * completeBlocks;

    /* number of bytes we'll have when done downloading. [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       use tr_cpSizeWhenDone() instead! */
    uint64_t sizeWhenDoneLazy;

    /* number of bytes we want or have now. [0..sizeWhenDone] */
    uint64_t sizeNow;
};

static void
tr_cpReset( tr_completion * cp )
{
    tr_bitfieldClear( cp->pieceBitfield );
    tr_bitfieldClear( cp->blockBitfield );
    memset( cp->completeBlocks, 0, sizeof(uint16_t) * cp->tor->info.pieceCount );
    cp->sizeNow = 0;
    cp->sizeWhenDoneIsDirty = 1;
}

tr_completion *
tr_cpInit( tr_torrent * tor )
{
    tr_completion * cp  = tr_new( tr_completion, 1 );
    cp->tor             = tor;
    cp->blockBitfield   = tr_bitfieldNew( tor->blockCount );
    cp->pieceBitfield   = tr_bitfieldNew( tor->info.pieceCount );
    cp->completeBlocks  = tr_new( uint16_t, tor->info.pieceCount );
    tr_cpReset( cp );
    return cp;
}

void
tr_cpClose( tr_completion * cp )
{
    tr_free        ( cp->completeBlocks );
    tr_bitfieldFree( cp->pieceBitfield );
    tr_bitfieldFree( cp->blockBitfield );
    tr_free        ( cp );
}

void
tr_cpInvalidateDND ( tr_completion * cp )
{
    cp->sizeWhenDoneIsDirty = 1;
}

uint64_t
tr_cpSizeWhenDone( const tr_completion * ccp )
{
    if( ccp->sizeWhenDoneIsDirty )
    {
        tr_completion * cp = (tr_completion *) ccp; /* mutable */
        const tr_torrent * tor = cp->tor;
        const tr_info * info = &tor->info;
        tr_piece_index_t i;
        uint64_t size = 0;

        for( i=0; i<info->pieceCount; ++i )
        {
            if( !info->pieces[i].dnd ) {
                /* we want the piece... */
                size += tr_torPieceCountBytes( tor, i );
            } else if( tr_cpPieceIsComplete( cp, i ) ) {
                /* we have the piece... */
                size += tr_torPieceCountBytes( tor, i );
            } else if( cp->completeBlocks[i] ) {
                /* we have part of the piece... */
                const tr_block_index_t b = tr_torPieceFirstBlock( tor, i );
                const tr_block_index_t e = b + tr_torPieceCountBlocks( tor, i );
                tr_block_index_t j;
                for( j=b; j<e; ++j )
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

int
tr_cpPieceIsComplete( const tr_completion  * cp,
                      tr_piece_index_t       piece )
{
    return cp->completeBlocks[piece] == tr_torPieceCountBlocks(cp->tor,piece);
}

const tr_bitfield *
tr_cpPieceBitfield( const tr_completion * cp )
{
    return cp->pieceBitfield;
}

void
tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t piece )
{
    const tr_torrent * tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock(tor,piece);
    const tr_block_index_t end = start + tr_torPieceCountBlocks(tor, piece);
    tr_block_index_t i;

    for( i=start; i<end; ++i )
        tr_cpBlockAdd( cp, i );
}

void
tr_cpPieceRem( tr_completion * cp, tr_piece_index_t piece )
{
    const tr_torrent * tor = cp->tor;
    const tr_block_index_t start = tr_torPieceFirstBlock( tor, piece );
    const tr_block_index_t end = start + tr_torPieceCountBlocks( tor, piece );
    tr_block_index_t block;

    assert( cp != NULL );
    assert( piece < tor->info.pieceCount );
    assert( start < tor->blockCount );
    assert( start <= end );
    assert( end <= tor->blockCount );

    for( block=start; block<end; ++block )
        if( tr_cpBlockIsComplete( cp, block ) )
            cp->sizeNow -= tr_torBlockCountBytes( tor, block );

    cp->sizeWhenDoneIsDirty = 1;
    cp->completeBlocks[piece] = 0;
    tr_bitfieldRemRange ( cp->blockBitfield, start, end );
    tr_bitfieldRem( cp->pieceBitfield, piece );
}

int
tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t block )
{
    return tr_bitfieldHas( cp->blockBitfield, block );
}

void
tr_cpBlockAdd( tr_completion * cp, tr_block_index_t block )
{
    const tr_torrent * tor = cp->tor;

    if( !tr_cpBlockIsComplete( cp, block ) )
    {
        const tr_piece_index_t piece = tr_torBlockPiece( tor, block );
        const int blockSize = tr_torBlockCountBytes( tor, block );

        ++cp->completeBlocks[piece];

        if( cp->completeBlocks[piece] == tr_torPieceCountBlocks( tor, piece ) )
            tr_bitfieldAdd( cp->pieceBitfield, piece );

        tr_bitfieldAdd( cp->blockBitfield, block );

        cp->sizeNow += blockSize;

        cp->sizeWhenDoneIsDirty = 1;
    }
}

const tr_bitfield *
tr_cpBlockBitfield( const tr_completion * cp )
{
    assert( cp );
    assert( cp->blockBitfield );
    assert( cp->blockBitfield->bits );
    assert( cp->blockBitfield->bitCount );

    return cp->blockBitfield;
}

tr_errno
tr_cpBlockBitfieldSet( tr_completion * cp, tr_bitfield * bitfield )
{
    tr_block_index_t i;

    assert( cp );
    assert( bitfield );
    assert( cp->blockBitfield );

    if( !cp || !tr_bitfieldTestFast( bitfield, cp->tor->blockCount ) )
        return TR_ERROR_ASSERT;

    tr_cpReset( cp );
    for( i=0; i<cp->tor->blockCount; ++i )
        if( tr_bitfieldHasFast( bitfield, i ) )
            tr_cpBlockAdd( cp, i );

    return 0;
}

int
tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t piece )
{
    return tr_torPieceCountBlocks( cp->tor, piece ) - cp->completeBlocks[piece];
}

/***
****
***/

float
tr_cpPercentDone( const tr_completion * cp )
{
    return tr_getRatio( cp->sizeNow, tr_cpSizeWhenDone(cp) );
}

float
tr_cpPercentComplete ( const tr_completion * cp )
{
    return tr_getRatio( cp->sizeNow, cp->tor->info.totalSize );
}

uint64_t
tr_cpLeftUntilDone ( const tr_completion * cp )
{
    return tr_cpSizeWhenDone(cp) - cp->sizeNow;
}

uint64_t
tr_cpLeftUntilComplete ( const tr_completion * cp )
{
    return cp->tor->info.totalSize - cp->sizeNow;
}

cp_status_t
tr_cpGetStatus ( const tr_completion * cp )
{
    if( cp->sizeNow == cp->tor->info.totalSize ) return TR_CP_COMPLETE;
    if( cp->sizeNow == tr_cpSizeWhenDone(cp) ) return TR_CP_DONE;
    return TR_CP_INCOMPLETE;
}

uint64_t
tr_cpHaveValid( const tr_completion * cp )
{
    uint64_t b = 0;
    tr_piece_index_t i;
    const tr_torrent * tor = cp->tor;

    for( i=0; i<tor->info.pieceCount; ++i )
        if( tr_cpPieceIsComplete( cp, i ) )
            b += tr_torPieceCountBytes( tor, i );

    return b;
}

uint64_t
tr_cpHaveTotal( const tr_completion * cp )
{
    return cp->sizeNow;
}

void
tr_cpGetAmountDone( const tr_completion * cp, float * tab, int tabCount )
{
    int i;
    const int isComplete = tr_cpGetStatus ( cp ) == TR_CP_COMPLETE;
    const int tabSpan = cp->tor->blockCount / tabCount;
    tr_block_index_t block_i = 0;
    for( i=0; i<tabCount; ++i ) {
        if( isComplete )
            tab[i] = 1.0f;
        else {
            int loop, have;
            for( loop=have=0; loop<tabSpan; ++loop )
                if( tr_cpBlockIsComplete( cp, block_i++ ) )
                    ++have;
            tab[i] = (float)have / tabSpan;
        }
    }
}
