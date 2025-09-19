// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::adjacent_find, std::sort
#include <cstddef>
#include <functional>
#include <utility>
#include <vector>

#include <small/set.hpp>
#include <small/vector.hpp>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/transmission.h"

#include "libtransmission/bitfield.h"
#include "libtransmission/crypto-utils.h" // for tr_salt_shaker
#include "libtransmission/tr-macros.h"
#include "libtransmission/peer-mgr-wishlist.h"

namespace
{
[[nodiscard]] std::vector<tr_block_span_t> make_spans(small::vector<tr_block_index_t> const& blocks)
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
            unrequested.reserve(block_span.end - block_span.begin);
            for (auto [block, end] = block_span; block < end; ++block)
            {
                if (!mediator_->client_has_block(block))
                {
                    unrequested.insert(block);
                }
            }
        }

        [[nodiscard]] int compare(Candidate const& that) const noexcept; // <=>

        [[nodiscard]] auto operator<(Candidate const& that) const // less than
        {
            return compare(that) < 0;
        }

        [[nodiscard]] constexpr auto block_belongs(tr_block_index_t const block) const
        {
            return block_span.begin <= block && block < block_span.end;
        }

        tr_piece_index_t piece;
        tr_block_span_t block_span;

        small::set<tr_block_index_t> unrequested;

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
    explicit Impl(Mediator& mediator_in);

    [[nodiscard]] std::vector<tr_block_span_t> next(
        size_t n_wanted_blocks,
        std::function<bool(tr_piece_index_t)> const& peer_has_piece);

private:
    TR_CONSTEXPR20 void dec_replication() noexcept
    {
        std::for_each(std::begin(candidates_), std::end(candidates_), [](Candidate& candidate) { --candidate.replication; });
    }

    TR_CONSTEXPR20 void dec_replication_bitfield(tr_bitfield const& bitfield)
    {
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
        std::for_each(std::begin(candidates_), std::end(candidates_), [](Candidate& candidate) { ++candidate.replication; });
    }

    void inc_replication_bitfield(tr_bitfield const& bitfield)
    {
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

    TR_CONSTEXPR20 void inc_replication_piece(tr_piece_index_t const piece)
    {
        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            ++iter->replication;
            resort_piece(iter);
        }
    }

    // ---

    TR_CONSTEXPR20 void requested_block_span(tr_block_span_t const block_span)
    {
        for (auto block = block_span.begin; block < block_span.end;)
        {
            auto it_p = find_by_block(block);
            if (it_p == std::end(candidates_))
            {
                // std::unreachable();
                break;
            }

            auto& unreq = it_p->unrequested;

            auto it_b_begin = std::begin(unreq);
            it_b_begin = *it_b_begin >= block_span.begin ? it_b_begin : unreq.lower_bound(block_span.begin);

            auto it_b_end = std::end(unreq);
            it_b_end = *std::prev(it_b_end) < block_span.end ? it_b_end : unreq.lower_bound(block_span.end);

            unreq.erase(it_b_begin, it_b_end);

            block = it_p->block_span.end;

            resort_piece(it_p);
        }
    }

    TR_CONSTEXPR20 void reset_block(tr_block_index_t block)
    {
        if (auto it_p = find_by_block(block); it_p != std::end(candidates_))
        {
            it_p->unrequested.insert(block);
            resort_piece(it_p);
        }
    }

    TR_CONSTEXPR20 void reset_blocks_bitfield(tr_bitfield const& requests)
    {
        for (auto& candidate : candidates_)
        {
            for (auto [block, end] = candidate.block_span; block < end; ++block)
            {
                if (requests.test(block))
                {
                    candidate.unrequested.insert(block);
                }
            }
        }

        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    // ---

    TR_CONSTEXPR20 void peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests)
    {
        dec_replication_bitfield(have);
        reset_blocks_bitfield(requests);
    }

    // ---

    TR_CONSTEXPR20 void got_bad_piece(tr_piece_index_t const piece)
    {
        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            for (auto [block, end] = iter->block_span; block < end; ++block)
            {
                iter->unrequested.insert(block);
            }
            resort_piece(iter);
        }
    }

    // ---

    [[nodiscard]] TR_CONSTEXPR20 CandidateVec::iterator find_by_piece(tr_piece_index_t const piece)
    {
        return std::find_if(
            std::begin(candidates_),
            std::end(candidates_),
            [piece](auto const& c) { return c.piece == piece; });
    }

    [[nodiscard]] TR_CONSTEXPR20 CandidateVec::iterator find_by_block(tr_block_index_t const block)
    {
        return std::find_if(
            std::begin(candidates_),
            std::end(candidates_),
            [block](auto const& c) { return c.block_belongs(block); });
    }

    static constexpr tr_piece_index_t get_salt(
        tr_piece_index_t const piece,
        tr_piece_index_t const n_pieces,
        tr_piece_index_t const random_salt,
        bool const is_sequential)
    {
        if (!is_sequential)
        {
            return random_salt;
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

        return piece + 1U;
    }

    // ---

    void recalculate_wanted_pieces()
    {
        auto n_old_c = std::size(candidates_);
        auto salter = tr_salt_shaker<tr_piece_index_t>{};
        auto const is_sequential = mediator_.is_sequential_download();
        auto const n_pieces = mediator_.piece_count();

        std::sort(
            std::begin(candidates_),
            std::end(candidates_),
            [](auto const& lhs, auto const& rhs) { return lhs.piece < rhs.piece; });

        for (tr_piece_index_t piece = 0U, idx_c = 0U; piece < n_pieces; ++piece)
        {
            auto const existing_candidate = idx_c < n_old_c && piece == candidates_[idx_c].piece;
            if (mediator_.client_wants_piece(piece))
            {
                if (existing_candidate)
                {
                    ++idx_c;
                }
                else
                {
                    auto const salt = get_salt(piece, n_pieces, salter(), is_sequential);
                    candidates_.emplace_back(piece, salt, &mediator_);
                }
            }
            else if (existing_candidate)
            {
                candidates_.erase(std::next(std::begin(candidates_), idx_c));
                --n_old_c;
            }
        }

        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    // ---

    TR_CONSTEXPR20 void remove_piece(tr_piece_index_t const piece)
    {
        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            candidates_.erase(iter);
        }
    }

    // ---

    void recalculate_salt()
    {
        auto salter = tr_salt_shaker<tr_piece_index_t>{};
        auto const is_sequential = mediator_.is_sequential_download();
        auto const n_pieces = mediator_.piece_count();
        for (auto& candidate : candidates_)
        {
            candidate.salt = get_salt(candidate.piece, n_pieces, salter(), is_sequential);
        }

        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    // ---

    void recalculate_priority()
    {
        for (auto& candidate : candidates_)
        {
            candidate.priority = mediator_.priority(candidate.piece);
        }

        std::sort(std::begin(candidates_), std::end(candidates_));
    }

    // ---

    TR_CONSTEXPR20 void resort_piece(CandidateVec::iterator const& pos_old)
    {
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

    std::array<libtransmission::ObserverTag, 14U> const tags_;

    Mediator& mediator_;
};

Wishlist::Impl::Impl(Mediator& mediator_in)
    : tags_{ {
          // candidates
          mediator_in.observe_files_wanted_changed([this](tr_torrent*, tr_file_index_t const*, tr_file_index_t, bool)
                                                   { recalculate_wanted_pieces(); }),
          // replication, unrequested
          mediator_in.observe_peer_disconnect([this](tr_torrent*, tr_bitfield const& b, tr_bitfield const& ar)
                                              { peer_disconnect(b, ar); }),
          // unrequested
          mediator_in.observe_got_bad_piece([this](tr_torrent*, tr_piece_index_t p) { got_bad_piece(p); }),
          // replication
          mediator_in.observe_got_bitfield([this](tr_torrent*, tr_bitfield const& b) { inc_replication_bitfield(b); }),
          // unrequested
          mediator_in.observe_got_choke([this](tr_torrent*, tr_bitfield const& b) { reset_blocks_bitfield(b); }),
          // replication
          mediator_in.observe_got_have([this](tr_torrent*, tr_piece_index_t p) { inc_replication_piece(p); }),
          // replication
          mediator_in.observe_got_have_all([this](tr_torrent*) { inc_replication(); }),
          // unrequested
          mediator_in.observe_got_reject([this](tr_torrent*, tr_peer*, tr_block_index_t b) { reset_block(b); }),
          // candidates
          mediator_in.observe_piece_completed([this](tr_torrent*, tr_piece_index_t p) { remove_piece(p); }),
          // priority
          mediator_in.observe_priority_changed([this](tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t)
                                               { recalculate_priority(); }),
          // unrequested
          mediator_in.observe_sent_cancel([this](tr_torrent*, tr_peer*, tr_block_index_t b) { reset_block(b); }),
          // unrequested
          mediator_in.observe_sent_request([this](tr_torrent*, tr_peer*, tr_block_span_t bs) { requested_block_span(bs); }),
          // salt
          mediator_in.observe_sequential_download_changed([this](tr_torrent*, bool) { recalculate_salt(); }),
      } }
    , mediator_{ mediator_in }
{
    auto salter = tr_salt_shaker<tr_piece_index_t>{};
    auto const is_sequential = mediator_.is_sequential_download();
    auto const n_pieces = mediator_.piece_count();
    candidates_.reserve(n_pieces);
    for (tr_piece_index_t piece = 0U; piece < n_pieces; ++piece)
    {
        if (mediator_.client_has_piece(piece) || !mediator_.client_wants_piece(piece))
        {
            continue;
        }

        auto const salt = get_salt(piece, n_pieces, salter(), is_sequential);
        candidates_.emplace_back(piece, salt, &mediator_);
    }
    std::sort(std::begin(candidates_), std::end(candidates_));
}

std::vector<tr_block_span_t> Wishlist::Impl::next(
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

        // walk the blocks in this piece that we don't have or not requested
        for (auto const& block : candidate.unrequested)
        {
            if (std::size(blocks) >= n_wanted_blocks)
            {
                break;
            }

            blocks.emplace_back(block);
        }
    }

    // Ensure the list of blocks are sorted
    // The list needs to be unique as well, but that should come naturally
    std::sort(std::begin(blocks), std::end(blocks));
    return make_spans(blocks);
}

int Wishlist::Impl::Candidate::compare(Candidate const& that) const noexcept
{
    // prefer pieces closer to completion
    if (auto const val = tr_compare_3way(std::size(unrequested), std::size(that.unrequested)); val != 0)
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

Wishlist::Wishlist(Mediator& mediator_in)
    : impl_{ std::make_unique<Impl>(mediator_in) }
{
}

Wishlist::~Wishlist() = default;

std::vector<tr_block_span_t> Wishlist::next(
    size_t const n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece)
{
    return impl_->next(n_wanted_blocks, peer_has_piece);
}
