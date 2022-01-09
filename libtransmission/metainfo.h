/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
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
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

#include "bitfield.h"
#include "magnet-metainfo.h"
#include "torrent.h"
#include "tr-macros.h"

struct tr_error;
struct tr_variant;

struct tr_metainfo_parsed
{
    tr_info info = {};
    uint64_t info_dict_size = 0;
    std::vector<tr_sha1_digest_t> pieces;
    tr_bitfield files_renamed = tr_bitfield{ 0 };

    tr_metainfo_parsed() = default;

    tr_metainfo_parsed(tr_metainfo_parsed&& that) noexcept
    {
        std::swap(this->info, that.info);
        std::swap(this->pieces, that.pieces);
        std::swap(this->info_dict_size, that.info_dict_size);
    }

    tr_metainfo_parsed(tr_metainfo_parsed const&) = delete;

    tr_metainfo_parsed& operator=(tr_metainfo_parsed const&) = delete;

    ~tr_metainfo_parsed()
    {
        tr_metainfoFree(&info);
    }
};

std::optional<tr_metainfo_parsed> tr_metainfoParse(tr_session const* session, tr_variant const* variant, tr_error** error);

/** @brief Private function that's exposed here only for unit tests */
bool tr_metainfoAppendSanitizedPathComponent(std::string& out, std::string_view in);
