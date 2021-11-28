/*
 * This file Copyright (C) 2009-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "transmission.h"

#include "block-info.h"
#include "bitfield.h"

/**
 * @brief knows which blocks and pieces we have
 */
struct tr_completion
{
    struct torrent_view
    {
        virtual bool pieceIsDnd(tr_piece_index_t piece) const = 0;

        virtual ~torrent_view() = default;
    };

    explicit tr_completion(torrent_view const* tor, tr_block_info const* block_info)
        : tor_{ tor }
        , block_info_{ block_info }
        , blocks_{ block_info_->n_blocks }
    {
        blocks_.setHasNone();
    }

    [[nodiscard]] constexpr tr_bitfield const& blocks() const
    {
        return blocks_;
    }

    [[nodiscard]] constexpr bool hasAll() const
    {
        return hasMetainfo() && blocks_.hasAll();
    }

    [[nodiscard]] bool hasBlock(tr_block_index_t block) const
    {
        return blocks_.test(block);
    }

    [[nodiscard]] bool hasBlocks(tr_block_span_t span) const
    {
        return blocks_.count(span.begin, span.end) == span.end - span.begin;
    }

    [[nodiscard]] constexpr bool hasNone() const
    {
        return !hasMetainfo() || blocks_.hasNone();
    }

    [[nodiscard]] bool hasPiece(tr_piece_index_t piece) const
    {
        return block_info_->piece_size != 0 && countMissingBlocksInPiece(piece) == 0;
    }

    [[nodiscard]] constexpr uint64_t hasTotal() const
    {
        return size_now_;
    }

    [[nodiscard]] uint64_t hasValid() const;

    [[nodiscard]] bool isDone() const
    {
        return hasMetainfo() && leftUntilDone() == 0;
    }

    [[nodiscard]] uint64_t leftUntilDone() const;

    [[nodiscard]] constexpr double percentComplete() const
    {
        auto const denom = block_info_->total_size;
        return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] double percentDone() const
    {
        auto const denom = sizeWhenDone();
        return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] uint64_t sizeWhenDone() const;

    [[nodiscard]] tr_completeness status() const;

    [[nodiscard]] std::vector<uint8_t> createPieceBitfield() const;

    [[nodiscard]] size_t countMissingBlocksInPiece(tr_piece_index_t) const;
    [[nodiscard]] size_t countMissingBytesInPiece(tr_piece_index_t) const;

    void amountDone(float* tab, size_t n_tabs) const;

    void addBlock(tr_block_index_t i);
    void addPiece(tr_piece_index_t i);
    void removePiece(tr_piece_index_t i);

    void setHasPiece(tr_piece_index_t i, bool has)
    {
        if (has)
        {
            addPiece(i);
        }
        else
        {
            removePiece(i);
        }
    }

    void setBlocks(tr_bitfield blocks);

    void invalidateSizeWhenDone()
    {
        size_when_done_.reset();
    }

private:
    [[nodiscard]] constexpr bool hasMetainfo() const
    {
        return !std::empty(blocks_);
    }

    [[nodiscard]] uint64_t computeHasValid() const;
    [[nodiscard]] uint64_t computeSizeWhenDone() const;
    [[nodiscard]] uint64_t countHasBytesInSpan(tr_block_span_t) const;

    torrent_view const* tor_;
    tr_block_info const* block_info_;

    tr_bitfield blocks_{ 0 };

    // Number of bytes we'll have when done downloading. [0..info.totalSize]
    // Mutable because lazy-calculated
    mutable std::optional<uint64_t> size_when_done_;

    // Number of verified bytes we have right now. [0..info.totalSize]
    // Mutable because lazy-calculated
    mutable std::optional<uint64_t> has_valid_;

    // Number of bytes we have now. [0..sizeWhenDone]
    uint64_t size_now_ = 0;
};
