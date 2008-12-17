/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
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
    tr_ptrArray * children; /* struct tr_bandwidth */
    tr_ptrArray * peers; /* tr_peerIo */
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
    b->peers = tr_ptrArrayNew( );
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
    tr_ptrArrayFree( b->peers, NULL );
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
#warning do not check the code in with this enabled
#define DEBUG_DIRECTION TR_UP
#endif

static void
allocateBandwidth( tr_bandwidth  * b,
                   tr_direction    dir,
                   int             period_msec,
                   tr_ptrArray   * peer_pool )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

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

    {
        int i;
        const int n = tr_ptrArraySize( b->peers );
        for( i=0; i<n; ++i )
            tr_ptrArrayAppend( peer_pool, tr_ptrArrayNth( b->peers, i ) );
    }

#ifdef DEBUG_DIRECTION
if( ( dir == DEBUG_DIRECTION ) && ( n > 1 ) )
fprintf( stderr, "bandwidth %p has %d peers\n", b, n );
#endif

    /* all children should reallocate too */
    if( 1 ) {
        int i, n=0;
        struct tr_bandwidth ** children = (struct tr_bandwidth**) tr_ptrArrayPeek( b->children, &n );
        for( i=0; i<n; ++i )
            allocateBandwidth( children[i], dir, period_msec, peer_pool );
    }
}

void
tr_bandwidthAllocate( tr_bandwidth  * b,
                      tr_direction    dir,
                      int             period_msec )
{
    int n;
    tr_ptrArray * tmp;
    struct tr_peerIo ** peers;

    tmp = tr_ptrArrayNew( );
    allocateBandwidth( b, dir, period_msec, tmp );
    peers = (struct tr_peerIo**) tr_ptrArrayPeek( tmp, &n );

    /* loop through all the peers, reading and writing in small chunks,
     * until we run out of bandwidth or peers. we do it this way to 
     * prevent one peer from using up all the bandwidth */
fprintf( stderr, "%s - %d peers\n", (dir==TR_UP)?"up":"down", n );
    while( n > 0 )
    {
        int i;
        for( i=0; i<n; )
        {
            const int increment = n==1 ? 4096 : 1024;
            const int byteCount = tr_peerIoFlush( peers[i], dir, increment);

            if( byteCount )
                fprintf( stderr, "peer %p: %d bytes\n", peers[i], byteCount );

            if( byteCount == increment )
                ++i;
            else
                peers[i] = peers[--n];
        }
    }

    /* cleanup */
    tr_ptrArrayFree( tmp, NULL );
}

/***
****
***/

void
tr_bandwidthAddPeer( tr_bandwidth   * b,
                     tr_peerIo      * peerIo )
{
    assert( isBandwidth( b ) );
    assert( peerIo );

    tr_ptrArrayInsertSorted( b->peers, peerIo, comparePointers );
}

void
tr_bandwidthRemovePeer( tr_bandwidth  * b,
                        tr_peerIo     * peerIo )
{
    assert( isBandwidth( b ) );
    assert( peerIo );

    tr_ptrArrayRemoveSorted( b->peers, peerIo, comparePointers );
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

    return getSpeed( &b->band[dir].raw, HISTORY_MSEC );
}

double
tr_bandwidthGetPieceSpeed( const tr_bandwidth * b, tr_direction dir )
{
    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

    return getSpeed( &b->band[dir].piece, HISTORY_MSEC );
}

void
tr_bandwidthUsed( tr_bandwidth  * b,
                  tr_direction    dir,
                  size_t          byteCount,
                  int             isPieceData )
{
    struct tr_band * band;
    size_t oldBytesLeft;

    assert( isBandwidth( b ) );
    assert( isDirection( dir ) );

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
