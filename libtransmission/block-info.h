// This file Copyright © 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "transmission.h"

#include "tr-assert.h"

struct tr_block_info
{
    static auto constexpr BlockSize = uint32_t{ 1024 * 16 };

    uint64_t total_size = 0;
    uint64_t piece_size = 0;
    uint64_t n_pieces = 0;

    tr_block_index_t n_blocks = 0;
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

    // return the number of bytes in `block`
    [[nodiscard]] constexpr auto blockSize(tr_block_index_t block) const
    {
        return block + 1 == n_blocks ? final_block_size : BlockSize;
    }

    [[nodiscard]] constexpr auto pieceCount() const
    {
        return n_pieces;
    }

    [[nodiscard]] constexpr auto pieceSize() const
    {
        return piece_size;
    }

    // return the number of bytes in `piece`
    [[nodiscard]] constexpr auto pieceSize(tr_piece_index_t piece) const
    {
        return piece + 1 == n_pieces ? final_piece_size : pieceSize();
    }

    [[nodiscard]] constexpr tr_block_span_t blockSpanForPiece(tr_piece_index_t piece) const
    {
        if (!isInitialized())
        {
            return {};
        }

        return { pieceLoc(piece).block, pieceLastLoc(piece).block + 1 };
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
    [[nodiscard]] Location constexpr blockLoc(tr_block_index_t block) const
    {
        TR_ASSERT(block < n_blocks);

        return byteLoc(uint64_t{ block } * BlockSize);
    }

    // Location of the last byte in `block`.
    [[nodiscard]] Location constexpr blockLastLoc(tr_block_index_t block) const
    {
        if (!isInitialized())
        {
            return {};
        }

        return byteLoc(uint64_t{ block } * BlockSize + blockSize(block) - 1);
    }

    // Location of the first byte (+ optional offset and length) in `piece`
    [[nodiscard]] Location constexpr pieceLoc(tr_piece_index_t piece, uint32_t offset = 0, uint32_t length = 0) const
    {
        TR_ASSERT(piece < n_pieces);

        return byteLoc(uint64_t{ piece } * pieceSize() + offset + length);
    }

    // Location of the last byte in `piece`.
    [[nodiscard]] Location constexpr pieceLastLoc(tr_piece_index_t piece) const
    {
        if (!isInitialized())
        {
            return {};
        }

        return byteLoc(uint64_t{ piece } * pieceSize() + pieceSize(piece) - 1);
    }

    // Location of the torrent's nth byte
    [[nodiscard]] Location constexpr byteLoc(uint64_t byte) const
    {
        TR_ASSERT(byte <= total_size);

        if (!isInitialized())
        {
            return {};
        }

        auto loc = Location{};

        loc.byte = byte;

        if (byte == totalSize()) // handle 0-byte files at the end of a torrent
        {
            loc.block = blockCount() - 1;
            loc.piece = pieceCount() - 1;
        }
        else
        {
            loc.block = byte / BlockSize;
            loc.piece = byte / pieceSize();
        }

        loc.block_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.block } * BlockSize));
        loc.piece_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.piece } * pieceSize()));

        return loc;
    }

    [[nodiscard]] static uint32_t bestBlockSize(uint64_t piece_size);

private:
    [[nodiscard]] bool constexpr isInitialized() const
    {
        return piece_size != 0;
    }
};
