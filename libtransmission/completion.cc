/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <memory>
#include <vector>

#include "transmission.h"

#include "completion.h"
#include "torrent.h"
#include "tr-assert.h"

uint64_t tr_completion::leftUntilDone() const
{
    auto const size_when_done = sizeWhenDone();
    auto const has_total = hasTotal();
    return size_when_done - has_total;
}

uint64_t tr_completion::computeHasValid() const
{
    uint64_t size = 0;

    for (tr_piece_index_t piece = 0, n = block_info_->n_pieces; piece < n; ++piece)
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
        has_valid_ = computeHasValid();
    }

    return *has_valid_;
}

uint64_t tr_completion::computeSizeWhenDone() const
{
    if (hasAll())
    {
        return block_info_->total_size;
    }

    // count bytes that we want or that we already have
    auto size = size_t{ 0 };
    for (tr_piece_index_t piece = 0; piece < block_info_->n_pieces; ++piece)
    {
        if (!tor_->pieceIsDnd(piece))
        {
            size += block_info_->pieceSize(piece);
        }
        else
        {
            size += countHasBytesInSpan(block_info_->blockSpanForPiece(piece));
        }
    }

    return size;
}

uint64_t tr_completion::sizeWhenDone() const
{
    if (!size_when_done_)
    {
        size_when_done_ = computeSizeWhenDone();
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
        auto const begin = i * n_tabs;
        auto const end = std::min(begin + blocks_per_tab, std::size(blocks_));
        auto const numerator = blocks_.count(begin, end);
        tab[i] = (double)numerator / (end - begin);
    }
}

size_t tr_completion::countMissingBlocksInPiece(tr_piece_index_t piece) const
{
    auto const [begin, end] = block_info_->blockSpanForPiece(piece);
    return (end - begin) - blocks_.count(begin, end);
}

size_t tr_completion::countMissingBytesInPiece(tr_piece_index_t piece) const
{
    return block_info_->pieceSize(piece) - countHasBytesInSpan(block_info_->blockSpanForPiece(piece));
}

tr_completeness tr_completion::status() const
{
    if (!hasMetainfo())
    {
        return TR_LEECH;
    }

    if (hasAll())
    {
        return TR_SEED;
    }

    if (size_now_ == sizeWhenDone())
    {
        return TR_PARTIAL_SEED;
    }

    return TR_LEECH;
}

std::vector<uint8_t> tr_completion::createPieceBitfield() const
{
    size_t const n = block_info_->n_pieces;
    auto pieces = tr_bitfield{ n };

    auto flags = std::make_unique<bool[]>(n);
    for (tr_piece_index_t piece = 0; piece < n; ++piece)
    {
        flags[piece] = hasPiece(piece);
    }
    pieces.setFromBools(flags.get(), n);

    return pieces.raw();
}

/// mutators

void tr_completion::addBlock(tr_block_index_t block)
{
    if (hasBlock(block))
    {
        return; // already had it
    }

    blocks_.set(block);
    size_now_ += block_info_->blockSize(block);

    has_valid_.reset();
}

void tr_completion::setBlocks(tr_bitfield blocks)
{
    TR_ASSERT(std::size(blocks_) == std::size(blocks));

    blocks_ = std::move(blocks);
    size_now_ = countHasBytesInSpan({ 0, tr_block_index_t(std::size(blocks_)) });
    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::addPiece(tr_piece_index_t piece)
{
    auto const [begin, end] = block_info_->blockSpanForPiece(piece);

    for (tr_block_index_t block = begin; block < end; ++block)
    {
        addBlock(block);
    }
}

void tr_completion::removePiece(tr_piece_index_t piece)
{
    auto const [begin, end] = block_info_->blockSpanForPiece(piece);
    size_now_ -= countHasBytesInSpan(block_info_->blockSpanForPiece(piece));
    has_valid_.reset();
    blocks_.unsetSpan(begin, end);
}

uint64_t tr_completion::countHasBytesInSpan(tr_block_span_t span) const
{
    auto const [begin, end] = span;

    auto n = blocks_.count(begin, end);
    n *= block_info_->block_size;

    if (end == block_info_->n_blocks && blocks_.test(end - 1))
    {
        n -= block_info_->block_size - block_info_->final_block_size;
    }

    return n;
}
