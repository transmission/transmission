/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <inttypes.h>
#include "peer-common.h"

class tr_peer;
struct tr_address;
struct tr_bitfield;
struct tr_peerIo;
struct tr_torrent;

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peerMsgs : public tr_peer
{
public:
    tr_peerMsgs(tr_torrent* torrent, peer_atom* atom_in)
        : tr_peer{ torrent, atom_in }
    {
    }

    virtual ~tr_peerMsgs() override = default;

    virtual bool is_peer_choked() const = 0;
    virtual bool is_peer_interested() const = 0;
    virtual bool is_client_choked() const = 0;
    virtual bool is_client_interested() const = 0;

    virtual bool is_utp_connection() const = 0;
    virtual bool is_encrypted() const = 0;
    virtual bool is_incoming_connection() const = 0;

    virtual bool is_active(tr_direction direction) const = 0;
    virtual void update_active(tr_direction direction) = 0;

    virtual time_t get_connection_age() const = 0;
    virtual bool is_reading_block(tr_block_index_t block) const = 0;

    virtual void cancel_block_request(tr_block_index_t block) = 0;

    virtual void set_choke(bool peer_is_choked) = 0;
    virtual void set_interested(bool client_is_interested) = 0;

    virtual void pulse() = 0;

    virtual void on_piece_completed(tr_piece_index_t) = 0;
};

tr_peerMsgs* tr_peerMsgsNew(
    tr_torrent* torrent,
    peer_atom* atom,
    tr_peerIo* io,
    tr_peer_callback callback,
    void* callback_data);

size_t tr_generateAllowedSet(
    tr_piece_index_t* setmePieces,
    size_t desiredSetSize,
    size_t pieceCount,
    uint8_t const* infohash,
    struct tr_address const* addr);

/* @} */
