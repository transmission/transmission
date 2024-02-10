// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <cstddef> // size_t
#include <functional>
#include <memory>
#include <vector>

#include "libtransmission/transmission.h"

#include "libtransmission/observable.h"
#include "libtransmission/utils.h"

class tr_bitfield;

/**
 * Figures out what blocks we want to request next.
 */
class Wishlist
{
public:
    static auto constexpr EndgameMaxPeers = size_t{ 2U };
    static auto constexpr NormalMaxPeers = size_t{ 1U };

    struct Mediator
    {
        [[nodiscard]] virtual bool client_has_block(tr_block_index_t block) const = 0;
        [[nodiscard]] virtual bool client_wants_piece(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual bool is_endgame() const = 0;
        [[nodiscard]] virtual bool is_sequential_download() const = 0;
        [[nodiscard]] virtual size_t count_active_requests(tr_block_index_t block) const = 0;
        [[nodiscard]] virtual size_t count_missing_blocks(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual size_t count_piece_replication(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_block_span_t block_span(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_piece_index_t piece_count() const = 0;
        [[nodiscard]] virtual tr_priority_t priority(tr_piece_index_t piece) const = 0;

        [[nodiscard]] virtual libtransmission::ObserverTag observe_peer_disconnect(
            libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_got_bitfield(
            libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_got_block(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t, tr_block_index_t>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_got_have(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_got_have_all(
            libtransmission::SimpleObservable<tr_torrent*>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_piece_completed(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_priority_changed(
            libtransmission::SimpleObservable<tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t>::Observer
                observer) = 0;
        [[nodiscard]] virtual libtransmission::ObserverTag observe_sequential_download_changed(
            libtransmission::SimpleObservable<tr_torrent*, bool>::Observer observer) = 0;

        virtual ~Mediator() = default;
    };

private:
    struct Candidate
    {
        Candidate(
            tr_piece_index_t piece_in,
            size_t replication_in,
            tr_priority_t priority_in,
            tr_piece_index_t salt_in,
            Mediator const* mediator)
            : piece{ piece_in }
            , replication{ replication_in }
            , priority{ priority_in }
            , salt{ salt_in }
            , mediator_{ mediator }
        {
        }

        [[nodiscard]] int compare(Candidate const& that) const noexcept; // <=>

        [[nodiscard]] auto operator<(Candidate const& that) const // less than
        {
            return compare(that) < 0;
        }

        tr_piece_index_t piece;

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

public:
    explicit Wishlist(std::unique_ptr<Mediator> mediator_in);

    constexpr void set_candidates_dirty() noexcept
    {
        candidates_dirty_ = true;
    }

    // the next blocks that we should request from a peer
    [[nodiscard]] std::vector<tr_block_span_t> next(
        size_t n_wanted_blocks,
        std::function<bool(tr_piece_index_t)> const& peer_has_piece,
        std::function<bool(tr_block_index_t)> const& has_active_pending_to_peer);

    void dec_replication();
    void dec_replication_from_bitfield(tr_bitfield const& bitfield);
    void inc_replication();
    void inc_replication_from_bitfield(tr_bitfield const& bitfield);
    void inc_replication_piece(tr_piece_index_t piece);

    void remove_piece(tr_piece_index_t piece);
    void resort_piece(tr_piece_index_t piece);

private:
    using CandidateVec = std::vector<Candidate>;

    CandidateVec::iterator piece_lookup(tr_piece_index_t piece);
    void maybe_rebuild_candidate_list();
    void resort_piece(CandidateVec::iterator pos_old);

    CandidateVec candidates_;
    bool candidates_dirty_ = true;

    std::array<libtransmission::ObserverTag, 8U> const tags_;

    std::unique_ptr<Mediator> const mediator_;
};
