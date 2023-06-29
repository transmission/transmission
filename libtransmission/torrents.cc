// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <set>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "magnet-metainfo.h"
#include "torrent.h"
#include "torrents.h"
#include "tr-assert.h"

namespace
{

constexpr struct
{
    bool operator()(tr_sha1_digest_t const& a, tr_sha1_digest_t const& b) const
    {
        return a < b;
    }

    bool operator()(tr_torrent const* a, tr_torrent const* b) const
    {
        return (*this)(a->infoHash(), b->infoHash());
    }

    bool operator()(tr_torrent const* a, tr_sha1_digest_t const& b) const
    {
        return (*this)(a->infoHash(), b);
    }

    bool operator()(tr_sha1_digest_t const& a, tr_torrent const* b) const
    {
        return (*this)(a, b->infoHash());
    }
} CompareTorrentByHash{};

} // namespace

tr_torrent* tr_torrents::get(std::string_view magnet_link)
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? get(magnet.infoHash()) : nullptr;
}

tr_torrent* tr_torrents::get(tr_sha1_digest_t const& hash)
{
    auto [begin, end] = std::equal_range(std::begin(by_hash_), std::end(by_hash_), hash, CompareTorrentByHash);
    return begin == end ? nullptr : *begin;
}

tr_torrent const* tr_torrents::get(tr_sha1_digest_t const& hash) const
{
    auto [begin, end] = std::equal_range(std::cbegin(by_hash_), std::cend(by_hash_), hash, CompareTorrentByHash);
    return begin == end ? nullptr : *begin;
}

tr_torrent_id_t tr_torrents::add(tr_torrent* tor)
{
    auto const id = static_cast<tr_torrent_id_t>(std::size(by_id_));
    by_id_.push_back(tor);
    by_hash_.insert(std::lower_bound(std::begin(by_hash_), std::end(by_hash_), tor, CompareTorrentByHash), tor);
    return id;
}

void tr_torrents::remove(tr_torrent const* tor, time_t current_time)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(get(tor->id()) == tor);

    by_id_[tor->id()] = nullptr;
    auto const [begin, end] = std::equal_range(std::begin(by_hash_), std::end(by_hash_), tor, CompareTorrentByHash);
    by_hash_.erase(begin, end);
    removed_.emplace_back(tor->id(), current_time);
}

std::vector<tr_torrent_id_t> tr_torrents::removedSince(time_t timestamp) const
{
    auto ids = std::set<tr_torrent_id_t>{};

    for (auto const& [id, removed_at] : removed_)
    {
        if (removed_at >= timestamp)
        {
            ids.insert(id);
        }
    }

    return { std::begin(ids), std::end(ids) };
}
