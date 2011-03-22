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

#ifndef TR_BITFIELD_H
#define TR_BITFIELD_H 1

#include "transmission.h"

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
typedef struct tr_bitfield
{
    uint8_t *  bits;
    size_t     bitCount;
    size_t     byteCount;
}
tr_bitfield;

extern const tr_bitfield TR_BITFIELD_INIT;

tr_bitfield* tr_bitfieldConstruct( tr_bitfield*, size_t bitCount );

tr_bitfield* tr_bitfieldDestruct( tr_bitfield* );

tr_bitfield* tr_bitfieldNew( size_t bitCount );

void tr_bitfieldFree( tr_bitfield * b );

void         tr_bitfieldClear( tr_bitfield* );

int          tr_bitfieldAdd( tr_bitfield*, size_t bit );

int          tr_bitfieldRem( tr_bitfield*, size_t bit );

int          tr_bitfieldAddRange( tr_bitfield *, size_t begin, size_t end );

int          tr_bitfieldRemRange( tr_bitfield*, size_t begin, size_t end );

size_t       tr_bitfieldCountTrueBits( const tr_bitfield* );

size_t       tr_bitfieldCountRange( const tr_bitfield * b, size_t begin, size_t end );


tr_bitfield* tr_bitfieldOr( tr_bitfield*, const tr_bitfield* );

/** A stripped-down version of bitfieldHas to be used
    for speed when you're looping quickly. This version
    has none of tr_bitfieldHas()'s safety checks, so you
    need to call tr_bitfieldTestFast() first before you
    start looping. */
static inline bool tr_bitfieldHasFast( const tr_bitfield * b, const size_t nth )
{
    return ( b->bits[nth>>3u] << ( nth & 7u ) & 0x80 ) != 0;
}

/** @param high the highest nth bit you're going to access */
static inline bool tr_bitfieldTestFast( const tr_bitfield * b, const size_t high )
{
    return ( b != NULL )
        && ( b->bits != NULL )
        && ( high < b->bitCount );
}

static inline bool tr_bitfieldHas( const tr_bitfield * b, size_t nth )
{
    return tr_bitfieldTestFast( b, nth ) && tr_bitfieldHasFast( b, nth );
}

#endif
