// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/util.h>

#include "transmission.h"

#include "block-info.h"
#include "tr-assert.h"

void tr_block_info::initSizes(uint64_t total_size_in, uint64_t piece_size_in) noexcept
{
    total_size = total_size_in;
    piece_size = piece_size_in;

    TR_ASSERT(piece_size == 0 || piece_size >= BlockSize);

    if (piece_size == 0)
    {
        *this = {};
        return;
    }

    n_pieces = (total_size + piece_size - 1) / piece_size;

    auto remainder = total_size % piece_size;
    final_piece_size = remainder != 0U ? remainder : piece_size;

    remainder = total_size % BlockSize;
    final_block_size = remainder != 0U ? remainder : BlockSize;

    n_blocks = (total_size + BlockSize - 1) / BlockSize;

#ifdef TR_ENABLE_ASSERTS
    uint64_t t = n_pieces - 1;
    t *= piece_size;
    t += final_piece_size;
    TR_ASSERT(t == total_size);

    t = n_blocks - 1;
    t *= BlockSize;
    t += final_block_size;
    TR_ASSERT(t == total_size);
#endif
}

tr_block_info::Location tr_block_info::byteLoc(uint64_t byte_idx) const noexcept
{
    TR_ASSERT(byte_idx <= total_size);

    if (!isInitialized())
    {
        return {};
    }

    auto loc = Location{};

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

    return loc;
}

tr_block_info::Location tr_block_info::blockLoc(tr_block_index_t block) const noexcept
{
    TR_ASSERT(block < n_blocks);

    return byteLoc(uint64_t{ block } * BlockSize);
}

tr_block_info::Location tr_block_info::pieceLoc(tr_piece_index_t piece, uint32_t offset, uint32_t length) const noexcept
{
    TR_ASSERT(piece < n_pieces);

    return byteLoc(uint64_t{ piece } * pieceSize() + offset + length);
}
