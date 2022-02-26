// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <event2/util.h>

#include "transmission.h"

#include "block-info.h"
#include "tr-assert.h"

void tr_block_info::initSizes(uint64_t total_size_in, uint64_t piece_size_in)
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
