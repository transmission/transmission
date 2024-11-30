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
#include <string>
#include <string_view>
#include <vector>

#include <small/vector.hpp>

#include "libtransmission/tr-macros.h"

// defined by BEP #9
inline constexpr auto MetadataPieceSize = 1024 * 16;

using tr_metadata_piece = small::max_size_vector<std::byte, MetadataPieceSize>;

class tr_metadata_download
{
public:
    tr_metadata_download(std::string_view log_name, int64_t size);

    [[nodiscard]] static constexpr auto is_valid_metadata_size(int64_t const size) noexcept
    {
        return size > 0 && size <= std::numeric_limits<int>::max();
    }

    bool set_metadata_piece(int64_t piece, void const* data, size_t len);

    [[nodiscard]] std::optional<int64_t> get_next_metadata_request(time_t now) noexcept;

    [[nodiscard]] double get_metadata_percent() const noexcept;

    [[nodiscard]] constexpr auto const& get_metadata() const noexcept
    {
        return metadata_;
    }

    [[nodiscard]] TR_CONSTEXPR20 std::string_view log_name() const noexcept
    {
        return log_name_;
    }

private:
    struct metadata_node
    {
        time_t requested_at = {};
        int64_t piece = {};
    };

    [[nodiscard]] constexpr size_t get_piece_length(int64_t const piece) const noexcept
    {
        return piece + 1 == piece_count_ ? // last piece
            std::size(metadata_) - (piece * MetadataPieceSize) :
            MetadataPieceSize;
    }

    void create_all_needed(int64_t n_pieces) noexcept;

    std::vector<char> metadata_;
    std::deque<metadata_node> pieces_needed_;
    int64_t piece_count_ = {};

    std::string log_name_;
};
