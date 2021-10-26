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

#include "peer-common.h"
#include "peer-mgr-active-requests.h"
#include "torrent.h"

class Wishlist
{
public:
    std::vector<tr_block_range> next(
        tr_torrent* tor,
        tr_peer* peer,
        size_t numwant,
        ActiveRequests& active_requests,
        bool is_endgame);
};
