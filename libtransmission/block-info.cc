// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/util.h>

#include "transmission.h"

#include "block-info.h"
#include "tr-assert.h"

// Decide on a block size. Constraints:
// (1) most clients decline requests over 16 KiB
// (2) pieceSize must be a multiple of block size
uint32_t tr_block_info::bestBlockSize(uint64_t piece_size)
{
    uint32_t b = piece_size;

    auto constexpr MaxBlockSize = uint32_t{ 1024 * 16 };
    while (b > MaxBlockSize)
    {
        b /= 2U;
    }

    if (b == 0 || piece_size % b != 0) // not cleanly divisible
    {
        return 0;
    }

    return b;
}

void tr_block_info::initSizes(uint64_t total_size_in, uint64_t piece_size_in)
{
    total_size = total_size_in;
    piece_size = piece_size_in;
    block_size = bestBlockSize(piece_size);

    if (piece_size == 0 || block_size == 0)
    {
        *this = {};
        return;
    }

    n_pieces = (total_size + piece_size - 1) / piece_size;

    auto remainder = total_size % piece_size;
    final_piece_size = remainder != 0U ? remainder : piece_size;

    remainder = total_size % block_size;
    final_block_size = remainder != 0U ? remainder : block_size;

    if (block_size != 0)
    {
        n_blocks = (total_size + block_size - 1) / block_size;
        n_blocks_in_piece = piece_size / block_size;
        n_blocks_in_final_piece = (final_piece_size + block_size - 1) / block_size;
    }

#ifdef TR_ENABLE_ASSERTS
    // check our work
    if (block_size != 0)
    {
        TR_ASSERT(piece_size % block_size == 0);
    }

    uint64_t t = n_pieces - 1;
    t *= piece_size;
    t += final_piece_size;
    TR_ASSERT(t == total_size);

    t = n_blocks - 1;
    t *= block_size;
    t += final_block_size;
    TR_ASSERT(t == total_size);

    t = n_pieces - 1;
    t *= n_blocks_in_piece;
    t += n_blocks_in_final_piece;
    TR_ASSERT(t == n_blocks);
#endif
}

tr_block_info::Location tr_block_info::blockLoc(tr_block_index_t block) const
{
    if (!isInitialized())
    {
        return {};
    }

    auto loc = Location{};
    loc.byte = block;
    loc.byte *= blockSize();

    loc.block = block;
    loc.block_offset = 0;

    loc.piece = pieceOf(loc.byte);
    loc.piece_offset = loc.byte - (uint64_t{ loc.piece } * pieceSize());

    // FIXME: file

    return loc;
}

tr_block_info::Location tr_block_info::blockLastLoc(tr_block_index_t block) const
{
    if (!isInitialized())
    {
        return {};
    }

    auto byte = uint64_t{ block };
    byte *= blockSize();
    byte += blockSize(block) - 1;
    return byteLoc(byte);
}

tr_block_info::Location tr_block_info::pieceLoc(tr_piece_index_t piece, uint32_t offset, uint32_t length) const
{
    if (!isInitialized())
    {
        return {};
    }

    auto loc = Location{};
    loc.byte = piece;
    loc.byte *= pieceSize();
    loc.byte += offset;
    loc.byte += length;

    loc.block = blockOf(loc.byte);
    loc.block_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.block } * blockSize()));

    loc.piece = pieceOf(loc.byte);
    loc.piece_offset = static_cast<uint32_t>(loc.byte - (uint64_t{ loc.piece } * pieceSize()));

    // FIXME: file

    return loc;
}

tr_block_info::Location tr_block_info::pieceLastLoc(tr_piece_index_t piece) const
{
    if (!isInitialized())
    {
        return {};
    }

    auto byte = uint64_t{ piece };
    byte *= pieceSize();
    byte += pieceSize(piece) - 1;
    return byteLoc(byte);
}

tr_block_info::Location tr_block_info::byteLoc(uint64_t byte) const
{
    if (!isInitialized())
    {
        return {};
    }

    auto loc = Location{};
    loc.byte = byte;

    loc.block = blockOf(loc.byte);
    loc.block_offset = loc.byte - (uint64_t{ loc.block } * blockSize());

    loc.piece = pieceOf(loc.byte);
    loc.piece_offset = loc.byte - (uint64_t{ loc.piece } * pieceSize());

    // FIXME: file

    return loc;
}
