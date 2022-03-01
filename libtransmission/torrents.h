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

struct tr_torrent;
struct tr_torrent_metainfo;

class tr_torrents
{
public:
    // return a fast lookup key for `tor`
    [[nodiscard]] int add(tr_torrent* tor);

    void remove(tr_torrent const* tor, time_t current_time);

    [[nodiscard]] tr_torrent const* get(int id) const;
    [[nodiscard]] tr_torrent const* get(std::string_view magnet_link) const;
    [[nodiscard]] tr_torrent const* get(tr_sha1_digest_t const& hash) const;
    [[nodiscard]] tr_torrent const* get(tr_torrent_metainfo const& metainfo) const;
    [[nodiscard]] tr_torrent* get(int id);
    [[nodiscard]] tr_torrent* get(std::string_view magnet_link);
    [[nodiscard]] tr_torrent* get(tr_sha1_digest_t const& hash);
    [[nodiscard]] tr_torrent* get(tr_torrent_metainfo const& metainfo);

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

    auto size() const
    {
        return std::size(by_hash_);
    }

    auto empty() const
    {
        return std::empty(by_hash_);
    }

private:
    std::vector<tr_torrent*> by_id_;
    std::vector<tr_torrent*> by_hash_;
    std::map<int, time_t> removed_;
};
