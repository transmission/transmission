/*
 * This file Copyright (C) 2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <iostream>
#include <map>
#include <memory>
#include <numeric>
#include <set>
#include <vector>

#define LIBTRANSMISSION_PEER_MODULE

#include "peer-mgr-active-requests.h"

namespace
{

struct peer_at
{
    tr_peer* peer;
    time_t when;

    peer_at(tr_peer* p, time_t w)
        : peer{ p }
        , when{ w }
    {
    }

    int compare(peer_at const& that) const // <=>
    {
        if (peer != that.peer)
        {
            return peer < that.peer ? -1 : 1;
        }

        return 0;
    }

    bool operator<(peer_at const& that) const
    {
        return compare(that) < 0;
    }
};

} // namespace

class ActiveRequests::Impl
{
public:
    std::map<tr_block_index_t, std::set<peer_at>> blocks_;
    size_t size_ = 0;
};

ActiveRequests::ActiveRequests()
    : impl_{ std::make_unique<Impl>() }
{
}

ActiveRequests::~ActiveRequests() = default;

bool ActiveRequests::add(tr_block_index_t block, tr_peer* peer, time_t when)
{
    // std::cout << __FILE__ << ':' << __LINE__ << " marking block " << block << " as requested from " << peer << " at " << when << std::endl;
    bool const added = impl_->blocks_[block].emplace(peer, when).second;

    if (added)
    {
        ++impl_->size_;
    }

    return added;
}

// remove a request to `peer` for `block`
bool ActiveRequests::remove(tr_block_index_t block, tr_peer const* peer)
{
    // std::cout << __FILE__ << ':' << __LINE__ << " unmarking block " << block << " request from " << peer << std::endl;
    auto const key = peer_at{ const_cast<tr_peer*>(peer), 0 };
    auto const it = impl_->blocks_.find(block);
    auto const removed = it != std::end(impl_->blocks_) && it->second.erase(key) != 0;

    if (removed)
    {
        --impl_->size_;

        if (std::empty(it->second))
        {
            impl_->blocks_.erase(it);
        }
    }

    return removed;
}

// remove requests to `peer` and return the associated blocks
std::vector<tr_block_index_t> ActiveRequests::remove(tr_peer const* peer)
{
    auto removed = std::vector<tr_block_index_t>{};
    removed.reserve(impl_->blocks_.size());

    auto const key = peer_at{ const_cast<tr_peer*>(peer), 0 };
    for (auto const& it : impl_->blocks_)
    {
        if (it.second.count(key))
        {
            removed.push_back(it.first);
        }
    }

    for (auto block : removed)
    {
        remove(block, peer);
    }

    return removed;
}

// remove requests for `block` and return the associated peers
std::vector<tr_peer*> ActiveRequests::remove(tr_block_index_t block)
{
    // std::cout << __FILE__ << ':' << __LINE__ << " removing all requests for block " << block << std::endl;
    auto removed = std::vector<tr_peer*>{};

    auto it = impl_->blocks_.find(block);
    if (it != std::end(impl_->blocks_))
    {
        auto const n = std::size(it->second);
        removed.resize(n);
        std::transform(
            std::begin(it->second),
            std::end(it->second),
            std::begin(removed),
            [](auto const& sent) { return sent.peer; });
        impl_->blocks_.erase(block);
        impl_->size_ -= n;
    }

    return removed;
}

// return true if there's an active request to `peer` for `block`
bool ActiveRequests::has(tr_block_index_t block, tr_peer const* peer) const
{
    auto const it = impl_->blocks_.find(block);
    return it != std::end(impl_->blocks_) && it->second.count(peer_at{ const_cast<tr_peer*>(peer), 0 });
}

// count how many peers we're asking for `block`
size_t ActiveRequests::count(tr_block_index_t block) const
{
    auto const n = impl_->blocks_.count(block);
    // std::cout << __FILE__ << ':' << __LINE__ << " number of active requests for " << block << ": " << n << std::endl;
    return n;
}

// count how many active block requests we have to `peer`
size_t ActiveRequests::count(tr_peer const* peer) const
{
    auto const key = peer_at{ const_cast<tr_peer*>(peer), 0 };
    auto const test = [&key](auto const& reqs)
    {
        return reqs.second.count(key) != 0;
    };
    return std::count_if(std::begin(impl_->blocks_), std::end(impl_->blocks_), test);
}

// return the total number of active requests
size_t ActiveRequests::size() const
{
    return impl_->size_;
}

// returns the active requests sent before `when`
std::vector<ActiveRequests::block_and_peer> ActiveRequests::sentBefore(time_t when) const
{
    auto sent_before = std::vector<block_and_peer>{};
    sent_before.reserve(std::size(impl_->blocks_));

    for (auto& perblock : impl_->blocks_)
    {
        for (auto& sent : perblock.second)
        {
            if (sent.when < when)
            {
                sent_before.push_back(block_and_peer{ perblock.first, sent.peer });
            }
        }
    }

    return sent_before;
}
