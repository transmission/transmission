// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>
#include <utility>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/bitfield.h"
#include "libtransmission/block-info.h"
#include "libtransmission/completion.h"
#include "libtransmission/tr-assert.h"

uint64_t tr_completion::compute_has_valid() const
{
    uint64_t size = 0;

    for (tr_piece_index_t piece = 0, n_pieces = block_info_->piece_count(); piece < n_pieces; ++piece)
    {
        if (has_piece(piece))
        {
            size += block_info_->piece_size(piece);
        }
    }

    return size;
}

uint64_t tr_completion::has_valid() const
{
    if (!has_valid_)
    {
        auto const val = compute_has_valid();
        has_valid_ = val;
        return val;
    }

    return *has_valid_;
}

uint64_t tr_completion::compute_size_when_done() const
{
    if (has_all())
    {
        return block_info_->total_size();
    }

    // count bytes that we want or that we already have
    auto size = uint64_t{ 0 };
    for (tr_piece_index_t piece = 0, n_pieces = block_info_->piece_count(); piece < n_pieces; ++piece)
    {
        if (tor_->piece_is_wanted(piece))
        {
            size += block_info_->piece_size(piece);
        }
        else
        {
            size += count_has_bytes_in_piece(piece);
        }
    }

    return size;
}

uint64_t tr_completion::size_when_done() const
{
    if (!size_when_done_)
    {
        auto const value = compute_size_when_done();
        size_when_done_ = value;
        return value;
    }

    return *size_when_done_;
}

void tr_completion::amount_done(float* tab, size_t n_tabs) const
{
    if (n_tabs < 1)
    {
        return;
    }

    auto const blocks_per_tab = std::size(blocks_) / n_tabs;
    for (size_t i = 0; i < n_tabs; ++i)
    {
        auto const begin = i * blocks_per_tab;
        auto const end = std::min(begin + blocks_per_tab, std::size(blocks_));
        auto const numerator = blocks_.count(begin, end);
        tab[i] = float(numerator) / (end - begin);
    }
}

std::vector<uint8_t> tr_completion::create_piece_bitfield() const
{
    size_t const n = block_info_->piece_count();
    auto pieces = tr_bitfield{ n };

    // NOLINTNEXTLINE modernize-avoid-c-arrays
    auto flags = std::make_unique<bool[]>(n);
    for (tr_piece_index_t piece = 0; piece < n; ++piece)
    {
        flags[piece] = has_piece(piece);
    }
    pieces.set_from_bools(flags.get(), n);

    return pieces.raw();
}

// --- mutators

void tr_completion::add_block(tr_block_index_t block)
{
    if (has_block(block))
    {
        return; // already had it
    }

    blocks_.set(block);
    size_now_ += block_info_->block_size(block);

    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::set_blocks(tr_bitfield blocks)
{
    TR_ASSERT(std::size(blocks_) == std::size(blocks));

    blocks_ = std::move(blocks);
    size_now_ = count_has_bytes_in_span({ 0, block_info_->total_size() });
    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::set_has_all() noexcept
{
    auto const total_size = block_info_->total_size();

    blocks_.set_has_all();
    size_now_ = total_size;
    size_when_done_ = total_size;
    has_valid_ = total_size;
}

void tr_completion::add_piece(tr_piece_index_t piece)
{
    auto const span = block_info_->block_span_for_piece(piece);

    for (tr_block_index_t block = span.begin; block < span.end; ++block)
    {
        add_block(block);
    }
}

void tr_completion::remove_block(tr_block_index_t block)
{
    if (!has_block(block))
    {
        return; // already didn't have it
    }

    blocks_.unset(block);
    size_now_ -= block_info_->block_size(block);

    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::remove_piece(tr_piece_index_t piece)
{
    for (auto [block, end] = block_info_->block_span_for_piece(piece); block < end; ++block)
    {
        remove_block(block);
    }
}

uint64_t tr_completion::count_has_bytes_in_span(tr_byte_span_t span) const
{
    // confirm the span is valid
    span.begin = std::clamp(span.begin, uint64_t{ 0 }, block_info_->total_size());
    span.end = std::clamp(span.end, uint64_t{ 0 }, block_info_->total_size());
    auto const [begin_byte, end_byte] = span;
    TR_ASSERT(end_byte >= begin_byte);
    if (begin_byte >= end_byte)
    {
        return 0;
    }

    // get the block span of the byte span
    auto const begin_block = block_info_->byte_loc(begin_byte).block;
    auto const final_block = block_info_->byte_loc(end_byte - 1).block;

    // if the entire span is in a single block
    if (begin_block == final_block)
    {
        return has_block(begin_block) ? end_byte - begin_byte : 0;
    }

    auto total = uint64_t{};

    // the first block
    if (has_block(begin_block))
    {
        uint64_t u = begin_block + 1;
        u *= tr_block_info::BlockSize;
        u -= begin_byte;
        total += u;
    }

    // the middle blocks
    if (begin_block + 1 < final_block)
    {
        uint64_t u = blocks_.count(begin_block + 1, final_block);
        u *= tr_block_info::BlockSize;
        total += u;
    }

    // the last block
    if (has_block(final_block))
    {
        uint64_t u = final_block;
        u *= tr_block_info::BlockSize;
        total += end_byte - u;
    }

    return total;
}
