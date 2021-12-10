/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "error.h"
#include "quark.h"
#include "tr-assert.h"
#include "utils.h"
#include "web-utils.h"

class tr_announce_list
{
public:
    struct tracker_info
    {
        std::string host;
        tr_url_parsed_t announce;
        tr_url_parsed_t scrape;
        tr_quark announce_interned = TR_KEY_NONE;
        tr_quark scrape_interned = TR_KEY_NONE;
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

        bool operator<(tracker_info const& that) const // <
        {
            return compare(that) < 0;
        }
    };

    size_t size() const
    {
        return std::size(trackers_);
    }

    tracker_info at(size_t i) const
    {
        TR_ASSERT(i < size());
        return i < size() ? trackers_.at(i) : tracker_info{};
    }

    bool save(char const* torrent_file, tr_error** error = nullptr);

    size_t set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n)
    {
        trackers_.clear();

        for (size_t i = 0; i < n; ++i)
        {
            add(tiers[i], announce_urls[i]);
        }

        return size();
    }

    size_t remove(std::string_view announce_url)
    {
        auto it = find(announce_url);
        if (it != std::end(trackers_))
        {
            trackers_.erase(it);
        }

        return size();
    }

    size_t remove(tr_tracker_id_t id)
    {
        auto it = find(id);
        if (it != std::end(trackers_))
        {
            trackers_.erase(it);
        }

        return size();
    }

    size_t add(tr_tracker_tier_t tier, std::string_view announce_url_sv)
    {
        auto const announce = tr_urlParseTracker(announce_url_sv);
        if (announce && canAdd(*announce))
        {
            auto tracker = tracker_info{};
            tracker.announce_interned = tr_quark_new(announce_url_sv);
            tracker.announce = *tr_urlParseTracker(tr_quark_get_string_view(tracker.announce_interned));
            tracker.tier = getTier(tier, *announce);
            tracker.id = nextUniqueId();
            tracker.host = tracker.announce.host;
            tracker.host += ':';
            tracker.host += tracker.announce.portstr;

            auto const scrape_str = announceToScrape(announce_url_sv);
            if (scrape_str)
            {
                tracker.scrape_interned = tr_quark_new(*scrape_str);
                tracker.scrape = *tr_urlParseTracker(tr_quark_get_string_view(tracker.scrape_interned));
            }

            auto const it = std::lower_bound(std::begin(trackers_), std::end(trackers_), tracker);
            trackers_.insert(it, tracker);
        }

        return size();
    }

    static std::optional<std::string> announceToScrape(std::string_view announce)
    {
        // To derive the scrape URL use the following steps:
        // Begin with the announce URL. Find the last '/' in it.
        // If the text immediately following that '/' isn't 'announce'
        // it will be taken as a sign that that tracker doesn't support
        // the scrape convention. If it does, substitute 'scrape' for
        // 'announce' to find the scrape page.
        auto constexpr oldval = std::string_view{ "/announce" };
        if (auto pos = announce.rfind(oldval.front()); pos != std::string_view::npos && announce.find(oldval, pos) == pos)
        {
            auto const prefix = announce.substr(0, pos);
            auto const suffix = announce.substr(pos + std::size(oldval));
            return tr_strvJoin(prefix, std::string_view{ "/scrape" }, suffix);
        }

        // some torrents with UDP announce URLs don't have /announce
        if (tr_strvStartsWith(announce, std::string_view{ "udp:" }))
        {
            return std::string{ announce };
        }

        return {};
    }

    static tr_quark announceToScrape(tr_quark announce)
    {
        auto const scrape_str = announceToScrape(tr_quark_get_string_view(announce));
        if (scrape_str)
        {
            return tr_quark_new(*scrape_str);
        }
        return TR_KEY_NONE;
    }

private:
    using trackers_t = std::vector<tracker_info>;
    trackers_t trackers_;

    tr_tracker_id_t nextUniqueId()
    {
        static tr_tracker_id_t id = 0;
        return id++;
    }

    trackers_t::iterator find(tr_tracker_id_t id)
    {
        auto const test = [&id](auto const& tracker)
        {
            return tracker.id == id;
        };
        return std::find_if(std::begin(trackers_), std::end(trackers_), test);
    }

    trackers_t::iterator find(std::string_view announce)
    {
        auto const test = [&announce](auto const& tracker)
        {
            return announce == tracker.announce.full;
        };
        return std::find_if(std::begin(trackers_), std::end(trackers_), test);
    }

    // if two announce URLs differ only by scheme, put them in the same tier.
    // (note: this can leave gaps in the `tier' values, but since the calling
    // function doesn't care, there's no point in removing the gaps...)
    tr_tracker_tier_t getTier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const
    {
        auto const is_sibling = [&announce](tracker_info const& tracker)
        {
            return tracker.announce.host == announce.host && tracker.announce.path == announce.path;
        };

        auto const it = std::find_if(std::begin(trackers_), std::end(trackers_), is_sibling);
        return it != std::end(trackers_) ? it->tier : tier;
    }

    bool canAdd(tr_url_parsed_t const& announce)
    {
        // looking at components instead of the full original URL lets
        // us weed out implicit-vs-explicit port duplicates e.g.
        // "http://tracker/announce" + "http://tracker:80/announce"
        auto const is_same = [&announce](auto const& tracker)
        {
            return tracker.announce.scheme == announce.scheme && tracker.announce.host == announce.host &&
                tracker.announce.port == announce.port && tracker.announce.path == announce.path;
        };
        return std::none_of(std::begin(trackers_), std::end(trackers_), is_same);
    }
};
