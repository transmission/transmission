// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // int8_t
#include <cstddef> // size_t
#include <ctime> // time_t

#include "bitfield.h"
#include "peer-common.h"
#include "torrent.h"

class tr_peer;
class tr_peerIo;
struct Bandwidth;
struct tr_address;

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peerMsgs : public tr_peer
{
public:
    tr_peerMsgs(tr_torrent* tor, peer_atom* atom_in)
        : tr_peer{ tor, atom_in }
        , have_{ tor->pieceCount() }
    {
    }

    virtual ~tr_peerMsgs() override = default;

    [[nodiscard]] virtual bool is_peer_choked() const noexcept = 0;
    [[nodiscard]] virtual bool is_peer_interested() const noexcept = 0;
    [[nodiscard]] virtual bool is_client_choked() const noexcept = 0;
    [[nodiscard]] virtual bool is_client_interested() const noexcept = 0;

    [[nodiscard]] virtual bool is_utp_connection() const noexcept = 0;
    [[nodiscard]] virtual bool is_encrypted() const = 0;
    [[nodiscard]] virtual bool is_incoming_connection() const = 0;

    [[nodiscard]] virtual bool is_active(tr_direction direction) const = 0;
    virtual void update_active(tr_direction direction) = 0;

    [[nodiscard]] virtual bool is_connection_older_than(time_t time) const noexcept = 0;

    [[nodiscard]] virtual std::pair<tr_address, tr_port> socketAddress() const = 0;

    virtual void cancel_block_request(tr_block_index_t block) = 0;

    virtual void set_choke(bool peer_is_choked) = 0;
    virtual void set_interested(bool client_is_interested) = 0;

    virtual void pulse() = 0;

    [[nodiscard]] virtual float percentDone() const noexcept = 0;
    [[nodiscard]] virtual bool isSeed() const noexcept = 0;
    virtual void onTorrentGotMetainfo() = 0;

    virtual void on_piece_completed(tr_piece_index_t) = 0;

    // The client name. This is the app name derived from the `v' string in LTEP's handshake dictionary
    tr_interned_string client;

protected:
    tr_bitfield have_;
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
