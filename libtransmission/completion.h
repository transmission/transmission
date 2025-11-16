// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstddef> // size_t
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/block-info.h"
#include "libtransmission/bitfield.h"
#include "libtransmission/tr-macros.h"

/**
 * @brief knows which blocks and pieces we have
 */
struct tr_completion
{
    using PieceIsWantedFunc = std::function<bool(tr_piece_index_t piece)>;

    tr_completion(PieceIsWantedFunc&& piece_is_wanted, tr_block_info const* block_info)
        : piece_is_wanted_{ std::move(piece_is_wanted) }
        , block_info_{ block_info }
        , blocks_{ block_info_->block_count() }
    {
        blocks_.set_has_none();
    }

    tr_completion(tr_torrent const* tor, tr_block_info const* block_info);

    [[nodiscard]] constexpr tr_bitfield const& blocks() const noexcept
    {
        return blocks_;
    }

    [[nodiscard]] constexpr bool has_all() const noexcept
    {
        return has_metainfo() && blocks_.has_all();
    }

    [[nodiscard]] TR_CONSTEXPR20 bool has_block(tr_block_index_t block) const
    {
        return blocks_.test(block);
    }

    [[nodiscard]] bool has_blocks(tr_block_span_t span) const
    {
        return blocks_.count(span.begin, span.end) == span.end - span.begin;
    }

    [[nodiscard]] constexpr bool has_none() const noexcept
    {
        return !has_metainfo() || blocks_.has_none();
    }

    [[nodiscard]] bool has_piece(tr_piece_index_t piece) const
    {
        return block_info_->piece_size() != 0 && count_missing_blocks_in_piece(piece) == 0;
    }

    [[nodiscard]] constexpr uint64_t has_total() const noexcept
    {
        return size_now_;
    }

    [[nodiscard]] uint64_t has_valid() const;

    [[nodiscard]] auto left_until_done() const
    {
        return size_when_done() - has_total();
    }

    [[nodiscard]] constexpr double percent_complete() const
    {
        auto const denom = block_info_->total_size();
        return denom != 0U ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] double percent_done() const
    {
        auto const denom = size_when_done();
        return denom != 0U ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] uint64_t size_when_done() const;

    [[nodiscard]] tr_completeness status() const
    {
        if (!has_metainfo())
        {
            return TR_LEECH;
        }

        if (has_all())
        {
            return TR_SEED;
        }

        if (size_now_ == size_when_done())
        {
            return TR_PARTIAL_SEED;
        }

        return TR_LEECH;
    }

    [[nodiscard]] std::vector<uint8_t> create_piece_bitfield() const;

    [[nodiscard]] size_t count_missing_blocks_in_piece(tr_piece_index_t piece) const
    {
        auto const [begin, end] = block_info_->block_span_for_piece(piece);
        return (end - begin) - blocks_.count(begin, end);
    }

    [[nodiscard]] size_t count_missing_bytes_in_piece(tr_piece_index_t piece) const
    {
        return block_info_->piece_size(piece) - count_has_bytes_in_piece(piece);
    }

    void amount_done(float* tab, size_t n_tabs) const;

    void add_block(tr_block_index_t block);
    void add_piece(tr_piece_index_t piece);
    void remove_piece(tr_piece_index_t piece);

    void set_has_piece(tr_piece_index_t i, bool has)
    {
        if (has)
        {
            add_piece(i);
        }
        else
        {
            remove_piece(i);
        }
    }

    void set_has_all() noexcept;

    void set_blocks(tr_bitfield blocks);

    void invalidate_size_when_done()
    {
        size_when_done_.reset();
    }

    [[nodiscard]] uint64_t count_has_bytes_in_span(tr_byte_span_t span) const;

    [[nodiscard]] constexpr bool has_metainfo() const noexcept
    {
        return !std::empty(blocks_);
    }

private:
    [[nodiscard]] uint64_t compute_has_valid() const;
    [[nodiscard]] uint64_t compute_size_when_done() const;

    [[nodiscard]] uint64_t count_has_bytes_in_piece(tr_piece_index_t piece) const
    {
        return count_has_bytes_in_span(block_info_->byte_span_for_piece(piece));
    }

    void remove_block(tr_block_index_t block);

    PieceIsWantedFunc piece_is_wanted_;
    tr_block_info const* block_info_;

    tr_bitfield blocks_{ 0 };

    // Number of bytes we'll have when done downloading. [0..totalSize]
    // Mutable because lazy-calculated
    mutable std::optional<uint64_t> size_when_done_;

    // Number of verified bytes we have right now. [0..totalSize]
    // Mutable because lazy-calculated
    mutable std::optional<uint64_t> has_valid_;

    // Number of bytes we have now. [0..sizeWhenDone]
    uint64_t size_now_ = 0;
};
