// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // int64_t
#include <ctime> // time_t
#include <optional>
#include <vector>

#include "transmission.h"

struct tr_torrent;
struct tr_torrent_metainfo;

// defined by BEP #9
inline constexpr int METADATA_PIECE_SIZE = 1024 * 16;

std::optional<std::vector<std::byte>> tr_torrentGetMetadataPiece(tr_torrent const* tor, int piece);

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, size_t len);

std::optional<int> tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now);

bool tr_torrentSetMetadataSizeHint(tr_torrent* tor, int64_t metadata_size);

double tr_torrentGetMetadataPercent(tr_torrent const* tor);

void tr_torrentMagnetDoIdleWork(tr_torrent* tor);

bool tr_torrentUseMetainfoFromFile(
    tr_torrent* tor,
    tr_torrent_metainfo const* metainfo,
    char const* filename,
    tr_error** error);
