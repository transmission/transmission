/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_COMPLETION_H
#define TR_COMPLETION_H

#include <assert.h>

#include "transmission.h"
#include "bitset.h"
#include "utils.h" /* tr_getRatio() */

typedef struct tr_completion
{
    bool  sizeWhenDoneIsDirty;
    bool  blocksWantedIsDirty;
    bool  haveValidIsDirty;

    tr_torrent *    tor;

    /* do we have this block? */
    tr_bitset    blockBitset;

    /* a block is complete if and only if we have it */
    uint16_t *  completeBlocks;

    /* total number of blocks that we want downloaded
       DON'T access this directly; it's a lazy field.
       Used by tr_cpBlocksMissing(). */
    tr_block_index_t    blocksWantedLazy;

    /* total number of completed blocks that we want downloaded
       DON'T access this directly; it's a lazy field.
       Used by tr_cpBlocksMissing(). */
    tr_block_index_t    blocksWantedCompleteLazy;

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

/**
*** General
**/

tr_completeness    tr_cpGetStatus( const tr_completion * );

uint64_t           tr_cpHaveValid( const tr_completion * );

tr_block_index_t   tr_cpBlocksMissing( const tr_completion * );

uint64_t           tr_cpSizeWhenDone( const tr_completion * );

void               tr_cpInvalidateDND( tr_completion * );

void               tr_cpGetAmountDone( const   tr_completion * completion,
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

static inline double tr_cpPercentComplete( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, tr_torrentInfo(cp->tor)->totalSize );
    if( (int)ratio == TR_RATIO_NA )
        return 0.0;
    else if( (int)ratio == TR_RATIO_INF )
        return 1.0;
    else
        return ratio;
}

static inline double tr_cpPercentDone( const tr_completion * cp )
{
    const double ratio = tr_getRatio( cp->sizeNow, tr_cpSizeWhenDone( cp ) );
    const int iratio = (int)ratio;
    return ((iratio == TR_RATIO_NA) || (iratio == TR_RATIO_INF)) ? 0.0 : ratio;
}

/**
*** Pieces
**/

int tr_cpMissingBlocksInPiece( const tr_completion * cp, tr_piece_index_t i );

static inline bool
tr_cpPieceIsComplete( const tr_completion * cp, tr_piece_index_t i )
{
    return tr_cpMissingBlocksInPiece( cp, i ) == 0;
}

void tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t i );

void tr_cpPieceRem( tr_completion * cp, tr_piece_index_t i );

bool tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t );

/**
*** Blocks
**/

static inline bool
tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t i )
{
    return tr_bitsetHas( &cp->blockBitset, i );
}

void tr_cpBlockAdd( tr_completion * cp, tr_block_index_t i );

bool tr_cpBlockBitsetInit( tr_completion * cp, const tr_bitset * blocks );

/***
****
***/

static inline const tr_bitset *
tr_cpBlockBitset( const tr_completion * cp )
{
    return &cp->blockBitset;
}

tr_bitfield * tr_cpCreatePieceBitfield( const tr_completion * cp );

#endif
