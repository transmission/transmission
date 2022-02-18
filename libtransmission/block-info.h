// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "transmission.h"

struct tr_block_info
{
    uint64_t total_size = 0;
    uint64_t piece_size = 0;
    uint64_t n_pieces = 0;

    tr_block_index_t n_blocks = 0;
    tr_block_index_t n_blocks_in_piece = 0;
    tr_block_index_t n_blocks_in_final_piece = 0;
    uint32_t block_size = 0;
    uint32_t final_block_size = 0;
    uint32_t final_piece_size = 0;

    tr_block_info() = default;
    tr_block_info(uint64_t total_size_in, uint64_t piece_size_in)
    {
        initSizes(total_size_in, piece_size_in);
    }

    void initSizes(uint64_t total_size_in, uint64_t piece_size_in);

    [[nodiscard]] constexpr auto blockCount() const
    {
        return n_blocks;
    }

    [[nodiscard]] constexpr auto blockSize() const
    {
        return block_size;
    }

    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const
    {
        // how many bytes are in this block?
        return block + 1 == n_blocks ? final_block_size : blockSize();
    }

    [[nodiscard]] constexpr auto pieceCount() const
    {
        return n_pieces;
    }

    [[nodiscard]] constexpr auto pieceSize() const
    {
        return piece_size;
    }

    [[nodiscard]] constexpr tr_piece_index_t pieceForBlock(tr_block_index_t block) const
    {
        if (!isInitialized())
        {
            return {};
        }

        return block / n_blocks_in_piece;
    }

    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const
    {
        // how many bytes are in this piece?
        return piece + 1 == n_pieces ? final_piece_size : pieceSize();
    }

    [[nodiscard]] constexpr tr_piece_index_t pieceOf(uint64_t offset) const
    {
        if (!isInitialized())
        {
            return {};
        }

        // handle 0-byte files at the end of a torrent
        if (offset == total_size)
        {
            return n_pieces - 1;
        }

        return offset / piece_size;
    }

    [[nodiscard]] constexpr uint64_t offset(tr_piece_index_t piece, uint32_t offset, uint32_t length = 0) const
    {
        auto ret = piece_size;
        ret *= piece;
        ret += offset;
        ret += length;
        return ret;
    }

    [[nodiscard]] constexpr tr_block_span_t blockSpanForPiece(tr_piece_index_t piece) const
    {
        if (!isInitialized())
        {
            return {};
        }

        auto const begin = blockOf(offset(piece, 0));
        auto const end = 1 + blockOf(offset(piece, pieceSize(piece) - 1));
        return { begin, end };
    }

    [[nodiscard]] constexpr auto totalSize() const
    {
        return total_size;
    }

    struct Location
    {
        uint64_t byte = 0;

        tr_piece_index_t piece = 0;
        uint32_t piece_offset = 0;

        tr_block_index_t block = 0;
        uint32_t block_offset = 0;

        // TODO
        // tr_file_index_t file = 0;
        // uint32_t file_offset = 0;a

        bool operator==(Location const& that) const
        {
            return this->byte == that.byte;
        }

        bool operator<(Location const& that) const
        {
            return this->byte < that.byte;
        }
    };

    // Location of the first byte in `block`.
    [[nodiscard]] Location blockLoc(tr_block_index_t block) const;

    // Location of the last byte in `block`.
    [[nodiscard]] Location blockLastLoc(tr_block_index_t block) const;

    // Location of the first byte (+ optional offset and length) in `piece`
    [[nodiscard]] Location pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const;

    // Location of the last byte in `piece`.
    [[nodiscard]] Location pieceLastLoc(tr_piece_index_t piece) const;

    [[nodiscard]] Location byteLoc(uint64_t byte) const;

    struct Span
    {
        Location begin;
        Location end;
    };

    [[nodiscard]] static uint32_t bestBlockSize(uint64_t piece_size);

private:
    [[nodiscard]] bool constexpr isInitialized() const
    {
        return n_blocks_in_piece != 0;
    }

    [[nodiscard]] constexpr tr_block_index_t blockOf(uint64_t offset) const
    {
        if (!isInitialized())
        {
            return {};
        }

        // handle 0-byte files at the end of a torrent
        if (offset == total_size)
        {
            return n_blocks - 1;
        }

        return offset / block_size;
    }
};
