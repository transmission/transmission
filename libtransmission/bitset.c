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

#include "transmission.h"
#include "bencode.h"
#include "bitset.h"
#include "utils.h"

const tr_bitset TR_BITSET_INIT = { FALSE, FALSE, { NULL, 0, 0 } };

void
tr_bitsetConstruct( tr_bitset * b, size_t bitCount )
{
    *b = TR_BITSET_INIT;
    b->bitfield.bitCount = bitCount;
}

void
tr_bitsetDestruct( tr_bitset * b )
{
    tr_free( b->bitfield.bits );
    *b = TR_BITSET_INIT;
}

static void
tr_bitsetClear( tr_bitset * b )
{
    tr_free( b->bitfield.bits );
    b->bitfield.bits = NULL;
    b->haveAll = FALSE;
    b->haveNone = FALSE;
}

void
tr_bitsetSetHaveAll( tr_bitset * b )
{
    tr_bitsetClear( b );
    b->haveAll = TRUE;
}

void
tr_bitsetSetHaveNone( tr_bitset * b )
{
    tr_bitsetClear( b );
    b->haveNone = TRUE;
}

void
tr_bitsetSetBitfield( tr_bitset * b, const tr_bitfield * bitfield )
{
    const size_t n = tr_bitfieldCountTrueBits( bitfield );

    if( n == 0 )
    {
        tr_bitsetSetHaveNone( b );
    }
    else if( n == bitfield->bitCount )
    {
        tr_bitsetSetHaveAll( b );
    }
    else
    {
        tr_bitsetDestruct( b );
        b->bitfield.bits = tr_memdup( bitfield->bits, bitfield->byteCount );
        b->bitfield.bitCount = bitfield->bitCount;
        b->bitfield.byteCount = bitfield->byteCount;
    }
}

/***
****
***/

void
tr_bitsetAdd( tr_bitset * b, size_t i )
{
    tr_bitfield * bf = &b->bitfield;

    if( b->haveAll )
        return;

    b->haveNone = FALSE;

    /* do we need to resize the bitfield to accomodate this bit? */
    if( !bf->bits || ( bf->bitCount < i+1 ) )
    {
        const size_t oldByteCount = bf->byteCount;
        if( bf->bitCount < i + 1 )
            bf->bitCount = i + 1;
        bf->byteCount = ( bf->bitCount + 7u ) / 8u;
        bf->bits = tr_renew( uint8_t, bf->bits, bf->byteCount );
        if( bf->byteCount > oldByteCount )
            memset( bf->bits + oldByteCount, 0, bf->byteCount - oldByteCount );
    }

    tr_bitfieldAdd( bf, i );
}

void
tr_bitsetRem( tr_bitset * b, size_t i )
{
    if( b->haveNone )
        return;

    b->haveAll = FALSE;

    if( !b->bitfield.bits )
    {
        tr_bitfieldConstruct( &b->bitfield, b->bitfield.bitCount );
        tr_bitfieldAddRange( &b->bitfield, 0, b->bitfield.bitCount );
    }

    tr_bitfieldRem( &b->bitfield, i );
}

void
tr_bitsetRemRange( tr_bitset * b, size_t begin, size_t end )
{
    if( b->haveNone )
        return;

    b->haveAll = FALSE;
    if( !b->bitfield.bits )
    {
        tr_bitfieldConstruct( &b->bitfield, b->bitfield.bitCount );
        tr_bitfieldAddRange( &b->bitfield, 0, b->bitfield.bitCount );
    }

    tr_bitfieldRemRange( &b->bitfield, begin, end );
}

/***
****
***/

tr_bool
tr_bitsetHas( const tr_bitset * b, const size_t nth )
{
    if( b->haveAll ) return TRUE;
    if( b->haveNone ) return FALSE;
    if( nth >= b->bitfield.bitCount ) return FALSE;
    return tr_bitfieldHas( &b->bitfield, nth );
}

size_t
tr_bitsetCountRange( const tr_bitset * b, const size_t begin, const size_t end )
{
    if( b->haveAll ) return end - begin;
    if( b->haveNone ) return 0;
    return tr_bitfieldCountRange( &b->bitfield, begin, end );
}

double
tr_bitsetPercent( const tr_bitset * b )
{
    if( b->haveAll ) return 1.0;
    if( b->haveNone ) return 0.0;
    if( b->bitfield.bitCount == 0 ) return 0.0;
    return tr_bitfieldCountTrueBits( &b->bitfield ) / (double)b->bitfield.bitCount;
}

void
tr_bitsetOr( tr_bitfield * a, const tr_bitset * b )
{
    if( b->haveAll )
        tr_bitfieldAddRange( a, 0, a->bitCount );
    else if( !b->haveNone )
        tr_bitfieldOr( a, &b->bitfield );
}

/***
****
***/

tr_bool
tr_bitsetFromBenc( tr_bitset * b, tr_benc * benc )
{
    size_t buflen;
    const uint8_t * buf;
    tr_bool handled = FALSE;

    if( tr_bencGetRaw( benc, &buf, &buflen ) )
    {
        if( ( buflen == 3 ) && !memcmp( buf, "all", 3 ) )
        {
            tr_bitsetSetHaveAll( b );
            handled = TRUE;
        }
        else if( ( buflen == 4 ) && !memcmp( buf, "none", 4 ) )
        {
            tr_bitsetSetHaveNone( b );
            handled = TRUE;
        }
        else
        {
            b->haveAll = FALSE;
            b->haveNone = FALSE;
            tr_free( b->bitfield.bits );
            b->bitfield.bits = tr_memdup( buf, buflen );
            b->bitfield.byteCount = buflen;
            b->bitfield.bitCount = buflen * 8;
            handled = TRUE;
        }
    }

    return handled;
}

void
tr_bitsetToBenc( const tr_bitset * b, tr_benc * benc )
{
    if( b->haveAll )
    {
        tr_bencInitStr( benc, "all", 3 );
    }
    else if( b->haveNone )
    {
        tr_bencInitStr( benc, "none", 4 );
    }
    else
    {
        const tr_bitfield * bf = &b->bitfield;
        const size_t n = tr_bitfieldCountTrueBits( bf );

        if( n == bf->bitCount )
        {
            tr_bencInitStr( benc, "all", 3 );
        }
        else if( n == 0 )
        {
            tr_bencInitStr( benc, "none", 4 );
        }
        else
        {
            tr_bencInitRaw( benc, bf->bits, bf->byteCount );
        }
    }
}
