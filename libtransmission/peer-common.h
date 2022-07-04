// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstdint> // uint8_t, uint32_t, uint64_t

#include "transmission.h"

#include "bitfield.h"
#include "history.h"
#include "interned-string.h"
#include "net.h" // tr_port

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peer;
class tr_swarm;
struct peer_atom;
struct tr_bandwidth;

/**
***  Peer Publish / Subscribe
**/

enum PeerEventType
{
    TR_PEER_CLIENT_GOT_BLOCK,
    TR_PEER_CLIENT_GOT_CHOKE,
    TR_PEER_CLIENT_GOT_PIECE_DATA,
    TR_PEER_CLIENT_GOT_ALLOWED_FAST,
    TR_PEER_CLIENT_GOT_SUGGEST,
    TR_PEER_CLIENT_GOT_PORT,
    TR_PEER_CLIENT_GOT_REJ,
    TR_PEER_CLIENT_GOT_BITFIELD,
    TR_PEER_CLIENT_GOT_HAVE,
    TR_PEER_CLIENT_GOT_HAVE_ALL,
    TR_PEER_CLIENT_GOT_HAVE_NONE,
    TR_PEER_PEER_GOT_PIECE_DATA,
    TR_PEER_ERROR
};

struct tr_peer_event
{
    PeerEventType eventType;

    uint32_t pieceIndex; /* for GOT_BLOCK, GOT_HAVE, CANCEL, ALLOWED, SUGGEST */
    tr_bitfield* bitfield; /* for GOT_BITFIELD */
    uint32_t offset; /* for GOT_BLOCK */
    uint32_t length; /* for GOT_BLOCK + GOT_PIECE_DATA */
    int err; /* errno for GOT_ERROR */
    tr_port port; /* for GOT_PORT */
};

using tr_peer_callback = void (*)(tr_peer* peer, tr_peer_event const* event, void* client_data);

/**
 * State information about a connected peer.
 *
 * @see struct peer_atom
 * @see tr_peerMsgs
 */
class tr_peer
{
public:
    tr_peer(tr_torrent const* tor, peer_atom* atom = nullptr);
    virtual ~tr_peer();

    virtual bool isTransferringPieces(uint64_t now, tr_direction direction, unsigned int* setme_Bps) const = 0;

    [[nodiscard]] virtual std::string readable() const = 0;

    [[nodiscard]] virtual bool hasPiece(tr_piece_index_t piece) const noexcept = 0;

    [[nodiscard]] virtual tr_bandwidth& bandwidth() noexcept = 0;

    // requests that have been made but haven't been fulfilled yet
    [[nodiscard]] virtual size_t activeReqCount(tr_direction) const noexcept = 0;

    virtual void requestBlocks(tr_block_span_t const* block_spans, size_t n_spans) = 0;

    struct RequestLimit
    {
        // How many blocks we could request.
        size_t max_spans = 0;

        // How many spans those blocks could be in.
        // This is for webseeds, which make parallel requests.
        size_t max_blocks = 0;
    };

    // how many blocks could we request from this peer right now?
    [[nodiscard]] virtual RequestLimit canRequest() const noexcept = 0;

    tr_session* const session;

    tr_swarm* const swarm;

    tr_recentHistory<uint16_t> blocks_sent_to_peer;

    tr_recentHistory<uint16_t> cancels_sent_to_client;

    /// The following fields are only to be used in peer-mgr.cc.
    /// TODO(ckerr): refactor them out of tr_peer

    // hook to private peer-mgr information
    peer_atom* const atom;

    // whether or not this peer sent us any given block
    tr_bitfield blame;

    // whether or not we should free this peer soon.
    bool do_purge = false;

    // how many bad pieces this piece has contributed to
    uint8_t strikes = 0;

    // how many blocks this peer has sent us
    tr_recentHistory<uint16_t> blocks_sent_to_client;

    // how many requests we made to this peer and then canceled
    tr_recentHistory<uint16_t> cancels_sent_to_peer;
};

/***
****
***/

struct tr_swarm_stats
{
    std::array<uint16_t, 2> active_peer_count;
    uint16_t active_webseed_count;
    uint16_t peer_count;
    std::array<uint16_t, TR_PEER_FROM__MAX> peer_from_count;
};

tr_swarm_stats tr_swarmGetStats(tr_swarm const* swarm);

void tr_swarmIncrementActivePeers(tr_swarm* swarm, tr_direction direction, bool is_active);

/***
****
***/

#ifdef _WIN32
#undef EMSGSIZE
#define EMSGSIZE WSAEMSGSIZE
#endif

/** @} */
