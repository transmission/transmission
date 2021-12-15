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

#include "transmission.h"

#include "announce-list.h"
#include "quark.h"

struct tr_error;
struct tr_variant;

struct tr_magnet_metainfo
{
    bool parseMagnet(std::string_view magnet_link, tr_error** error = nullptr);

    std::string magnet() const;

    void toVariant(tr_variant*) const;

    std::string_view infoHashString() const
    {
        // trim one byte off the end because of zero termination
        return std::string_view{ std::data(info_hash_chars), std::size(info_hash_chars) - 1 };
    }

    tr_announce_list announce_list;

    std::vector<std::string> webseed_urls;

    std::string name;

    tr_sha1_digest_string_t info_hash_chars;

    tr_sha1_digest_t info_hash;
};
