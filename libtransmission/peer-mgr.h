// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstddef> // size_t
#include <cstdint> // uint8_t, uint64_t
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h> /* struct in_addr */
#endif

#include "net.h" /* tr_address */
#include "peer-common.h"
#include "peer-socket.h"

/**
 * @addtogroup peers Peers
 * @{
 */

class tr_peerIo;
class tr_peerMsgs;
class tr_swarm;
struct UTPSocket;
struct peer_atom;
struct tr_peerMgr;
struct tr_peer_stat;
struct tr_torrent;

/* added_f's bitwise-or'ed flags */
enum
{
    /* true if the peer supports encryption */
    ADDED_F_ENCRYPTION_FLAG = 1,
    /* true if the peer is a seed or partial seed */
    ADDED_F_SEED_FLAG = 2,
    /* true if the peer supports µTP */
    ADDED_F_UTP_FLAGS = 4,
    /* true if the peer has holepunch support */
    ADDED_F_HOLEPUNCH = 8,
    /* true if the peer telling us about this peer
     * initiated the connection (implying that it is connectible) */
    ADDED_F_CONNECTABLE = 16
};

struct tr_pex
{
    tr_pex() = default;

    tr_pex(tr_address addr_in, tr_port port_in, uint8_t flags_in = {})
        : addr{ addr_in }
        , port{ port_in }
        , flags{ flags_in }
    {
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv4(OutputIt out) const
    {
        return this->addr.to_compact_ipv4(out, this->port);
    }

    template<typename OutputIt>
    OutputIt to_compact_ipv6(OutputIt out) const
    {
        return this->addr.to_compact_ipv6(out, this->port);
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv4(OutputIt out, tr_pex const* pex, size_t n_pex)
    {
        for (size_t i = 0; i < n_pex; ++i)
        {
            out = pex[i].to_compact_ipv4(out);
        }
        return out;
    }

    template<typename OutputIt>
    static OutputIt to_compact_ipv6(OutputIt out, tr_pex const* pex, size_t n_pex)
    {
        for (size_t i = 0; i < n_pex; ++i)
        {
            out = pex[i].to_compact_ipv6(out);
        }
        return out;
    }

    [[nodiscard]] static std::vector<tr_pex> from_compact_ipv4(
        void const* compact,
        size_t compact_len,
        uint8_t const* added_f,
        size_t added_f_len);

    [[nodiscard]] static std::vector<tr_pex> from_compact_ipv6(
        void const* compact,
        size_t compact_len,
        uint8_t const* added_f,
        size_t added_f_len);

    template<typename OutputIt>
    [[nodiscard]] OutputIt display_name(OutputIt out) const
    {
        return addr.display_name(out, port);
    }

    [[nodiscard]] std::string display_name() const
    {
        return addr.display_name(port);
    }

    [[nodiscard]] int compare(tr_pex const& that) const noexcept // <=>
    {
        if (auto const i = addr.compare(that.addr); i != 0)
        {
            return i;
        }

        if (port != that.port)
        {
            return port < that.port ? -1 : 1;
        }

        return 0;
    }

    [[nodiscard]] bool operator==(tr_pex const& that) const noexcept
    {
        return compare(that) == 0;
    }

    [[nodiscard]] bool operator<(tr_pex const& that) const noexcept
    {
        return compare(that) < 0;
    }

    [[nodiscard]] bool is_valid_for_peers() const noexcept
    {
        return addr.is_valid_for_peers(port);
    }

    tr_address addr = {};
    tr_port port = {}; /* this field is in network byte order */
    uint8_t flags = 0;
};

constexpr bool tr_isPex(tr_pex const* pex)
{
    return pex != nullptr && pex->addr.is_valid();
}

[[nodiscard]] tr_peerMgr* tr_peerMgrNew(tr_session* session);

void tr_peerMgrFree(tr_peerMgr* manager);

void tr_peerMgrSetUtpSupported(tr_torrent* tor, tr_address const& addr);

void tr_peerMgrSetUtpFailed(tr_torrent* tor, tr_address const& addr, bool failed);

[[nodiscard]] std::vector<tr_block_span_t> tr_peerMgrGetNextRequests(tr_torrent* torrent, tr_peer const* peer, size_t numwant);

[[nodiscard]] bool tr_peerMgrDidPeerRequest(tr_torrent const* torrent, tr_peer const* peer, tr_block_index_t block);

void tr_peerMgrClientSentRequests(tr_torrent* torrent, tr_peer* peer, tr_block_span_t span);

[[nodiscard]] size_t tr_peerMgrCountActiveRequestsToPeer(tr_torrent const* torrent, tr_peer const* peer);

void tr_peerMgrAddIncoming(tr_peerMgr* manager, tr_peer_socket&& socket);

size_t tr_peerMgrAddPex(tr_torrent* tor, uint8_t from, tr_pex const* pex, size_t n_pex);

void tr_peerMgrSetSwarmIsAllSeeds(tr_torrent* tor);

enum
{
    TR_PEERS_CONNECTED,
    TR_PEERS_INTERESTING
};

[[nodiscard]] std::vector<tr_pex> tr_peerMgrGetPeers(
    tr_torrent const* tor,
    uint8_t address_type,
    uint8_t peer_list_mode,
    size_t max_peer_count);

void tr_peerMgrStartTorrent(tr_torrent* tor);

void tr_peerMgrStopTorrent(tr_torrent* tor);

void tr_peerMgrAddTorrent(tr_peerMgr* manager, struct tr_torrent* tor);

void tr_peerMgrRemoveTorrent(tr_torrent* tor);

// return the number of connected peers that have `piece`, or -1 if we already have it
[[nodiscard]] int8_t tr_peerMgrPieceAvailability(tr_torrent const* tor, tr_piece_index_t piece);

void tr_peerMgrTorrentAvailability(tr_torrent const* tor, int8_t* tab, unsigned int n_tabs);

[[nodiscard]] uint64_t tr_peerMgrGetDesiredAvailable(tr_torrent const* tor);

void tr_peerMgrOnTorrentGotMetainfo(tr_torrent* tor);

void tr_peerMgrOnBlocklistChanged(tr_peerMgr* mgr);

[[nodiscard]] struct tr_peer_stat* tr_peerMgrPeerStats(tr_torrent const* tor, size_t* setme_count);

[[nodiscard]] tr_webseed_view tr_peerMgrWebseed(tr_torrent const* tor, size_t i);

void tr_peerMgrClearInterest(tr_torrent* tor);

void tr_peerMgrGotBadPiece(tr_torrent* tor, tr_piece_index_t piece_index);

void tr_peerMgrPieceCompleted(tr_torrent* tor, tr_piece_index_t pieceIndex);

/* @} */
