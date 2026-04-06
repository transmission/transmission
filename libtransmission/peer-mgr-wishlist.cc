// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::adjacent_find, std::sort
#include <array>
#include <cstddef>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

#include <small/vector.hpp>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/crypto-utils.h" // for tr_salt_shaker
#include "libtransmission/peer-mgr-wishlist.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-macros.h" // TR_CONSTEXPR_VEC
#include "libtransmission/types.h"

namespace
{
[[nodiscard]] TR_CONSTEXPR_VEC std::vector<tr_block_span_t> make_spans(small::vector<tr_block_index_t> const& blocks)
{
    if (std::empty(blocks))
    {
        return {};
    }

    auto spans = std::vector<tr_block_span_t>{};
    spans.reserve(std::size(blocks));
    for (auto span_begin = std::begin(blocks), end = std::end(blocks); span_begin != end;)
    {
        auto constexpr NotAdjacent = [](tr_block_index_t const lhs, tr_block_index_t const rhs)
        {
            return lhs + 1U != rhs;
        };

        auto const span_end = std::min(std::adjacent_find(span_begin, end, NotAdjacent), std::prev(end));
        spans.push_back({ .begin = *span_begin, .end = *span_end + 1U });

        span_begin = std::next(span_end);
    }

    return spans;
}
} // namespace

Wishlist::Candidate::Candidate(tr_piece_index_t piece_in, tr_piece_index_t salt_in, Mediator const* mediator)
    : piece{ piece_in }
    , block_span{ mediator->block_span(piece_in) }
    , raw_block_span{ block_span }
    , replication{ mediator->count_piece_replication(piece_in) }
    , priority{ mediator->priority(piece_in) }
    , salt{ salt_in }
{
    unrequested.reserve(block_span.end - block_span.begin);
    for (auto [begin, i] = block_span; i > begin; --i)
    {
        if (auto const block = i - 1U; !mediator->client_has_block(block))
        {
            unrequested.insert(block);
        }
    }
}

// ---

std::vector<tr_block_span_t> Wishlist::next(
    size_t const n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece)
{
    if (n_wanted_blocks == 0U)
    {
        return {};
    }

    auto blocks = small::vector<tr_block_index_t>{};
    blocks.reserve(n_wanted_blocks);
    for (auto const& candidate : candidates_)
    {
        auto const n_added = std::size(blocks);
        TR_ASSERT(n_added <= n_wanted_blocks);

        // do we have enough?
        if (n_added >= n_wanted_blocks)
        {
            break;
        }

        // if the peer doesn't have this piece that we want...
        if (candidate.replication == 0 || !peer_has_piece(candidate.piece))
        {
            continue;
        }

        // walk the blocks in this piece that we don't have or not requested
        auto const n_to_add = std::min(std::size(candidate.unrequested), n_wanted_blocks - n_added);
        std::copy_n(std::rbegin(candidate.unrequested), n_to_add, std::back_inserter(blocks));
    }

    // Ensure the list of blocks are sorted
    // The list needs to be unique as well, but that should come naturally
    std::ranges::sort(blocks);
    return make_spans(blocks);
}

void Wishlist::on_got_bad_piece(tr_piece_index_t const piece)
{
    auto iter = find_by_piece(piece);
    if (auto const salt = get_salt(piece); iter != std::end(candidates_))
    {
        *iter = { piece, salt, &mediator_ };
    }
    else
    {
        iter = candidates_.emplace(iter, piece, salt, &mediator_);
    }

    if (piece > 0U)
    {
        if (auto const prev = find_by_piece(piece - 1U); prev != std::end(candidates_))
        {
            iter->block_span.begin = std::max(iter->block_span.begin, prev->block_span.end);
            TR_ASSERT(iter->block_span.begin == prev->block_span.end);
            for (tr_block_index_t i = iter->block_span.begin; i > iter->raw_block_span.begin; --i)
            {
                auto const block = i - 1U;
                prev->unrequested.insert(block);
                iter->unrequested.erase(block);
            }
        }
    }
    if (piece < mediator_.piece_count() - 1U)
    {
        if (auto const next = find_by_piece(piece + 1U); next != std::end(candidates_))
        {
            iter->block_span.end = std::min(iter->block_span.end, next->block_span.begin);
            TR_ASSERT(iter->block_span.end == next->block_span.begin);
            for (tr_block_index_t i = iter->raw_block_span.end; i > iter->block_span.end; --i)
            {
                auto const block = i - 1U;
                next->unrequested.insert(block);
                iter->unrequested.erase(block);
            }
        }
    }

    std::ranges::sort(candidates_);
}

tr_piece_index_t Wishlist::get_salt(tr_piece_index_t const piece)
{
    auto const is_sequential = mediator_.is_sequential_download();
    auto const sequential_download_from_piece = mediator_.sequential_download_from_piece();
    auto const n_pieces = mediator_.piece_count();

    if (!is_sequential)
    {
        return salter_();
    }

    // Download first and last piece first
    if (piece == 0U)
    {
        return 0U;
    }

    if (piece == n_pieces - 1U)
    {
        return 1U;
    }

    if (sequential_download_from_piece <= 1)
    {
        return piece + 1U;
    }

    // Rotate remaining pieces
    // 1 2 3 4 5 -> 3 4 5 1 2 if sequential_download_from_piece is 3
    if (piece < sequential_download_from_piece)
    {
        return n_pieces - (sequential_download_from_piece - piece);
    }

    return piece - sequential_download_from_piece + 2U;
}

void Wishlist::candidate_list_upkeep()
{
    auto n_old_c = std::size(candidates_);
    auto const n_pieces = mediator_.piece_count();
    candidates_.reserve(n_pieces);

    std::ranges::sort(candidates_, [](auto const& lhs, auto const& rhs) { return lhs.piece < rhs.piece; });

    Candidate* prev = nullptr;
    for (tr_piece_index_t piece = 0U, idx_c = 0U; piece < n_pieces; ++piece)
    {
        auto const existing_candidate = idx_c < n_old_c && piece == candidates_[idx_c].piece;
        auto const client_wants_piece = mediator_.client_wants_piece(piece);
        auto const client_has_piece = mediator_.client_has_piece(piece);
        if (client_wants_piece && !client_has_piece)
        {
            if (existing_candidate)
            {
                auto& candidate = candidates_[idx_c];

                if (auto& begin = candidate.block_span.begin; prev != nullptr)
                {
                    // Shrink the block span of the previous candidate if the
                    // previous candidate shares the edge block with this candidate.
                    auto& previous_end = prev->block_span.end;
                    for (tr_block_index_t i = previous_end; i > begin; --i)
                    {
                        prev->unrequested.erase(i - 1U);
                    }
                    previous_end = std::min(previous_end, begin);
                }

                ++idx_c;
                prev = &candidate;
            }
            else
            {
                auto const salt = get_salt(piece);
                auto& candidate = candidates_.emplace_back(piece, salt, &mediator_);

                if (auto& begin = candidate.block_span.begin; prev != nullptr)
                {
                    // Shrink the block span of this candidate if the previous candidate
                    // shares the edge block with this candidate.
                    auto const previous_end = prev->block_span.end;
                    for (tr_block_index_t i = previous_end; i > begin; --i)
                    {
                        candidate.unrequested.erase(i - 1U);
                    }
                    begin = std::max(previous_end, begin);
                }

                prev = &candidate;
            }
        }
        else if (existing_candidate)
        {
            auto const iter = std::next(std::begin(candidates_), idx_c);

            if (prev != nullptr && prev->piece + 1U == iter->piece)
            {
                // If the previous candidate was consecutive with this candidate,
                // reset its ending block index and transfer unrequested blocks to it.
                for (auto i = prev->raw_block_span.end; i > prev->block_span.end; --i)
                {
                    if (auto const block = i - 1U; iter->unrequested.contains(block))
                    {
                        prev->unrequested.insert(block);
                    }
                }
                prev->block_span.end = prev->raw_block_span.end;
            }

            if (auto const idx_next = idx_c + 1U; idx_next < n_old_c && candidates_[idx_next].piece == iter->piece + 1U)
            {
                // If the next candidate was consecutive with this candidate,
                // reset its beginning block index and transfer unrequested blocks to it.
                auto& next = candidates_[idx_next];
                for (auto i = next.block_span.begin; i > next.raw_block_span.begin; --i)
                {
                    if (auto const block = i - 1U; iter->unrequested.contains(block))
                    {
                        next.unrequested.insert(block);
                    }
                }
                next.block_span.begin = next.raw_block_span.begin;
            }

            candidates_.erase(iter);
            --n_old_c;

            // We can be sure that the next candidate's first block will not
            // be shared with the previous candidate
            prev = nullptr;
        }
    }

    std::ranges::sort(candidates_);
}

void Wishlist::recalculate_salt()
{
    for (auto& candidate : candidates_)
    {
        candidate.salt = get_salt(candidate.piece);
    }

    std::ranges::sort(candidates_);
}

void Wishlist::on_priority_changed()
{
    for (auto& candidate : candidates_)
    {
        candidate.priority = mediator_.priority(candidate.piece);
    }

    std::ranges::sort(candidates_);
}
