/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <limits.h>

#include "event.h"

#include "transmission.h"
#include "bandwidth.h"
#include "crypto.h"
#include "peer-io.h"
#include "ptrarray.h"
#include "utils.h"

/***
****
***/

enum
{
    HISTORY_MSEC = 2000,
    INTERVAL_MSEC = HISTORY_MSEC,
    GRANULARITY_MSEC = 50,
    HISTORY_SIZE = ( INTERVAL_MSEC / GRANULARITY_MSEC ),
    MAGIC_NUMBER = 43143
};

struct bratecontrol
{
    int newest;
    struct { uint64_t date, size; } transfers[HISTORY_SIZE];
};

static float
getSpeed( const struct bratecontrol * r, int interval_msec )
{
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
    tr_bool isLimited;
    tr_bool honorParentLimits;
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
    tr_ptrArray children; /* struct tr_bandwidth */
    tr_ptrArray peers; /* tr_peerIo */
};

/***
****
***/

static int
comparePointers( const void * a, const void * b )
{
    if( a != b )
        return a < b ? -1 : 1;

    return 0;
}

tr_bool
tr_isBandwidth( const tr_bandwidth * b )
{
    return ( b != NULL ) && ( b->magicNumber == MAGIC_NUMBER );
}

/***
****
***/

tr_bandwidth*
tr_bandwidthNew( tr_session * session, tr_bandwidth * parent )
{
    tr_bandwidth * b = tr_new0( tr_bandwidth, 1 );
    b->session = session;
    b->children = TR_PTR_ARRAY_INIT;
    b->peers = TR_PTR_ARRAY_INIT;
    b->magicNumber = MAGIC_NUMBER;
    b->band[TR_UP].honorParentLimits = TRUE;
    b->band[TR_DOWN].honorParentLimits = TRUE;
    tr_bandwidthSetParent( b, parent );
    return b;
}

void
tr_bandwidthFree( tr_bandwidth * b )
{
    assert( tr_isBandwidth( b ) );

    tr_bandwidthSetParent( b, NULL );
    tr_ptrArrayDestruct( &b->peers, NULL );
    tr_ptrArrayDestruct( &b->children, NULL );
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
    assert( tr_isBandwidth( b ) );
    assert( b != parent );

    if( b->parent )
    {
        assert( tr_isBandwidth( b->parent ) );

        tr_ptrArrayRemoveSorted( &b->parent->children, b, comparePointers );
        b->parent = NULL;
    }

    if( parent )
    {
        assert( tr_isBandwidth( parent ) );
        assert( parent->parent != b );

        tr_ptrArrayInsertSorted( &parent->children, b, comparePointers );
        b->parent = parent;
    }
}

void
tr_bandwidthHonorParentLimits( tr_bandwidth  * b,
                               tr_direction    dir,
                               tr_bool         honorParentLimits )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    b->band[dir].honorParentLimits = honorParentLimits;
}

/***
****
***/

void
tr_bandwidthSetDesiredSpeed( tr_bandwidth  * b,
                             tr_direction    dir,
                             double          desiredSpeed )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    b->band[dir].desiredSpeed = desiredSpeed; 
}

double
tr_bandwidthGetDesiredSpeed( const tr_bandwidth  * b,
                             tr_direction          dir )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return b->band[dir].desiredSpeed;
}

void
tr_bandwidthSetLimited( tr_bandwidth  * b,
                        tr_direction    dir,
                        tr_bool         isLimited )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    b->band[dir].isLimited = isLimited;
}

tr_bool
tr_bandwidthIsLimited( const tr_bandwidth  * b,
                       tr_direction          dir )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return b->band[dir].isLimited;
}

#if 0
#warning do not check the code in with this enabled
#define DEBUG_DIRECTION TR_UP
#endif

static void
allocateBandwidth( tr_bandwidth  * b,
                   tr_direction    dir,
                   int             period_msec,
                   tr_ptrArray   * peer_pool )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    /* set the available bandwidth */
    if( b->band[dir].isLimited )
    {
        const double desiredSpeed = b->band[dir].desiredSpeed;
        const double nextPulseSpeed = desiredSpeed;
        b->band[dir].bytesLeft = MAX( 0.0, nextPulseSpeed * 1024.0 * period_msec / 1000.0 );

#ifdef DEBUG_DIRECTION
        if( dir == DEBUG_DIRECTION )
                fprintf( stderr, "bandwidth %p currentPieceSpeed(%5.2f of %5.2f) desiredSpeed(%5.2f), allocating %5.2f\n",
                         b, currentSpeed, tr_bandwidthGetRawSpeed( b, dir ), desiredSpeed,
                         b->band[dir].bytesLeft/1024.0 );
#endif
    }

    /* traverse & repeat for the subtree */
    {
        int i;
        const int n = TR_PTR_ARRAY_LENGTH( &b->peers );
        for( i=0; i<n; ++i )
            tr_ptrArrayAppend( peer_pool, tr_ptrArrayNth( &b->peers, i ) );
    }

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && ( n > 1 ) )
fprintf( stderr, "bandwidth %p has %d peers\n", b, n );
#endif

    /* all children should reallocate too */
    if( 1 ) {
        int i;
        struct tr_bandwidth ** children = (struct tr_bandwidth**) TR_PTR_ARRAY_DATA( &b->children );
        const int n = TR_PTR_ARRAY_LENGTH( &b->children );
        for( i=0; i<n; ++i )
            allocateBandwidth( children[i], dir, period_msec, peer_pool );
    }
}

void
tr_bandwidthAllocate( tr_bandwidth  * b,
                      tr_direction    dir,
                      int             period_msec )
{
    int i, n, peerCount;
    tr_ptrArray tmp = TR_PTR_ARRAY_INIT;
    struct tr_peerIo ** peers;
    const uint64_t now = tr_date( );
    const uint64_t cutoff = now + 100; /* 1/10th of a second */


    /* allocateBandwidth() is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    allocateBandwidth( b, dir, period_msec, &tmp );
    peers = (struct tr_peerIo**) tr_ptrArrayPeek( &tmp, &peerCount );

    /* Stop all peers from listening for the socket to be ready for IO.
     * See "Second phase of IO" lower in this function for more info. */
    for( i=0; i<peerCount; ++i )
        tr_peerIoSetEnabled( peers[i], dir, FALSE );

    /* First phase of IO.  Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others.  Loop through the peers, giving each a
     * small chunk of bandwidth.  Keep looping until we reach the cutoff or
     * run out of bandwidth and/or peers that can use it */
    n = peerCount;
    i = n ? tr_cryptoWeakRandInt( n ) : 0; /* pick a random starting point */
    for( ; n>0 && tr_date()<=cutoff; )
    {
        const int increment = n==1 ? 4096 : 1024;
        const int byteCount = tr_peerIoFlush( peers[i], dir, increment);

        if( byteCount == increment )
            ++i;
        else {
            /* peer is done writing for now; move it to the end of the list */
            tr_peerIo * tmp = peers[i];
            peers[i] = peers[n-1];
            peers[n-1] = tmp;
            --n;
        }

        assert( i <= n );
        if( i == n )
            i = 0;
    }

    /* Second phase of IO.  To help us scale in high bandwidth situations,
     * enable on-demand IO for peers with bandwidth left to burn.
     * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
     * or (2) the next tr_bandwidthAllocate() call, when we start over again. */
    for( i=0; i<peerCount; ++i )
        if( tr_peerIoHasBandwidthLeft( peers[i], dir ) )
            tr_peerIoSetEnabled( peers[i], dir, TRUE );

    /* cleanup */
    tr_ptrArrayDestruct( &tmp, NULL );
}

/***
****
***/

void
tr_bandwidthAddPeer( tr_bandwidth   * b,
                     tr_peerIo      * peerIo )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isPeerIo( peerIo ) );

    tr_ptrArrayInsertSorted( &b->peers, peerIo, comparePointers );
}

void
tr_bandwidthRemovePeer( tr_bandwidth  * b,
                        tr_peerIo     * peerIo )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isPeerIo( peerIo ) );

    tr_ptrArrayRemoveSorted( &b->peers, peerIo, comparePointers );
}

/***
****
***/

size_t
tr_bandwidthClamp( const tr_bandwidth  * b,
                   tr_direction          dir,
                   size_t                byteCount )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

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
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return getSpeed( &b->band[dir].raw, HISTORY_MSEC );
}

double
tr_bandwidthGetPieceSpeed( const tr_bandwidth * b, tr_direction dir )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return getSpeed( &b->band[dir].piece, HISTORY_MSEC );
}

void
tr_bandwidthUsed( tr_bandwidth  * b,
                  tr_direction    dir,
                  size_t          byteCount,
                  tr_bool         isPieceData )
{
    struct tr_band * band;
    size_t oldBytesLeft;

    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    band = &b->band[dir];

    oldBytesLeft = band->bytesLeft;

    if( band->isLimited && isPieceData )
        band->bytesLeft -= MIN( band->bytesLeft, byteCount );

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && ( band->isLimited ) )
fprintf( stderr, "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
         b, byteCount, (isPieceData?"piece":"raw"), oldBytesLeft, band->bytesLeft );
#endif

    bytesUsed( &band->raw, byteCount );

    if( isPieceData )
        bytesUsed( &band->piece, byteCount );

    if( b->parent != NULL )
        tr_bandwidthUsed( b->parent, dir, byteCount, isPieceData );
}
