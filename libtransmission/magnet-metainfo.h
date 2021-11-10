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

#include <map>
#include <string>
#include <string_view>
#include <vector>

struct tr_variant;

#include "transmission.h"

#include "error.h"
#include "quark.h"

struct tr_magnet_metainfo
{
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);

    std::string magnet() const;

    void toVariant(tr_variant*) const;

    static bool convertAnnounceToScrape(std::string& setme, std::string_view announce_url);

    std::string_view infoHashString() const
    {
        // trim one byte off the end because of zero termination
        return std::string_view{ std::data(info_hash_chars), std::size(info_hash_chars) - 1 };
    }

    struct tracker_t
    {
        tr_quark announce_url;
        tr_quark scrape_url;
        tr_tracker_tier_t tier;

        tracker_t(tr_quark announce_in, tr_quark scrape_in, tr_tracker_tier_t tier_in)
            : announce_url{ announce_in }
            , scrape_url{ scrape_in }
            , tier{ tier_in }
        {
        }

        bool operator<(tracker_t const& that) const
        {
            return announce_url < that.announce_url;
        }
    };

    std::vector<std::string> webseed_urls;

    std::string name;

    std::multimap<tr_tracker_tier_t, tracker_t> trackers;

    tr_sha1_digest_string_t info_hash_chars;

    tr_sha1_digest_t info_hash;

protected:
    bool addTracker(tr_tracker_tier_t tier, std::string_view announce_url);
};
