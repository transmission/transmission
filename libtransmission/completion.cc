// This file Copyright 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <memory>
#include <utility>
#include <vector>

#include "transmission.h"

#include "completion.h"
#include "torrent.h"
#include "tr-assert.h"

uint64_t tr_completion::computeHasValid() const
{
    uint64_t size = 0;

    for (tr_piece_index_t piece = 0, n_pieces = block_info_->pieceCount(); piece < n_pieces; ++piece)
    {
        if (hasPiece(piece))
        {
            size += block_info_->pieceSize(piece);
        }
    }

    return size;
}

uint64_t tr_completion::hasValid() const
{
    if (!has_valid_)
    {
        auto const val = computeHasValid();
        has_valid_ = val;
        return val;
    }

    return *has_valid_;
}

uint64_t tr_completion::computeSizeWhenDone() const
{
    if (hasAll())
    {
        return block_info_->totalSize();
    }

    // count bytes that we want or that we already have
    auto size = uint64_t{ 0 };
    for (tr_piece_index_t piece = 0, n_pieces = block_info_->pieceCount(); piece < n_pieces; ++piece)
    {
        if (tor_->pieceIsWanted(piece))
        {
            size += block_info_->pieceSize(piece);
        }
        else
        {
            size += countHasBytesInPiece(piece);
        }
    }

    return size;
}

uint64_t tr_completion::sizeWhenDone() const
{
    if (!size_when_done_)
    {
        auto const value = computeSizeWhenDone();
        size_when_done_ = value;
        return value;
    }

    return *size_when_done_;
}

void tr_completion::amountDone(float* tab, size_t n_tabs) const
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

std::vector<uint8_t> tr_completion::createPieceBitfield() const
{
    size_t const n = block_info_->pieceCount();
    auto pieces = tr_bitfield{ n };

    // NOLINTNEXTLINE modernize-avoid-c-arrays
    auto flags = std::make_unique<bool[]>(n);
    for (tr_piece_index_t piece = 0; piece < n; ++piece)
    {
        flags[piece] = hasPiece(piece);
    }
    pieces.setFromBools(flags.get(), n);

    return pieces.raw();
}

// --- mutators

void tr_completion::addBlock(tr_block_index_t block)
{
    if (hasBlock(block))
    {
        return; // already had it
    }

    blocks_.set(block);
    size_now_ += block_info_->blockSize(block);

    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::setBlocks(tr_bitfield blocks)
{
    TR_ASSERT(std::size(blocks_) == std::size(blocks));

    blocks_ = std::move(blocks);
    size_now_ = countHasBytesInSpan({ 0, block_info_->totalSize() });
    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::setHasAll() noexcept
{
    auto const total_size = block_info_->totalSize();

    blocks_.setHasAll();
    size_now_ = total_size;
    size_when_done_ = total_size;
    has_valid_ = total_size;
}

void tr_completion::addPiece(tr_piece_index_t piece)
{
    auto const span = block_info_->blockSpanForPiece(piece);

    for (tr_block_index_t block = span.begin; block < span.end; ++block)
    {
        addBlock(block);
    }
}

void tr_completion::removeBlock(tr_block_index_t block)
{
    if (!hasBlock(block))
    {
        return; // already didn't have it
    }

    blocks_.unset(block);
    size_now_ -= block_info_->blockSize(block);

    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::removePiece(tr_piece_index_t piece)
{
    for (auto [block, end] = block_info_->blockSpanForPiece(piece); block < end; ++block)
    {
        removeBlock(block);
    }
}

uint64_t tr_completion::countHasBytesInSpan(tr_byte_span_t span) const
{
    // confirm the span is valid
    span.begin = std::clamp(span.begin, uint64_t{ 0 }, block_info_->totalSize());
    span.end = std::clamp(span.end, uint64_t{ 0 }, block_info_->totalSize());
    auto const [begin_byte, end_byte] = span;
    if (begin_byte >= end_byte)
    {
        return 0;
    }

    // get the block span of the byte span
    auto const begin_block = block_info_->byteLoc(begin_byte).block;
    auto const final_block = block_info_->byteLoc(end_byte - 1).block;

    // if the entire span is in a single block
    if (begin_block == final_block)
    {
        return hasBlock(begin_block) ? end_byte - begin_byte : 0;
    }

    auto total = uint64_t{};

    // the first block
    if (hasBlock(begin_block))
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
    if (hasBlock(final_block))
    {
        uint64_t u = final_block;
        u *= tr_block_info::BlockSize;
        total += end_byte - u;
    }

    return total;
}
