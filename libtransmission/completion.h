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

#ifndef TR_COMPLETION_H
#define TR_COMPLETION_H

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <assert.h>

#include "transmission.h"
#include "utils.h" /* tr_bitfield */

typedef struct tr_completion
{
    tr_bool  sizeWhenDoneIsDirty;
    tr_bool  haveValidIsDirty;

    tr_torrent *    tor;

    /* do we have this block? */
    tr_bitfield    blockBitfield;

    /* do we have this piece? */
    tr_bitfield    pieceBitfield;

    /* a block is complete if and only if we have it */
    uint16_t *  completeBlocks;

    /* number of bytes we'll have when done downloading. [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       use tr_cpSizeWhenDone() instead! */
    uint64_t    sizeWhenDoneLazy;

    /* number of bytes we'll have when done downloading. [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       use tr_cpHaveValid() instead! */
    uint64_t    haveValidLazy;

    /* number of bytes we want or have now. [0..sizeWhenDone] */
    uint64_t    sizeNow;
}
tr_completion;

/**
*** Life Cycle
**/

tr_completion * tr_cpConstruct( tr_completion *, tr_torrent * );

tr_completion * tr_cpDestruct( tr_completion * );

static inline tr_completion* tr_cpNew( tr_torrent * tor )
{
    return tr_cpConstruct( tr_new0( tr_completion, 1 ), tor );
}

static inline void tr_cpFree( tr_completion * cp )
{
    tr_free( tr_cpDestruct( cp ) );
}

/**
*** General
**/

tr_completeness            tr_cpGetStatus( const tr_completion * );

uint64_t                   tr_cpHaveValid( const tr_completion * );

uint64_t                   tr_cpSizeWhenDone( const tr_completion * );

void                       tr_cpInvalidateDND( tr_completion * );

void                       tr_cpGetAmountDone( const   tr_completion * completion,
                                               float                 * tab,
                                               int                     tabCount );

static inline uint64_t tr_cpHaveTotal( const tr_completion * cp )
{
    return cp->sizeNow;
}

static inline uint64_t tr_cpLeftUntilComplete( const tr_completion * cp )
{
    return tr_torrentInfo(cp->tor)->totalSize - cp->sizeNow;
}

static inline uint64_t tr_cpLeftUntilDone( const tr_completion * cp )
{
    return tr_cpSizeWhenDone( cp ) - cp->sizeNow;
}

static inline float tr_cpPercentComplete( const tr_completion * cp )
{
    return tr_getRatio( cp->sizeNow, tr_torrentInfo(cp->tor)->totalSize );
}

static inline float tr_cpPercentDone( const tr_completion * cp )
{
    return tr_getRatio( cp->sizeNow, tr_cpSizeWhenDone( cp ) );
}

/**
*** Pieces
**/

int tr_cpMissingBlocksInPiece( const tr_completion  * cp,
                               tr_piece_index_t       piece );

int    tr_cpPieceIsComplete( const tr_completion * cp,
                             tr_piece_index_t      piece );

void   tr_cpPieceAdd( tr_completion    * completion,
                      tr_piece_index_t   piece );

void   tr_cpPieceRem( tr_completion     * completion,
                      tr_piece_index_t   piece );

/**
*** Blocks
**/

static inline int tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t block ) {
    return tr_bitfieldHas( &cp->blockBitfield, block );
}

void                       tr_cpBlockAdd( tr_completion * completion,
                                          tr_block_index_t block );

int                        tr_cpBlockBitfieldSet( tr_completion      * completion,
                                                  struct tr_bitfield * blocks );

/***
****
***/

static inline const struct tr_bitfield * tr_cpPieceBitfield( const tr_completion * cp ) {
    return &cp->pieceBitfield;
}

static inline const struct tr_bitfield * tr_cpBlockBitfield( const tr_completion * cp ) {
    assert( cp );
    assert( cp->blockBitfield.bits );
    assert( cp->blockBitfield.bitCount );
    return &cp->blockBitfield;
}

#endif
