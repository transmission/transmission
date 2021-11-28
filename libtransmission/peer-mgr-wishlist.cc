/*
 * This file Copyright (C) 2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <numeric>
#include <utility>
#include <vector>

#define LIBTRANSMISSION_PEER_MODULE

#include "transmission.h"
#include "crypto-utils.h" // tr_rand_buffer()
#include "peer-mgr-wishlist.h"

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

    int compare(Candidate const& that) const // <=>
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

std::vector<Candidate> getCandidates(Wishlist::PeerInfo const& peer_info)
{
    // count up the pieces that we still want
    auto wanted_pieces = std::vector<std::pair<tr_piece_index_t, size_t>>{};
    auto const n_pieces = peer_info.countAllPieces();
    wanted_pieces.reserve(n_pieces);
    for (tr_piece_index_t i = 0; i < n_pieces; ++i)
    {
        if (!peer_info.clientCanRequestPiece(i))
        {
            continue;
        }

        size_t const n_missing = peer_info.countMissingBlocks(i);
        if (n_missing == 0)
        {
            continue;
        }

        wanted_pieces.emplace_back(i, n_missing);
    }

    // transform them into candidates
    auto const n = std::size(wanted_pieces);
    auto saltbuf = std::vector<char>(n);
    tr_rand_buffer(std::data(saltbuf), n);
    auto candidates = std::vector<Candidate>{};
    candidates.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        auto const [piece, n_missing] = wanted_pieces[i];
        candidates.emplace_back(piece, n_missing, peer_info.priority(piece), saltbuf[i]);
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

std::vector<tr_block_span_t> Wishlist::next(Wishlist::PeerInfo const& peer_info, size_t n_wanted_blocks) const
{
    size_t n_blocks = 0;
    auto spans = std::vector<tr_block_span_t>{};

    // sanity clause
    TR_ASSERT(n_wanted_blocks > 0);

    // We usually won't need all the candidates until endgame, so don't
    // waste cycles sorting all of them here. partial sort is enough.
    auto candidates = getCandidates(peer_info);
    auto constexpr MaxSortedPieces = size_t{ 30 };
    auto const middle = std::min(std::size(candidates), MaxSortedPieces);
    std::partial_sort(std::begin(candidates), std::begin(candidates) + middle, std::end(candidates));

    for (auto const& candidate : candidates)
    {
        // do we have enough?
        if (n_blocks >= n_wanted_blocks)
        {
            break;
        }

        // walk the blocks in this piece
        auto const [begin, end] = peer_info.blockSpan(candidate.piece);
        auto blocks = std::vector<tr_block_index_t>{};
        blocks.reserve(end - begin);
        for (tr_block_index_t block = begin; block < end && n_blocks + std::size(blocks) < n_wanted_blocks; ++block)
        {
            // don't request blocks we've already got
            if (!peer_info.clientCanRequestBlock(block))
            {
                continue;
            }

            // don't request from too many peers
            size_t const n_peers = peer_info.countActiveRequests(block);
            if (size_t const max_peers = peer_info.isEndgame() ? 2 : 1; n_peers >= max_peers)
            {
                continue;
            }

            blocks.push_back(block);
        }

        if (std::empty(blocks))
        {
            continue;
        }

        // copy the spans into `spans`
        auto const tmp = makeSpans(std::data(blocks), std::size(blocks));
        std::copy(std::begin(tmp), std::end(tmp), std::back_inserter(spans));
        n_blocks += std::accumulate(
            std::begin(tmp),
            std::end(tmp),
            size_t{},
            [](size_t sum, auto span) { return sum + span.end - span.begin; });
        if (n_blocks >= n_wanted_blocks)
        {
            break;
        }
    }

    return spans;
}
