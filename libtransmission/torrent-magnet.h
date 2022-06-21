// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // int64_t
#include <cstddef> // size_t
#include <ctime> // time_t

#include "transmission.h"

struct tr_torrent;
struct tr_torrent_metainfo;

// defined by BEP #9
inline constexpr int METADATA_PIECE_SIZE = 1024 * 16;

void* tr_torrentGetMetadataPiece(tr_torrent const* tor, int piece, size_t* len);

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, int len);

bool tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now, int* setme);

bool tr_torrentSetMetadataSizeHint(tr_torrent* tor, int64_t metadata_size);

double tr_torrentGetMetadataPercent(tr_torrent const* tor);

bool tr_torrentUseMetainfoFromFile(
    tr_torrent* tor,
    tr_torrent_metainfo const* metainfo,
    char const* filename,
    tr_error** error);
