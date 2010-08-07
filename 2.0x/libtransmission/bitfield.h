/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: utils.h 8685 2009-06-14 01:00:36Z charles $
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_BITFIELD_H
#define TR_BITFIELD_H 1

#include "transmission.h"
#include "utils.h" /* tr_new0 */

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
typedef struct tr_bitfield
{
    uint8_t *  bits;
    size_t     bitCount;
    size_t     byteCount;
}
tr_bitfield;

tr_bitfield* tr_bitfieldConstruct( tr_bitfield*, size_t bitcount );

tr_bitfield* tr_bitfieldDestruct( tr_bitfield* );

static inline tr_bitfield* tr_bitfieldNew( size_t bitcount )
{
    return tr_bitfieldConstruct( tr_new0( tr_bitfield, 1 ), bitcount );
}

static inline void tr_bitfieldFree( tr_bitfield * b )
{
    tr_free( tr_bitfieldDestruct( b ) );
}

tr_bitfield* tr_bitfieldDup( const tr_bitfield* ) TR_GNUC_MALLOC;

void         tr_bitfieldClear( tr_bitfield* );

int          tr_bitfieldAdd( tr_bitfield*, size_t bit );

int          tr_bitfieldRem( tr_bitfield*, size_t bit );

int          tr_bitfieldAddRange( tr_bitfield *, size_t begin, size_t end );

int          tr_bitfieldRemRange( tr_bitfield*, size_t begin, size_t end );

void         tr_bitfieldDifference( tr_bitfield *, const tr_bitfield * );

int          tr_bitfieldIsEmpty( const tr_bitfield* );

size_t       tr_bitfieldCountTrueBits( const tr_bitfield* );

tr_bitfield* tr_bitfieldOr( tr_bitfield*, const tr_bitfield* );

/** A stripped-down version of bitfieldHas to be used
    for speed when you're looping quickly.  This version
    has none of tr_bitfieldHas()'s safety checks, so you
    need to call tr_bitfieldTestFast() first before you
    start looping. */
static inline tr_bool tr_bitfieldHasFast( const tr_bitfield * b, const size_t nth )
{
    return ( b->bits[nth>>3u] << ( nth & 7u ) & 0x80 ) != 0;
}

/** @param high the highest nth bit you're going to access */
static inline tr_bool tr_bitfieldTestFast( const tr_bitfield * b, const size_t high )
{
    return ( b != NULL )
        && ( b->bits != NULL )
        && ( high < b->bitCount );
}

static inline tr_bool tr_bitfieldHas( const tr_bitfield * b, size_t nth )
{
    return tr_bitfieldTestFast( b, nth ) && tr_bitfieldHasFast( b, nth );
}

#endif
