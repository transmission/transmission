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

#include <assert.h>
#include <stdlib.h> /* realloc() */
#include <string.h> /* memset */

#include "transmission.h"
#include "bitfield.h"
#include "utils.h" /* tr_new0() */

const tr_bitfield TR_BITFIELD_INIT = { NULL, 0, 0, 0, false, false };

/****
*****
****/

static const int8_t trueBitCount[256] =
{
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8
};

static size_t
countArray( const tr_bitfield * b )
{
    size_t i;
    size_t ret = 0;

    for( i=0; i<b->alloc_count; ++i )
        ret += trueBitCount[b->bits[i]];

    return ret;
}

static size_t
countRange( const tr_bitfield * b, size_t begin, size_t end )
{
    size_t ret = 0;
    const size_t first_byte = begin >> 3u;
    const size_t last_byte = ( end - 1 ) >> 3u;

    if( !b->bit_count )
        return 0;
    if( first_byte >= b->alloc_count )
        return 0;

    assert( begin < end );
    assert( b->bits != NULL );

    if( first_byte == last_byte )
    {
        int i;
        uint8_t val = b->bits[first_byte];

        i = begin - (first_byte * 8);
        val <<= i;
        val >>= i;
        i = (last_byte+1)*8 - end;
        val >>= i;
        val <<= i;

        ret += trueBitCount[val];
    }
    else
    {
        size_t i;
        uint8_t val;

        /* first byte */
        i = begin - (first_byte * 8);
        val = b->bits[first_byte];
        val <<= i;
        val >>= i;
        ret += trueBitCount[val];

        /* middle bytes */
        for( i=first_byte+1; i<b->alloc_count && i<last_byte; ++i )
            ret += trueBitCount[b->bits[i]];

        /* last byte */
        if( last_byte < b->alloc_count ) {
            i = (last_byte+1)*8 - end;
            val = b->bits[last_byte];
            val >>= i;
            val <<= i;
            ret += trueBitCount[val];
        }
    }

    assert( ret <= ( begin - end ) );
    return ret;
}

size_t
tr_bitfieldCountRange( const tr_bitfield * b, size_t begin, size_t end )
{
    if( tr_bitfieldHasAll( b ) )
        return end - begin;

    if( tr_bitfieldHasNone( b ) )
        return 0;

    return countRange( b, begin, end );
}

/***
****
***/

static bool
tr_bitfieldIsValid( const tr_bitfield * b UNUSED )
{
    assert( b != NULL );
    assert( ( b->alloc_count == 0 ) == ( b->bits == 0 ) );
    assert( !b->bits || ( b->true_count == countArray ( b ) ) );
    return true;
}

size_t
tr_bitfieldCountTrueBits( const tr_bitfield * b )
{
    tr_bitfieldIsValid( b );
    return b->true_count;
}

static size_t
get_bytes_needed( size_t bit_count )
{
    return ( bit_count + 7u ) / 8u;
}

static void
set_all_true( uint8_t * array, size_t bit_count )
{
    const uint8_t val = 0xFF;
    const size_t n = get_bytes_needed( bit_count );
    memset( array, val, n-1 );
    array[n-1] = val << (n*8 - bit_count);
}

void*
tr_bitfieldGetRaw( const tr_bitfield * b, size_t * byte_count )
{
    const size_t n = get_bytes_needed( b->bit_count );
    uint8_t * bits = tr_new0( uint8_t, n );

    assert( b->bit_count > 0 );

    if( b->alloc_count )
        memcpy( bits, b->bits, b->alloc_count );
    else if( tr_bitfieldHasAll( b ) )
        set_all_true( bits, b->bit_count );

    *byte_count = n;
    return bits;
}

static void
tr_bitfieldEnsureBitsAlloced( tr_bitfield * b, size_t nth )
{
    size_t bytes_needed;
    const bool has_all = tr_bitfieldHasAll( b );

    if( has_all )
        bytes_needed = get_bytes_needed( MAX( nth, b->true_count ) + 1 );
    else
        bytes_needed = get_bytes_needed( nth + 1 );

    if( b->alloc_count < bytes_needed )
    {
        b->bits = tr_renew( uint8_t, b->bits, bytes_needed );
        memset( b->bits + b->alloc_count, 0, bytes_needed - b->alloc_count );
        b->alloc_count = bytes_needed;

        if( has_all )
            set_all_true( b->bits, b->true_count );
    }

    assert( tr_bitfieldIsValid( b ) );
}

static void
tr_bitfieldFreeArray( tr_bitfield * b )
{
    tr_free( b->bits );
    b->bits = NULL;
    b->alloc_count = 0;
}

static void
tr_bitfieldSetTrueCount( tr_bitfield * b, size_t n )
{
    b->true_count = n;

    if( tr_bitfieldHasAll( b ) || tr_bitfieldHasNone( b ) )
        tr_bitfieldFreeArray( b );

    assert( tr_bitfieldIsValid(  b ) );
}

static void
tr_bitfieldRebuildTrueCount( tr_bitfield * b )
{
    tr_bitfieldSetTrueCount( b, countArray( b ) );
}

static void
tr_bitfieldIncTrueCount( tr_bitfield * b, int i )
{
    tr_bitfieldSetTrueCount( b, b->true_count + i );
}

/****
*****
****/

void
tr_bitfieldConstruct( tr_bitfield * b, size_t bit_count )
{
    b->bit_count = bit_count;
    b->true_count = 0;
    b->bits = NULL;
    b->alloc_count = 0;
    b->have_all_hint = false;
    b->have_none_hint = false;

    assert( tr_bitfieldIsValid( b ) );
}

void
tr_bitfieldSetHasNone( tr_bitfield * b )
{
    tr_bitfieldFreeArray( b );
    b->true_count = 0;
    b->have_all_hint = false;
    b->have_none_hint = true;

    assert( tr_bitfieldIsValid( b ) );
}

void
tr_bitfieldSetHasAll( tr_bitfield * b )
{
    tr_bitfieldFreeArray( b );
    b->true_count = b->bit_count;
    b->have_all_hint = true;
    b->have_none_hint = false;

    assert( tr_bitfieldIsValid( b ) );
}

void
tr_bitfieldSetFromBitfield( tr_bitfield * b, const tr_bitfield * src )
{
    if( tr_bitfieldHasAll( src ) )
        tr_bitfieldSetHasAll( b );
    else if( tr_bitfieldHasNone( src ) )
        tr_bitfieldSetHasNone( b );
    else
        tr_bitfieldSetRaw( b, src->bits, src->alloc_count );
}

void
tr_bitfieldSetRaw( tr_bitfield * b, const void * bits, size_t byte_count )
{
    tr_bitfieldFreeArray( b );
    b->true_count = 0;
    b->bits = tr_memdup( bits, byte_count );
    b->alloc_count = byte_count;
    tr_bitfieldRebuildTrueCount( b );
}

void
tr_bitfieldSetFromFlags( tr_bitfield * b, const bool * flags, size_t n )
{
    size_t i;
    size_t trueCount = 0;

    tr_bitfieldFreeArray( b );
    tr_bitfieldEnsureBitsAlloced( b, n );

    for( i=0; i<n; ++i )
    {
        if( flags[i] )
        {
            ++trueCount;
            b->bits[i >> 3u] |= ( 0x80 >> ( i & 7u ) );
        }
    }

    tr_bitfieldSetTrueCount( b, trueCount );
}

void
tr_bitfieldAdd( tr_bitfield * b, size_t nth )
{
    if( !tr_bitfieldHas( b, nth ) )
    {
        tr_bitfieldEnsureBitsAlloced( b, nth );
        b->bits[nth >> 3u] |= ( 0x80 >> ( nth & 7u ) );
        tr_bitfieldIncTrueCount( b, 1 );
    }
}

/* Sets bit range [begin, end) to 1 */
void
tr_bitfieldAddRange( tr_bitfield * b, size_t begin, size_t end )
{
    size_t sb, eb;
    unsigned char sm, em;
    const size_t diff = (end-begin) - tr_bitfieldCountRange( b, begin, end );

    if( diff == 0 )
        return;

    end--;
    if( ( end >= b->bit_count ) || ( begin > end ) )
        return;

    sb = begin >> 3;
    sm = ~( 0xff << ( 8 - ( begin & 7 ) ) );
    eb = end >> 3;
    em = 0xff << ( 7 - ( end & 7 ) );

    tr_bitfieldEnsureBitsAlloced( b, end );
    if( sb == eb )
    {
        b->bits[sb] |= ( sm & em );
    }
    else
    {
        b->bits[sb] |= sm;
        b->bits[eb] |= em;
        if( ++sb < eb )
            memset ( b->bits + sb, 0xff, eb - sb );
    }

    tr_bitfieldIncTrueCount( b, diff );
}

void
tr_bitfieldRem( tr_bitfield * b, size_t nth )
{
    assert( tr_bitfieldIsValid( b ) );

    if( !tr_bitfieldHas( b, nth ) )
    {
        tr_bitfieldEnsureBitsAlloced( b, nth );
        b->bits[nth >> 3u] &= ( 0xff7f >> ( nth & 7u ) );
        tr_bitfieldIncTrueCount( b, -1 );
    }
}

/* Clears bit range [begin, end) to 0 */
void
tr_bitfieldRemRange( tr_bitfield * b, size_t begin, size_t end )
{
    size_t sb, eb;
    unsigned char sm, em;
    const size_t diff = tr_bitfieldCountRange( b, begin, end );

    if( !diff )
        return;

    end--;

    if( ( end >= b->bit_count ) || ( begin > end ) )
        return;

    sb = begin >> 3;
    sm = 0xff << ( 8 - ( begin & 7 ) );
    eb = end >> 3;
    em = ~( 0xff << ( 7 - ( end & 7 ) ) );

    tr_bitfieldEnsureBitsAlloced( b, end );
    if( sb == eb )
    {
        b->bits[sb] &= ( sm | em );
    }
    else
    {
        b->bits[sb] &= sm;
        b->bits[eb] &= em;
        if( ++sb < eb )
            memset ( b->bits + sb, 0, eb - sb );
    }

    tr_bitfieldIncTrueCount( b, -diff );
}
