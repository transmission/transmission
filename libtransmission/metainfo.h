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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "transmission.h"

struct tr_error;
struct tr_variant;

enum tr_metainfo_basename_format
{
    TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH,
    TR_METAINFO_BASENAME_HASH
};

struct tr_metainfo_parsed
{
    tr_info info = {};
    uint64_t info_dict_length = 0;
    std::vector<tr_sha1_digest_t> pieces;

    tr_metainfo_parsed() = default;

    tr_metainfo_parsed(tr_metainfo_parsed&& that) noexcept
    {
        std::swap(this->info, that.info);
        std::swap(this->pieces, that.pieces);
        std::swap(this->info_dict_length, that.info_dict_length);
    }

    tr_metainfo_parsed(tr_metainfo_parsed const&) = delete;

    tr_metainfo_parsed& operator=(tr_metainfo_parsed const&) = delete;

    ~tr_metainfo_parsed()
    {
        tr_metainfoFree(&info);
    }
};

std::optional<tr_metainfo_parsed> tr_metainfoParse(tr_session const* session, tr_variant const* variant, tr_error** error);

void tr_metainfoRemoveSaved(tr_session const* session, tr_info const* info);

std::string tr_buildTorrentFilename(
    std::string_view dirname,
    tr_info const* inf,
    enum tr_metainfo_basename_format format,
    std::string_view suffix);

void tr_metainfoMigrateFile(
    tr_session const* session,
    tr_info const* info,
    enum tr_metainfo_basename_format old_format,
    enum tr_metainfo_basename_format new_format);

/** @brief Private function that's exposed here only for unit tests */
bool tr_metainfoAppendSanitizedPathComponent(std::string& out, std::string_view in, bool* is_adjusted);
