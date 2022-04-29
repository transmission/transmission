// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <set>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "crypto-utils.h"
#include "magnet-metainfo.h"
#include "torrent.h"
#include "torrents.h"
#include "tr-assert.h"

namespace
{
struct CompareTorrentByHashLen
{
    bool operator()(std::string_view info_hash_prefix, tr_torrent* const tor) const
    {
        auto const n = std::size(info_hash_prefix);
        auto const tor_info_hash_prefix = std::string_view{ tor->infoHashString() }.substr(0, n);
        return info_hash_prefix < tor_info_hash_prefix;
    }
    bool operator()(tr_torrent* const tor, std::string_view info_hash_prefix) const
    {
        auto const n = std::size(info_hash_prefix);
        auto const tor_info_hash_prefix = std::string_view{ tor->infoHashString() }.substr(0, n);
        return tor_info_hash_prefix < info_hash_prefix;
    }
};

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

tr_torrent* tr_torrents::get(int id)
{
    TR_ASSERT(0 < id);
    TR_ASSERT(static_cast<size_t>(id) < std::size(by_id_));
    if (static_cast<size_t>(id) >= std::size(by_id_))
    {
        return nullptr;
    }

    auto* const tor = by_id_.at(id);
    TR_ASSERT(tor == nullptr || tor->uniqueId == id);
    TR_ASSERT(
        std::count_if(std::begin(removed_), std::end(removed_), [&id](auto const& removed) { return id == removed.first; }) ==
        (tor == nullptr ? 1 : 0));
    return tor;
}

// str may be a hash string (in hex or benc encoding), or a magnet URL.
tr_torrent* tr_torrents::get(std::string_view str)
{
    if (auto magnet = tr_magnet_metainfo{}; magnet.parseMagnet(str))
    {
        return get(magnet.infoHash());
    }
    // Allow partial hash comparison
    if (std::size(str) < 4 || std::size(str) >= TR_SHA1_DIGEST_STRLEN)
    {
        return nullptr;
    }

    if (!std::all_of(std::begin(str), std::end(str), [](unsigned char ch) { return isxdigit(ch); }))
    {
        return nullptr;
    }

    auto [begin, end] = std::equal_range(std::begin(by_hash_), std::end(by_hash_), str, CompareTorrentByHashLen{});
    return begin == end ? nullptr : *begin;
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
    removed_.emplace_back(tor->uniqueId, timestamp);
}

std::vector<int> tr_torrents::removedSince(time_t timestamp) const
{
    auto ids = std::set<int>{};

    for (auto const& [id, removed_at] : removed_)
    {
        if (removed_at >= timestamp)
        {
            ids.insert(id);
        }
    }

    return { std::begin(ids), std::end(ids) };
}
