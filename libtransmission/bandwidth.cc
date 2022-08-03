// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <utility> // std::swap()
#include <vector>

#include <fmt/core.h>

#include "transmission.h"

#include "bandwidth.h"
#include "crypto-utils.h" // tr_rand_int_weak()
#include "log.h"
#include "peer-io.h"
#include "tr-assert.h"
#include "utils.h" // tr_time_msec()

/***
****
***/

unsigned int tr_bandwidth::getSpeedBytesPerSecond(RateControl& r, unsigned int interval_msec, uint64_t now)
{
    if (now == 0)
    {
        now = tr_time_msec();
    }

    if (now != r.cache_time_)
    {
        uint64_t bytes = 0;
        uint64_t const cutoff = now - interval_msec;

        for (int i = r.newest_; r.date_[i] > cutoff;)
        {
            bytes += r.size_[i];

            if (--i == -1)
            {
                i = HistorySize - 1; /* circular history */
            }

            if (i == r.newest_)
            {
                break; /* we've come all the way around */
            }
        }

        r.cache_val_ = unsigned(bytes * 1000U / interval_msec);
        r.cache_time_ = now;
    }

    return r.cache_val_;
}

void tr_bandwidth::notifyBandwidthConsumedBytes(uint64_t const now, RateControl* r, size_t size)
{
    if (r->date_[r->newest_] + GranularityMSec >= now)
    {
        r->size_[r->newest_] += size;
    }
    else
    {
        if (++r->newest_ == HistorySize)
        {
            r->newest_ = 0;
        }

        r->date_[r->newest_] = now;
        r->size_[r->newest_] = size;
    }

    /* invalidate cache_val*/
    r->cache_time_ = 0;
}

/***
****
***/

tr_bandwidth::tr_bandwidth(tr_bandwidth* parent)
{
    this->setParent(parent);
}

/***
****
***/

static void remove_child(std::vector<tr_bandwidth*>& v, tr_bandwidth* remove_me)
{
    auto it = std::find(std::begin(v), std::end(v), remove_me);
    if (it == std::end(v))
    {
        return;
    }

    // the list isn't sorted -- so instead of erase()ing `it`,
    // do the cheaper option of overwriting it with the final item
    *it = v.back();
    v.resize(v.size() - 1);
}

void tr_bandwidth::setParent(tr_bandwidth* new_parent)
{
    TR_ASSERT(this != new_parent);

    if (this->parent_ != nullptr)
    {
        remove_child(this->parent_->children_, this);
        this->parent_ = nullptr;
    }

    if (new_parent != nullptr)
    {
#ifdef TR_ENABLE_ASSERTS
        TR_ASSERT(new_parent->parent_ != this);
        auto& children = new_parent->children_;
        TR_ASSERT(std::find(std::begin(children), std::end(children), this) == std::end(children)); // not already there
#endif

        new_parent->children_.push_back(this);
        this->parent_ = new_parent;
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
    tr_priority_t const priority = std::max(parent_priority, this->priority_);

    /* set the available bandwidth */
    if (this->band_[dir].is_limited_)
    {
        uint64_t const next_pulse_speed = this->band_[dir].desired_speed_bps_;
        this->band_[dir].bytes_left_ = next_pulse_speed * period_msec / 1000U;
    }

    /* add this bandwidth's peer, if any, to the peer pool */
    if (this->peer_ != nullptr)
    {
        this->peer_->priority = priority;
        peer_pool.push_back(this->peer_);
    }

    // traverse & repeat for the subtree
    for (auto* child : this->children_)
    {
        child->allocateBandwidth(priority, dir, period_msec, peer_pool);
    }
}

void tr_bandwidth::phaseOne(std::vector<tr_peerIo*>& peer_array, tr_direction dir)
{
    /* First phase of IO. Tries to distribute bandwidth fairly to keep faster
     * peers from starving the others. Loop through the peers, giving each a
     * small chunk of bandwidth. Keep looping until we run out of bandwidth
     * and/or peers that can use it */
    tr_logAddTrace(fmt::format("{} peers to go round-robin for {}", peer_array.size(), dir == TR_UP ? "upload" : "download"));

    auto n = peer_array.size();
    while (n > 0)
    {
        int const i = tr_rand_int_weak(n); /* pick a peer at random */

        /* value of 3000 bytes chosen so that when using uTP we'll send a full-size
         * frame right away and leave enough buffered data for the next frame to go
         * out in a timely manner. */
        size_t const increment = 3000;

        int const bytes_used = tr_peerIoFlush(peer_array[i], dir, increment);

        tr_logAddTrace(fmt::format("peer #{} of {} used {} bytes in this pass", i, n, bytes_used));

        if (bytes_used != int(increment))
        {
            /* peer is done writing for now; move it to the end of the list */
            std::swap(peer_array[i], peer_array[n - 1]);
            --n;
        }
    }
}

void tr_bandwidth::allocate(tr_direction dir, unsigned int period_msec)
{
    TR_ASSERT(tr_isDirection(dir));

    auto high = std::vector<tr_peerIo*>{};
    auto low = std::vector<tr_peerIo*>{};
    auto normal = std::vector<tr_peerIo*>{};
    auto tmp = std::vector<tr_peerIo*>{};

    /* allocateBandwidth () is a helper function with two purposes:
     * 1. allocate bandwidth to b and its subtree
     * 2. accumulate an array of all the peerIos from b and its subtree. */
    this->allocateBandwidth(TR_PRI_LOW, dir, period_msec, tmp);

    for (auto* io : tmp)
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
    for (auto* io : tmp)
    {
        tr_peerIoSetEnabled(io, dir, io->hasBandwidthLeft(dir));
    }

    for (auto* io : tmp)
    {
        tr_peerIoUnref(io);
    }
}

/***
****
***/

unsigned int tr_bandwidth::clamp(uint64_t now, tr_direction dir, unsigned int byte_count) const
{
    TR_ASSERT(tr_isDirection(dir));

    if (this->band_[dir].is_limited_)
    {
        byte_count = std::min(byte_count, this->band_[dir].bytes_left_);

        /* if we're getting close to exceeding the speed limit,
         * clamp down harder on the bytes available */
        if (byte_count > 0)
        {
            if (now == 0)
            {
                now = tr_time_msec();
            }

            auto const current = this->getRawSpeedBytesPerSecond(now, TR_DOWN);
            auto const desired = this->getDesiredSpeedBytesPerSecond(TR_DOWN);
            auto const r = desired >= 1 ? double(current) / desired : 0;

            if (r > 1.0)
            {
                byte_count = 0;
            }
            else if (r > 0.9)
            {
                byte_count = static_cast<unsigned int>(byte_count * 0.8);
            }
            else if (r > 0.8)
            {
                byte_count = static_cast<unsigned int>(byte_count * 0.9);
            }
        }
    }

    if (this->parent_ != nullptr && this->band_[dir].honor_parent_limits_ && byte_count > 0)
    {
        byte_count = this->parent_->clamp(now, dir, byte_count);
    }

    return byte_count;
}

void tr_bandwidth::notifyBandwidthConsumed(tr_direction dir, size_t byte_count, bool is_piece_data, uint64_t now)
{
    TR_ASSERT(tr_isDirection(dir));

    Band* band = &this->band_[dir];

    if (band->is_limited_ && is_piece_data)
    {
        band->bytes_left_ -= std::min(size_t{ band->bytes_left_ }, byte_count);
    }

#ifdef DEBUG_DIRECTION

    if (dir == DEBUG_DIRECTION && band_->isLimited)
    {
        fprintf(
            stderr,
            "%p consumed %5zu bytes of %5s data... was %6zu, now %6zu left\n",
            this,
            byte_count,
            is_piece_data ? "piece" : "raw",
            oldBytesLeft,
            band_->bytesLeft);
    }

#endif

    notifyBandwidthConsumedBytes(now, &band->raw_, byte_count);

    if (is_piece_data)
    {
        notifyBandwidthConsumedBytes(now, &band->piece_, byte_count);
    }

    if (this->parent_ != nullptr)
    {
        this->parent_->notifyBandwidthConsumed(dir, byte_count, is_piece_data, now);
    }
}

/***
****
***/

tr_bandwidth_limits tr_bandwidth::getLimits() const
{
    tr_bandwidth_limits limits;
    limits.up_limit_KBps = tr_toSpeedKBps(this->getDesiredSpeedBytesPerSecond(TR_UP));
    limits.down_limit_KBps = tr_toSpeedKBps(this->getDesiredSpeedBytesPerSecond(TR_DOWN));
    limits.up_limited = this->isLimited(TR_UP);
    limits.down_limited = this->isLimited(TR_DOWN);
    return limits;
}

void tr_bandwidth::setLimits(tr_bandwidth_limits const* limits)
{
    this->setDesiredSpeedBytesPerSecond(TR_UP, tr_toSpeedBytes(limits->up_limit_KBps));
    this->setDesiredSpeedBytesPerSecond(TR_DOWN, tr_toSpeedBytes(limits->down_limit_KBps));
    this->setLimited(TR_UP, limits->up_limited);
    this->setLimited(TR_DOWN, limits->down_limited);
}
