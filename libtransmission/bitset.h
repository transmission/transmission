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
    tr_bool haveAll;
    tr_bool haveNone;
    tr_bitfield bitfield;
}
tr_bitset;

void tr_bitsetReserve( tr_bitset * b, size_t size );
void tr_bitsetConstructor( tr_bitset * b, size_t size );
void tr_bitsetDestructor( tr_bitset * b );

void tr_bitsetSetHaveAll( tr_bitset * b );
void tr_bitsetSetHaveNone( tr_bitset * b );

int  tr_bitsetAdd( tr_bitset * b, size_t i );

/***
****
***/

double tr_bitsetPercent( const tr_bitset * b );

tr_bool tr_bitsetHas( const tr_bitset * b, const size_t nth );

void tr_bitsetOr( tr_bitfield * a, const tr_bitset * b );

/* set 'a' to all the flags that were in 'a' but not 'b' */
void tr_bitsetDifference( tr_bitfield * a, const tr_bitset * b );

#endif
