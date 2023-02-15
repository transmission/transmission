// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cstdint>
#include <cstddef> // size_t
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
        virtual bool pieceIsWanted(tr_piece_index_t piece) const = 0;

        virtual ~torrent_view() = default;
    };

    explicit tr_completion(torrent_view const* tor, tr_block_info const* block_info)
        : tor_{ tor }
        , block_info_{ block_info }
        , blocks_{ block_info_->blockCount() }
    {
        blocks_.setHasNone();
    }

    [[nodiscard]] constexpr tr_bitfield const& blocks() const noexcept
    {
        return blocks_;
    }

    [[nodiscard]] constexpr bool hasAll() const noexcept
    {
        return hasMetainfo() && blocks_.hasAll();
    }

    [[nodiscard]] TR_CONSTEXPR20 bool hasBlock(tr_block_index_t block) const
    {
        return blocks_.test(block);
    }

    [[nodiscard]] bool hasBlocks(tr_block_span_t span) const
    {
        return blocks_.count(span.begin, span.end) == span.end - span.begin;
    }

    [[nodiscard]] constexpr bool hasNone() const noexcept
    {
        return !hasMetainfo() || blocks_.hasNone();
    }

    [[nodiscard]] bool hasPiece(tr_piece_index_t piece) const
    {
        return block_info_->pieceSize() != 0 && countMissingBlocksInPiece(piece) == 0;
    }

    [[nodiscard]] constexpr uint64_t hasTotal() const noexcept
    {
        return size_now_;
    }

    [[nodiscard]] uint64_t hasValid() const;

    [[nodiscard]] auto leftUntilDone() const
    {
        return sizeWhenDone() - hasTotal();
    }

    [[nodiscard]] constexpr double percentComplete() const
    {
        auto const denom = block_info_->totalSize();
        return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] double percentDone() const
    {
        auto const denom = sizeWhenDone();
        return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
    }

    [[nodiscard]] uint64_t sizeWhenDone() const;

    [[nodiscard]] tr_completeness status() const
    {
        if (!hasMetainfo())
        {
            return TR_LEECH;
        }

        if (hasAll())
        {
            return TR_SEED;
        }

        if (size_now_ == sizeWhenDone())
        {
            return TR_PARTIAL_SEED;
        }

        return TR_LEECH;
    }

    [[nodiscard]] std::vector<uint8_t> createPieceBitfield() const;

    [[nodiscard]] size_t countMissingBlocksInPiece(tr_piece_index_t piece) const
    {
        auto const [begin, end] = block_info_->blockSpanForPiece(piece);
        return (end - begin) - blocks_.count(begin, end);
    }

    [[nodiscard]] size_t countMissingBytesInPiece(tr_piece_index_t piece) const
    {
        return block_info_->pieceSize(piece) - countHasBytesInPiece(piece);
    }

    void amountDone(float* tab, size_t n_tabs) const;

    void addBlock(tr_block_index_t block);
    void addPiece(tr_piece_index_t piece);
    void removePiece(tr_piece_index_t piece);

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

    void setHasAll() noexcept;

    void setBlocks(tr_bitfield blocks);

    void invalidateSizeWhenDone()
    {
        size_when_done_.reset();
    }

    [[nodiscard]] uint64_t countHasBytesInSpan(tr_byte_span_t) const;

    [[nodiscard]] constexpr bool hasMetainfo() const noexcept
    {
        return !std::empty(blocks_);
    }

private:
    [[nodiscard]] uint64_t computeHasValid() const;
    [[nodiscard]] uint64_t computeSizeWhenDone() const;

    [[nodiscard]] uint64_t countHasBytesInPiece(tr_piece_index_t piece) const
    {
        return countHasBytesInSpan(block_info_->byteSpanForPiece(piece));
    }

    void removeBlock(tr_block_index_t block);

    torrent_view const* tor_;
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
