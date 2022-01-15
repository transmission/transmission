/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#if 0 // TODO(ckerr): re-enable this after tr_info is made private
#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif
#endif

#include <cstddef>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "quark.h"
#include "interned-string.h"
#include "web-utils.h"

struct tr_error;

class tr_announce_list
{
public:
    struct tracker_info
    {
        tr_url_parsed_t announce;
        tr_url_parsed_t scrape;
        tr_interned_string announce_str;
        tr_interned_string scrape_str;
        tr_interned_string host;
        tr_tracker_tier_t tier = 0;
        tr_tracker_id_t id = 0;

        int compare(tracker_info const& that) const // <=>
        {
            if (this->tier != that.tier)
            {
                return this->tier < that.tier ? -1 : 1;
            }

            if (this->announce.full != that.announce.full)
            {
                return this->announce.full < that.announce.full ? -1 : 1;
            }

            return 0;
        }

        bool operator<(tracker_info const& that) const
        {
            return compare(that) < 0;
        }

        bool operator==(tracker_info const& that) const
        {
            return compare(that) == 0;
        }
    };

private:
    using trackers_t = std::vector<tracker_info>;

public:
    auto begin() const
    {
        return std::begin(trackers_);
    }
    auto end() const
    {
        return std::end(trackers_);
    }
    bool empty() const
    {
        return std::empty(trackers_);
    }
    size_t size() const
    {
        return std::size(trackers_);
    }
    tracker_info const& at(size_t i) const
    {
        return trackers_.at(i);
    }

    std::set<tr_tracker_tier_t> tiers() const;
    tr_tracker_tier_t nextTier() const;

    bool add(tr_tracker_tier_t tier, std::string_view announce_url_sv);
    bool remove(std::string_view announce_url);
    bool remove(tr_tracker_id_t id);
    bool replace(tr_tracker_id_t id, std::string_view announce_url_sv);
    size_t set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n);
    void clear()
    {
        return trackers_.clear();
    }

    bool save(std::string const& torrent_file, tr_error** error = nullptr) const;

    static std::optional<std::string> announceToScrape(std::string_view announce);
    static tr_quark announceToScrape(tr_quark announce);

private:
    tr_tracker_tier_t getTier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const;

    bool canAdd(tr_url_parsed_t const& announce);
    tr_tracker_id_t nextUniqueId();
    trackers_t::iterator find(std::string_view announce);
    trackers_t::iterator find(tr_tracker_id_t id);

    trackers_t trackers_;
};
