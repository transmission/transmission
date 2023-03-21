// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <cstddef> // size_t
#include <vector>

#include "transmission.h"

#include "torrent.h"

/**
 * Figures out what blocks we want to request next.
 */
class Wishlist
{
public:
    struct Mediator
    {
        [[nodiscard]] virtual bool clientCanRequestBlock(tr_block_index_t block) const = 0;
        [[nodiscard]] virtual bool clientCanRequestPiece(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual bool isEndgame() const = 0;
        [[nodiscard]] virtual size_t countActiveRequests(tr_block_index_t block) const = 0;
        [[nodiscard]] virtual size_t countMissingBlocks(tr_piece_index_t piece) const = 0;
        [[nodiscard]] virtual tr_block_span_t blockSpan(tr_piece_index_t) const = 0;
        [[nodiscard]] virtual tr_piece_index_t countAllPieces() const = 0;
        [[nodiscard]] virtual tr_priority_t priority(tr_piece_index_t) const = 0;
        virtual ~Mediator() = default;
    };

    constexpr explicit Wishlist(Mediator const& mediator)
        : mediator_{ mediator }
    {
    }

    // the next blocks that we should request from a peer
    [[nodiscard]] std::vector<tr_block_span_t> next(size_t n_wanted_blocks);

private:
    Mediator const& mediator_;
};
