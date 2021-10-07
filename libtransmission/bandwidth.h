/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"
#include "ptrarray.h"
#include "tr-assert.h"
#include "utils.h" /* tr_new(), tr_free() */

struct tr_peerIo;

/**
 * @addtogroup networked_io Networked IO
 * @{
 */

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
enum
{
    HISTORY_MSEC = 2000U,
    INTERVAL_MSEC = HISTORY_MSEC,
    GRANULARITY_MSEC = 200,
    HISTORY_SIZE = (INTERVAL_MSEC / GRANULARITY_MSEC),
    BANDWIDTH_MAGIC_NUMBER = 43143
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
struct bratecontrol
{
    int newest;
    struct
    {
        uint64_t date;
        uint64_t size;
    } transfers[HISTORY_SIZE];
    uint64_t cache_time;
    unsigned int cache_val;
};

/* these are PRIVATE IMPLEMENTATION details that should not be touched.
 * it's included in the header for inlining and composition. */
struct tr_band
{
    bool isLimited;
    bool honorParentLimits;
    unsigned int bytesLeft;
    unsigned int desiredSpeed_Bps;
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
 *   I/O can be counted in the global raw totals. When the handshake is done,
 *   the bandwidth's ownership passes to a tr_peer.
 *
 * MEASURING
 *
 *   When you ask a bandwidth object for its speed, it gives the speed of the
 *   subtree underneath it as well. So you can get Transmission's overall
 *   speed by quering tr_session's bandwidth, per-torrent speeds by asking
 *   tr_torrent's bandwidth, and per-peer speeds by asking tr_peer's bandwidth.
 *
 * CONSTRAINING
 *
 *   Call tr_bandwidthAllocate() periodically. tr_bandwidth knows its current
 *   speed and will decide how many bytes to make available over the
 *   user-specified period to reach the user-specified desired speed.
 *   If appropriate, it notifies its peer-ios that new bandwidth is available.
 *
 *   tr_bandwidthAllocate() operates on the tr_bandwidth subtree, so usually
 *   you'll only need to invoke it for the top-level tr_session bandwidth.
 *
 *   The peer-ios all have a pointer to their associated tr_bandwidth object,
 *   and call tr_bandwidth::clamp() before performing I/O to see how much
 *   bandwidth they can safely use.
 */
struct tr_bandwidth
{
    /* these are PRIVATE IMPLEMENTATION details that should not be touched.
     * it's included in the header for inlining and composition. */

    struct tr_band band[2];
    struct tr_bandwidth* parent;
    tr_priority_t priority;
    int magicNumber;
    unsigned int uniqueKey;
    tr_ptrArray children; /* struct tr_bandwidth, TODO: replace with std::vector<tr_bandwidth> */
    struct tr_peerIo* peer;

    /**
     * @brief clamps byteCount down to a number that this bandwidth will allow to be consumed
    */
    [[nodiscard]] unsigned int clamp(tr_direction dir, unsigned int byteCount) const;

    void setPeer(tr_peerIo* peer);

    /** @brief Get the raw total of bytes read or sent by this bandwidth subtree. */
    unsigned int getRawSpeed_Bps(uint64_t const now, tr_direction const dir) const;

    /** @brief Get the number of piece data bytes read or sent by this bandwidth subtree. */
    unsigned int getPieceSpeed_Bps(uint64_t const now, tr_direction const dir) const;

    /**
     * @brief Notify the bandwidth object that some of its allocated bandwidth has been consumed.
     * This is is usually invoked by the peer-io after a read or write.
     */
    void used(tr_direction dir, size_t byteCount, bool isPieceData, uint64_t now);

    /**
     * @brief allocate the next period_msec's worth of bandwidth for the peer-ios to consume
     */
    void allocate(tr_direction dir, unsigned int period_msec);

    void setParent(tr_bandwidth* parent);

    void construct(tr_bandwidth* parent);
    void destruct();

private:
    static unsigned int getSpeed_Bps(struct bratecontrol const* r, unsigned int interval_msec, uint64_t now);
    static void bytesUsed(uint64_t const now, struct bratecontrol* r, size_t size);
    [[nodiscard]] unsigned int clamp(uint64_t now, tr_direction dir, unsigned int byteCount) const;
    static void phaseOne(tr_ptrArray const* peerArray, tr_direction dir);
    void allocateBandwidth(tr_priority_t parent_priority, tr_direction dir, unsigned int period_msec, tr_ptrArray* peer_pool);
    static int compareBandwidth(void const* va, void const* vb);
};

/**
***
**/

void tr_bandwidthConstruct(tr_bandwidth* bandwidth, tr_bandwidth* parent);

void tr_bandwidthDestruct(tr_bandwidth* bandwidth);

/** @brief test to see if the pointer refers to a live bandwidth object */
constexpr bool tr_isBandwidth(tr_bandwidth const* b)
{
    return b != nullptr && b->magicNumber == BANDWIDTH_MAGIC_NUMBER;
}

/******
*******
******/

/**
 * @brief Set the desired speed for this bandwidth subtree.
 * @see tr_bandwidthAllocate
 * @see tr_bandwidthGetDesiredSpeed
 */
constexpr bool tr_bandwidthSetDesiredSpeed_Bps(tr_bandwidth* bandwidth, tr_direction dir, unsigned int desiredSpeed)
{
    unsigned int* value = &bandwidth->band[dir].desiredSpeed_Bps;
    bool const didChange = desiredSpeed != *value;
    *value = desiredSpeed;
    return didChange;
}

/**
 * @brief Get the desired speed for the bandwidth subtree.
 * @see tr_bandwidthSetDesiredSpeed
 */
constexpr double tr_bandwidthGetDesiredSpeed_Bps(tr_bandwidth const* bandwidth, tr_direction dir)
{
    return bandwidth->band[dir].desiredSpeed_Bps;
}

/**
 * @brief Set whether or not this bandwidth should throttle its peer-io's speeds
 */
constexpr bool tr_bandwidthSetLimited(tr_bandwidth* bandwidth, tr_direction dir, bool isLimited)
{
    bool* value = &bandwidth->band[dir].isLimited;
    bool const didChange = isLimited != *value;
    *value = isLimited;
    return didChange;
}

/**
 * @return nonzero if this bandwidth throttles its peer-ios speeds
 */
constexpr bool tr_bandwidthIsLimited(tr_bandwidth const* bandwidth, tr_direction dir)
{
    return bandwidth->band[dir].isLimited;
}

/******
*******
******/

void tr_bandwidthSetParent(tr_bandwidth* bandwidth, tr_bandwidth* parent);

/**
 * Almost all the time we do want to honor a parents' bandwidth cap, so that
 * (for example) a peer is constrained by a per-torrent cap and the global cap.
 * But when we set a torrent's speed mode to TR_SPEEDLIMIT_UNLIMITED, then
 * in that particular case we want to ignore the global speed limit...
 */
constexpr bool tr_bandwidthHonorParentLimits(tr_bandwidth* bandwidth, tr_direction direction, bool isEnabled)
{
    bool* value = &bandwidth->band[direction].honorParentLimits;
    bool const didChange = isEnabled != *value;
    *value = isEnabled;
    return didChange;
}

constexpr bool tr_bandwidthAreParentLimitsHonored(tr_bandwidth const* bandwidth, tr_direction direction)
{
    TR_ASSERT(tr_isBandwidth(bandwidth));
    TR_ASSERT(tr_isDirection(direction));

    return bandwidth->band[direction].honorParentLimits;
}

/* @} */
