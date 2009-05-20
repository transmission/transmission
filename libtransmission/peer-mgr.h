/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_MGR_H
#define TR_PEER_MGR_H

#include <inttypes.h> /* uint16_t */

#ifdef WIN32
 #include <winsock2.h> /* struct in_addr */
#endif

#include "net.h"
#include "publish.h" /* tr_publisher_tag */

struct tr_peer_stat;
struct tr_torrent;
typedef struct tr_peerMgr tr_peerMgr;

enum
{
    /* corresponds to ut_pex's added.f flags */
    ADDED_F_ENCRYPTION_FLAG = 1,

    /* corresponds to ut_pex's added.f flags */
    ADDED_F_SEED_FLAG = 2,
};

typedef struct tr_pex
{
    tr_address addr;
    tr_port    port;
    uint8_t    flags;
}
tr_pex;


struct tr_bandwidth;
struct tr_bitfield;
struct tr_peerIo;
struct tr_peermsgs;

enum
{
    ENCRYPTION_PREFERENCE_UNKNOWN,
    ENCRYPTION_PREFERENCE_YES,
    ENCRYPTION_PREFERENCE_NO
};

/* opaque forward declaration */
struct peer_atom;

/**
 * State information about a connected peer.
 *
 * @see struct peer_atom
 * @see tr_peermsgs
 */
typedef struct tr_peer
{
    tr_bool                  peerIsChoked;
    tr_bool                  peerIsInterested;
    tr_bool                  clientIsChoked;
    tr_bool                  clientIsInterested;
    tr_bool                  doPurge;

    /* number of bad pieces they've contributed to */
    uint8_t                  strikes;

    uint8_t                  encryption_preference;
    tr_port                  port;
    tr_port                  dht_port;
    tr_address               addr;
    struct tr_peerIo       * io;
    struct peer_atom       * atom;

    struct tr_bitfield     * blame;
    struct tr_bitfield     * have;

    /** how complete the peer's copy of the torrent is. [0.0...1.0] */
    float                    progress;

    /* the client name from the `v' string in LTEP's handshake dictionary */
    char                   * client;

    time_t                   chokeChangedAt;

    struct tr_peermsgs     * msgs;
    tr_publisher_tag         msgsTag;
}
tr_peer;


int tr_pexCompare( const void * a, const void * b );

tr_peerMgr* tr_peerMgrNew( tr_session * );

void tr_peerMgrFree( tr_peerMgr * manager );

tr_bool tr_peerMgrPeerIsSeed( const tr_torrent * tor,
                              const tr_address * addr );

void tr_peerMgrAddIncoming( tr_peerMgr  * manager,
                            tr_address  * addr,
                            tr_port       port,
                            int           socket );

tr_pex * tr_peerMgrCompactToPex( const void    * compact,
                                 size_t          compactLen,
                                 const uint8_t * added_f,
                                 size_t          added_f_len,
                                 size_t        * setme_pex_count );

tr_pex * tr_peerMgrCompact6ToPex( const void    * compact,
                                  size_t          compactLen,
                                  const uint8_t * added_f,
                                  size_t          added_f_len,
                                  size_t        * pexCount );

tr_pex * tr_peerMgrArrayToPex( const void * array,
                               size_t       arrayLen,
                               size_t      * setme_pex_count );

void tr_peerMgrAddPex( tr_torrent     * tor,
                       uint8_t          from,
                       const tr_pex   * pex );

void tr_peerMgrSetBlame( tr_torrent        * tor,
                         tr_piece_index_t    pieceIndex,
                         int                 success );

int  tr_peerMgrGetPeers( tr_torrent      * tor,
                         tr_pex         ** setme_pex,
                         uint8_t           af);

void tr_peerMgrStartTorrent( tr_torrent * tor );

void tr_peerMgrStopTorrent( tr_torrent * tor );

void tr_peerMgrAddTorrent( tr_peerMgr         * manager,
                           struct tr_torrent  * tor );

void tr_peerMgrRemoveTorrent( tr_torrent * tor );

void tr_peerMgrTorrentAvailability( const tr_torrent * tor,
                                    int8_t           * tab,
                                    unsigned int       tabCount );

struct tr_bitfield* tr_peerMgrGetAvailable( const tr_torrent * tor );

void tr_peerMgrTorrentStats( tr_torrent * tor,
                             int * setmePeersKnown,
                             int * setmePeersConnected,
                             int * setmeSeedsConnected,
                             int * setmeWebseedsSendingToUs,
                             int * setmePeersSendingToUs,
                             int * setmePeersGettingFromUs,
                             int * setmePeersFrom ); /* TR_PEER_FROM__MAX */

struct tr_peer_stat* tr_peerMgrPeerStats( const tr_torrent * tor,
                                          int              * setmeCount );

float* tr_peerMgrWebSpeeds( const tr_torrent * tor );


double tr_peerGetPieceSpeed( const tr_peer    * peer,
                             uint64_t           now,
                             tr_direction       direction );

#endif
