// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

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

    virtual bool is_transferring_pieces(uint64_t now, tr_direction direction, unsigned int* setme_Bps) const = 0;

    /* whether or not we should free this peer soon.
       NOTE: private to peer-mgr.c */
    bool doPurge = false;

    /* number of bad pieces they've contributed to */
    uint8_t strikes = 0;

    tr_session* const session;

    /* Hook to private peer-mgr information */
    peer_atom* const atom;

    tr_swarm* const swarm;

    /** how complete the peer's copy of the torrent is. [0.0...1.0] */
    float progress = 0.0f;

    tr_bitfield blame;
    tr_bitfield have;

    /* the client name.
       For BitTorrent peers, this is the app name derived from the `v' string in LTEP's handshake dictionary */
    tr_interned_string client;

    tr_recentHistory<uint16_t> blocksSentToClient;
    tr_recentHistory<uint16_t> blocksSentToPeer;

    tr_recentHistory<uint16_t> cancelsSentToClient;
    tr_recentHistory<uint16_t> cancelsSentToPeer;
};

/** Update the tr_peer.progress field based on the 'have' bitset. */
void tr_peerUpdateProgress(tr_torrent* tor, tr_peer*);

bool tr_peerIsSeed(tr_peer const* peer);

/***
****
***/

struct tr_swarm_stats
{
    uint16_t active_peer_count[2];
    uint16_t active_webseed_count;
    uint16_t peer_count;
    uint16_t peer_from_count[TR_PEER_FROM__MAX];
};

void tr_swarmGetStats(tr_swarm const* swarm, tr_swarm_stats* setme);

void tr_swarmIncrementActivePeers(tr_swarm* swarm, tr_direction direction, bool is_active);

/***
****
***/

#ifdef _WIN32
#undef EMSGSIZE
#define EMSGSIZE WSAEMSGSIZE
#endif

/** @} */
