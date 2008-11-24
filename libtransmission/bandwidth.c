/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <limits.h>
#include "transmission.h"
#include "bandwidth.h"
#include "utils.h"

/***
****
***/

enum
{
    HISTORY_MSEC = 2000,
    INTERVAL_MSEC = HISTORY_MSEC,
    GRANULARITY_MSEC = 250,
    HISTORY_SIZE = ( INTERVAL_MSEC / GRANULARITY_MSEC )
};

struct bratecontrol
{
    int newest;
    struct { uint64_t date, size; } transfers[HISTORY_SIZE];
};

static float
getSpeed( const struct bratecontrol * r )
{
    const uint64_t interval_msec = HISTORY_MSEC;
    uint64_t       bytes = 0;
    const uint64_t cutoff = tr_date ( ) - interval_msec;
    int            i = r->newest;

    for( ;; )
    {
        if( r->transfers[i].date <= cutoff )
            break;

        bytes += r->transfers[i].size;

        if( --i == -1 ) i = HISTORY_SIZE - 1; /* circular history */
        if( i == r->newest ) break; /* we've come all the way around */
    }

    return ( bytes / 1024.0 ) * ( 1000.0 / interval_msec );
}

static void
bytesUsed( struct bratecontrol * r, size_t size )
{
    const uint64_t now = tr_date ( );

    if( r->transfers[r->newest].date + GRANULARITY_MSEC >= now )
        r->transfers[r->newest].size += size;
    else
    {
        if( ++r->newest == HISTORY_SIZE ) r->newest = 0;
        r->transfers[r->newest].date = now;
        r->transfers[r->newest].size = size;
    }
}

/******
*******
*******
******/

struct tr_bandwidth
{
    unsigned int isLimited : 1;

    size_t bytesLeft;

    struct bratecontrol raw;
    struct bratecontrol piece;

    tr_session * session;
};

/***
****
***/

tr_bandwidth*
tr_bandwidthNew( tr_session * session )
{
    tr_bandwidth * b = tr_new0( tr_bandwidth, 1 );
    b->session = session;
    return b;
}

void
tr_bandwidthFree( tr_bandwidth * b )
{
    tr_free( b );
}

/***
****
***/

void
tr_bandwidthSetLimited( tr_bandwidth  * b,
                        size_t          bytesLeft )
{
    b->isLimited = 1;
    b->bytesLeft = bytesLeft;
}

void
tr_bandwidthSetUnlimited( tr_bandwidth * b )
{
    b->isLimited = 0;
}

size_t
tr_bandwidthClamp( const tr_bandwidth  * b,
                   size_t                byteCount )
{
const size_t n = byteCount;

    if( b && b->isLimited )
        byteCount = MIN( byteCount, b->bytesLeft );

/* if( n != byteCount ) fprintf( stderr, "%p: %zu clamped to %zu\n", b, n, byteCount ); */
    return byteCount;
}

/***
****
***/

double
tr_bandwidthGetRawSpeed( const tr_bandwidth * b )
{
    return getSpeed( &b->raw );
}

double
tr_bandwidthGetPieceSpeed( const tr_bandwidth * b UNUSED )
{
    return getSpeed( &b->piece );
}

void
tr_bandwidthUsed( tr_bandwidth  * b,
                  size_t          byteCount,
                  int             isPieceData )
{
    if( b->isLimited && isPieceData )
    {
        b->bytesLeft -= MIN( b->bytesLeft, byteCount );
        /* fprintf( stderr, "%p used %zu bytes ... %zu left\n", b, byteCount, b->bytesLeft ); */
    }

    bytesUsed( &b->raw, byteCount );

    if( isPieceData )
        bytesUsed( &b->piece, byteCount );
}
