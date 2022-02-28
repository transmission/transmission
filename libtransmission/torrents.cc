// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <map>
#include <set>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "magnet-metainfo.h"
#include "torrent.h"
#include "torrents.h"
#include "tr-assert.h"

tr_torrent const* tr_torrents::fromId(int id) const
{
    TR_ASSERT(0 <= id);
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

tr_torrent* tr_torrents::fromId(int id)
{
    TR_ASSERT(0 <= id);
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

tr_torrent const* tr_torrents::fromMagnet(std::string_view magnet_link) const
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? fromHash(magnet.infoHash()) : nullptr;
}

tr_torrent* tr_torrents::fromMagnet(std::string_view magnet_link)
{
    auto magnet = tr_magnet_metainfo{};
    return magnet.parseMagnet(magnet_link) ? fromHash(magnet.infoHash()) : nullptr;
}

tr_torrent const* tr_torrents::fromMetainfo(tr_torrent_metainfo const& metainfo) const
{
    return fromHash(metainfo.infoHash());
}

tr_torrent* tr_torrents::fromMetainfo(tr_torrent_metainfo const* metainfo)
{
    return fromHash(metainfo->infoHash());
}

int tr_torrents::add(tr_torrent* tor)
{
    int const id = static_cast<int>(std::size(by_id_));
    by_id_.push_back(tor);
    by_hash_.insert_or_assign(tor->infoHash(), tor);
    return id;
}

void tr_torrents::remove(tr_torrent const* tor, time_t timestamp)
{
    TR_ASSERT(tor != nullptr);
    TR_ASSERT(fromId(tor->uniqueId) == tor);
    by_id_[tor->uniqueId] = nullptr;
    by_hash_.erase(tor->infoHash());
    removed_.insert_or_assign(tor->uniqueId, timestamp);
}

std::vector<int> tr_torrents::removedSince(time_t timestamp) const
{
    auto ret = std::vector<int>{};
    ret.reserve(std::size(removed_));

    for (auto const& [id, removed_at] : removed_)
    {
        if (removed_at >= timestamp)
        {
            ret.push_back(id);
        }
    }

    return ret;
}
