// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::adjacent_find, std::sort
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <small/vector.hpp>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/transmission.h"

#include "libtransmission/bitfield.h"
#include "libtransmission/crypto-utils.h" // for tr_salt_shaker
#include "libtransmission/peer-mgr-wishlist.h"

namespace
{
std::vector<tr_block_span_t> make_spans(small::vector<tr_block_index_t> const& blocks)
{
    if (std::empty(blocks))
    {
        return {};
    }

    auto spans = std::vector<tr_block_span_t>{};
    spans.reserve(std::size(blocks));
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

class Wishlist::Impl
{
    struct Candidate
    {
        Candidate(tr_piece_index_t piece_in, tr_piece_index_t salt_in, Mediator const* mediator)
            : piece{ piece_in }
            , block_span{ mediator->block_span(piece_in) }
            , replication{ mediator->count_piece_replication(piece_in) }
            , priority{ mediator->priority(piece_in) }
            , salt{ salt_in }
            , mediator_{ mediator }
        {
            n_active_requests.reserve(block_span.end - block_span.begin);
            for (auto [block, end] = block_span; block < end; ++block)
            {
                n_active_requests.emplace_back(mediator_->count_active_requests(block));
            }
        }

        [[nodiscard]] int compare(Candidate const& that) const noexcept; // <=>

        [[nodiscard]] auto operator<(Candidate const& that) const // less than
        {
            return compare(that) < 0;
        }

        tr_piece_index_t piece;
        tr_block_span_t block_span;

        std::vector<uint8_t> n_active_requests;

        // Caching the following 2 values are highly beneficial, because:
        // - they are often used (mainly because resort_piece() is called
        //   every time we receive a block)
        // - does not change as often compared to missing blocks
        // - calculating their values involves sifting through bitfield(s),
        //   which is expensive.
        size_t replication;
        tr_priority_t priority;

        tr_piece_index_t salt;

    private:
        Mediator const* mediator_;
    };

    using CandidateVec = std::vector<Candidate>;

public:
    explicit Impl(std::unique_ptr<Mediator> mediator_in);

    std::vector<tr_block_span_t> next(
        size_t n_wanted_blocks,
        std::function<bool(tr_piece_index_t)> const& peer_has_piece,
        std::function<bool(tr_block_index_t)> const& has_active_request_to_peer);

private:
    constexpr void set_candidates_dirty() noexcept
    {
        candidates_dirty_ = true;
    }

    // ---

    TR_CONSTEXPR20 void dec_replication() noexcept
    {
        if (!candidates_dirty_)
        {
            std::for_each(
                std::begin(candidates_),
                std::end(candidates_),
                [](Candidate& candidate) { --candidate.replication; });
        }
    }

    TR_CONSTEXPR20 void dec_replication_from_bitfield(tr_bitfield const& bitfield)
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
                --candidate.replication;
            }
        }

        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    TR_CONSTEXPR20 void inc_replication() noexcept
    {
        if (!candidates_dirty_)
        {
            std::for_each(
                std::begin(candidates_),
                std::end(candidates_),
                [](Candidate& candidate) { ++candidate.replication; });
        }
    }

    void inc_replication_from_bitfield(tr_bitfield const& bitfield)
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

    TR_CONSTEXPR20 void inc_replication_piece(tr_piece_index_t piece)
    {
        if (candidates_dirty_)
        {
            return;
        }

        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            ++iter->replication;
            resort_piece(iter);
        }
    }

    // ---

    TR_CONSTEXPR20 void client_sent_request(tr_block_span_t block_span)
    {
        if (candidates_dirty_)
        {
            return;
        }

        for (auto block = block_span.begin; block < block_span.end;)
        {
            auto iter = find_by_block(block);
            if (iter == std::end(candidates_))
            {
                set_candidates_dirty();
                break;
            }

            auto const block_end = std::min(block_span.end, iter->block_span.end);
            for (; block < block_end; ++block)
            {
                ++iter->n_active_requests[block - iter->block_span.begin];
            }
        }
    }

    TR_CONSTEXPR20 void client_got_reject(tr_block_index_t block)
    {
        if (candidates_dirty_)
        {
            return;
        }

        if (auto iter = find_by_block(block); iter != std::end(candidates_))
        {
            if (auto& active_request = iter->n_active_requests[block - iter->block_span.begin]; active_request > 0)
            {
                --active_request;
            }
        }
    }

    TR_CONSTEXPR20 void client_got_choke(tr_bitfield const& requests)
    {
        for (auto& candidate : candidates_)
        {
            auto const block_begin = candidate.block_span.begin;
            for (size_t i = 0, n = std::size(candidate.n_active_requests); i < n; ++i)
            {
                auto const block = block_begin + i;
                if (auto& active_request = candidate.n_active_requests[i]; active_request > 0 && requests.test(block))
                {
                    --active_request;
                }
            }
        }
    }

    TR_CONSTEXPR20 void client_got_block(tr_block_index_t block)
    {
        if (auto iter = find_by_block(block); iter != std::end(candidates_))
        {
            resort_piece(iter);
        }
    }

    // ---

    TR_CONSTEXPR20 CandidateVec::iterator find_by_piece(tr_piece_index_t const piece)
    {
        return std::find_if(
            std::begin(candidates_),
            std::end(candidates_),
            [piece](auto const& c) { return c.piece == piece; });
    }

    TR_CONSTEXPR20 CandidateVec::iterator find_by_block(tr_block_index_t const block)
    {
        return std::find_if(
            std::begin(candidates_),
            std::end(candidates_),
            [block](auto const& c) { return c.block_span.begin <= block && block < c.block_span.end; });
    }

    void maybe_rebuild_candidate_list()
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
            candidates_.emplace_back(piece, salt, mediator_.get());
        }
        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    TR_CONSTEXPR20 void remove_piece(tr_piece_index_t const piece)
    {
        if (candidates_dirty_)
        {
            return;
        }

        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            candidates_.erase(iter);
        }
    }

    TR_CONSTEXPR20 void resort_piece(CandidateVec::iterator const pos_old)
    {
        if (candidates_dirty_)
        {
            return;
        }

        auto const pos_begin = std::begin(candidates_);

        // Candidate needs to be moved towards the front of the list
        if (auto const pos_next = std::next(pos_old); pos_old > pos_begin && *pos_old < *std::prev(pos_old))
        {
            auto const pos_new = std::lower_bound(pos_begin, pos_old, *pos_old);
            std::rotate(pos_new, pos_old, pos_next);
        }
        // Candidate needs to be moved towards the end of the list
        else if (auto const pos_end = std::end(candidates_); pos_next < pos_end && *pos_next < *pos_old)
        {
            auto const pos_new = std::lower_bound(pos_next, pos_end, *pos_old);
            std::rotate(pos_old, pos_next, pos_new);
        }
    }

    CandidateVec candidates_;
    bool candidates_dirty_ = true;
    bool is_endgame_ = false;

    std::array<libtransmission::ObserverTag, 11U> const tags_;

    std::unique_ptr<Mediator> const mediator_;
};

Wishlist::Impl::Impl(std::unique_ptr<Mediator> mediator_in)
    : tags_{ {
          mediator_in->observe_peer_disconnect([this](tr_torrent*, tr_bitfield const& b) { dec_replication_from_bitfield(b); }),
          mediator_in->observe_got_bitfield([this](tr_torrent*, tr_bitfield const& b) { inc_replication_from_bitfield(b); }),
          mediator_in->observe_got_block([this](tr_torrent*, tr_block_index_t b) { client_got_block(b); }),
          mediator_in->observe_got_choke([this](tr_torrent*, tr_bitfield const& b) { client_got_choke(b); }),
          mediator_in->observe_got_have([this](tr_torrent*, tr_piece_index_t p) { inc_replication_piece(p); }),
          mediator_in->observe_got_have_all([this](tr_torrent*) { inc_replication(); }),
          mediator_in->observe_got_reject([this](tr_torrent*, tr_peer*, tr_block_index_t b) { client_got_reject(b); }),
          mediator_in->observe_piece_completed([this](tr_torrent*, tr_piece_index_t p) { remove_piece(p); }),
          mediator_in->observe_priority_changed([this](tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t)
                                                { set_candidates_dirty(); }),
          mediator_in->observe_sent_request([this](tr_torrent*, tr_peer*, tr_block_span_t bs) { client_sent_request(bs); }),
          mediator_in->observe_sequential_download_changed([this](tr_torrent*, bool) { set_candidates_dirty(); }),
      } }
    , mediator_{ std::move(mediator_in) }
{
}

std::vector<tr_block_span_t> Wishlist::Impl::next(
    size_t n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece,
    std::function<bool(tr_block_index_t)> const& has_active_request_to_peer)
{
    if (n_wanted_blocks == 0U)
    {
        return {};
    }

    maybe_rebuild_candidate_list();

    auto const max_peers = is_endgame_ ? EndgameMaxPeers : NormalMaxPeers;
    auto blocks = small::vector<tr_block_index_t>{};
    blocks.reserve(n_wanted_blocks);
    for (auto const& candidate : candidates_)
    {
        // do we have enough?
        if (std::size(blocks) >= n_wanted_blocks)
        {
            break;
        }

        // if the peer doesn't have this piece that we want...
        if (candidate.replication == 0 || !peer_has_piece(candidate.piece))
        {
            continue;
        }

        // walk the blocks in this piece
        for (auto [block, end] = candidate.block_span; block < end && std::size(blocks) < n_wanted_blocks; ++block)
        {
            // don't request from too many peers
            auto const block_idx = block - candidate.block_span.begin;
            if (auto const n_peers = candidate.n_active_requests[block_idx]; n_peers >= max_peers)
            {
                continue;
            }

            // don't request blocks that:
            // 1. we've already got, or
            // 2. already has an active request to that peer
            if (mediator_->client_has_block(block) || has_active_request_to_peer(block))
            {
                continue;
            }

            blocks.emplace_back(block);
        }
    }

    is_endgame_ = std::size(blocks) < n_wanted_blocks;

    // Ensure the list of blocks are sorted
    // The list needs to be unique as well, but that should come naturally
    std::sort(std::begin(blocks), std::end(blocks));
    return make_spans(blocks);
}

int Wishlist::Impl::Candidate::compare(Wishlist::Impl::Candidate const& that) const noexcept
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

// ---

Wishlist::Wishlist(std::unique_ptr<Mediator> mediator_in)
    : impl_{ std::make_unique<Impl>(std::move(mediator_in)) }
{
}

Wishlist::~Wishlist() = default;

std::vector<tr_block_span_t> Wishlist::next(
    size_t n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece,
    std::function<bool(tr_block_index_t)> const& has_active_pending_to_peer)
{
    return impl_->next(n_wanted_blocks, peer_has_piece, has_active_pending_to_peer);
}
