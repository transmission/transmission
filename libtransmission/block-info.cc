// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "block-info.h"
#include "tr-assert.h"

void tr_block_info::initSizes(uint64_t total_size_in, uint32_t piece_size_in) noexcept
{
    TR_ASSERT(piece_size_in == 0 || piece_size_in >= BlockSize);
    if (piece_size_in == 0)
    {
        *this = {};
        return;
    }

    total_size_ = total_size_in;
    piece_size_ = piece_size_in;
    n_pieces_ = (total_size_ + piece_size_ - 1) / piece_size_;
    n_blocks_ = (total_size_ + BlockSize - 1) / BlockSize;

    uint32_t remainder = total_size_ % piece_size_;
    final_piece_size_ = remainder != 0U ? remainder : piece_size_;

    remainder = total_size_ % BlockSize;
    final_block_size_ = remainder != 0U ? remainder : BlockSize;
}
