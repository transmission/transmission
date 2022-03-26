// This file Copyright 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "announce-list.h"
#include "tr-strbuf.h"
#include "web-utils.h"

struct tr_error;
struct tr_variant;

class tr_magnet_metainfo
{
public:
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);

    auto const& infoHash() const
    {
        return info_hash_;
    }

    auto const& name() const
    {
        return name_;
    }

    auto webseedCount() const
    {
        return std::size(webseed_urls_);
    }

    auto const& webseed(size_t i) const
    {
        return webseed_urls_[i];
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

    void setName(std::string_view name)
    {
        name_ = name;
    }

    template<typename OutputIt>
    void magnet(OutputIt out) const
    {
        using namespace std::literals;
        auto constexpr append = [](OutputIt out, auto x)
        {
            return std::copy(std::begin(x), std::end(x), out);
        };

        out = append(out, "magnet:?xt=urn:btih:"sv);
        out = append(out, infoHashString());

        if (!std::empty(name_))
        {
            out = append(out, "&dn="sv);
            tr_http_escape(out, name_, true);
        }

        for (auto const& tracker : this->announceList())
        {
            out = append(out, "&tr="sv);
            tr_http_escape(out, tracker.announce.full, true);
        }

        for (auto const& webseed : webseed_urls_)
        {
            out = append(out, "&ws="sv);
            tr_http_escape(out, webseed, true);
        }
    }

protected:
    tr_announce_list announce_list_;
    std::vector<std::string> webseed_urls_;
    tr_sha1_digest_t info_hash_;
    std::string info_hash_str_;
    std::string name_;
};
