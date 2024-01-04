// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef> // size_t
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/interned-string.h"
#include "libtransmission/tr-macros.h" // TR_CONSTEXPR20
#include "libtransmission/variant.h"
#include "libtransmission/web-utils.h"

struct tr_error;
struct tr_url_parsed_t;

class tr_announce_list
{
public:
    struct tracker_info
    {
        tr_interned_string announce;
        tr_interned_string scrape;
        tr_url_parsed_t announce_parsed;
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

    void add_to_map(tr_variant::Map& setme) const;

    bool add(std::string_view announce_url)
    {
        return add(announce_url, this->nextTier());
    }

    bool add(std::string_view announce_url, tr_tracker_tier_t tier);
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
    [[nodiscard]] std::string to_string() const;

    bool save(std::string_view torrent_file, tr_error* error = nullptr) const;

    [[nodiscard]] static std::optional<std::string> announce_to_scrape(std::string_view announce);

private:
    [[nodiscard]] tr_variant to_tiers_variant() const;

    [[nodiscard]] tr_tracker_tier_t get_tier(tr_tracker_tier_t tier, tr_url_parsed_t const& announce) const;

    [[nodiscard]] bool can_add(tr_url_parsed_t const& announce) const noexcept;
    static tr_tracker_id_t next_unique_id();
    trackers_t::iterator find(std::string_view announce);
    trackers_t::iterator find(tr_tracker_id_t id);

    trackers_t trackers_;
};
