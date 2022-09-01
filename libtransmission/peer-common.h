// This file Copyright © 2008-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
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
#include "block-info.h"
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

class tr_peer_event
{
public:
    PeerEventType eventType;

    uint32_t pieceIndex = 0; /* for GOT_BLOCK, GOT_HAVE, CANCEL, ALLOWED, SUGGEST */
    tr_bitfield* bitfield = nullptr; /* for GOT_BITFIELD */
    uint32_t offset = 0; /* for GOT_BLOCK */
    uint32_t length = 0; /* for GOT_BLOCK + GOT_PIECE_DATA */
    int err = 0; /* errno for GOT_ERROR */
    tr_port port = {}; /* for GOT_PORT */

    [[nodiscard]] constexpr static tr_peer_event GotBlock(tr_block_info const& block_info, tr_block_index_t block) noexcept
    {
        auto const loc = block_info.blockLoc(block);
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_BLOCK;
        event.pieceIndex = loc.piece;
        event.offset = loc.piece_offset;
        event.length = block_info.blockSize(block);
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotPieceData(uint32_t length) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_PEER_GOT_PIECE_DATA;
        event.length = length;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotError(int err) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_ERROR;
        event.err = err;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotRejected(tr_block_info const& block_info, tr_block_index_t block) noexcept
    {
        auto const loc = block_info.blockLoc(block);
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_REJ;
        event.pieceIndex = loc.piece;
        event.offset = loc.piece_offset;
        event.length = block_info.blockSize(block);
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotChoke() noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_CHOKE;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotHaveAll() noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_HAVE_ALL;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotHaveNone() noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_HAVE_NONE;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotHave(tr_piece_index_t piece) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_HAVE;
        event.pieceIndex = piece;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotPort(tr_port port) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_PORT;
        event.port = port;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotBitfield(tr_bitfield* bitfield) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_BITFIELD;
        event.bitfield = bitfield;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotAllowedFast(tr_piece_index_t piece) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_ALLOWED_FAST;
        event.pieceIndex = piece;
        return event;
    }

    [[nodiscard]] constexpr static tr_peer_event GotSuggest(tr_piece_index_t piece) noexcept
    {
        auto event = tr_peer_event{};
        event.eventType = TR_PEER_CLIENT_GOT_SUGGEST;
        event.pieceIndex = piece;
        return event;
    }
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
