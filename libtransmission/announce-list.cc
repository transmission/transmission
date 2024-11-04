// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

#include <small/map.hpp>

#include "libtransmission/transmission.h"

#include "libtransmission/announce-list.h"
#include "libtransmission/error.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"
#include "libtransmission/web-utils.h"

using namespace std::literals;

size_t tr_announce_list::set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n)
{
    trackers_.clear();

    for (size_t i = 0; i < n; ++i)
    {
        add(announce_urls[i], tiers[i]);
    }

    return size();
}

bool tr_announce_list::remove(std::string_view announce_url)
{
    auto const it = find(announce_url);
    if (it == std::end(trackers_))
    {
        return false;
    }

    trackers_.erase(it);
    return true;
}

bool tr_announce_list::remove(tr_tracker_id_t id)
{
    auto it = find(id);
    if (it == std::end(trackers_))
    {
        return false;
    }

    trackers_.erase(it);
    return true;
}

bool tr_announce_list::replace(tr_tracker_id_t id, std::string_view announce_url_sv)
{
    if (auto const announce = tr_urlParseTracker(announce_url_sv); !announce || !can_add(*announce))
    {
        return false;
    }

    auto it = find(id);
    if (it == std::end(trackers_))
    {
        return false;
    }

    auto const tier = it->tier;
    trackers_.erase(it);
    return add(announce_url_sv, tier);
}

bool tr_announce_list::add(std::string_view announce_url, tr_tracker_tier_t tier)
{
    // Make sure the announce URL is usable before we intern it.
    if (auto const announce = tr_urlParseTracker(announce_url); !announce || !can_add(*announce))
    {
        return false;
    }

    // Parse again with the interned string so that `parsed` fields all
    // point to the interned addresses. This second call should never
    // fail, but check anyway to make the linter happy.
    auto const announce_interned = tr_interned_string{ announce_url };
    auto const parsed = tr_urlParseTracker(announce_interned.sv());
    if (!parsed)
    {
        return false;
    }

    auto tracker = tracker_info{};
    tracker.announce = announce_interned;
    tracker.announce_parsed = *parsed;
    tracker.tier = get_tier(tier, *parsed);
    tracker.id = next_unique_id();

    if (auto const scrape_str = announce_to_scrape(tracker.announce.sv()); scrape_str)
    {
        tracker.scrape = *scrape_str;
    }

    auto const it = std::lower_bound(std::begin(trackers_), std::end(trackers_), tracker);
    trackers_.insert(it, tracker);

    return true;
}

void tr_announce_list::add(tr_announce_list const& src)
{
    if (std::empty(src))
    {
        return;
    }

    auto src_tier = src.at(0).tier;
    auto& tgt = *this;
    auto tgt_tier = tgt.nextTier();

    for (auto const& tracker : src)
    {
        if (src_tier != tracker.tier)
        {
            src_tier = tracker.tier;
            ++tgt_tier;
        }

        tgt.add(tracker.announce.sv(), tgt_tier);
    }
}

std::optional<std::string> tr_announce_list::announce_to_scrape(std::string_view announce)
{
    // To derive the scrape URL use the following steps:
    // Begin with the announce URL. Find the last '/' in it.
    // If the text immediately following that '/' isn't 'announce'
    // it will be taken as a sign that that tracker doesn't support
    // the scrape convention. If it does, substitute 'scrape' for
    // 'announce' to find the scrape page.
    auto constexpr Oldval = "/announce"sv;
    auto constexpr Newval = "/scrape"sv;
    if (auto pos = announce.rfind(Oldval.front()); pos != std::string_view::npos && announce.find(Oldval, pos) == pos)
    {
        return std::string{ announce }.replace(pos, std::size(Oldval), Newval);
    }

    // some torrents with UDP announce URLs don't have /announce
    if (tr_strv_starts_with(announce, "udp:"sv))
    {
        return std::string{ announce };
    }

    return {};
}

tr_tracker_tier_t tr_announce_list::nextTier() const
{
    return std::empty(trackers_) ? 0 : trackers_.back().tier + 1;
}

tr_tracker_id_t tr_announce_list::next_unique_id()
{
    static tr_tracker_id_t id = 0;
    return id++;
}

tr_announce_list::trackers_t::iterator tr_announce_list::find(tr_tracker_id_t id)
{
    auto const test = [&id](auto const& tracker)
    {
        return tracker.id == id;
    };
    return std::find_if(std::begin(trackers_), std::end(trackers_), test);
}

tr_announce_list::trackers_t::iterator tr_announce_list::find(std::string_view announce)
{
    auto const test = [&announce](auto const& tracker)
    {
        return announce == tracker.announce.sv();
    };
    return std::find_if(std::begin(trackers_), std::end(trackers_), test);
}

// if two announce URLs differ only by scheme, put them in the same tier.
// (note: this can leave gaps in the `tier` values, but since the calling
// function doesn't care, there's no point in removing the gaps...)
tr_tracker_tier_t tr_announce_list::get_tier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const
{
    auto const is_sibling = [&announce](auto const& tracker)
    {
        auto const tracker_announce = tracker.announce.sv();

        // fast test to avoid tr_urlParse()ing most trackers
        if (!tr_strv_contains(tracker_announce, announce.host))
        {
            return false;
        }

        auto const candidate = tr_urlParse(tracker_announce);
        return candidate->host == announce.host && candidate->path == announce.path;
    };

    auto const it = std::find_if(std::begin(trackers_), std::end(trackers_), is_sibling);
    return it != std::end(trackers_) ? it->tier : tier;
}

bool tr_announce_list::can_add(tr_url_parsed_t const& announce) const noexcept
{
    // looking at components instead of the full original URL lets
    // us weed out implicit-vs-explicit port duplicates e.g.
    // "http://tracker/announce" + "http://tracker:80/announce"
    auto const is_same = [&announce](auto const& tracker)
    {
        auto const tracker_announce = tracker.announce.sv();

        // fast test to avoid tr_urlParse()ing most trackers
        if (!tr_strv_contains(tracker_announce, announce.host))
        {
            return false;
        }

        auto const tracker_parsed = tr_urlParse(tracker_announce);
        return tracker_parsed->scheme == announce.scheme && tracker_parsed->host == announce.host &&
            tracker_parsed->port == announce.port && tracker_parsed->path == announce.path &&
            tracker_parsed->query == announce.query;
    };
    return std::none_of(std::begin(trackers_), std::end(trackers_), is_same);
}

tr_variant tr_announce_list::to_tiers_variant() const
{
    TR_ASSERT(size() > 1U);

    auto tiers_map = small::map<tr_tracker_tier_t, tr_variant::Vector>{};
    for (auto const& tracker : *this)
    {
        tiers_map[tracker.tier].emplace_back(tracker.announce.sv());
    }

    auto tiers_vec = tr_variant::Vector{};
    tiers_vec.reserve(std::size(tiers_map));
    for (auto& [tier_num, trackers_vec] : tiers_map)
    {
        tiers_vec.emplace_back(std::move(trackers_vec));
    }

    return tr_variant{ std::move(tiers_vec) };
}

void tr_announce_list::add_to_map(tr_variant::Map& setme) const
{
    setme.erase(TR_KEY_announce);
    setme.erase(TR_KEY_announce_list);
    setme.reserve(std::size(setme) + 2U);

    if (!empty())
    {
        setme.try_emplace(TR_KEY_announce, at(0).announce.sv());
    }

    if (size() > 1U)
    {
        setme.try_emplace(TR_KEY_announce_list, tr_announce_list::to_tiers_variant());
    }
}

bool tr_announce_list::save(std::string_view torrent_file, tr_error* error) const
{
    // load the torrent file
    auto serde = tr_variant_serde::benc();
    auto ometainfo = serde.parse_file(torrent_file);
    if (!ometainfo)
    {
        if (error != nullptr)
        {
            *error = std::move(serde.error_);
            serde.error_ = {};
        }

        return false;
    }
    auto& metainfo = *ometainfo;

    // replace the trackers
    if (auto* const metainfo_map = metainfo.get_if<tr_variant::Map>(); metainfo_map != nullptr)
    {
        add_to_map(*metainfo_map);
    }

    // confirm that it's good by parsing it back again
    auto const contents = serde.to_string(metainfo);
    if (!serde.parse(contents).has_value())
    {
        return false;
    }

    // save it
    return tr_file_save(torrent_file, contents, error);
}

bool tr_announce_list::parse(std::string_view text)
{
    auto scratch = tr_announce_list{};

    auto current_tier = tr_tracker_tier_t{ 0 };
    auto current_tier_size = size_t{ 0 };
    auto line = std::string_view{};
    while (tr_strv_sep(&text, &line, '\n'))
    {
        if (tr_strv_ends_with(line, '\r'))
        {
            line = line.substr(0, std::size(line) - 1);
        }

        line = tr_strv_strip(line);

        if (std::empty(line))
        {
            if (current_tier_size > 0)
            {
                ++current_tier;
                current_tier_size = 0;
            }
        }
        else if (scratch.add(line, current_tier))
        {
            ++current_tier_size;
        }
        else
        {
            return false;
        }
    }

    *this = scratch;
    return true;
}

std::string tr_announce_list::to_string() const
{
    auto text = std::string{};
    auto current_tier = std::optional<tr_tracker_tier_t>{};

    for (auto const& tracker : *this)
    {
        if (current_tier && *current_tier != tracker.tier)
        {
            text += '\n';
        }

        text += tracker.announce.sv();
        text += '\n';

        current_tier = tracker.tier;
    }

    return text;
}
