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

#ifndef TR_BITSET_H
#define TR_BITSET_H 1

#include "transmission.h"
#include "bitfield.h"

/** @brief like a tr_bitfield, but supports haveAll and haveNone */
typedef struct tr_bitset
{
    bool haveAll;
    bool haveNone;
    tr_bitfield bitfield;
}
tr_bitset;

extern const tr_bitset TR_BITSET_INIT;

void tr_bitsetConstruct( tr_bitset * b, size_t bitCount );
void tr_bitsetDestruct( tr_bitset * b );

void tr_bitsetSetHaveAll( tr_bitset * b );
void tr_bitsetSetHaveNone( tr_bitset * b );

void tr_bitsetSetBitfield( tr_bitset * b, const tr_bitfield * bitfield );

void tr_bitsetAdd( tr_bitset * b, size_t i );
void tr_bitsetRem( tr_bitset * b, size_t i );
void tr_bitsetRemRange ( tr_bitset * b, size_t begin, size_t end );

struct tr_benc;
bool tr_bitsetFromBenc( tr_bitset * bitset, struct tr_benc * benc );
void tr_bitsetToBenc( const tr_bitset * bitset, struct tr_benc * benc );

/***
****
***/

double tr_bitsetPercent( const tr_bitset * b );

bool tr_bitsetHas( const tr_bitset * b, const size_t nth );
size_t tr_bitsetCountRange( const tr_bitset * b, const size_t begin, const size_t end );

void tr_bitsetOr( tr_bitfield * a, const tr_bitset * b );

#endif
