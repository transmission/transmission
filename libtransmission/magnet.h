/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <string_view>

#include "transmission.h"

struct tr_variant;

struct tr_magnet_info
{
    uint8_t hash[20];

    char* displayName;

    int trackerCount;
    char** trackers;

    int webseedCount;
    char** webseeds;
};

tr_magnet_info* tr_magnetParse(std::string_view magnet_link);

void tr_magnetCreateMetainfo(tr_magnet_info const*, tr_variant*);

void tr_magnetFree(tr_magnet_info* info);
