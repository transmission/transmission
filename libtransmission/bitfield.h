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

#include <assert.h>
#include "transmission.h"

/** @brief Implementation of the BitTorrent spec's Bitfield array of bits */
typedef struct tr_bitfield
{
    uint8_t *  bits;
    size_t     alloc_count;

    size_t     bit_count;

    size_t     true_count;

    /* Special cases for when full or empty but we don't know the bitCount.
       This occurs when a magnet link's peers send have all / have none */
    bool       have_all_hint;
    bool       have_none_hint;
}
tr_bitfield;

/***
****
***/

void   tr_bitfieldSetHasAll( tr_bitfield* );

void   tr_bitfieldSetHasNone( tr_bitfield* );

void   tr_bitfieldAdd( tr_bitfield*, size_t bit );

void   tr_bitfieldRem( tr_bitfield*, size_t bit );

void   tr_bitfieldAddRange( tr_bitfield*, size_t begin, size_t end );

void   tr_bitfieldRemRange( tr_bitfield*, size_t begin, size_t end );

/***
****  life cycle
***/

extern const tr_bitfield TR_BITFIELD_INIT;

void   tr_bitfieldConstruct( tr_bitfield*, size_t bit_count );

static inline void
tr_bitfieldDestruct( tr_bitfield * b )
{
    tr_bitfieldSetHasNone( b );
}

/***
****
***/

void   tr_bitfieldSetFromBitfield( tr_bitfield*, const tr_bitfield* );

void   tr_bitfieldSetRaw( tr_bitfield*, const void * bits, size_t byte_count );

void*  tr_bitfieldGetRaw( const tr_bitfield * b, size_t * byte_count );

/***
****
***/

size_t  tr_bitfieldCountRange( const tr_bitfield*, size_t begin, size_t end );

size_t  tr_bitfieldCountTrueBits( const tr_bitfield * b );

static inline bool
tr_bitfieldHasAll( const tr_bitfield * b )
{
    return b->bit_count ? ( b->true_count == b->bit_count ) : b->have_all_hint;
}

static inline bool
tr_bitfieldHasNone( const tr_bitfield * b )
{
    return b->bit_count ? ( b->true_count == 0 ) : b->have_none_hint;
}

static inline bool
tr_bitfieldHas( const tr_bitfield * b, size_t n )
{
    if( tr_bitfieldHasAll( b ) ) return true;
    if( tr_bitfieldHasNone( b ) ) return false;
    if( n>>3u >= b->alloc_count ) return false;
    return ( b->bits[n>>3u] << ( n & 7u ) & 0x80 ) != 0;
}

#endif
