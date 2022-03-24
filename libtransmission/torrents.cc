// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
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

struct CompareTorrentByHash
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
};

} // namespace

tr_torrents::tr_torrents()
    // Insert an empty pointer at by_id_[0] to ensure that the first added
    // torrent doesn't get an ID of 0; ie, that every torrent has a positive
    // ID number. This constraint isn't needed by libtransmission code but
    // the ID is exported in the RPC API to 3rd party clients that may be
    // testing for >0 as a validity check.
    : by_id_{ nullptr }
{
}

tr_torrent const* tr_torrents::get(int id) const
{
    TR_ASSERT(0 < id);
    TR_ASSERT(static_cast<size_t>(id) < std::size(by_id_));
    if (static_cast<size_t>(id) >= std::size(by_id_))
    {
        return nullptr;
    }

    auto const* tor = by_id_.at(id);
    TR_ASSERT(tor == nullptr || tor->uniqueId == id);
    TR_ASSERT(removed_.count(id) == (tor == nullptr ? 1 : 0));
    return tor;
}

tr_torrent* tr_torrents::get(int id)
{
    TR_ASSERT(0 < id);
    TR_ASSERT(static_cast<size_t>(id) < std::size(by_id_));
    if (static_cast<size_t>(id) >= std::size(by_id_))
    {
        return nullptr;
    }

    auto* tor = by_id_.at(id);
    TR_ASSERT(tor == nullptr || tor->uniqueId == id);
    TR_ASSERT(removed_.count(id) == (tor == nullptr ? 1 : 0));
    return tor;
}

tr_torrent const* tr_torrents::get(std::string_view magnet_link) const
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? get(magnet.infoHash()) : nullptr;
}

tr_torrent* tr_torrents::get(std::string_view magnet_link)
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? get(magnet.infoHash()) : nullptr;
}

tr_torrent* tr_torrents::get(tr_sha1_digest_t const& hash)
{
    auto [begin, end] = std::equal_range(std::begin(by_hash_), std::end(by_hash_), hash, CompareTorrentByHash{});
    return begin == end ? nullptr : *begin;
}

tr_torrent const* tr_torrents::get(tr_sha1_digest_t const& hash) const
{
    auto [begin, end] = std::equal_range(std::cbegin(by_hash_), std::cend(by_hash_), hash, CompareTorrentByHash{});
    return begin == end ? nullptr : *begin;
}

int tr_torrents::add(tr_torrent* tor)
{
    auto const id = static_cast<int>(std::size(by_id_));
    by_id_.push_back(tor);
    by_hash_.insert(std::lower_bound(std::begin(by_hash_), std::end(by_hash_), tor, CompareTorrentByHash{}), tor);
    return id;
}

void tr_torrents::remove(tr_torrent const* tor, time_t timestamp)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(get(tor->uniqueId) == tor);

    by_id_[tor->uniqueId] = nullptr;
    auto const [begin, end] = std::equal_range(std::begin(by_hash_), std::end(by_hash_), tor, CompareTorrentByHash{});
    by_hash_.erase(begin, end);
    removed_.insert_or_assign(tor->uniqueId, timestamp);
}

std::set<int> tr_torrents::removedSince(time_t timestamp) const
{
    auto ret = std::set<int>{};

    for (auto const& [id, removed_at] : removed_)
    {
        if (removed_at >= timestamp)
        {
            ret.insert(id);
        }
    }

    return ret;
}
