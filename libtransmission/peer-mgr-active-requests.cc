// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <memory>
#include <numeric> // std::accumulate()
#include <unordered_map>
#include <unordered_set>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/transmission.h"

#include "libtransmission/peer-mgr-active-requests.h"
#include "libtransmission/tr-assert.h"

class ActiveRequests::Impl
{
public:
    std::unordered_map<tr_block_index_t, std::unordered_set<tr_peer*>> block_to_peers_;
    std::unordered_map<tr_peer*, std::unordered_set<tr_block_index_t>> peer_to_blocks_;
};

ActiveRequests::ActiveRequests()
    : impl_{ std::make_unique<Impl>() }
{
}

ActiveRequests::~ActiveRequests() = default;

bool ActiveRequests::add(tr_block_index_t block, tr_peer* peer)
{
    auto const added = impl_->block_to_peers_[block].emplace(peer).second;
    auto const also_added = added && impl_->peer_to_blocks_[peer].emplace(block).second;
    TR_ASSERT(added == also_added);
    return added;
}

// remove a request to `peer` for `block`
bool ActiveRequests::remove(tr_block_index_t block, tr_peer const* peer)
{
    auto* const peer_key = const_cast<tr_peer*>(peer);
    auto const removed = impl_->block_to_peers_[block].erase(peer_key) != 0U;
    auto const also_removed = removed && impl_->peer_to_blocks_[peer_key].erase(block) != 0U;
    TR_ASSERT(removed == also_removed);
    return removed;
}

// remove requests to `peer` and return the associated blocks
std::vector<tr_block_index_t> ActiveRequests::remove(tr_peer const* peer)
{
    auto* const peer_key = const_cast<tr_peer*>(peer);

    if (auto nh = impl_->peer_to_blocks_.extract(peer_key); nh)
    {
        auto const& blocks = nh.mapped();

        for (auto const block : blocks)
        {
            [[maybe_unused]] auto const removed = impl_->block_to_peers_[block].erase(peer_key) != 0U;
            TR_ASSERT(removed);
        }

        return { std::begin(blocks), std::end(blocks) };
    }

    return {};
}

// remove requests for `block` and return the associated peers
std::vector<tr_peer*> ActiveRequests::remove(tr_block_index_t block)
{
    if (auto nh = impl_->block_to_peers_.extract(block); nh)
    {
        auto const& peers = nh.mapped();

        for (auto* const peer : peers)
        {
            [[maybe_unused]] auto const removed = impl_->peer_to_blocks_[peer].erase(block) != 0U;
            TR_ASSERT(removed);
        }

        return { std::begin(peers), std::end(peers) };
    }

    return {};
}

// return true if there's an active request to `peer` for `block`
bool ActiveRequests::has(tr_block_index_t block, tr_peer const* peer) const noexcept
{
    auto const& btop = impl_->block_to_peers_;

    if (auto const iter = btop.find(block); iter != std::end(btop))
    {
        auto* const peer_key = const_cast<tr_peer*>(peer);
        return iter->second.count(peer_key) != 0U;
    }

    return false;
}

// count how many peers we're asking for `block`
size_t ActiveRequests::count(tr_block_index_t block) const noexcept
{
    auto const& btop = impl_->block_to_peers_;

    if (auto const iter = btop.find(block); iter != std::end(btop))
    {
        return std::size(iter->second);
    }

    return {};
}

// count how many active block requests we have to `peer`
size_t ActiveRequests::count(tr_peer const* peer) const noexcept
{
    auto const& ptob = impl_->peer_to_blocks_;
    auto* const peer_key = const_cast<tr_peer*>(peer);

    if (auto const iter = ptob.find(peer_key); iter != std::end(ptob))
    {
        return std::size(iter->second);
    }

    return {};
}

// return the total number of active requests
size_t ActiveRequests::size() const noexcept
{
    return std::accumulate(
            std::begin(impl_->peer_to_blocks_),
            std::end(impl_->peer_to_blocks_),
            size_t{},
            [](auto sum, auto iter){ return sum + std::size(iter.second); });
}
