// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <ctime>
#include <optional>

#include <small/map.hpp>

#include "libtransmission/net.h"
#include "libtransmission/utils.h"

namespace bep55
{

// Bounded per-swarm store tracking which connected peer advertised a target via PEX.
// Maps target endpoint -> { advertising peer socket address, time recorded }.
// Used to find a relay candidate when a direct connection fails.
class HolepunchIntroducerStore
{
public:
    struct Entry
    {
        tr_socket_address introducer;
        time_t recorded_at = 0;
    };

    void record(tr_socket_address const& target, tr_socket_address const& introducer)
    {
        auto const now = tr_time();

        // Enforce max size only when inserting a new key (updates don't grow the store).
        // Each call inserts at most one new key, so at most one eviction is needed.
        if (store_.find(target) == store_.end() && std::size(store_) >= MaxSize)
        {
            store_.erase(
                std::ranges::min_element(
                    store_,
                    [](auto const& a, auto const& b) { return a.second.recorded_at < b.second.recorded_at; }));
        }

        store_[target] = Entry{ introducer, now };
    }

    [[nodiscard]] std::optional<tr_socket_address> find_introducer(tr_socket_address const& target) const
    {
        auto const now = tr_time();
        auto const cutoff = now - MaxAgeSecs;

        if (auto it = store_.find(target); it != store_.end() && it->second.recorded_at >= cutoff)
        {
            return it->second.introducer;
        }
        return std::nullopt;
    }

    static auto constexpr MaxSize = size_t{ 256 };
    static auto constexpr MaxAgeSecs = time_t{ 300 }; // 5 minutes

private:
    small::max_size_map<tr_socket_address, Entry, MaxSize> store_;
};

} // namespace bep55
