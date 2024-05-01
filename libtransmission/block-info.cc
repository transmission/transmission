// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint>

#include "libtransmission/block-info.h"
#include "libtransmission/tr-assert.h" // TR_ASSERT

void tr_block_info::init_sizes(uint64_t const total_size_in, uint32_t const piece_size_in) noexcept
{
    TR_ASSERT(piece_size_in == 0 || piece_size_in >= BlockSize);
    if (piece_size_in == 0)
    {
        *this = {};
        return;
    }

    total_size_ = total_size_in;
    piece_size_ = piece_size_in;
    n_pieces_ = static_cast<tr_piece_index_t>((total_size_ + piece_size_ - 1) / piece_size_);
    n_blocks_ = static_cast<tr_block_index_t>((total_size_ + BlockSize - 1) / BlockSize);

    uint32_t remainder = total_size_ % piece_size_;
    final_piece_size_ = remainder != 0U ? remainder : piece_size_;

    remainder = total_size_ % BlockSize;
    final_block_size_ = remainder != 0U ? remainder : BlockSize;
}
