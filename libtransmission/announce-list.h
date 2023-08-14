// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "quark.h"
#include "interned-string.h"

struct tr_error;
struct tr_url_parsed_t;

class tr_announce_list
{
public:
    struct tracker_info
    {
        tr_interned_string announce;
        tr_interned_string scrape;
        tr_interned_string host; // 'example.org:80'
        tr_interned_string sitename; // 'example'
        tr_interned_string query; // 'name=ferret'
        tr_tracker_tier_t tier = 0;
        tr_tracker_id_t id = 0;

        [[nodiscard]] constexpr int compare(tracker_info const& that) const noexcept // <=>
        {
            if (this->tier != that.tier)
            {
                return this->tier < that.tier ? -1 : 1;
            }

            if (int const i{ this->announce.compare(that.announce) }; i != 0)
            {
                return i;
            }

            return 0;
        }

        [[nodiscard]] constexpr bool operator<(tracker_info const& that) const noexcept
        {
            return compare(that) < 0;
        }

        [[nodiscard]] constexpr bool operator==(tracker_info const& that) const noexcept
        {
            return compare(that) == 0;
        }
    };

private:
    using trackers_t = std::vector<tracker_info>;

public:
    [[nodiscard]] TR_CONSTEXPR20 auto begin() const noexcept
    {
        return std::begin(trackers_);
    }

    [[nodiscard]] TR_CONSTEXPR20 auto end() const noexcept
    {
        return std::end(trackers_);
    }

    [[nodiscard]] TR_CONSTEXPR20 bool empty() const noexcept
    {
        return std::empty(trackers_);
    }

    [[nodiscard]] TR_CONSTEXPR20 size_t size() const noexcept
    {
        return std::size(trackers_);
    }

    [[nodiscard]] TR_CONSTEXPR20 tracker_info const& at(size_t i) const
    {
        return trackers_.at(i);
    }

    [[nodiscard]] tr_tracker_tier_t nextTier() const;

    [[nodiscard]] TR_CONSTEXPR20 bool operator==(tr_announce_list const& that) const
    {
        return trackers_ == that.trackers_;
    }

    [[nodiscard]] bool operator!=(tr_announce_list const& that) const
    {
        return trackers_ != that.trackers_;
    }

    bool add(std::string_view announce_url_sv)
    {
        return add(announce_url_sv, this->nextTier());
    }

    bool add(std::string_view announce_url_sv, tr_tracker_tier_t tier);
    void add(tr_announce_list const& src);
    bool remove(std::string_view announce_url);
    bool remove(tr_tracker_id_t id);
    bool replace(tr_tracker_id_t id, std::string_view announce_url_sv);
    size_t set(char const* const* announce_urls, tr_tracker_tier_t const* tiers, size_t n);

    TR_CONSTEXPR20 void clear()
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

    bool save(std::string_view torrent_file, tr_error** error = nullptr) const;

    [[nodiscard]] static std::optional<std::string> announceToScrape(std::string_view announce);
    [[nodiscard]] static tr_quark announceToScrape(tr_quark announce);

private:
    [[nodiscard]] tr_tracker_tier_t getTier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const;

    bool canAdd(tr_url_parsed_t const& announce);
    static tr_tracker_id_t nextUniqueId();
    trackers_t::iterator find(std::string_view announce);
    trackers_t::iterator find(tr_tracker_id_t id);

    trackers_t trackers_;
};
