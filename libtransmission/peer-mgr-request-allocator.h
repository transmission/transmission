// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numeric>
#include <map>
#include <optional>
#include <utility>
#include <vector>

template<typename PeerKey, typename PoolKey>
class BlockRequestAllocator
{
public:
    class Mediator
    {
    public:
        // Get keys to the peers that we want to download from
        [[nodiscard]] virtual std::vector<PeerKey> peers() const = 0;

        // How many pending block requests we have awaiting a response from the peer
        [[nodiscard]] virtual size_t pendingReqCount(PeerKey peer) const = 0;

        // Get keys to the speed-limited bandwidth pools that constrain a peer.
        // There will always be at least one.
        [[nodiscard]] virtual std::vector<PoolKey> pools(PeerKey peer) const = 0;

        // Get the maximum number of blocks per second that a bandwidth pool allows
        [[nodiscard]] virtual size_t poolBlockLimit(PoolKey pool) const = 0;
    };

    static std::vector<std::pair<PeerKey, size_t>> allocateBlockReqs(Mediator const& mediator, size_t n_reqs_to_distribute)
    {
        // how many more blocks each pool will allow
        auto pool_left = PoolLimits(mediator);

        auto candidates = getCandidatesSortedByFewestPending(mediator);

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
                auto const n_pending = mediator.pendingReqCount(peer);

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

    [[nodiscard]] static auto getCandidatesSortedByFewestPending(Mediator const& mediator)
    {
        auto const peers = mediator.peers();
        auto candidates = std::vector<Candidate>{};
        candidates.resize(std::size(peers));
        std::transform(
            std::begin(peers),
            std::end(peers),
            std::begin(candidates),
            [&mediator](auto& peer) {
                return Candidate{ peer, mediator.pendingReqCount(peer), {}, mediator.pools(peer) };
            });

        // sort them from fewest pending to most pending
        std::sort(
            std::begin(candidates),
            std::end(candidates),
            [](auto const& a, auto const& b) { return a.n_pending < b.n_pending; });
        return candidates;
    }

}; // class BlockRequestAllocator
