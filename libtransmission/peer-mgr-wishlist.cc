// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <cstddef>
#include <set>
#include <utility>
#include <vector>

#define LIBTRANSMISSION_PEER_MODULE

#include "transmission.h"

#include "crypto-utils.h" // for tr_salt_shaker
#include "peer-mgr-wishlist.h"
#include "tr-assert.h"

namespace
{

struct Candidate
{
    tr_piece_index_t piece;
    size_t n_blocks_missing;
    tr_priority_t priority;
    uint8_t salt;

    Candidate(tr_piece_index_t piece_in, size_t missing_in, tr_priority_t priority_in, uint8_t salt_in)
        : piece{ piece_in }
        , n_blocks_missing{ missing_in }
        , priority{ priority_in }
        , salt{ salt_in }
    {
    }

    [[nodiscard]] int compare(Candidate const& that) const // <=>
    {
        // prefer pieces closer to completion
        if (n_blocks_missing != that.n_blocks_missing)
        {
            return n_blocks_missing < that.n_blocks_missing ? -1 : 1;
        }

        // prefer higher priority
        if (priority != that.priority)
        {
            return priority > that.priority ? -1 : 1;
        }

        if (salt != that.salt)
        {
            return salt < that.salt ? -1 : 1;
        }

        return 0;
    }

    bool operator<(Candidate const& that) const // less than
    {
        return compare(that) < 0;
    }
};

std::vector<Candidate> getCandidates(Wishlist::Mediator const& mediator)
{
    // count up the pieces that we still want
    auto wanted_pieces = std::vector<std::pair<tr_piece_index_t, size_t>>{};
    auto const n_pieces = mediator.countAllPieces();
    wanted_pieces.reserve(n_pieces);
    for (tr_piece_index_t i = 0; i < n_pieces; ++i)
    {
        if (!mediator.clientCanRequestPiece(i))
        {
            continue;
        }

        size_t const n_missing = mediator.countMissingBlocks(i);
        if (n_missing == 0)
        {
            continue;
        }

        wanted_pieces.emplace_back(i, n_missing);
    }

    // transform them into candidates
    auto salter = tr_salt_shaker{};
    auto const n = std::size(wanted_pieces);
    auto candidates = std::vector<Candidate>{};
    candidates.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        auto const [piece, n_missing] = wanted_pieces[i];
        candidates.emplace_back(piece, n_missing, mediator.priority(piece), salter());
    }

    return candidates;
}

std::vector<tr_block_span_t> makeSpans(tr_block_index_t const* sorted_blocks, size_t n_blocks)
{
    if (n_blocks == 0)
    {
        return {};
    }

    auto spans = std::vector<tr_block_span_t>{};
    auto cur = tr_block_span_t{ sorted_blocks[0], sorted_blocks[0] + 1 };
    for (size_t i = 1; i < n_blocks; ++i)
    {
        if (cur.end == sorted_blocks[i])
        {
            ++cur.end;
        }
        else
        {
            spans.push_back(cur);
            cur = tr_block_span_t{ sorted_blocks[i], sorted_blocks[i] + 1 };
        }
    }
    spans.push_back(cur);

    return spans;
}

} // namespace

std::vector<tr_block_span_t> Wishlist::next(size_t n_wanted_blocks)
{
    if (n_wanted_blocks == 0)
    {
        return {};
    }

    // We usually won't need all the candidates until endgame, so don't
    // waste cycles sorting all of them here. partial sort is enough.
    auto candidates = getCandidates(mediator_);
    auto constexpr MaxSortedPieces = size_t{ 30 };
    auto const middle = std::min(std::size(candidates), MaxSortedPieces);
    std::partial_sort(std::begin(candidates), std::begin(candidates) + middle, std::end(candidates));

    auto blocks = std::set<tr_block_index_t>{};
    for (auto const& candidate : candidates)
    {
        // do we have enough?
        if (std::size(blocks) >= n_wanted_blocks)
        {
            break;
        }

        // walk the blocks in this piece
        auto const [begin, end] = mediator_.blockSpan(candidate.piece);
        for (tr_block_index_t block = begin; block < end && std::size(blocks) < n_wanted_blocks; ++block)
        {
            // don't request blocks we've already got
            if (!mediator_.clientCanRequestBlock(block))
            {
                continue;
            }

            // don't request from too many peers
            size_t const n_peers = mediator_.countActiveRequests(block);
            if (size_t const max_peers = mediator_.isEndgame() ? 2 : 1; n_peers >= max_peers)
            {
                continue;
            }

            blocks.insert(block);
        }
    }

    auto const blocks_v = std::vector<tr_block_index_t>{ std::begin(blocks), std::end(blocks) };
    return makeSpans(std::data(blocks_v), std::size(blocks_v));
}
