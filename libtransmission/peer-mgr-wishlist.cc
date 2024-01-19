// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::adjacent_find
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <small/set.hpp>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/transmission.h"

#include "libtransmission/bitfield.h"
#include "libtransmission/crypto-utils.h" // for tr_salt_shaker
#include "libtransmission/peer-mgr-wishlist.h"

namespace
{
std::vector<tr_block_span_t> make_spans(small::set<tr_block_index_t> const& blocks)
{
    if (std::empty(blocks))
    {
        return {};
    }

    auto spans = std::vector<tr_block_span_t>{};
    for (auto span_begin = std::begin(blocks), end = std::end(blocks); span_begin != end;)
    {
        static auto constexpr NotAdjacent = [](tr_block_index_t const lhs, tr_block_index_t const rhs)
        {
            return lhs + 1U != rhs;
        };

        auto span_end = std::adjacent_find(span_begin, end, NotAdjacent);
        if (span_end == end)
        {
            --span_end;
        }
        spans.push_back({ *span_begin, *span_end + 1 });

        span_begin = std::next(span_end);
    }

    return spans;
}
} // namespace

Wishlist::Wishlist(std::unique_ptr<Mediator> mediator_in)
    : tags_{ {
          mediator_in->observe_peer_disconnect([this](tr_torrent*, tr_bitfield const& b) { dec_replication_from_bitfield(b); }),
          mediator_in->observe_got_bitfield([this](tr_torrent*, tr_bitfield const& b) { inc_replication_from_bitfield(b); }),
          mediator_in->observe_got_block([this](tr_torrent*, tr_piece_index_t p, tr_block_index_t) { resort_piece(p); }),
          mediator_in->observe_got_have([this](tr_torrent*, tr_piece_index_t p) { inc_replication_piece(p); }),
          mediator_in->observe_got_have_all([this](tr_torrent*) { inc_replication(); }),
          mediator_in->observe_piece_completed([this](tr_torrent*, tr_piece_index_t p) { remove_piece(p); }),
          mediator_in->observe_priority_changed([this](tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t)
                                                { set_candidates_dirty(); }),
          mediator_in->observe_sequential_download_changed([this](tr_torrent*, bool) { set_candidates_dirty(); }),
      } }
    , mediator_{ std::move(mediator_in) }
{
}

std::vector<tr_block_span_t> Wishlist::next(
    size_t n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece,
    std::function<bool(tr_block_index_t)> const& has_active_pending_to_peer)
{
    if (n_wanted_blocks == 0U)
    {
        return {};
    }

    maybe_rebuild_candidate_list();

    auto blocks = small::set<tr_block_index_t>{};
    blocks.reserve(n_wanted_blocks);
    for (auto const& candidate : candidates_)
    {
        // do we have enough?
        if (std::size(blocks) >= n_wanted_blocks)
        {
            break;
        }

        // if the peer doesn't have this piece that we want...
        if (!peer_has_piece(candidate.piece))
        {
            continue;
        }

        // walk the blocks in this piece
        for (auto [block, end] = mediator_->block_span(candidate.piece); block < end && std::size(blocks) < n_wanted_blocks;
             ++block)
        {
            // don't request blocks that:
            // 1. we've already got, or
            // 2. already has an active request to that peer
            if (mediator_->client_has_block(block) || has_active_pending_to_peer(block))
            {
                continue;
            }

            // don't request from too many peers
            auto const n_peers = mediator_->count_active_requests(block);
            if (auto const max_peers = mediator_->is_endgame() ? EndgameMaxPeers : NormalMaxPeers; n_peers >= max_peers)
            {
                continue;
            }

            blocks.insert(block);
        }
    }

    return make_spans(blocks);
}

void Wishlist::maybe_rebuild_candidate_list()
{
    if (!candidates_dirty_)
    {
        return;
    }
    candidates_dirty_ = false;
    candidates_.clear();

    auto salter = tr_salt_shaker<tr_piece_index_t>{};
    auto const is_sequential = mediator_->is_sequential_download();
    auto const n_pieces = mediator_->piece_count();
    candidates_.reserve(n_pieces);
    for (tr_piece_index_t piece = 0U; piece < n_pieces; ++piece)
    {
        if (mediator_->count_missing_blocks(piece) <= 0U || !mediator_->client_wants_piece(piece))
        {
            continue;
        }

        auto const salt = is_sequential ? piece : salter();
        candidates_
            .emplace_back(piece, mediator_->count_piece_replication(piece), mediator_->priority(piece), salt, mediator_.get());
    }
    candidates_.shrink_to_fit();
    std::sort(std::begin(candidates_), std::end(candidates_));
}

Wishlist::CandidateVec::iterator Wishlist::piece_lookup(tr_piece_index_t const piece)
{
    return std::find_if(
        std::begin(candidates_),
        std::end(candidates_),
        [piece](auto const& candidate) { return candidate.piece == piece; });
}

void Wishlist::dec_replication()
{
    if (!candidates_dirty_)
    {
        std::for_each(
            std::begin(candidates_),
            std::end(candidates_),
            [](Candidate& candidate)
            {
                TR_ASSERT(candidate.replication > 0U);
                --candidate.replication;
            });
    }
}

void Wishlist::dec_replication_from_bitfield(tr_bitfield const& bitfield)
{
    if (candidates_dirty_)
    {
        return;
    }

    if (bitfield.has_none())
    {
        return;
    }

    if (bitfield.has_all())
    {
        dec_replication();
        return;
    }

    for (auto& candidate : candidates_)
    {
        if (bitfield.test(candidate.piece))
        {
            TR_ASSERT(candidate.replication > 0U);
            --candidate.replication;
        }
    }

    std::sort(std::begin(candidates_), std::end(candidates_));
}

void Wishlist::inc_replication()
{
    if (!candidates_dirty_)
    {
        std::for_each(std::begin(candidates_), std::end(candidates_), [](Candidate& candidate) { ++candidate.replication; });
    }
}

void Wishlist::inc_replication_from_bitfield(tr_bitfield const& bitfield)
{
    if (candidates_dirty_)
    {
        return;
    }

    if (bitfield.has_none())
    {
        return;
    }

    if (bitfield.has_all())
    {
        inc_replication();
        return;
    }

    for (auto& candidate : candidates_)
    {
        if (bitfield.test(candidate.piece))
        {
            ++candidate.replication;
        }
    }

    std::sort(std::begin(candidates_), std::end(candidates_));
}

void Wishlist::inc_replication_piece(tr_piece_index_t piece)
{
    if (candidates_dirty_)
    {
        return;
    }

    if (auto iter = piece_lookup(piece); iter != std::end(candidates_))
    {
        ++iter->replication;
        resort_piece(iter);
    }
}

void Wishlist::resort_piece(tr_piece_index_t const piece)
{
    if (candidates_dirty_)
    {
        return;
    }

    if (auto iter = piece_lookup(piece); iter != std::end(candidates_))
    {
        resort_piece(iter);
    }
}

void Wishlist::resort_piece(CandidateVec::iterator const pos_old)
{
    if (candidates_dirty_)
    {
        return;
    }

    TR_ASSERT(pos_old != std::end(candidates_));
    if (auto const pos_next = std::next(pos_old); std::is_sorted(
            pos_old == std::begin(candidates_) ? pos_old : std::prev(pos_old),
            pos_next == std::end(candidates_) ? pos_next : std::next(pos_next)))
    {
        return;
    }

    auto const tmp = *pos_old;
    candidates_.erase(pos_old);

    auto const pos_new = std::lower_bound(std::begin(candidates_), std::end(candidates_), tmp);
    candidates_.insert(pos_new, tmp);
}

void Wishlist::remove_piece(tr_piece_index_t const piece)
{
    if (candidates_dirty_)
    {
        return;
    }

    if (auto iter = piece_lookup(piece); iter != std::end(candidates_))
    {
        candidates_.erase(iter);
    }
}

// ---

int Wishlist::Candidate::compare(Wishlist::Candidate const& that) const noexcept
{
    // prefer pieces closer to completion
    if (auto const val = tr_compare_3way(mediator_->count_missing_blocks(piece), mediator_->count_missing_blocks(that.piece));
        val != 0)
    {
        return val;
    }

    // prefer higher priority
    if (auto const val = tr_compare_3way(priority, that.priority); val != 0)
    {
        return -val;
    }

    // prefer rarer pieces
    if (auto const val = tr_compare_3way(replication, that.replication); val != 0)
    {
        return val;
    }

    return tr_compare_3way(salt, that.salt);
}
