/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cinttypes> // intX_t
#include <cstddef> // size_t
#include <ctime>

#include "transmission.h"

#include "quark.h"
#include "variant.h"

struct tr_torrent;

// defined by BEP #9
inline constexpr int METADATA_PIECE_SIZE = 1024 * 16;

void* tr_torrentGetMetadataPiece(tr_torrent* tor, int piece, size_t* len);

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, int len);

bool tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now, int* setme);

bool tr_torrentSetMetadataSizeHint(tr_torrent* tor, int64_t metadata_size);

double tr_torrentGetMetadataPercent(tr_torrent const* tor);

template<typename T>
void tr_buildMetainfoExceptInfoDict(T const& tm, tr_variant* top)
{
    tr_variantInitDict(top, 6);

    if (auto const& val = tm.comment(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_comment, val);
    }

    if (auto const& val = tm.source(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_source, val);
    }

    if (auto const& val = tm.creator(); !std::empty(val))
    {
        tr_variantDictAddStr(top, TR_KEY_created_by, val);
    }

    if (auto const val = tm.dateCreated(); val != 0)
    {
        tr_variantDictAddInt(top, TR_KEY_creation_date, val);
    }

    if (auto const& announce_list = tm.announceList(); !std::empty(announce_list))
    {
        auto const n = std::size(announce_list);
        if (n == 1)
        {
            tr_variantDictAddStr(top, TR_KEY_announce, announce_list.at(0).announce_str.sv());
        }
        else
        {
            auto* const announce_list_variant = tr_variantDictAddList(top, TR_KEY_announce_list, n);
            tr_variant* tier_variant = nullptr;
            auto current_tier = std::optional<tr_tracker_tier_t>{};
            for (auto const& tracker : announce_list)
            {
                if (!current_tier || *current_tier != tracker.tier)
                {
                    tier_variant = tr_variantListAddList(announce_list_variant, n);
                }

                tr_variantListAddStr(tier_variant, tracker.announce_str.sv());
            }
        }
    }

    if (auto const n_webseeds = tm.webseedCount(); n_webseeds > 0)
    {
        auto* const webseeds_variant = tr_variantDictAddList(top, TR_KEY_url_list, n_webseeds);
        for (size_t i = 0; i < n_webseeds; ++i)
        {
            tr_variantListAddStr(webseeds_variant, tm.webseed(i));
        }
    }

    if (tm.fileCount() == 0)
    {
        // local transmission extensions.
        // these temporary placeholders are used for magnets until we have the info dict.
        auto* const magnet_info = tr_variantDictAddDict(top, TR_KEY_magnet_info, 2);
        tr_variantDictAddStr(
            magnet_info,
            TR_KEY_info_hash,
            std::string_view{ reinterpret_cast<char const*>(std::data(tm.infoHash())), std::size(tm.infoHash()) });
        if (auto const& val = tm.name(); !std::empty(val))
        {
            tr_variantDictAddStr(magnet_info, TR_KEY_display_name, val);
        }
    }
}
