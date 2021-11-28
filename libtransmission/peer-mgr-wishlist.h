/*
 * This file Copyright (C) 2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include "transmission.h"
#include "torrent.h"

/**
 * Figures out what blocks we want to request next.
 */
class Wishlist
{
public:
    struct PeerInfo
    {
        virtual bool clientCanRequestBlock(tr_block_index_t block) const = 0;
        virtual bool clientCanRequestPiece(tr_piece_index_t piece) const = 0;
        virtual bool isEndgame() const = 0;
        virtual size_t countActiveRequests(tr_block_index_t block) const = 0;
        virtual size_t countMissingBlocks(tr_piece_index_t piece) const = 0;
        virtual tr_block_span_t blockSpan(tr_piece_index_t) const = 0;
        virtual tr_piece_index_t countAllPieces() const = 0;
        virtual tr_priority_t priority(tr_piece_index_t) const = 0;
        virtual ~PeerInfo() = default;
    };

    // get a list of the next blocks that we should request from a peer
    std::vector<tr_block_span_t> next(PeerInfo const& peer_info, size_t n_wanted_blocks) const;
};
