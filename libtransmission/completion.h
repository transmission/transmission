/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <vector>

#include "transmission.h"
#include "bitfield.h"

struct tr_completion
{
    tr_torrent* tor;

    // Changed to non-owning pointer temporarily till tr_completion becomes C++-constructible and destructible
    // TODO: remove * and own the value
    tr_bitfield* blockBitfield;

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
};

/**
*** Life Cycle
**/

void tr_cpConstruct(tr_completion*, tr_torrent*);

void tr_cpBlockInit(tr_completion* cp, tr_bitfield const& blocks);

static inline void tr_cpDestruct(tr_completion* cp)
{
    delete cp->blockBitfield;
}

/**
*** General
**/

double tr_cpPercentComplete(tr_completion const* cp);

double tr_cpPercentDone(tr_completion const* cp);

tr_completeness tr_cpGetStatus(tr_completion const*);

uint64_t tr_cpHaveValid(tr_completion const*);

uint64_t tr_cpSizeWhenDone(tr_completion const*);

uint64_t tr_cpLeftUntilDone(tr_completion const*);

void tr_cpGetAmountDone(tr_completion const* completion, float* tab, int tabCount);

constexpr uint64_t tr_cpHaveTotal(tr_completion const* cp)
{
    return cp->sizeNow;
}

static inline bool tr_cpHasAll(tr_completion const* cp)
{
    return tr_torrentHasMetadata(cp->tor) && cp->blockBitfield->hasAll();
}

static inline bool tr_cpHasNone(tr_completion const* cp)
{
    return !tr_torrentHasMetadata(cp->tor) || cp->blockBitfield->hasNone();
}

/**
***  Pieces
**/

void tr_cpPieceAdd(tr_completion* cp, tr_piece_index_t i);

void tr_cpPieceRem(tr_completion* cp, tr_piece_index_t i);

size_t tr_cpMissingBlocksInPiece(tr_completion const*, tr_piece_index_t);

size_t tr_cpMissingBytesInPiece(tr_completion const*, tr_piece_index_t);

static inline bool tr_cpPieceIsComplete(tr_completion const* cp, tr_piece_index_t i)
{
    return tr_cpMissingBlocksInPiece(cp, i) == 0;
}

/**
***  Blocks
**/

void tr_cpBlockAdd(tr_completion* cp, tr_block_index_t i);

static inline bool tr_cpBlockIsComplete(tr_completion const* cp, tr_block_index_t i)
{
    return cp->blockBitfield->test(i);
}

/***
****  Misc
***/

bool tr_cpFileIsComplete(tr_completion const* cp, tr_file_index_t);

std::vector<uint8_t> tr_cpCreatePieceBitfield(tr_completion const* cp);

constexpr void tr_cpInvalidateDND(tr_completion* cp)
{
    cp->sizeWhenDoneIsDirty = true;
}
