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

#include "transmission.h"
#include "bitfield.h"
#include "utils.h" /* tr_getRatio() */

typedef struct tr_completion
{
    tr_torrent * tor;

    tr_bitfield blockBitfield;

    /* number of bytes we'll have when done downloading. [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       use tr_cpSizeWhenDone() instead! */
    uint64_t sizeWhenDoneLazy;

    /* whether or not sizeWhenDone needs to be recalculated */
    bool sizeWhenDoneIsDirty;

    /* number of bytes we'll have when done downloading. [0..info.totalSize]
       DON'T access this directly; it's a lazy field.
       use tr_cpHaveValid() instead! */
    uint64_t haveValidLazy;

    /* whether or not haveValidLazy needs to be recalculated */
    bool haveValidIsDirty;

    /* number of bytes we want or have now. [0..sizeWhenDone] */
    uint64_t sizeNow;
}
tr_completion;

/**
*** Life Cycle
**/

void  tr_cpConstruct( tr_completion *, tr_torrent * );

void  tr_cpBlockInit( tr_completion * cp, const tr_bitfield * blocks );

static inline void
tr_cpDestruct( tr_completion * cp )
{
    tr_bitfieldDestruct( &cp->blockBitfield );
}

/**
*** General
**/

double            tr_cpPercentComplete( const tr_completion * cp );

double            tr_cpPercentDone( const tr_completion * cp );

tr_completeness   tr_cpGetStatus( const tr_completion * );

uint64_t          tr_cpHaveValid( const tr_completion * );

uint64_t          tr_cpSizeWhenDone( const tr_completion * );

void              tr_cpGetAmountDone( const   tr_completion * completion,
                                      float                 * tab,
                                      int                     tabCount );


static inline uint64_t
tr_cpHaveTotal( const tr_completion * cp )
{
    return cp->sizeNow;
}

static inline uint64_t
tr_cpLeftUntilComplete( const tr_completion * cp )
{
    return tr_torrentInfo(cp->tor)->totalSize - cp->sizeNow;
}

static inline uint64_t
tr_cpLeftUntilDone( const tr_completion * cp )
{
    return tr_cpSizeWhenDone( cp ) - cp->sizeNow;
}

static inline bool tr_cpHasAll( const tr_completion * cp )
{
    return tr_bitfieldHasAll( &cp->blockBitfield );
}

static inline bool tr_cpHasNone( const tr_completion * cp )
{
    return tr_bitfieldHasNone( &cp->blockBitfield );
}

/**
***  Pieces
**/

void    tr_cpPieceAdd( tr_completion * cp, tr_piece_index_t i );

void    tr_cpPieceRem( tr_completion * cp, tr_piece_index_t i );

size_t  tr_cpMissingBlocksInPiece( const tr_completion *, tr_piece_index_t );

size_t  tr_cpMissingBytesInPiece ( const tr_completion *, tr_piece_index_t );

static inline bool
tr_cpPieceIsComplete( const tr_completion * cp, tr_piece_index_t i )
{
    return tr_cpMissingBlocksInPiece( cp, i ) == 0;
}

/**
***  Blocks
**/

void  tr_cpBlockAdd( tr_completion * cp, tr_block_index_t i );

static inline bool
tr_cpBlockIsComplete( const tr_completion * cp, tr_block_index_t i )
{
    return tr_bitfieldHas( &cp->blockBitfield, i );
}

/***
****  Misc
***/

bool  tr_cpFileIsComplete( const tr_completion * cp, tr_file_index_t );

void* tr_cpCreatePieceBitfield( const tr_completion * cp, size_t * byte_count );

static inline void
tr_cpInvalidateDND( tr_completion * cp )
{
    cp->sizeWhenDoneIsDirty = true;
}


#endif
