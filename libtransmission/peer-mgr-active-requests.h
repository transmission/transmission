// This file Copyright © 2021-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_PEER_MODULE
#error only the libtransmission peer module should #include this header.
#endif

#include <cstddef> // size_t
#include <ctime> // time_t
#include <memory>
#include <utility>
#include <vector>

#include "transmission.h" // tr_block_index_t
#include "peer-common.h" // tr_peer*

/**
 * Bookkeeping for the active requests we have --
 *  e.g. the requests we've sent and are awaiting a response.
 */
class ActiveRequests
{
public:
    ActiveRequests();
    ~ActiveRequests();

    // record that we've requested `block` from `peer`
    bool add(tr_block_index_t block, tr_peer* peer, time_t when);

    // erase any record of a request for `block` from `peer`
    bool remove(tr_block_index_t block, tr_peer const* peer);

    // erase any record of requests to `peer` and return the previously-associated blocks
    std::vector<tr_block_index_t> remove(tr_peer const* peer);

    // erase any record of requests to `block` and return the previously-associated peers
    std::vector<tr_peer*> remove(tr_block_index_t block);

    // return true if there's a record of a request for `block` from `peer`
    [[nodiscard]] bool has(tr_block_index_t block, tr_peer const* peer) const;

    // count how many peers we're asking for `block`
    [[nodiscard]] size_t count(tr_block_index_t block) const;

    // count how many active block requests we have to `peer`
    [[nodiscard]] size_t count(tr_peer const* peer) const;

    // return the total number of active requests
    [[nodiscard]] size_t size() const;

    // returns the active requests sent before `when`
    [[nodiscard]] std::vector<std::pair<tr_block_index_t, tr_peer*>> sentBefore(time_t when) const;

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
