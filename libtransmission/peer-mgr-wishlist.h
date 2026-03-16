// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <algorithm>
#include <compare>
#include <cstddef> // size_t
#include <functional>
#include <memory>
#include <vector>

#include <small/set.hpp>

#include "libtransmission/bitfield.h"
#include "libtransmission/crypto-utils.h"
#include "libtransmission/tr-macros.h"
#include "libtransmission/types.h"

/**
 * Figures out what blocks we want to request next.
 */
class Wishlist
{
public:
    struct Mediator
    {
        [[nodiscard]] virtual bool client_has_block(tr_block_index_t block) const = 0;
        [[nodiscard]] virtual bool client_has_piece(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual bool client_wants_piece(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual bool is_sequential_download() const = 0;
        [[nodiscard]] virtual tr_piece_index_t sequential_download_from_piece() const = 0;
        [[nodiscard]] virtual size_t count_piece_replication(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_block_span_t block_span(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_piece_index_t piece_count() const = 0;
        [[nodiscard]] virtual tr_priority_t priority(tr_piece_index_t piece) const = 0;
        virtual ~Mediator() = default;
    };

private:
    struct Candidate
    {
        Candidate(tr_piece_index_t piece_in, tr_piece_index_t salt_in, Mediator const* mediator);

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
    explicit Wishlist(Mediator& mediator_in)
        : mediator_{ mediator_in }
    {
        candidate_list_upkeep();
    }

    void on_files_wanted_changed()
    {
        candidate_list_upkeep();
    }

    void on_got_bad_piece(tr_piece_index_t piece);

    constexpr void on_got_bitfield(tr_bitfield const& bitfield)
    {
        inc_replication_bitfield(bitfield);
    }

    constexpr void on_got_block(tr_block_index_t const block)
    {
        if (auto const iter = find_by_block(block); iter != std::end(candidates_))
        {
            iter->unrequested.erase(block);
            resort_piece(iter);
        }
    }

    TR_CONSTEXPR_VEC void on_got_choke(tr_bitfield const& requests)
    {
        reset_blocks_bitfield(requests);
    }

    constexpr void on_got_have(tr_piece_index_t const piece)
    {
        if (auto const iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            ++iter->replication;
            resort_piece(iter);
        }
    }

    constexpr void on_got_have_all() noexcept
    {
        inc_replication();
    }

    constexpr void on_got_reject(tr_block_index_t const block)
    {
        reset_block(block);
    }

    TR_CONSTEXPR_VEC void on_peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests)
    {
        dec_replication_bitfield(have);
        reset_blocks_bitfield(requests);
    }

    constexpr void on_piece_completed(tr_piece_index_t const piece)
    {
        remove_piece(piece);
    }

    void on_priority_changed();

    constexpr void on_sent_cancel(tr_block_index_t const block)
    {
        reset_block(block);
    }

    constexpr void on_sent_request(tr_block_span_t const block_span)
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

    // the next blocks that we should request from a peer
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

    constexpr void inc_replication_bitfield(tr_bitfield const& bitfield)
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

    // ---

    constexpr void requested_block_span(tr_block_span_t const block_span)
    {
        for (auto block = block_span.begin; block < block_span.end;)
        {
            auto const it_p = find_by_block(block);
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
        if (auto const it_p = find_by_block(block); it_p != std::end(candidates_))
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

    [[nodiscard]] constexpr CandidateVec::iterator find_by_piece(tr_piece_index_t const piece)
    {
        return std::ranges::find_if(candidates_, [piece](auto const& c) { return c.piece == piece; });
    }

    [[nodiscard]] constexpr CandidateVec::iterator find_by_block(tr_block_index_t const block)
    {
        return std::ranges::find_if(candidates_, [block](auto const& c) { return c.block_belongs(block); });
    }

    tr_piece_index_t get_salt(tr_piece_index_t piece);

    // ---

    void candidate_list_upkeep();

    // ---

    constexpr void remove_piece(tr_piece_index_t const piece)
    {
        if (auto const iter = find_by_piece(piece); iter != std::end(candidates_))
        {
            candidates_.erase(iter);
        }
    }

    // ---

    void recalculate_salt();

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

    tr_salt_shaker<tr_piece_index_t> salter_ = {};

    Mediator& mediator_;
};
