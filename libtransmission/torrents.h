// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime>
#include <iterator>
#include <map>
#include <set>
#include <string_view>
#include <vector>

#include "tr-macros.h" // tr_sha1_digest_t

#include "torrent-metainfo.h"

struct tr_torrent;
struct tr_torrent_metainfo;

// A helper class to manage tracking sets of tr_torrent objects.
class tr_torrents
{
public:
    // returns a fast lookup id for `tor`
    [[nodiscard]] int add(tr_torrent* tor);

    void remove(tr_torrent const* tor, time_t current_time);

    // O(1)
    [[nodiscard]] tr_torrent* get(int id);
    [[nodiscard]] tr_torrent const* get(int id) const;

    // O(log n)
    [[nodiscard]] tr_torrent const* get(tr_sha1_digest_t const& hash) const;
    [[nodiscard]] tr_torrent* get(tr_sha1_digest_t const& hash);

    [[nodiscard]] tr_torrent const* get(tr_torrent_metainfo const& metainfo) const
    {
        return get(metainfo.infoHash());
    }

    [[nodiscard]] tr_torrent* get(tr_torrent_metainfo const& metainfo)
    {
        return get(metainfo.infoHash());
    }

    // These convenience functions use get(tr_sha1_digest_t const&)
    // after parsing the magnet link to get the info hash. If you have
    // the info hash already, use get() instead to avoid excess parsing.
    [[nodiscard]] tr_torrent const* get(std::string_view magnet_link) const;
    [[nodiscard]] tr_torrent* get(std::string_view magnet_link);

    template<typename T>
    [[nodiscard]] bool contains(T const& key) const
    {
        return get(key) != nullptr;
    }

    [[nodiscard]] std::set<int> removedSince(time_t) const;

    [[nodiscard]] auto cbegin() const
    {
        return std::cbegin(by_hash_);
    }
    [[nodiscard]] auto begin() const
    {
        return cbegin();
    }
    [[nodiscard]] auto begin()
    {
        return std::begin(by_hash_);
    }

    [[nodiscard]] auto cend() const
    {
        return std::cend(by_hash_);
    }

    [[nodiscard]] auto end() const
    {
        return cend();
    }

    [[nodiscard]] auto end()
    {
        return std::end(by_hash_);
    }

    [[nodiscard]] auto size() const
    {
        return std::size(by_hash_);
    }

    [[nodiscard]] auto empty() const
    {
        return std::empty(by_hash_);
    }

private:
    std::vector<tr_torrent*> by_hash_;

    // This is a lookup table where by_id_[id]->uniqueId == id.
    // There is a small tradeoff here -- lookup is O(1) at the cost
    // of a wasted slot in the lookup table whenever a torrent is
    // removed. This improves speed for all use cases at the cost of
    // wasting a small amount of memory in uncommon use cases, e.g. a
    // long-lived session where thousands of torrents are removed
    std::vector<tr_torrent*> by_id_;

    std::map<int, time_t> removed_;
};
