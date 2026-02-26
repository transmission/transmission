// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <ctime>
#include <string_view>
#include <vector>

#include "libtransmission/magnet-metainfo.h"
#include "libtransmission/torrent.h"
#include "libtransmission/torrents.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

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
        return (*this)(a->info_hash(), b->info_hash());
    }

    bool operator()(tr_torrent const* a, tr_sha1_digest_t const& b) const
    {
        return (*this)(a->info_hash(), b);
    }

    bool operator()(tr_sha1_digest_t const& a, tr_torrent const* b) const
    {
        return (*this)(a, b->info_hash());
    }
} CompareTorrentByHash{};

} // namespace

tr_torrent* tr_torrents::get(std::string_view magnet_link) const
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? get(magnet.info_hash()) : nullptr;
}

tr_torrent* tr_torrents::get(tr_sha1_digest_t const& hash) const
{
    auto const range = std::ranges::equal_range(by_hash_, hash, CompareTorrentByHash);
    return range.empty() ? nullptr : range.front();
}

tr_torrent* tr_torrents::find_from_obfuscated_hash(tr_sha1_digest_t const& obfuscated_hash) const
{
    for (auto* const tor : *this)
    {
        if (tor->obfuscated_hash_equals(obfuscated_hash))
        {
            return tor;
        }
    }

    return nullptr;
}

tr_torrent_id_t tr_torrents::add(tr_torrent* tor)
{
    auto const id = static_cast<tr_torrent_id_t>(std::size(by_id_));
    by_id_.push_back(tor);
    by_hash_.insert(std::ranges::lower_bound(by_hash_, tor, CompareTorrentByHash), tor);
    return id;
}

void tr_torrents::remove(tr_torrent const* tor, time_t current_time)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(get(tor->id()) == tor);

    by_id_[tor->id()] = nullptr;
    auto const [begin, end] = std::ranges::equal_range(by_hash_, tor, CompareTorrentByHash);
    by_hash_.erase(begin, end);
    removed_.emplace_back(tor->id(), current_time);
}

std::vector<tr_torrent_id_t> tr_torrents::removedSince(time_t timestamp) const
{
    auto ids = std::vector<tr_torrent_id_t>{};
    ids.reserve(std::size(removed_));

    for (auto const& [id, removed_at] : removed_)
    {
        if (removed_at >= timestamp)
        {
            ids.emplace_back(id);
        }
    }

    std::ranges::sort(ids);
    auto const [first, last] = std::ranges::unique(ids);
    ids.erase(first, last);
    return ids;
}
