// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::adjacent_find, std::sort
#include <array>
#include <compare>
#include <cstddef>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

#include <small/set.hpp>
#include <small/vector.hpp>

#define LIBTRANSMISSION_PEER_MODULE

#include "libtransmission/bitfield.h"
#include "libtransmission/crypto-utils.h" // for tr_salt_shaker
#include "libtransmission/peer-mgr-wishlist.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/tr-macros.h" // TR_CONSTEXPR_VEC
#include "libtransmission/types.h"
#include "libtransmission/utils.h"

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

class Wishlist::Impl
{
    struct Candidate
    {
        Candidate(tr_piece_index_t piece_in, tr_piece_index_t salt_in, Mediator const* mediator)
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

        [[nodiscard]] constexpr auto operator<=>(Candidate const& that) const noexcept
        {
            // prefer pieces closer to completion
            if (auto const val = std::size(unrequested) <=> std::size(that.unrequested); val != 0)
            {
                return val;
            }

            // prefer higher priority
            if (auto const val = that.priority <=> priority; val != 0)
            {
                return val;
            }

            // prefer rarer pieces
            if (auto const val = replication <=> that.replication; val != 0)
            {
                return val;
            }

            return salt <=> that.salt;
        }

        [[nodiscard]] constexpr auto operator==(Candidate const& that) const noexcept
        {
            return (*this <=> that) == 0;
        }

        [[nodiscard]] constexpr auto block_belongs(tr_block_index_t const block) const
        {
            return block_span.begin <= block && block < block_span.end;
        }

        tr_piece_index_t piece;
        tr_block_span_t block_span;
        tr_block_span_t raw_block_span;

        // This is sorted in descending order so that smaller blocks indices
        // can be taken from the end of the list, avoiding a move operation.
        small::set<tr_block_index_t, small::default_inline_storage_v<tr_block_index_t>, std::greater<>> unrequested;

        // Caching the following 2 values are highly beneficial, because:
        // - they are often used (mainly because resort_piece() is called
        //   every time we receive a block)
        // - does not change as often compared to missing blocks
        // - calculating their values involves sifting through bitfield(s),
        //   which is expensive.
        size_t replication;
        tr_priority_t priority;

        tr_piece_index_t salt;
    };

    using CandidateVec = std::vector<Candidate>;

public:
    explicit Impl(Mediator& mediator_in);

    void on_files_wanted_changed()
    {
        candidate_list_upkeep();
    }

    void on_got_bad_piece(tr_piece_index_t const piece)
    {
        got_bad_piece(piece);
    }

    void on_got_bitfield(tr_bitfield const& bitfield)
    {
        inc_replication_bitfield(bitfield);
    }

    void on_got_block(tr_block_index_t const block)
    {
        client_got_block(block);
    }

    void on_got_choke(tr_bitfield const& requests)
    {
        reset_blocks_bitfield(requests);
    }

    void on_got_have(tr_piece_index_t const piece)
    {
        inc_replication_piece(piece);
    }

    void on_got_have_all()
    {
        inc_replication();
    }

    void on_got_reject(tr_block_index_t const block)
    {
        reset_block(block);
    }

    void on_peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests)
    {
        peer_disconnect(have, requests);
    }

    void on_piece_completed(tr_piece_index_t const piece)
    {
        remove_piece(piece);
    }

    void on_priority_changed()
    {
        recalculate_priority();
    }

    void on_sent_cancel(tr_block_index_t const block)
    {
        reset_block(block);
    }

    void on_sent_request(tr_block_span_t const block_span)
    {
        requested_block_span(block_span);
    }

    void on_sequential_download_changed()
    {
        recalculate_salt();
    }

    void on_sequential_download_from_piece_changed()
    {
        recalculate_salt();
    }

    [[nodiscard]] std::vector<tr_block_span_t> next(
        size_t n_wanted_blocks,
        std::function<bool(tr_piece_index_t)> const& peer_has_piece);

private:
    constexpr void dec_replication() noexcept
    {
        std::ranges::for_each(candidates_, [](Candidate& candidate) { --candidate.replication; });
    }

    constexpr void dec_replication_bitfield(tr_bitfield const& bitfield)
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

        std::ranges::sort(candidates_);
    }

    constexpr void inc_replication() noexcept
    {
        std::ranges::for_each(candidates_, [](Candidate& candidate) { ++candidate.replication; });
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

        std::ranges::sort(candidates_);
    }

    constexpr void inc_replication_piece(tr_piece_index_t const piece)
    {
        if (auto iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            ++iter->replication;
            resort_piece(iter);
        }
    }

    // ---

    constexpr void requested_block_span(tr_block_span_t const block_span)
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

            auto it_b_end = std::end(unreq);
            it_b_end = *std::prev(it_b_end) >= block_span.begin ? it_b_end : unreq.upper_bound(block_span.begin);

            auto it_b_begin = std::begin(unreq);
            it_b_begin = *it_b_begin < block_span.end ? it_b_begin : unreq.upper_bound(block_span.end);

            unreq.erase(it_b_begin, it_b_end);

            block = it_p->block_span.end;

            resort_piece(it_p);
        }
    }

    constexpr void reset_block(tr_block_index_t block)
    {
        if (auto it_p = find_by_block(block); it_p != std::end(candidates_))
        {
            it_p->unrequested.insert(block);
            resort_piece(it_p);
        }
    }

    TR_CONSTEXPR_VEC void reset_blocks_bitfield(tr_bitfield const& requests)
    {
        for (auto& candidate : candidates_)
        {
            auto const [begin, end] = candidate.block_span;
            if (requests.count(begin, end) == 0U)
            {
                continue;
            }

            for (auto i = end; i > begin; --i)
            {
                if (auto const block = i - 1U; requests.test(block))
                {
                    candidate.unrequested.insert(block);
                }
            }
        }

        std::ranges::sort(candidates_);
    }

    // ---

    constexpr void client_got_block(tr_block_index_t block)
    {
        if (auto const iter = find_by_block(block); iter != std::end(candidates_))
        {
            iter->unrequested.erase(block);
            resort_piece(iter);
        }
    }

    // ---

    TR_CONSTEXPR_VEC void peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests)
    {
        dec_replication_bitfield(have);
        reset_blocks_bitfield(requests);
    }

    // ---

    void got_bad_piece(tr_piece_index_t const piece)
    {
        auto const iter = find_by_piece(piece);
        if (iter == std::end(candidates_))
        {
            return;
        }
        TR_ASSERT(std::empty(iter->unrequested));

        iter->block_span = iter->raw_block_span;
        if (piece > 0U)
        {
            if (auto const prev = find_by_piece(piece - 1U); prev != std::end(candidates_))
            {
                iter->block_span.begin = std::max(iter->block_span.begin, prev->block_span.end);
                TR_ASSERT(iter->block_span.begin == prev->block_span.end);
                for (tr_block_index_t i = iter->block_span.begin; i > iter->raw_block_span.begin; --i)
                {
                    prev->unrequested.insert(i - 1U);
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
                    next->unrequested.insert(i - 1U);
                }
            }
        }

        for (auto [begin, i] = iter->block_span; i > begin; --i)
        {
            iter->unrequested.insert(i - 1U);
        }

        std::ranges::sort(candidates_);
    }

    // ---

    [[nodiscard]] constexpr CandidateVec::iterator find_by_piece(tr_piece_index_t const piece)
    {
        return std::ranges::find_if(candidates_, [piece](auto const& c) { return c.piece == piece; });
    }

    [[nodiscard]] constexpr CandidateVec::iterator find_by_block(tr_block_index_t const block)
    {
        return std::ranges::find_if(candidates_, [block](auto const& c) { return c.block_belongs(block); });
    }

    static constexpr tr_piece_index_t get_salt(
        tr_piece_index_t const piece,
        tr_piece_index_t const n_pieces,
        tr_piece_index_t const random_salt,
        bool const is_sequential,
        tr_piece_index_t const sequential_download_from_piece)
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

    // ---

    void candidate_list_upkeep()
    {
        auto n_old_c = std::size(candidates_);
        auto salter = tr_salt_shaker<tr_piece_index_t>{};
        auto const is_sequential = mediator_.is_sequential_download();
        auto const sequential_download_from_piece = mediator_.sequential_download_from_piece();
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
                    auto const salt = get_salt(piece, n_pieces, salter(), is_sequential, sequential_download_from_piece);
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

    // ---

    constexpr void remove_piece(tr_piece_index_t const piece)
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
        auto const sequential_download_from_piece = mediator_.sequential_download_from_piece();
        auto const n_pieces = mediator_.piece_count();
        for (auto& candidate : candidates_)
        {
            candidate.salt = get_salt(candidate.piece, n_pieces, salter(), is_sequential, sequential_download_from_piece);
        }

        std::ranges::sort(candidates_);
    }

    // ---

    void recalculate_priority()
    {
        for (auto& candidate : candidates_)
        {
            candidate.priority = mediator_.priority(candidate.piece);
        }

        std::ranges::sort(candidates_);
    }

    // ---

    constexpr void resort_piece(CandidateVec::iterator const& pos_old)
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

    Mediator& mediator_;
};

Wishlist::Impl::Impl(Mediator& mediator_in)
    : mediator_{ mediator_in }
{
    candidate_list_upkeep();
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

// ---

Wishlist::Wishlist(Mediator& mediator_in)
    : impl_{ std::make_unique<Impl>(mediator_in) }
{
}

Wishlist::~Wishlist() = default;

void Wishlist::on_files_wanted_changed()
{
    impl_->on_files_wanted_changed();
}

void Wishlist::on_got_bad_piece(tr_piece_index_t const piece)
{
    impl_->on_got_bad_piece(piece);
}

void Wishlist::on_got_bitfield(tr_bitfield const& bitfield)
{
    impl_->on_got_bitfield(bitfield);
}

void Wishlist::on_got_block(tr_block_index_t const block)
{
    impl_->on_got_block(block);
}

void Wishlist::on_got_choke(tr_bitfield const& requests)
{
    impl_->on_got_choke(requests);
}

void Wishlist::on_got_have(tr_piece_index_t const piece)
{
    impl_->on_got_have(piece);
}

void Wishlist::on_got_have_all()
{
    impl_->on_got_have_all();
}

void Wishlist::on_got_reject(tr_block_index_t const block)
{
    impl_->on_got_reject(block);
}

void Wishlist::on_peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests)
{
    impl_->on_peer_disconnect(have, requests);
}

void Wishlist::on_piece_completed(tr_piece_index_t const piece)
{
    impl_->on_piece_completed(piece);
}

void Wishlist::on_priority_changed()
{
    impl_->on_priority_changed();
}

void Wishlist::on_sent_cancel(tr_block_index_t const block)
{
    impl_->on_sent_cancel(block);
}

void Wishlist::on_sent_request(tr_block_span_t const block_span)
{
    impl_->on_sent_request(block_span);
}

void Wishlist::on_sequential_download_changed()
{
    impl_->on_sequential_download_changed();
}

void Wishlist::on_sequential_download_from_piece_changed()
{
    impl_->on_sequential_download_from_piece_changed();
}

std::vector<tr_block_span_t> Wishlist::next(
    size_t const n_wanted_blocks,
    std::function<bool(tr_piece_index_t)> const& peer_has_piece)
{
    return impl_->next(n_wanted_blocks, peer_has_piece);
}
