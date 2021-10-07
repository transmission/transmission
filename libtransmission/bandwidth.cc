/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstring> /* memset() */

#include "transmission.h"
#include "bandwidth.h"
#include "crypto-utils.h" /* tr_rand_int_weak() */
#include "log.h"
#include "peer-io.h"
#include "tr-assert.h"
#include "utils.h"

#define dbgmsg(...) tr_logAddDeepNamed(nullptr, __VA_ARGS__)

/***
****
***/

unsigned int tr_bandwidth::getSpeed_Bps(struct bratecontrol const* r, unsigned int interval_msec, uint64_t now)
{
    if (now == 0)
    {
        now = tr_time_msec();
    }

    if (now != r->cache_time)
    {
        uint64_t bytes = 0;
        uint64_t const cutoff = now - interval_msec;
        auto* rvolatile = (struct bratecontrol*)r;

        for (int i = r->newest; r->transfers[i].date > cutoff;)
        {
            bytes += r->transfers[i].size;

            if (--i == -1)
            {
                i = HISTORY_SIZE - 1; /* circular history */
            }

            if (i == r->newest)
            {
                break; /* we've come all the way around */
            }
        }

        rvolatile->cache_val = (unsigned int)(bytes * 1000U / interval_msec);
        rvolatile->cache_time = now;
    }

    return r->cache_val;
}

void tr_bandwidth::bytesUsed(uint64_t const now, struct bratecontrol* r, size_t size)
{
    if (r->transfers[r->newest].date + GRANULARITY_MSEC >= now)
    {
        r->transfers[r->newest].size += size;
    }
    else
    {
        if (++r->newest == HISTORY_SIZE)
        {
            r->newest = 0;
        }

        r->transfers[r->newest].date = now;
        r->transfers[r->newest].size = size;
    }

    /* invalidate cache_val*/
    r->cache_time = 0;
}

/******
*******
*******
******/

int tr_bandwidth::compareBandwidth(void const* va, void const* vb)
{
    auto const* a = static_cast<tr_bandwidth const*>(va);
    auto const* b = static_cast<tr_bandwidth const*>(vb);
    return a->uniqueKey - b->uniqueKey;
}

/***
****
***/

void tr_bandwidth::construct(tr_bandwidth* parent)
{
    static unsigned int uniqueKey = 0;

    this->children = {};
    this->magicNumber = BANDWIDTH_MAGIC_NUMBER;
    this->uniqueKey = uniqueKey++;
    this->band[TR_UP].honorParentLimits = true;
    this->band[TR_DOWN].honorParentLimits = true;
    this->setParent(parent);
}

void tr_bandwidth::destruct()
{
    TR_ASSERT(tr_isBandwidth(this));

    this->setParent(nullptr);
    tr_ptrArrayDestruct(&this->children, nullptr);

    memset(this, ~0, sizeof(tr_bandwidth));
}

/***
****
***/

void tr_bandwidth::setParent(tr_bandwidth* parent)
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(this != parent);

    if (this->parent != nullptr)
    {
        TR_ASSERT(tr_isBandwidth(this->parent));
        tr_ptrArrayRemoveSortedPointer(&this->parent->children, this, compareBandwidth);
        this->parent = nullptr;
    }

    if (parent != nullptr)
    {
        TR_ASSERT(tr_isBandwidth(parent));
        TR_ASSERT(parent->parent != this);

        TR_ASSERT(tr_ptrArrayFindSorted(&parent->children, this, compareBandwidth) == nullptr);
        tr_ptrArrayInsertSorted(&parent->children, this, compareBandwidth);
        TR_ASSERT(tr_ptrArrayFindSorted(&parent->children, this, compareBandwidth) == this);
        this->parent = parent;
    }
}

/***
****
***/

void tr_bandwidth::allocateBandwidth(
    tr_priority_t parent_priority,
    tr_direction dir,
    unsigned int period_msec,
    tr_ptrArray* peer_pool)
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(tr_isDirection(dir));

    tr_priority_t const priority = std::max(parent_priority, this->priority);

    /* set the available bandwidth */
    if (this->band[dir].isLimited)
    {
        uint64_t const nextPulseSpeed = this->band[dir].desiredSpeed_Bps;
        this->band[dir].bytesLeft = nextPulseSpeed * period_msec / 1000U;
    }

    /* add this bandwidth's peer, if any, to the peer pool */
    if (this->peer != nullptr)
    {
        this->peer->priority = priority;
        tr_ptrArrayAppend(peer_pool, this->peer);
    }

    /* traverse & repeat for the subtree
     * TODO: Replace with std::for_each over std::vector */
    auto** children = (struct tr_bandwidth**)tr_ptrArrayBase(&this->children);
    struct tr_bandwidth** const end = children + tr_ptrArraySize(&this->children);
    for (; children != end; ++children)
    {
        (*children)->allocateBandwidth(priority, dir, period_msec, peer_pool);
    }
}

void tr_bandwidth::phaseOne(tr_ptrArray const* peerArray, tr_direction dir)
{
    int n;
    int peerCount = tr_ptrArraySize(peerArray);
    auto** peers = (struct tr_peerIo**)tr_ptrArrayBase(peerArray);

    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    n = peerCount;
    dbgmsg("%d peers to go round-robin for %s", n, dir == TR_UP ? "upload" : "download");

    while (n > 0)
    {
        int const i = tr_rand_int_weak(n); /* pick a peer at random */

        /* value of 3000 bytes chosen so that when using uTP we'll send a full-size
         * frame right away and leave enough buffered data for the next frame to go
         * out in a timely manner. */
        size_t const increment = 3000;

        int const bytesUsed = tr_peerIoFlush(peers[i], dir, increment);

        dbgmsg("peer #%d of %d used %d bytes in this pass", i, n, bytesUsed);

        if (bytesUsed != (int)increment)
        {
            /* peer is done writing for now; move it to the end of the list */
            tr_peerIo* pio = peers[i];
            peers[i] = peers[n - 1];
            peers[n - 1] = pio;
            --n;
        }
    }
}

void tr_bandwidth::allocate(tr_direction dir, unsigned int period_msec)
{
    int peerCount;
    auto tmp = tr_ptrArray{};
    auto low = tr_ptrArray{};
    auto high = tr_ptrArray{};
    auto normal = tr_ptrArray{};
    struct tr_peerIo** peers;

    /* allocateBandwidth () is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    this->allocateBandwidth(TR_PRI_LOW, dir, period_msec, &tmp);
    peers = (struct tr_peerIo**)tr_ptrArrayBase(&tmp);
    peerCount = tr_ptrArraySize(&tmp);

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIo* io = peers[i];
        tr_peerIoRef(io);

        tr_peerIoFlushOutgoingProtocolMsgs(io);

        switch (io->priority)
        {
        case TR_PRI_HIGH:
            tr_ptrArrayAppend(&high, io);
            [[fallthrough]];

        case TR_PRI_NORMAL:
            tr_ptrArrayAppend(&normal, io);
            [[fallthrough]];

        default:
            tr_ptrArrayAppend(&low, io);
        }
    }

    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    phaseOne(&high, dir);
    phaseOne(&normal, dir);
    phaseOne(&low, dir);

    /* Second phase of IO. To help us scale in high bandwidth situations,
     * enable on-demand IO for peers with bandwidth left to burn.
     * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
     * or (2) the next tr_bandwidthAllocate () call, when we start over again. */
    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIoSetEnabled(peers[i], dir, tr_peerIoHasBandwidthLeft(peers[i], dir));
    }

    for (int i = 0; i < peerCount; ++i)
    {
        tr_peerIoUnref(peers[i]);
    }

    /* cleanup */
    tr_ptrArrayDestruct(&normal, nullptr);
    tr_ptrArrayDestruct(&high, nullptr);
    tr_ptrArrayDestruct(&low, nullptr);
    tr_ptrArrayDestruct(&tmp, nullptr);
}

void tr_bandwidth::setPeer(tr_peerIo* peer)
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(peer == nullptr || tr_isPeerIo(peer));

    this->peer = peer;
}

/***
****
***/

unsigned int tr_bandwidth::clamp(uint64_t now, tr_direction dir, unsigned int byteCount) const
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(tr_isDirection(dir));

    if (this->band[dir].isLimited)
    {
        byteCount = std::min(byteCount, this->band[dir].bytesLeft);

        /* if we're getting close to exceeding the speed limit,
         * clamp down harder on the bytes available */
        if (byteCount > 0)
        {
            double current;
            double desired;
            double r;

            if (now == 0)
            {
                now = tr_time_msec();
            }

            current = this->getRawSpeed_Bps(now, TR_DOWN);
            desired = tr_bandwidthGetDesiredSpeed_Bps(this, TR_DOWN);
            r = desired >= 1 ? current / desired : 0;

            if (r > 1.0)
            {
                byteCount = 0;
            }
            else if (r > 0.9)
            {
                byteCount = static_cast<unsigned int>(byteCount * 0.8);
            }
            else if (r > 0.8)
            {
                byteCount = static_cast<unsigned int>(byteCount * 0.9);
            }
        }
    }

    if (this->parent != nullptr && this->band[dir].honorParentLimits && byteCount > 0)
    {
        byteCount = this->parent->clamp(now, dir, byteCount);
    }

    return byteCount;
}

unsigned int tr_bandwidth::clamp(tr_direction dir, unsigned int byteCount) const
{
    return this->clamp(0, dir, byteCount);
}

unsigned int tr_bandwidth::getRawSpeed_Bps(uint64_t const now, tr_direction const dir) const
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(tr_isDirection(dir));

    return getSpeed_Bps(&this->band[dir].raw, HISTORY_MSEC, now);
}

unsigned int tr_bandwidth::getPieceSpeed_Bps(uint64_t const now, tr_direction const dir) const
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(tr_isDirection(dir));

    return getSpeed_Bps(&this->band[dir].piece, HISTORY_MSEC, now);
}

void tr_bandwidth::used(tr_direction dir, size_t byteCount, bool isPieceData, uint64_t now)
{
    TR_ASSERT(tr_isBandwidth(this));
    TR_ASSERT(tr_isDirection(dir));

    struct tr_band* band = &this->band[dir];

    if (band->isLimited && isPieceData)
    {
        band->bytesLeft -= std::min(size_t{ band->bytesLeft }, byteCount);
    }

#ifdef DEBUG_DIRECTION

    if (dir == DEBUG_DIRECTION && band->isLimited)
    {
        fprintf(
            stderr,
            "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
            this,
            byteCount,
            isPieceData ? "piece" : "raw",
            oldBytesLeft,
            band->bytesLeft);
    }

#endif

    bytesUsed(now, &band->raw, byteCount);

    if (isPieceData)
    {
        bytesUsed(now, &band->piece, byteCount);
    }

    if (this->parent != nullptr)
    {
        this->parent->used(dir, byteCount, isPieceData, now);
    }
}
