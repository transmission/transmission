/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
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

#ifndef TR_BANDWIDTH_H
#define TR_BANDWIDTH_H

#include "transmission.h"
#include "ptrarray.h"
#include "utils.h" /* tr_new(), tr_free() */

struct tr_peerIo;

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
enum
{
    HISTORY_MSEC = 2000,
    INTERVAL_MSEC = HISTORY_MSEC,
    GRANULARITY_MSEC = 50,
    HISTORY_SIZE = ( INTERVAL_MSEC / GRANULARITY_MSEC ),
    MAGIC_NUMBER = 43143
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
struct bratecontrol
{
    int newest;
    struct { uint64_t date, size; } transfers[HISTORY_SIZE];
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
struct tr_band
{
    tr_bool isLimited;
    tr_bool honorParentLimits;
    size_t bytesLeft;
    double desiredSpeed;
    struct bratecontrol raw;
    struct bratecontrol piece;
};

/**
 * Bandwidth is an object for measuring and constraining bandwidth speeds.
 *
 * Bandwidth objects can be "stacked" so that a peer can be made to obey
 * multiple constraints (for example, obeying the global speed limit and a
 * per-torrent speed limit).
 *
 * HIERARCHY
 *
 *   Transmission's bandwidth hierarchy is a tree.
 *   At the top is the global bandwidth object owned by tr_session.
 *   Its children are per-torrent bandwidth objects owned by tr_torrent.
 *   Underneath those are per-peer bandwidth objects owned by tr_peer.
 *
 *   tr_session also owns a tr_handshake's bandwidths, so that the handshake
 *   I/O can be counted in the global raw totals.  When the handshake is done,
 *   the bandwidth's ownership passes to a tr_peer.
 * 
 * MEASURING
 *
 *   When you ask a bandwidth object for its speed, it gives the speed of the
 *   subtree underneath it as well.  So you can get Transmission's overall
 *   speed by quering tr_session's bandwidth, per-torrent speeds by asking
 *   tr_torrent's bandwidth, and per-peer speeds by asking tr_peer's bandwidth.
 *
 * CONSTRAINING
 * 
 *   Call tr_bandwidthAllocate() periodically.  tr_bandwidth knows its current
 *   speed and will decide how many bytes to make available over the
 *   user-specified period to reach the user-specified desired speed.
 *   If appropriate, it notifies its peer-ios that new bandwidth is available.
 * 
 *   tr_bandwidthAllocate() operates on the tr_bandwidth subtree, so usually 
 *   you'll only need to invoke it for the top-level tr_session bandwidth.
 *
 *   The peer-ios all have a pointer to their associated tr_bandwidth object,
 *   and call tr_bandwidthClamp() before performing I/O to see how much 
 *   bandwidth they can safely use.
 */
typedef struct tr_bandwidth
{
    /* these are PRIVATE IMPLEMENTATION details that should not be touched.
     * it's included in the header for inlining and composition. */

    struct tr_band band[2];
    struct tr_bandwidth * parent;
    int magicNumber;
    tr_session * session;
    tr_ptrArray children; /* struct tr_bandwidth */
    struct tr_peerIo * peer;
}
tr_bandwidth;


/**
***
**/

tr_bandwidth* tr_bandwidthConstruct( tr_bandwidth * bandwidth,
                                     tr_session   * session,
                                     tr_bandwidth * parent );

/** @brief create a new tr_bandwidth object */
static TR_INLINE tr_bandwidth* tr_bandwidthNew( tr_session * session, tr_bandwidth * parent )
{
    return tr_bandwidthConstruct( tr_new0( tr_bandwidth, 1 ), session, parent );
}

tr_bandwidth* tr_bandwidthDestruct( tr_bandwidth * bandwidth );

/** @brief free a tr_bandwidth object */
static TR_INLINE void tr_bandwidthFree( tr_bandwidth * bandwidth )
{
    tr_free( tr_bandwidthDestruct( bandwidth ) );
}

/** @brief test to see if the pointer refers to a live bandwidth object */
static TR_INLINE tr_bool tr_isBandwidth( const tr_bandwidth  * b )
{
    return ( b != NULL ) && ( b->magicNumber == MAGIC_NUMBER );
}

/******
*******
******/

/**
 * @brief Set the desired speed (in KiB/s) for this bandwidth subtree.
 * @see tr_bandwidthAllocate
 * @see tr_bandwidthGetDesiredSpeed
 */
static TR_INLINE void tr_bandwidthSetDesiredSpeed( tr_bandwidth        * bandwidth,
                                                tr_direction          dir,
                                                double                desiredSpeed )
{
    bandwidth->band[dir].desiredSpeed = desiredSpeed;
}

/**
 * @brief Get the desired speed (in KiB/s) for ths bandwidth subtree.
 * @see tr_bandwidthSetDesiredSpeed
 */
static TR_INLINE double
tr_bandwidthGetDesiredSpeed( const tr_bandwidth  * bandwidth,
                             tr_direction          dir )
{
    return bandwidth->band[dir].desiredSpeed;
}

/**
 * @brief Set whether or not this bandwidth should throttle its peer-io's speeds
 */
static TR_INLINE void tr_bandwidthSetLimited( tr_bandwidth        * bandwidth,
                                              tr_direction          dir,
                                              tr_bool               isLimited )
{
    bandwidth->band[dir].isLimited = isLimited;
}

/**
 * @return nonzero if this bandwidth throttles its peer-ios speeds
 */
static TR_INLINE tr_bool tr_bandwidthIsLimited( const tr_bandwidth  * bandwidth,
                                                tr_direction          dir )
{
    return bandwidth->band[dir].isLimited;
}

/**
 * @brief allocate the next period_msec's worth of bandwidth for the peer-ios to consume
 */
void    tr_bandwidthAllocate          ( tr_bandwidth        * bandwidth,
                                        tr_direction          direction,
                                        int                   period_msec );

/**
 * @brief clamps byteCount down to a number that this bandwidth will allow to be consumed
 */
size_t  tr_bandwidthClamp             ( const tr_bandwidth  * bandwidth,
                                        tr_direction          direction,
                                        size_t                byteCount );

/******
*******
******/

/** @brief Get the raw total of bytes read or sent by this bandwidth subtree. */
double tr_bandwidthGetRawSpeed( const tr_bandwidth  * bandwidth,
                                const uint64_t        now,
                                const tr_direction    direction );

/** @brief Get the number of piece data bytes read or sent by this bandwidth subtree. */
double tr_bandwidthGetPieceSpeed( const tr_bandwidth  * bandwidth,
                                  const uint64_t        now,
                                  const tr_direction    direction );

/**
 * @brief Notify the bandwidth object that some of its allocated bandwidth has been consumed.
 * This is is usually invoked by the peer-io after a read or write.
 */
void    tr_bandwidthUsed              ( tr_bandwidth        * bandwidth,
                                        tr_direction          direction,
                                        size_t                byteCount,
                                        tr_bool               isPieceData );

/******
*******
******/

void    tr_bandwidthSetParent         ( tr_bandwidth        * bandwidth,
                                        tr_bandwidth        * parent );

/**
 * Almost all the time we do want to honor a parents' bandwidth cap, so that
 * (for example) a peer is constrained by a per-torrent cap and the global cap.
 * But when we set a torrent's speed mode to TR_SPEEDLIMIT_UNLIMITED, then
 * in that particular case we want to ignore the global speed limit...
 */
static TR_INLINE void tr_bandwidthHonorParentLimits ( tr_bandwidth        * bandwidth,
                                                      tr_direction          direction,
                                                      tr_bool               isEnabled )
{
    assert( tr_isBandwidth( bandwidth ) );
    assert( tr_isDirection( direction ) );

    bandwidth->band[direction].honorParentLimits = isEnabled;
}

/******
*******
******/

void tr_bandwidthSetPeer( tr_bandwidth        * bandwidth,
                          struct tr_peerIo    * peerIo );

#endif
