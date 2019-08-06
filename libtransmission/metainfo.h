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

#include "transmission.h"
#include "variant.h"

enum tr_metainfo_basename_format
{
    TR_METAINFO_BASENAME_NAME_AND_PARTIAL_HASH,
    TR_METAINFO_BASENAME_HASH
};

bool tr_metainfoParse(tr_session const* session, tr_variant const* variant, tr_info* setmeInfo, bool* setmeHasInfoDict,
    size_t* setmeInfoDictLength);

void tr_metainfoRemoveSaved(tr_session const* session, tr_info const* info);

char* tr_metainfoGetBasename(tr_info const*, enum tr_metainfo_basename_format format);

void tr_metainfoMigrateFile(tr_session const* session, tr_info const* info, enum tr_metainfo_basename_format old_format,
    enum tr_metainfo_basename_format new_format);

/** @brief Private function that's exposed here only for unit tests */
char* tr_metainfo_sanitize_path_component(char const* str, size_t len, bool* is_adjusted);
