/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "announce-list.h"

struct tr_error;
struct tr_variant;

class tr_magnet_metainfo
{
public:
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);
    std::string magnet() const;
    virtual ~tr_magnet_metainfo() = default;

    auto const& infoHash() const
    {
        return info_hash_;
    }
    auto const& name() const
    {
        return name_;
    }
    auto const& webseeds() const
    {
        return webseed_urls_;
    }

    auto& announceList()
    {
        return announce_list_;
    }

    auto const& announceList() const
    {
        return announce_list_;
    }

    std::string const& infoHashString() const
    {
        return info_hash_str_;
    }

    virtual void clear();

    void setName(std::string_view name)
    {
        name_ = name;
    }

    void toVariant(tr_variant* top) const;

protected:
    tr_announce_list announce_list_;
    std::vector<std::string> webseed_urls_;
    tr_sha1_digest_t info_hash_;
    std::string info_hash_str_;
    std::string name_;
};
