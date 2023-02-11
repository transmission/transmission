// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <ctime>
#include <string_view>
#include <utility>
#include <vector>

#include "transmission.h"

#include "torrent-metainfo.h"

struct tr_torrent;
struct tr_torrent_metainfo;

// A helper class to manage tracking sets of tr_torrent objects.
class tr_torrents
{
public:
    // returns a fast lookup id for `tor`
    [[nodiscard]] tr_torrent_id_t add(tr_torrent* tor);

    void remove(tr_torrent const* tor, time_t current_time);

    // O(1)
    [[nodiscard]] TR_CONSTEXPR20 tr_torrent* get(tr_torrent_id_t id)
    {
        auto const uid = static_cast<size_t>(id);
        return uid >= std::size(by_id_) ? nullptr : by_id_.at(uid);
    }

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
    [[nodiscard]] tr_torrent* get(std::string_view magnet_link);

    template<typename T>
    [[nodiscard]] bool contains(T const& key) const
    {
        return get(key) != nullptr;
    }

    [[nodiscard]] std::vector<tr_torrent_id_t> removedSince(time_t timestamp) const;

    [[nodiscard]] TR_CONSTEXPR20 auto cbegin() const noexcept
    {
        return std::cbegin(by_hash_);
    }
    [[nodiscard]] TR_CONSTEXPR20 auto begin() const noexcept
    {
        return cbegin();
    }
    [[nodiscard]] TR_CONSTEXPR20 auto begin() noexcept
    {
        return std::begin(by_hash_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto cend() const noexcept
    {
        return std::cend(by_hash_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto end() const noexcept
    {
        return cend();
    }

    [[nodiscard]] TR_CONSTEXPR20 auto end() noexcept
    {
        return std::end(by_hash_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto size() const noexcept
    {
        return std::size(by_hash_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto empty() const noexcept
    {
        return std::empty(by_hash_);
    }

private:
    std::vector<tr_torrent*> by_hash_;

    // This is a lookup table where by_id_[id]->id() == id.
    // There is a small tradeoff here -- lookup is O(1) at the cost
    // of a wasted slot in the lookup table whenever a torrent is
    // removed. This improves speed for all use cases at the cost of
    // wasting a small amount of memory in uncommon use cases, e.g. a
    // long-lived session where thousands of torrents are removed.
    //
    // Insert an empty pointer at by_id_[0] to ensure that the first
    // added torrent doesn't get an ID of 0; ie, that every torrent has
    // a positive ID number. This constraint isn't needed by libtransmission
    // code but the ID is exported in the RPC API to 3rd party clients that
    // may be testing for >0 as a validity check.
    std::vector<tr_torrent*> by_id_{ nullptr };

    std::vector<std::pair<tr_torrent_id_t, time_t>> removed_;
};
