// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint32_t, uint64_t

#include "transmission.h"

struct tr_block_info
{
private:
    uint64_t total_size_ = 0;
    uint32_t piece_size_ = 0;
    tr_piece_index_t n_pieces_ = 0;

    tr_block_index_t n_blocks_ = 0;
    // should be same type as BlockSize
    uint32_t final_block_size_ = 0;
    // should be same type as piece_size
    uint32_t final_piece_size_ = 0;

public:
    static auto constexpr BlockSize = uint32_t{ 1024 * 16 };

    tr_block_info() noexcept = default;

    tr_block_info(uint64_t total_size_in, uint32_t piece_size_in) noexcept
    {
        initSizes(total_size_in, piece_size_in);
    }

    void initSizes(uint64_t total_size_in, uint32_t piece_size_in) noexcept;

    [[nodiscard]] constexpr auto blockCount() const noexcept
    {
        return n_blocks_;
    }

    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const noexcept
    {
        return block + 1 == n_blocks_ ? final_block_size_ : BlockSize;
    }

    [[nodiscard]] constexpr auto pieceCount() const noexcept
    {
        return n_pieces_;
    }

    [[nodiscard]] constexpr auto pieceSize() const noexcept
    {
        return piece_size_;
    }

    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const noexcept
    {
        return piece + 1 == n_pieces_ ? final_piece_size_ : pieceSize();
    }

    [[nodiscard]] constexpr auto totalSize() const noexcept
    {
        return total_size_;
    }

    [[nodiscard]] tr_block_span_t blockSpanForPiece(tr_piece_index_t piece) const noexcept
    {
        if (!isInitialized())
        {
            return { 0U, 0U };
        }

        return { pieceLoc(piece).block, pieceLastLoc(piece).block + 1 };
    }

    [[nodiscard]] tr_byte_span_t byteSpanForPiece(tr_piece_index_t piece) const noexcept
    {
        if (!isInitialized())
        {
            return { 0U, 0U };
        }

        auto const offset = pieceLoc(piece).byte;
        return { offset, offset + pieceSize(piece) };
    }

    struct Location
    {
        uint64_t byte = 0;

        tr_piece_index_t piece = 0;
        uint32_t piece_offset = 0;

        tr_block_index_t block = 0;
        uint32_t block_offset = 0;

        [[nodiscard]] bool operator==(Location const& that) const
        {
            return this->byte == that.byte;
        }

        [[nodiscard]] bool operator<(Location const& that) const
        {
            return this->byte < that.byte;
        }
    };

    // Location of the first byte in `block`.
    [[nodiscard]] Location blockLoc(tr_block_index_t block) const noexcept;

    // Location of the first byte (+ optional offset and length) in `piece`
    [[nodiscard]] Location pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const noexcept;

    // Location of the torrent's nth byte
    [[nodiscard]] Location byteLoc(uint64_t byte_idx) const noexcept;

private:
    // Location of the last byte in `piece`.
    [[nodiscard]] Location pieceLastLoc(tr_piece_index_t piece) const
    {
        return byteLoc(static_cast<uint64_t>(piece) * pieceSize() + pieceSize(piece) - 1);
    }

    [[nodiscard]] bool constexpr isInitialized() const noexcept
    {
        return piece_size_ != 0;
    }
};
