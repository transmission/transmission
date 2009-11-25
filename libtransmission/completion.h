/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_COMPLETION_H
#define TR_COMPLETION_H

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#include <assert.h>

#include "transmission.h"
#include "bitfield.h"
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

static TR_INLINE tr_completion* tr_cpNew( tr_torrent * tor )
{
    return tr_cpConstruct( tr_new0( tr_completion, 1 ), tor );
}

static TR_INLINE void tr_cpFree( tr_completion * cp )
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

static TR_INLINE uint64_t tr_cpHaveTotal( const tr_completion * cp )
{
    return cp->sizeNow;
}

static TR_INLINE uint64_t tr_cpLeftUntilComplete( const tr_completion * cp )
{
    return tr_torrentInfo(cp->tor)->totalSize - cp->sizeNow;
}

static TR_INLINE uint64_t tr_cpLeftUntilDone( const tr_completion * cp )
{
    return tr_cpSizeWhenDone( cp ) - cp->sizeNow;
}

static TR_INLINE float tr_cpPercentComplete( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, tr_torrentInfo(cp->tor)->totalSize );
    if( (int)ratio == TR_RATIO_NA )
        return 0.0;
    else if( (int)ratio == TR_RATIO_INF )
        return 1.0;
    else
        return ratio;
}

static TR_INLINE float tr_cpPercentDone( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, tr_cpSizeWhenDone( cp ) );
    return (ratio == TR_RATIO_NA ||  ratio == TR_RATIO_INF) ? 0.0f : ratio;
}

/**
*** Pieces
**/

int tr_cpMissingBlocksInPiece( const tr_completion  * cp,
                               tr_piece_index_t       piece );

tr_bool  tr_cpPieceIsComplete( const tr_completion * cp,
                               tr_piece_index_t      piece );

void   tr_cpPieceAdd( tr_completion    * completion,
                      tr_piece_index_t   piece );

void   tr_cpPieceRem( tr_completion     * completion,
                      tr_piece_index_t   piece );

tr_bool tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t );

/**
*** Blocks
**/

static TR_INLINE tr_bool tr_cpBlockIsCompleteFast( const tr_completion * cp, tr_block_index_t block )
{
    return tr_bitfieldHasFast( &cp->blockBitfield, block );
}

static TR_INLINE tr_bool tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t block )
{
    return tr_bitfieldHas( &cp->blockBitfield, block );
}

void      tr_cpBlockAdd( tr_completion * completion,
                         tr_block_index_t block );

tr_bool   tr_cpBlockBitfieldSet( tr_completion      * completion,
                                 struct tr_bitfield * blocks );

/***
****
***/

static TR_INLINE const struct tr_bitfield * tr_cpPieceBitfield( const tr_completion * cp ) {
    return &cp->pieceBitfield;
}

static TR_INLINE const struct tr_bitfield * tr_cpBlockBitfield( const tr_completion * cp ) {
    assert( cp );
    assert( cp->blockBitfield.bits );
    assert( cp->blockBitfield.bitCount );
    return &cp->blockBitfield;
}

#endif
