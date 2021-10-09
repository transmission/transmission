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

void tr_bandwidth::notifyBandwidthConsumedBytes(uint64_t const now, struct bratecontrol* r, size_t size)
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

/***
****
***/

tr_bandwidth::tr_bandwidth(tr_bandwidth* newParent)
    : band{}
    , parent{ nullptr }
    , children{}
    , peer{ nullptr }
{
    this->children = {};
    this->band[TR_UP].honorParentLimits = true;
    this->band[TR_DOWN].honorParentLimits = true;
    this->setParent(newParent);
}

/***
****
***/

void tr_bandwidth::setParent(tr_bandwidth* newParent)
{
    TR_ASSERT(this != newParent);

    if (this->parent != nullptr)
    {
        this->parent->children.erase(this);
        this->parent = nullptr;
    }

    if (newParent != nullptr)
    {
        TR_ASSERT(newParent->parent != this);
        TR_ASSERT(newParent->children.find(this) == newParent->children.end()); // does not exist

        newParent->children.insert(this);
        this->parent = newParent;
    }
}

/***
****
***/

void tr_bandwidth::allocateBandwidth(
    tr_priority_t parent_priority,
    tr_direction dir,
    unsigned int period_msec,
    std::vector<tr_peerIo*>& peer_pool)
{
    TR_ASSERT(tr_isDirection(dir));

    tr_priority_t const priority_ = std::max(parent_priority, this->priority);

    /* set the available bandwidth */
    if (this->band[dir].isLimited)
    {
        uint64_t const nextPulseSpeed = this->band[dir].desiredSpeed_Bps;
        this->band[dir].bytesLeft = nextPulseSpeed * period_msec / 1000U;
    }

    /* add this bandwidth's peer, if any, to the peer pool */
    if (this->peer != nullptr)
    {
        this->peer->priority = priority_;
        peer_pool.push_back(this->peer);
    }

    // traverse & repeat for the subtree
    for (auto child : this->children)
    {
        child->allocateBandwidth(priority_, dir, period_msec, peer_pool);
    }
}

void tr_bandwidth::phaseOne(std::vector<tr_peerIo*>& peerArray, tr_direction dir)
{
    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    dbgmsg("%lu peers to go round-robin for %s", peerArray.size(), dir == TR_UP ? "upload" : "download");

    size_t n = peerArray.size();
    while (n > 0)
    {
        int const i = tr_rand_int_weak(n); /* pick a peer at random */

        /* value of 3000 bytes chosen so that when using uTP we'll send a full-size
         * frame right away and leave enough buffered data for the next frame to go
         * out in a timely manner. */
        size_t const increment = 3000;

        int const bytesUsed = tr_peerIoFlush(peerArray[i], dir, increment);

        dbgmsg("peer #%d of %zu used %d bytes in this pass", i, n, bytesUsed);

        if (bytesUsed != (int)increment)
        {
            /* peer is done writing for now; move it to the end of the list */
            std::swap(peerArray[i], peerArray[n - 1]);
            --n;
        }
    }
}

void tr_bandwidth::allocate(tr_direction dir, unsigned int period_msec)
{
    std::vector<tr_peerIo*> tmp;
    std::vector<tr_peerIo*> low;
    std::vector<tr_peerIo*> normal;
    std::vector<tr_peerIo*> high;

    /* allocateBandwidth () is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    this->allocateBandwidth(TR_PRI_LOW, dir, period_msec, tmp);

    for (auto io : tmp)
    {
        tr_peerIoRef(io);
        tr_peerIoFlushOutgoingProtocolMsgs(io);

        switch (io->priority)
        {
        case TR_PRI_HIGH:
            high.push_back(io);
            [[fallthrough]];

        case TR_PRI_NORMAL:
            normal.push_back(io);
            [[fallthrough]];

        default:
            low.push_back(io);
        }
    }

    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    phaseOne(high, dir);
    phaseOne(normal, dir);
    phaseOne(low, dir);

    /* Second phase of IO. To help us scale in high bandwidth situations,
     * enable on-demand IO for peers with bandwidth left to burn.
     * This on-demand IO is enabled until (1) the peer runs out of bandwidth,
     * or (2) the next tr_bandwidth::allocate () call, when we start over again. */
    for (auto io : tmp)
    {
        tr_peerIoSetEnabled(io, dir, tr_peerIoHasBandwidthLeft(io, dir));
    }

    for (auto io : tmp)
    {
        tr_peerIoUnref(io);
    }
}

/***
****
***/

unsigned int tr_bandwidth::clamp(uint64_t now, tr_direction dir, unsigned int byteCount) const
{
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
            desired = this->getDesiredSpeed_Bps(TR_DOWN);
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

void tr_bandwidth::notifyBandwidthConsumed(tr_direction dir, size_t byteCount, bool isPieceData, uint64_t now)
{
    TR_ASSERT(tr_isDirection(dir));

    struct tr_band* band_ = &this->band[dir];

    if (band_->isLimited && isPieceData)
    {
        band_->bytesLeft -= std::min(size_t{ band_->bytesLeft }, byteCount);
    }

#ifdef DEBUG_DIRECTION

    if (dir == DEBUG_DIRECTION && band_->isLimited)
    {
        fprintf(
            stderr,
            "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
            this,
            byteCount,
            isPieceData ? "piece" : "raw",
            oldBytesLeft,
            band_->bytesLeft);
    }

#endif

    notifyBandwidthConsumedBytes(now, &band_->raw, byteCount);

    if (isPieceData)
    {
        notifyBandwidthConsumedBytes(now, &band_->piece, byteCount);
    }

    if (this->parent != nullptr)
    {
        this->parent->notifyBandwidthConsumed(dir, byteCount, isPieceData, now);
    }
}
