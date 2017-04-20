/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#pragma once

#include "transmission.h"
#include "variant.h"

bool tr_metainfoParse(tr_session const* session, tr_variant const* variant, tr_info* setmeInfo, bool* setmeHasInfoDict,
    size_t* setmeInfoDictLength);

void tr_metainfoRemoveSaved(tr_session const* session, tr_info const* info);

char* tr_metainfoGetBasename(tr_info const*);
