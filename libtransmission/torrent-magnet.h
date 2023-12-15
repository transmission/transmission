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
#include <limits>
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

    [[nodiscard]] static constexpr auto is_valid_metadata_size(int64_t const size) noexcept
    {
        return size > 0 && size <= std::numeric_limits<int>::max();
    }

    [[nodiscard]] constexpr size_t get_piece_length(int const piece) const noexcept
    {
        return piece + 1 == piece_count ? // last piece
            std::size(metadata) - (piece * MetadataPieceSize) :
            MetadataPieceSize;
    }

    std::vector<char> metadata;

    /** sorted from least to most recently requested */
    std::deque<metadata_node> pieces_needed;

    int piece_count = 0;
};

void tr_torrentSetMetadataPiece(tr_torrent* tor, int piece, void const* data, size_t len);

std::optional<int> tr_torrentGetNextMetadataRequest(tr_torrent* tor, time_t now);

double tr_torrentGetMetadataPercent(tr_torrent const* tor);

void tr_torrentMagnetDoIdleWork(tr_torrent* tor);
