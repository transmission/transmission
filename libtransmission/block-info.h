// This file Copyright © 2021-2023 Mnemosyne LLC.
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

    struct Location
    {
        uint64_t byte = 0;

        tr_piece_index_t piece = 0;
        uint32_t piece_offset = 0;

        tr_block_index_t block = 0;
        uint32_t block_offset = 0;

        [[nodiscard]] constexpr bool operator==(Location const& that) const noexcept
        {
            return this->byte == that.byte;
        }

        [[nodiscard]] constexpr bool operator<(Location const& that) const noexcept
        {
            return this->byte < that.byte;
        }
    };

    // Location of the torrent's nth byte
    [[nodiscard]] constexpr auto byteLoc(uint64_t byte_idx) const noexcept
    {
        auto loc = Location{};

        if (isInitialized())
        {
            loc.byte = byte_idx;

            if (byte_idx == totalSize()) // handle 0-byte files at the end of a torrent
            {
                loc.block = blockCount() - 1;
                loc.piece = pieceCount() - 1;
            }
            else
            {
                loc.block = byte_idx / BlockSize;
                loc.piece = byte_idx / pieceSize();
            }

            loc.block_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.block } * BlockSize));
            loc.piece_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.piece } * pieceSize()));
        }

        return loc;
    }

    // Location of the first byte in `block`.
    [[nodiscard]] constexpr auto blockLoc(tr_block_index_t block) const noexcept
    {
        return byteLoc(uint64_t{ block } * BlockSize);
    }

    // Location of the first byte (+ optional offset and length) in `piece`
    [[nodiscard]] constexpr auto pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const noexcept
    {
        return byteLoc(uint64_t{ piece } * pieceSize() + offset + length);
    }

    [[nodiscard]] constexpr tr_block_span_t blockSpanForPiece(tr_piece_index_t piece) const noexcept
    {
        if (!isInitialized())
        {
            return { 0U, 0U };
        }

        return { pieceLoc(piece).block, pieceLastLoc(piece).block + 1 };
    }

    [[nodiscard]] constexpr tr_byte_span_t byteSpanForPiece(tr_piece_index_t piece) const noexcept
    {
        if (!isInitialized())
        {
            return { 0U, 0U };
        }

        auto const offset = pieceLoc(piece).byte;
        return { offset, offset + pieceSize(piece) };
    }

private:
    // Location of the last byte in `piece`.
    [[nodiscard]] constexpr Location pieceLastLoc(tr_piece_index_t piece) const noexcept
    {
        return byteLoc(static_cast<uint64_t>(piece) * pieceSize() + pieceSize(piece) - 1);
    }

    [[nodiscard]] constexpr bool isInitialized() const noexcept
    {
        return piece_size_ != 0;
    }
};
