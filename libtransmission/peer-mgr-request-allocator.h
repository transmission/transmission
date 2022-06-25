// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <algorithm> // std::sort
#include <cstddef> // size_t
#include <cstdint> // uint32_t
#include <limits> // std::numeric_limits
#include <map>
#include <numeric>
#include <utility> // std::pair
#include <vector>

#include "transmission.h"

#include "block-info.h"

template<typename PeerKey, typename PoolKey>
class BlockRequestAllocator
{
public:
    class Mediator
    {
    public:
        // Get keys to the all peers that we want to download from
        [[nodiscard]] virtual std::vector<PeerKey> peers() const = 0;

        // How many block requests we've sent and are awaiting a response
        [[nodiscard]] virtual size_t activeReqCount(PeerKey peer) const = 0;

        // Get keys to the speed-limited bandwidth pools that constrain a peer.
        // There will always be at least one.
        [[nodiscard]] virtual std::vector<PoolKey> pools(PeerKey peer) const = 0;

        // Get the maximum number of blocks per second that a bandwidth pool allows
        [[nodiscard]] virtual size_t poolBlockLimit(PoolKey pool) const = 0;

        // The maximum observed download speed, in bytes per second
        [[nodiscard]] virtual uint32_t maxObservedDlSpeedBps() const = 0;

        // The period over which we are trying to fill our download bandwidth
        [[nodiscard]] virtual size_t downloadReqPeriod() const = 0;
    };

    // How many blocks should we request now to keep our
    // download bandwidth saturated over the next PeriodSecs?
    [[nodiscard]] static size_t decideHowManyNewReqsToSend(Mediator const& mediator)
    {
        auto const period_secs = mediator.downloadReqPeriod();

        auto const target_blocks_per_period = (guessMaxPhysicalDlSpeed(mediator) * period_secs) / tr_block_info::BlockSize;

        auto const peers = mediator.peers();
        auto const n_active = std::accumulate(
            std::begin(peers),
            std::end(peers),
            size_t{},
            [&](auto sum, auto const& peer) { return sum + mediator.activeReqCount(peer); });

        if (target_blocks_per_period > n_active)
        {
            return target_blocks_per_period - n_active;
        }

        return {};
    }

    [[nodiscard]] static std::vector<std::pair<PeerKey, size_t>> allocateBlockReqs(
        Mediator const& mediator,
        size_t n_reqs_to_distribute)
    {
        // how many more blocks each pool will allow
        auto pool_left = PoolLimits(mediator);

        // get candidates sorted from fewest pending reqs to most pending reqs
        auto candidates = getCandidates(mediator);
        std::sort(
            std::begin(candidates),
            std::end(candidates),
            [](auto const& a, auto const& b) { return a.n_pending < b.n_pending; });

        // We want to distribute the unallocated block requests among the
        // peers in a way that minimizes the number of pending requests.
        //
        // Setting the goal as "how do we meet our speed goals with the fewest
        // pending requests" is a more reliable way of rewarding fast peers
        // than measuring transfer speeds, which fluctuate from moment to moment.
        //
        // To do it with fewest pending requests, we take the peer with the
        // fewest pending requests and give it more until it has as many as
        // the candidate with the *second* fewest. Then we evenly allocate
        // requests between both of them until they each have as many as the
        // *third* fewest, and so on until all block reqs are allocated or
        // until there are no peers that can take them.
        //
        // We must also be mindful to not request too many blocks from a
        // peer, e.g. by checking their bandwidth pools.

        auto const increase_requests_evenly_before_pivot =
            [&candidates, &pool_left](size_t pivot, size_t n_target, size_t n_reqs_to_distribute)
        {
            for (;;)
            {
                bool did_increase = false;

                // loop through all the candidates to the left of the pivot,
                // giving them a block if possible
                for (size_t i = 0; i < pivot; ++i)
                {
                    auto& candidate = candidates[i];

                    if (n_reqs_to_distribute > 0U && (candidate.n_pending + candidate.n_alloced + 1 < n_target) &&
                        pool_left.hasOpenSlot(std::data(candidate.pools), std::size(candidate.pools)))
                    {
                        --n_reqs_to_distribute;
                        ++candidate.n_alloced;
                        pool_left.decrement(std::data(candidate.pools), std::size(candidate.pools));
                        did_increase = true;
                    }
                }

                if (!did_increase)
                {
                    return n_reqs_to_distribute;
                }
            }
        };

        for (size_t i = 0; i < std::size(candidates); ++i)
        {
            n_reqs_to_distribute = increase_requests_evenly_before_pivot(i, candidates[i].n_pending, n_reqs_to_distribute);
        }

        n_reqs_to_distribute = increase_requests_evenly_before_pivot(
            std::size(candidates),
            std::numeric_limits<size_t>::max(),
            n_reqs_to_distribute);

        auto ret = std::vector<std::pair<PeerKey, size_t>>{};
        ret.reserve(std::size(candidates));
        for (auto const& candidate : candidates)
        {
            if (candidate.n_alloced > 0)
            {
                ret.emplace_back(std::make_pair(candidate.peer_key, candidate.n_alloced));
            }
        }

        return ret;
    }

private:
    // Track how many more blocks each bandwidth pool will allow
    class PoolLimits
    {
    public:
        PoolLimits(Mediator const& mediator)
            : blocks_left_{ createBlocksLeft(mediator) }
        {
        }

        void decrement(PoolKey const* pools, size_t n_pools)
        {
            for (auto const* const end = pools + n_pools; pools != end; ++pools)
            {
                auto& val = blocks_left_[*pools];
                if (val > 0)
                {
                    --val;
                }
            }
        }

        [[nodiscard]] auto hasOpenSlot(PoolKey const* pools, size_t n_pools)
        {
            return std::all_of(pools, pools + n_pools, [&](auto const& pool) { return blocks_left_.at(pool) > 0U; });
        }

    private:
        std::map<PoolKey, size_t> blocks_left_;

        // Get a map of how many more blocks each pool would allow
        [[nodiscard]] static std::map<PoolKey, size_t> createBlocksLeft(Mediator const& mediator)
        {
            // make a map of how many more blocks each pool would allow

            auto left = std::map<PoolKey, size_t>{};
            auto peers = mediator.peers();
            for (auto& peer : peers)
            {
                for (auto& pool : mediator.pools(peer))
                {
                    if (left.count(pool) == 0)
                    {
                        left.try_emplace(pool, mediator.poolBlockLimit(pool));
                    }
                }
            }

            for (auto& peer : peers)
            {
                auto const n_pending = mediator.activeReqCount(peer);

                for (auto& pool : mediator.pools(peer))
                {
                    auto& val = left[pool];
                    val = val > n_pending ? val - n_pending : 0;
                }
            }

            return left;
        }
    };

    struct Candidate
    {
        PeerKey peer_key;
        size_t n_pending;
        size_t n_alloced;
        std::vector<PoolKey> pools;
    };

    [[nodiscard]] static auto getCandidates(Mediator const& mediator)
    {
        auto const peers = mediator.peers();
        auto candidates = std::vector<Candidate>{};
        candidates.resize(std::size(peers));
        std::transform(
            std::begin(peers),
            std::end(peers),
            std::begin(candidates),
            [&mediator](auto& peer) {
                return Candidate{ peer, mediator.activeReqCount(peer), {}, mediator.pools(peer) };
            });

        return candidates;
    }

    static uint32_t guessMaxPhysicalDlSpeed(Mediator const& mediator)
    {
        // Our guess is probably wrong, so apply a multiplier > 1 to push us faster
        // to self-correct past bad guesses. The multiplier will have a cumulative
        // effect, so even a small bump will have a big effect.
        static auto constexpr Multiplier = 1.05;

        // If we have no other info, use a default of 100 Mbit/s based on
        // https://en.wikipedia.org/wiki/List_of_countries_by_Internet_connection_speeds .
        // This number will be wrong for most users, but we've got to start somewhere.
        static auto constexpr BaselineBps = uint32_t{ 12500000U };

        return std::max(BaselineBps, mediator.maxObservedDlSpeedBps()) * Multiplier;
    }

}; // class BlockRequestAllocator
