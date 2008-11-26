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

#include <assert.h>
#include <limits.h>

#include "event.h"

#include "transmission.h"
#include "bandwidth.h"
#include "iobuf.h"
#include "ptrarray.h"
#include "utils.h"

/***
****
***/

enum
{
    HISTORY_MSEC = 2000,
    INTERVAL_MSEC = HISTORY_MSEC,
    GRANULARITY_MSEC = 250,
    HISTORY_SIZE = ( INTERVAL_MSEC / GRANULARITY_MSEC ),
    MAGIC_NUMBER = 43143
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

struct tr_band
{
    unsigned int isLimited : 1;
    unsigned int honorParentLimits : 1;
    size_t bytesLeft;
    double desiredSpeed;
    struct bratecontrol raw;
    struct bratecontrol piece;
};

struct tr_bandwidth
{
    struct tr_band band[2];
    struct tr_bandwidth * parent;
    int magicNumber;
    tr_session * session;
    tr_ptrArray * children; /* struct tr_bandwidth */
    tr_ptrArray * iobufs; /* struct tr_iobuf */
};

/***
****
***/

static int
comparePointers( const void * a, const void * b )
{
    return a - b;
}

static int
isBandwidth( const tr_bandwidth * b )
{
    return ( b != NULL ) && ( b->magicNumber == MAGIC_NUMBER );
}

static int
isDirection( const tr_direction dir )
{
    return ( dir == TR_UP ) || ( dir == TR_DOWN );
}

/***
****
***/

tr_bandwidth*
tr_bandwidthNew( tr_session * session, tr_bandwidth * parent )
{
    tr_bandwidth * b = tr_new0( tr_bandwidth, 1 );
    b->session = session;
    b->children = tr_ptrArrayNew( );
    b->iobufs = tr_ptrArrayNew( );
    b->magicNumber = MAGIC_NUMBER;
    b->band[TR_UP].honorParentLimits = 1;
    b->band[TR_DOWN].honorParentLimits = 1;
    tr_bandwidthSetParent( b, parent );
    return b;
}

void
tr_bandwidthFree( tr_bandwidth * b )
{
    assert( isBandwidth( b ) );

    tr_bandwidthSetParent( b, NULL );
    tr_ptrArrayFree( b->iobufs, NULL );
    tr_ptrArrayFree( b->children, NULL );
    b->magicNumber = 0xDEAD;
    tr_free( b );
}

/***
****
***/

void
tr_bandwidthSetParent( tr_bandwidth  * b,
                       tr_bandwidth  * parent )
{
    assert( isBandwidth( b ) );
    assert( b != parent );

    if( b->parent )
    {
        assert( isBandwidth( b->parent ) );

        tr_ptrArrayRemoveSorted( b->parent->children, b, comparePointers );
        b->parent= NULL;
    }

    if( parent )
    {
        assert( isBandwidth( parent ) );
        assert( parent->parent != b );

        tr_ptrArrayInsertSorted( parent->children, b, comparePointers );
        b->parent = parent;
    }
}

void
tr_bandwidthHonorParentLimits( tr_bandwidth  * b,
                               tr_direction    dir,
                               int             honorParentLimits )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    b->band[dir].honorParentLimits = honorParentLimits != 0;
}

/***
****
***/

void
tr_bandwidthSetDesiredSpeed( tr_bandwidth  * b,
                             tr_direction    dir,
                             double          desiredSpeed )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    b->band[dir].desiredSpeed = desiredSpeed; 
}

double
tr_bandwidthGetDesiredSpeed( const tr_bandwidth  * b,
                             tr_direction          dir )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    return b->band[dir].desiredSpeed;
}

void
tr_bandwidthSetLimited( tr_bandwidth  * b,
                        tr_direction    dir,
                        int             isLimited )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    b->band[dir].isLimited = isLimited != 0;
}

int
tr_bandwidthIsLimited( const tr_bandwidth  * b,
                       tr_direction          dir )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    return b->band[dir].isLimited != 0;
}

#if 0
#define DEBUG_DIRECTION TR_DOWN
#endif

void
tr_bandwidthAllocate( tr_bandwidth  * b,
                      tr_direction    dir,
                      int             period_msec )
{
    const double currentSpeed = tr_bandwidthGetPieceSpeed( b, dir ); /* KiB/s */
    const double desiredSpeed = b->band[dir].desiredSpeed;           /* KiB/s */
    const double seconds_per_pulse = period_msec / 1000.0;
    const double current_bytes_per_pulse = currentSpeed * 1024.0 * seconds_per_pulse;
    const double desired_bytes_per_pulse = desiredSpeed * 1024.0 * seconds_per_pulse;
    const double pulses_per_history = (double)HISTORY_MSEC / period_msec;
    const double min = desired_bytes_per_pulse * 0.90;
    const double max = desired_bytes_per_pulse * 1.20;
    const double next_pulse_bytes = desired_bytes_per_pulse * ( pulses_per_history + 1 )
                                  - ( current_bytes_per_pulse * pulses_per_history );
    double clamped;

    /* clamp the return value to lessen oscillation */
    clamped = next_pulse_bytes;
    clamped = MAX( clamped, min );
    clamped = MIN( clamped, max );
    b->band[dir].bytesLeft = clamped;

#ifdef DEBUG_DIRECTION
if( dir == DEBUG_DIRECTION )
fprintf( stderr, "bandwidth %p currentSpeed(%5.2f) desiredSpeed(%5.2f), allocating %5.2f (unclamped: %5.2f)\n",
         b, currentSpeed, desiredSpeed,
         clamped/1024.0, next_pulse_bytes/1024.0 );
#endif

    /* notify the io buffers that there's more bandwidth available */
    if( !b->band[dir].isLimited || ( clamped > 0 ) ) {
        int i, n=0;
        short what = dir==TR_UP ? EV_WRITE : EV_READ;
        struct tr_iobuf ** iobufs = (struct tr_iobuf**) tr_ptrArrayPeek( b->iobufs, &n );
#ifdef DEBUG_DIRECTION
if( dir == DEBUG_DIRECTION )
fprintf( stderr, "bandwidth %p has %d iobufs\n", b, n );
#endif
        for( i=0; i<n; ++i )
            tr_iobuf_enable( iobufs[i], what );
    }

    /* all children should reallocate too */
    if( 1 ) {
        int i, n=0;
        struct tr_bandwidth ** children = (struct tr_bandwidth**) tr_ptrArrayPeek( b->children, &n );
        for( i=0; i<n; ++i )
            tr_bandwidthAllocate( children[i], dir, period_msec );
    }
}

/***
****
***/

void
tr_bandwidthAddBuffer( tr_bandwidth        * b,
                       struct tr_iobuf     * iobuf )
{
    assert( isBandwidth( b ) );
    assert( iobuf );

    tr_ptrArrayInsertSorted( b->iobufs, iobuf, comparePointers );
}

void
tr_bandwidthRemoveBuffer( tr_bandwidth        * b,
                          struct tr_iobuf     * iobuf )
{
    assert( isBandwidth( b ) );
    assert( iobuf );

    tr_ptrArrayRemoveSorted( b->iobufs, iobuf, comparePointers );
}

/***
****
***/

size_t
tr_bandwidthClamp( const tr_bandwidth  * b,
                   tr_direction          dir,
                   size_t                byteCount )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    if( b )
    {
        if( b->band[dir].isLimited )
            byteCount = MIN( byteCount, b->band[dir].bytesLeft );

        if( b->parent && b->band[dir].honorParentLimits )
            byteCount = tr_bandwidthClamp( b->parent, dir, byteCount );
    }

    return byteCount;
}

double
tr_bandwidthGetRawSpeed( const tr_bandwidth * b, tr_direction dir )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    return getSpeed( &b->band[dir].raw );
}

double
tr_bandwidthGetPieceSpeed( const tr_bandwidth * b, tr_direction dir )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    return getSpeed( &b->band[dir].piece );
}

void
tr_bandwidthUsed( tr_bandwidth  * b,
                  tr_direction    dir,
                  size_t          byteCount,
                  int             isPieceData )
{
    struct tr_band * band;

    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    band = &b->band[dir];

    if( band->isLimited && isPieceData )
        band->bytesLeft -= MIN( band->bytesLeft, byteCount );

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && band->isLimited && isPieceData )
fprintf( stderr, "%p consumed %zu bytes of piece data... %zu left\n", b, byteCount, band->bytesLeft );
#endif

    bytesUsed( &band->raw, byteCount );

    if( isPieceData )
        bytesUsed( &band->piece, byteCount );

    if( b->parent != NULL )
        tr_bandwidthUsed( b->parent, dir, byteCount, isPieceData );
}
