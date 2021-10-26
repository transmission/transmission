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
#include <iostream>
#include <numeric>
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
    size_t n_blocks_requested;
    tr_priority_t priority;
    uint8_t salt;
    Candidate(tr_piece_index_t p, size_t mis, size_t req, tr_priority_t pri, uint8_t s)
        : piece{ p }
        , n_blocks_missing{ mis }
        , n_blocks_requested{ req }
        , priority{ pri }
        , salt{ s }
    {
    }

    int compare(Candidate const& that) const // <=>
    {
        // prever to not over-request
        bool this_overreq = n_blocks_requested > n_blocks_missing;
        bool that_overreq = that.n_blocks_requested > that.n_blocks_missing;
        if (this_overreq != that_overreq)
        {
            return this_overreq ? 1 : -1;
        }

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

std::vector<Candidate> getCandidates(tr_torrent* tor)
{
    auto const* const inf = tr_torrentInfo(tor);

    // count up how many pieces we still want
    auto wanted_pieces = std::vector<std::pair<tr_piece_index_t, size_t>>{};
    wanted_pieces.reserve(inf->pieceCount);
    for (tr_piece_index_t i = 0; i < inf->pieceCount; ++i)
    {
        if (inf->pieces[i].dnd)
        {
            continue;
        }

        auto const n_missing = tr_torrentMissingBlocksInPiece(tor, i);
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
        candidates.emplace_back(
            piece,
            n_missing,
            0, // FIXME: number of pending requests in piece
            inf->pieces[piece].priority,
            saltbuf[i]);
    }

    return candidates;
}

static std::vector<tr_block_range> makeRanges(tr_block_index_t const* sorted_blocks, size_t n_blocks)
{
    if (n_blocks == 0)
    {
        // std::cout << __FILE__ << ':' << __LINE__ << " makeRanges returning empty vector" << std::endl;
        return {};
    }

#if 0
    std::cout << __FILE__ << ':' << __LINE__ << " makeRanges in: ";
    for (size_t i = 0; i < n_blocks; ++i)
        std::cout << sorted_blocks[i] << ' ';
    std::cout << std::endl;
#endif

    auto ranges = std::vector<tr_block_range>{};
    auto cur = tr_block_range{ sorted_blocks[0], sorted_blocks[0] };
    for (size_t i = 1; i < n_blocks; ++i)
    {
        if (cur.last + 1 == sorted_blocks[i])
        {
            cur.last = sorted_blocks[i];
        }
        else
        {
            ranges.push_back(cur);
            cur = tr_block_range{ sorted_blocks[i], sorted_blocks[i] };
        }
    }
    ranges.push_back(cur);

#if 0
    std::cout << __FILE__ << ':' << __LINE__ << " makeRanges out: ";
    for (auto const range : ranges)
    {
        if (range.first == range.last)
            std::cout << '[' << range.first << ']';
        else
            std::cout << '[' << range.first << "..." << range.last << ']';
    }
    std::cout << std::endl;
#endif

    return ranges;
}

} // namespace

std::vector<tr_block_range> Wishlist::next(
    tr_torrent* tor,
    tr_peer* peer,
    size_t numwant,
    ActiveRequests& active_requests,
    bool is_endgame)
{
    size_t n_blocks = 0;
    auto ranges = std::vector<tr_block_range>{};

    // sanity clause
    TR_ASSERT(tr_isTorrent(tor));
    TR_ASSERT(numwant > 0);

    // Until we're in endgame, we usually won't need allt he candidates.
    // Just do a partial sort to avoid unnecessary comparisons.
    auto candidates = getCandidates(tor);
    auto constexpr MaxSorted = size_t{ 20 };
    auto const middle = std::min(std::size(candidates), MaxSorted);
    std::partial_sort(std::begin(candidates), std::begin(candidates) + middle, std::end(candidates));

    // std::cerr << __FILE__ << ':' << __LINE__ << " got " << std::size(candidates) << " candidates" << std::endl;
    for (auto const& candidate : candidates)
    {
        // do we have enough?
        if (n_blocks >= numwant)
        {
            break;
        }

        // does the peer have this piece?
        auto const piece = candidate.piece;
        if (!peer->have.test(piece))
        {
            // std::cout << __FILE__ << ':' << __LINE__ << " skipping piece " << piece << " because peer does not have it" << std::endl;
            continue;
        }

        // walk the blocks in this piece
        auto const [first, last] = tr_torGetPieceBlockRange(tor, piece);
        // std::cout << __FILE__ << ':' << __LINE__ << " piece " << piece << " block range is [" << first << "..." << last << ']' << std::endl;
        auto blocks = std::vector<tr_block_index_t>{};
        blocks.reserve(last + 1 - first);
        for (tr_block_index_t block = first; block <= last && n_blocks + std::size(blocks) < numwant; ++block)
        {
            // don't request blocks we've already got
            if (tr_torrentBlockIsComplete(tor, block))
            {
                // std::cout << __FILE__ << ':' << __LINE__ << " skipping " << block << " because we have it" << std::endl;
                continue;
            }

            // don't request from too many peers
            size_t const n_peers = active_requests.count(block);
            size_t const max_peers = is_endgame ? 2 : 1;
            if (n_peers >= max_peers)
            {
                // std::cout << __FILE__ << ':' << __LINE__ << " skipping " << block << " because we it already has enough active requests" << std::endl;
                continue;
            }

            // don't send the same request to the same peer twice
            if (active_requests.has(block, peer))
            {
                // std::cout << __FILE__ << ':' << __LINE__ << " skipping " << block << " because we already requested it from this peer" << std::endl;
                continue;
            }

            // std::cout << __FILE__ << ':' << __LINE__ << " this one looks good " << block << std::endl;
            blocks.push_back(block);
        }

        if (std::empty(blocks))
        {
            continue;
        }

        // copy the ranges into `ranges`
        auto const tmp = makeRanges(std::data(blocks), std::size(blocks));
        std::copy(std::begin(tmp), std::end(tmp), std::back_inserter(ranges));
        n_blocks += std::accumulate(
            std::begin(tmp),
            std::end(tmp),
            size_t{},
            [](size_t sum, auto range) { return sum + range.last + 1 - range.first; });
        if (n_blocks >= numwant)
        {
            break;
        }
    }

#if 0
    std::cout << __FILE__ << ':' << __LINE__ << " returning " << std::size(ranges) << " ranges for " << n_blocks << " blocks; wanted " << numwant << std::endl;
    std::cout << __FILE__ << ':' << __LINE__ << " next() returning ranges: ";
    for (auto const range : ranges)
    {
        if (range.first == range.last)
            std::cout << '[' << range.first << ']';
        else
            std::cout << '[' << range.first << "..." << range.last << ']';
    }
    std::cout << std::endl;
#endif
    return ranges;
}
