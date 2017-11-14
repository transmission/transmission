/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
typedef struct tr_bitfield
{
    uint8_t* bits;
    size_t alloc_count;

    size_t bit_count;

    size_t true_count;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool have_all_hint;
    bool have_none_hint;
}
tr_bitfield;

/***
****
***/

void tr_bitfieldSetHasAll(tr_bitfield*);

void tr_bitfieldSetHasNone(tr_bitfield*);

void tr_bitfieldAdd(tr_bitfield*, size_t bit);

void tr_bitfieldRem(tr_bitfield*, size_t bit);

void tr_bitfieldAddRange(tr_bitfield*, size_t begin, size_t end);

void tr_bitfieldRemRange(tr_bitfield*, size_t begin, size_t end);

/***
****  life cycle
***/

extern tr_bitfield const TR_BITFIELD_INIT;

void tr_bitfieldConstruct(tr_bitfield*, size_t bit_count);

static inline void tr_bitfieldDestruct(tr_bitfield* b)
{
    tr_bitfieldSetHasNone(b);
}

/***
****
***/

void tr_bitfieldSetFromFlags(tr_bitfield*, bool const* bytes, size_t n);

void tr_bitfieldSetFromBitfield(tr_bitfield*, tr_bitfield const*);

void tr_bitfieldSetRaw(tr_bitfield*, void const* bits, size_t byte_count, bool bounded);

void* tr_bitfieldGetRaw(tr_bitfield const* b, size_t* byte_count);

/***
****
***/

size_t tr_bitfieldCountRange(tr_bitfield const*, size_t begin, size_t end);

size_t tr_bitfieldCountTrueBits(tr_bitfield const* b);

static inline bool tr_bitfieldHasAll(tr_bitfield const* b)
{
    return b->bit_count != 0 ? (b->true_count == b->bit_count) : b->have_all_hint;
}

static inline bool tr_bitfieldHasNone(tr_bitfield const* b)
{
    return b->bit_count != 0 ? (b->true_count == 0) : b->have_none_hint;
}

bool tr_bitfieldHas(tr_bitfield const* b, size_t n);
