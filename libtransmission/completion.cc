/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <vector>

#include "transmission.h"

#include "completion.h"
#include "torrent.h"
#include "tr-assert.h"

bool tr_completion::hasAll() const
{
    return hasMetainfo() && blocks_.hasAll();
}
bool tr_completion::hasBlock(tr_block_index_t i) const
{
    return blocks_.test(i);
}
bool tr_completion::hasBlocks(tr_block_range_t range) const
{
    return blocks_.count(range.first, range.last + 1) == range.last + 1 - range.first;
}
bool tr_completion::hasNone() const
{
    return !hasMetainfo() || blocks_.hasNone();
}
bool tr_completion::hasPiece(tr_piece_index_t i) const
{
    return countMissingBlocksInPiece(i) == 0;
}
bool tr_completion::isDone() const
{
    auto left_until_done = leftUntilDone();
    return hasMetainfo() && left_until_done == 0;
}
tr_bitfield const& tr_completion::blocks() const
{
    return blocks_;
}
uint64_t tr_completion::hasTotal() const
{
    return size_now_;
}
uint64_t tr_completion::leftUntilDone() const
{
    auto const size_when_done = sizeWhenDone();
    auto const has_total = hasTotal();
    return size_when_done - has_total;
}

double tr_completion::percentComplete() const
{
    auto const denom = block_info_->total_size;
    return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
}

double tr_completion::percentDone() const
{
    auto const denom = sizeWhenDone();
    return denom ? std::clamp(double(size_now_) / denom, 0.0, 1.0) : 0.0;
}

uint64_t tr_completion::computeHasValid() const
{
    uint64_t size = 0;

    for (tr_piece_index_t piece = 0, n = block_info_->n_pieces; piece < n; ++piece)
    {
        if (hasPiece(piece))
        {
            size += block_info_->countBytesInPiece(piece);
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
            size += block_info_->countBytesInPiece(piece);
        }
        else
        {
            size += countHasBytesInRange(block_info_->blockRangeForPiece(piece));
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
    if (hasAll())
    {
        return 0;
    }

    auto const [first, last] = block_info_->blockRangeForPiece(piece);
    return (last + 1 - first) - blocks_.count(first, last + 1);
}

size_t tr_completion::countMissingBytesInPiece(tr_piece_index_t piece) const
{
    if (hasAll())
    {
        return 0;
    }

    return block_info_->countBytesInPiece(piece) - countHasBytesInRange(block_info_->blockRangeForPiece(piece));
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
    auto const n = block_info_->n_pieces;
    auto pieces = tr_bitfield{ n };

    if (hasAll())
    {
        pieces.setHasAll();
    }
    else if (!hasNone())
    {
        bool* flags = tr_new(bool, n);

        for (tr_piece_index_t piece = 0; piece < n; ++piece)
        {
            flags[piece] = hasPiece(piece);
        }

        pieces.setFromBools(flags, n);
        tr_free(flags);
    }

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
    size_now_ += block_info_->countBytesInBlock(block);

    has_valid_.reset();
}

void tr_completion::setBlocks(tr_bitfield blocks)
{
    TR_ASSERT(std::size(blocks_) == std::size(blocks));

    blocks_ = std::move(blocks);
    size_now_ = countHasBytesInRange({ 0, tr_block_index_t(std::size(blocks_) - 1) });
    size_when_done_.reset();
    has_valid_.reset();
}

void tr_completion::addPiece(tr_piece_index_t piece)
{
    auto const [first, last] = block_info_->blockRangeForPiece(piece);

    for (tr_block_index_t block = first; block <= last; ++block)
    {
        addBlock(block);
    }
}

void tr_completion::removePiece(tr_piece_index_t piece)
{
    auto const block_range = block_info_->blockRangeForPiece(piece);
    size_now_ -= countHasBytesInRange(block_info_->blockRangeForPiece(piece));
    has_valid_.reset();
    blocks_.unsetRange(block_range.first, block_range.last + 1);
}

uint64_t tr_completion::countHasBytesInRange(tr_block_range_t range) const
{
    auto const [first, last] = range;

    auto n = blocks_.count(first, last + 1);
    n *= block_info_->block_size;

    if (last + 1 == block_info_->n_blocks && blocks_.test(last))
    {
        n -= block_info_->block_size - block_info_->final_block_size;
    }

    return n;
}

bool tr_completion::hasMetainfo() const
{
    return !std::empty(blocks_);
}
