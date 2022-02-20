// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <optional>
#include <set>
#include <string>
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

        [[nodiscard]] int compare(tracker_info const& that) const // <=>
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

        [[nodiscard]] bool operator<(tracker_info const& that) const
        {
            return compare(that) < 0;
        }

        [[nodiscard]] bool operator==(tracker_info const& that) const
        {
            return compare(that) == 0;
        }
    };

private:
    using trackers_t = std::vector<tracker_info>;

public:
    [[nodiscard]] auto begin() const
    {
        return std::begin(trackers_);
    }

    [[nodiscard]] auto end() const
    {
        return std::end(trackers_);
    }

    [[nodiscard]] bool empty() const
    {
        return std::empty(trackers_);
    }

    [[nodiscard]] size_t size() const
    {
        return std::size(trackers_);
    }

    [[nodiscard]] tracker_info const& at(size_t i) const
    {
        return trackers_.at(i);
    }

    [[nodiscard]] std::set<tr_tracker_tier_t> tiers() const;

    [[nodiscard]] tr_tracker_tier_t nextTier() const;

    bool add(std::string_view announce_url_sv)
    {
        return add(announce_url_sv, this->nextTier());
    }

    bool add(std::string_view announce_url_sv, tr_tracker_tier_t tier);
    void add(tr_announce_list const& that);
    bool remove(std::string_view announce_url);
    bool remove(tr_tracker_id_t id);
    bool replace(tr_tracker_id_t id, std::string_view announce_url_sv);
    size_t set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n);
    void clear()
    {
        return trackers_.clear();
    }

    /**
     * Populate the announce list from a text string.
     * - One announce URL per line
     * - Blank line denotes a new tier
     */
    bool parse(std::string_view text);
    [[nodiscard]] std::string toString() const;

    bool save(std::string const& torrent_file, tr_error** error = nullptr) const;

    static std::optional<std::string> announceToScrape(std::string_view announce);
    static tr_quark announceToScrape(tr_quark announce);

private:
    [[nodiscard]] tr_tracker_tier_t getTier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const;

    bool canAdd(tr_url_parsed_t const& announce);
    static tr_tracker_id_t nextUniqueId();
    trackers_t::iterator find(std::string_view announce);
    trackers_t::iterator find(tr_tracker_id_t id);

    trackers_t trackers_;
};
