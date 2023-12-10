// This file Copyright © Mnemosyne LLC.
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
#include <deque>
#include <optional>
#include <vector>

#include <small/vector.hpp>

struct tr_error;
struct tr_torrent;
struct tr_torrent_metainfo;

// defined by BEP #9
inline constexpr int MetadataPieceSize = 1024 * 16;

using tr_metadata_piece = small::max_size_vector<std::byte, MetadataPieceSize>;

struct tr_incomplete_metadata
{
    struct metadata_node
    {
        time_t requested_at = 0U;
        int piece = 0;
    };

    std::vector<char> metadata;

    /** sorted from least to most recently requested */
    std::deque<metadata_node> pieces_needed;

    int piece_count = 0;
};

bool tr_torrentGetMetadataPiece(tr_torrent const* tor, int piece, tr_metadata_piece& setme);

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, size_t len);

std::optional<int> tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now);

bool tr_torrentSetMetadataSizeHint(tr_torrent* tor, int64_t metadata_size);

double tr_torrentGetMetadataPercent(tr_torrent const* tor);

void tr_torrentMagnetDoIdleWork(tr_torrent* tor);

bool tr_torrentUseMetainfoFromFile(tr_torrent* tor, tr_torrent_metainfo const* metainfo, char const* filename, tr_error* error);
