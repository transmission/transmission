/*
 * This file Copyright (C) 2008-2009 Mnemosyne LLC
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

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

/***
****
***/

static float
getSpeed( const struct bratecontrol * r, int interval_msec, uint64_t now )
{
    uint64_t       bytes = 0;
    const uint64_t cutoff = (now?now:tr_date()) - interval_msec;
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
bytesUsed( const uint64_t now, struct bratecontrol * r, size_t size )
{
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

static TR_INLINE int
comparePointers( const void * a, const void * b )
{
    if( a != b )
        return a < b ? -1 : 1;

    return 0;
}

/***
****
***/

tr_bandwidth*
tr_bandwidthConstruct( tr_bandwidth * b, tr_session * session, tr_bandwidth * parent )
{
    b->session = session;
    b->children = TR_PTR_ARRAY_INIT;
    b->magicNumber = MAGIC_NUMBER;
    b->band[TR_UP].honorParentLimits = TRUE;
    b->band[TR_DOWN].honorParentLimits = TRUE;
    tr_bandwidthSetParent( b, parent );
    return b;
}

tr_bandwidth*
tr_bandwidthDestruct( tr_bandwidth * b )
{
    assert( tr_isBandwidth( b ) );

    tr_bandwidthSetParent( b, NULL );
    tr_ptrArrayDestruct( &b->children, NULL );

    memset( b, ~0, sizeof( tr_bandwidth ) );
    return b;
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
        void * removed;

        assert( tr_isBandwidth( b->parent ) );

        removed = tr_ptrArrayRemoveSorted( &b->parent->children, b, comparePointers );
        assert( removed == b );
        assert( tr_ptrArrayFindSorted( &b->parent->children, b, comparePointers ) == NULL );

        b->parent = NULL;
    }

    if( parent )
    {
        assert( tr_isBandwidth( parent ) );
        assert( parent->parent != b );

        tr_ptrArrayInsertSorted( &parent->children, b, comparePointers );
        assert( tr_ptrArrayFindSorted( &parent->children, b, comparePointers ) == b );
        b->parent = parent;
    }
}

/***
****
***/
#if 0
#warning do not check the code in with this enabled
#define DEBUG_DIRECTION TR_UP
#endif

static void
allocateBandwidth( tr_bandwidth  * b,
                   tr_priority_t   parent_priority,
                   tr_direction    dir,
                   int             period_msec,
                   tr_ptrArray   * peer_pool )
{
    tr_priority_t priority;

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

    priority = MAX( parent_priority, b->priority );

    /* add this bandwidth's peer, if any, to the peer pool */
    if( b->peer != NULL ) {
        b->peer->priority = priority;
        tr_ptrArrayAppend( peer_pool, b->peer );
    }

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && ( n > 1 ) )
fprintf( stderr, "bandwidth %p has %d peers\n", b, n );
#endif

    /* traverse & repeat for the subtree */
    if( 1 ) {
        int i;
        struct tr_bandwidth ** children = (struct tr_bandwidth**) tr_ptrArrayBase( &b->children );
        const int n = tr_ptrArraySize( &b->children );
        for( i=0; i<n; ++i )
            allocateBandwidth( children[i], priority, dir, period_msec, peer_pool );
    }
}

static void
phaseOne( tr_ptrArray * peerArray, tr_direction dir )
{
    int i, n;
    int peerCount = tr_ptrArraySize( peerArray );
    struct tr_peerIo ** peers = (struct tr_peerIo**) tr_ptrArrayBase( peerArray );

    /* First phase of IO.  Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others.  Loop through the peers, giving each a
     * small chunk of bandwidth.  Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    n = peerCount;
    dbgmsg( "%d peers to go round-robin for %s", n, (dir==TR_UP?"upload":"download") );
    i = n ? tr_cryptoWeakRandInt( n ) : 0; /* pick a random starting point */
    while( n > 1 )
    {
        const size_t increment = 1024;
        const int bytesUsed = tr_peerIoFlush( peers[i], dir, increment );

        dbgmsg( "peer #%d of %d used %d bytes in this pass", i, n, bytesUsed );

        if( bytesUsed == (int)increment )
            ++i;
        else {
            /* peer is done writing for now; move it to the end of the list */
            tr_peerIo * pio = peers[i];
            peers[i] = peers[n-1];
            peers[n-1] = pio;
            --n;
        }

        if( i == n )
            i = 0;
    }
}

void
tr_bandwidthAllocate( tr_bandwidth  * b,
                      tr_direction    dir,
                      int             period_msec )
{
    int i, peerCount;
    tr_ptrArray tmp = TR_PTR_ARRAY_INIT;
    tr_ptrArray low = TR_PTR_ARRAY_INIT;
    tr_ptrArray high = TR_PTR_ARRAY_INIT;
    tr_ptrArray normal = TR_PTR_ARRAY_INIT;
    struct tr_peerIo ** peers;

    /* allocateBandwidth() is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    allocateBandwidth( b, TR_PRI_LOW, dir, period_msec, &tmp );
    peers = (struct tr_peerIo**) tr_ptrArrayBase( &tmp );
    peerCount = tr_ptrArraySize( &tmp );

    for( i=0; i<peerCount; ++i )
    {
        tr_peerIo * io = peers[i];
        tr_peerIoRef( io );

        tr_peerIoFlushOutgoingProtocolMsgs( io );

        switch( io->priority ) {
            case TR_PRI_HIGH:   tr_ptrArrayAppend( &high,   io ); /* fall through */
            case TR_PRI_NORMAL: tr_ptrArrayAppend( &normal, io ); /* fall through */
            default:            tr_ptrArrayAppend( &low,    io );
        }
    }

    /* First phase of IO.  Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others.  Loop through the peers, giving each a
     * small chunk of bandwidth.  Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    phaseOne( &high, dir );
    phaseOne( &normal, dir );
    phaseOne( &low, dir );

    /* Second phase of IO.  To help us scale in high bandwidth situations,
     * enable on-demand IO for peers with bandwidth left to burn.
     * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
     * or (2) the next tr_bandwidthAllocate() call, when we start over again. */
    for( i=0; i<peerCount; ++i )
        tr_peerIoSetEnabled( peers[i], dir, tr_peerIoHasBandwidthLeft( peers[i], dir ) );

    for( i=0; i<peerCount; ++i )
        tr_peerIoUnref( peers[i] );

    /* cleanup */
    tr_ptrArrayDestruct( &normal, NULL );
    tr_ptrArrayDestruct( &high, NULL );
    tr_ptrArrayDestruct( &low, NULL );
    tr_ptrArrayDestruct( &tmp, NULL );
}

void
tr_bandwidthSetPeer( tr_bandwidth * b, tr_peerIo * peer )
{
    assert( tr_isBandwidth( b ) );
    assert( ( peer == NULL ) || tr_isPeerIo( peer ) );

    b->peer = peer;
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
tr_bandwidthGetRawSpeed( const tr_bandwidth * b, const uint64_t now, const tr_direction dir )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return getSpeed( &b->band[dir].raw, HISTORY_MSEC, now );
}

double
tr_bandwidthGetPieceSpeed( const tr_bandwidth * b, const uint64_t now, const tr_direction dir )
{
    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    return getSpeed( &b->band[dir].piece, HISTORY_MSEC, now );
}

static void
bandwidthUsedImpl( tr_bandwidth  * b,
                   tr_direction    dir,
                   size_t          byteCount,
                   tr_bool         isPieceData,
                   uint64_t        now )
{
    struct tr_band * band;

    assert( tr_isBandwidth( b ) );
    assert( tr_isDirection( dir ) );

    band = &b->band[dir];

    if( band->isLimited && isPieceData )
        band->bytesLeft -= MIN( band->bytesLeft, byteCount );

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && ( band->isLimited ) )
fprintf( stderr, "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
         b, byteCount, (isPieceData?"piece":"raw"), oldBytesLeft, band->bytesLeft );
#endif

    bytesUsed( now, &band->raw, byteCount );

    if( isPieceData )
        bytesUsed( now, &band->piece, byteCount );

    if( b->parent != NULL )
        bandwidthUsedImpl( b->parent, dir, byteCount, isPieceData, now );
}

void
tr_bandwidthUsed( tr_bandwidth  * b,
                  tr_direction    dir,
                  size_t          byteCount,
                  tr_bool         isPieceData )
{
    bandwidthUsedImpl( b, dir, byteCount, isPieceData, tr_date( ) );
}
