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
#include <string.h> /* memset */

#include "transmission.h"
#include "bitfield.h"
#include "bitset.h"
#include "utils.h" /* tr_new0() */

const tr_bitfield TR_BITFIELD_INIT = { NULL, 0, 0 };

tr_bitfield*
tr_bitfieldConstruct( tr_bitfield * b, size_t bitCount )
{
    b->bitCount = bitCount;
    b->byteCount = ( bitCount + 7u ) / 8u;
    b->bits = tr_new0( uint8_t, b->byteCount );
    return b;
}

tr_bitfield*
tr_bitfieldDestruct( tr_bitfield * b )
{
    if( b )
        tr_free( b->bits );
    return b;
}

tr_bitfield*
tr_bitfieldNew( size_t bitCount )
{
    return tr_bitfieldConstruct( tr_new( tr_bitfield, 1 ), bitCount );
}

void
tr_bitfieldFree( tr_bitfield * b )
{
    tr_free( tr_bitfieldDestruct( b ) );
}

void
tr_bitfieldClear( tr_bitfield * bitfield )
{
    memset( bitfield->bits, 0, bitfield->byteCount );
}

int
tr_bitfieldAdd( tr_bitfield * bitfield,
                size_t        nth )
{
    assert( bitfield );
    assert( bitfield->bits );

    if( nth >= bitfield->bitCount )
        return -1;

    bitfield->bits[nth >> 3u] |= ( 0x80 >> ( nth & 7u ) );
    return 0;
}

/* Sets bit range [begin, end) to 1 */
int
tr_bitfieldAddRange( tr_bitfield * b,
                     size_t        begin,
                     size_t        end )
{
    size_t        sb, eb;
    unsigned char sm, em;

    end--;

    if( ( end >= b->bitCount ) || ( begin > end ) )
        return -1;

    sb = begin >> 3;
    sm = ~( 0xff << ( 8 - ( begin & 7 ) ) );
    eb = end >> 3;
    em = 0xff << ( 7 - ( end & 7 ) );

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

    return 0;
}

int
tr_bitfieldRem( tr_bitfield * bitfield,
                size_t        nth )
{
    assert( bitfield );
    assert( bitfield->bits );

    if( nth >= bitfield->bitCount )
        return -1;

    bitfield->bits[nth >> 3u] &= ( 0xff7f >> ( nth & 7u ) );
    return 0;
}

/* Clears bit range [begin, end) to 0 */
int
tr_bitfieldRemRange( tr_bitfield * b,
                     size_t        begin,
                     size_t        end )
{
    size_t        sb, eb;
    unsigned char sm, em;

    end--;

    if( ( end >= b->bitCount ) || ( begin > end ) )
        return -1;

    sb = begin >> 3;
    sm = 0xff << ( 8 - ( begin & 7 ) );
    eb = end >> 3;
    em = ~( 0xff << ( 7 - ( end & 7 ) ) );

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

    return 0;
}

tr_bitfield*
tr_bitfieldOr( tr_bitfield * a, const tr_bitfield * b )
{
    uint8_t * ait = a->bits;
    const uint8_t * aend = ait + a->byteCount;
    const uint8_t * bit = b->bits;
    const uint8_t * bend = bit + b->byteCount;

    while( ait!=aend && bit!=bend )
        *ait++ |= *bit++;

    return a;
}

static const int trueBitCount[256] =
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

size_t
tr_bitfieldCountTrueBits( const tr_bitfield* b )
{
    size_t           ret = 0;
    const uint8_t *  it, *end;

    if( !b )
        return 0;

    for( it = b->bits, end = it + b->byteCount; it != end; ++it )
        ret += trueBitCount[*it];

    return ret;
}

size_t
tr_bitfieldCountRange( const tr_bitfield * b, size_t begin, size_t end )
{
    size_t ret = 0;
    const int first_byte = begin >> 3u; 
    const int last_byte = ( end - 1 ) >> 3u;

    assert( begin < end );

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
        int i;
        uint8_t val;

        /* first byte */
        i = begin - (first_byte * 8);
        val = b->bits[first_byte];
        val <<= i;
        val >>= i;
        ret += trueBitCount[val];

        /* middle bytes */
        for( i=first_byte+1; i<last_byte; ++i )
            ret += trueBitCount[b->bits[i]];

        /* last byte */
        i = (last_byte+1)*8 - end;
        val = b->bits[last_byte];
        val >>= i;
        val <<= i;
        ret += trueBitCount[val];
    }

    assert( ret <= ( begin - end ) );
    return ret;
}
