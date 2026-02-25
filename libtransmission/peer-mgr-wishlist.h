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

#include "libtransmission/types.h"

class tr_bitfield;

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
        [[nodiscard]] virtual bool download_first_last_pieces_first() const = 0;
        [[nodiscard]] virtual tr_piece_index_t sequential_download_from_piece() const = 0;
        [[nodiscard]] virtual size_t count_piece_replication(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_block_span_t block_span(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_piece_index_t piece_count() const = 0;
        [[nodiscard]] virtual tr_priority_t priority(tr_piece_index_t piece) const = 0;
        virtual ~Mediator() = default;
    };

    explicit Wishlist(Mediator& mediator_in);
    ~Wishlist();

    void on_files_wanted_changed();
    void on_got_bad_piece(tr_piece_index_t piece);
    void on_got_bitfield(tr_bitfield const& bitfield);
    void on_got_block(tr_block_index_t block);
    void on_got_choke(tr_bitfield const& requests);
    void on_got_have(tr_piece_index_t piece);
    void on_got_have_all();
    void on_got_reject(tr_block_index_t block);
    void on_peer_disconnect(tr_bitfield const& have, tr_bitfield const& requests);
    void on_piece_completed(tr_piece_index_t piece);
    void on_priority_changed();
    void on_sent_cancel(tr_block_index_t block);
    void on_sent_request(tr_block_span_t block_span);
    void on_download_first_last_pieces_first_changed();
    void on_sequential_download_changed();
    void on_sequential_download_from_piece_changed();

    // the next blocks that we should request from a peer
    [[nodiscard]] std::vector<tr_block_span_t> next(
        size_t n_wanted_blocks,
        std::function<bool(tr_piece_index_t)> const& peer_has_piece);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
