/*
 * This file Copyright (C) 2008-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_H
#define TR_PEER_H

#include "transmission.h"
#include "bitfield.h"
#include "history.h"
#include "quark.h"

/**
 * @addtogroup peers Peers
 * @{
 */

struct tr_peer;
struct tr_swarm;

enum
{
  /* this is the maximum size of a block request.
     most bittorrent clients will reject requests
     larger than this size. */
  MAX_BLOCK_SIZE = (1024 * 16)
};

/**
***  Peer Publish / Subscribe
**/

typedef enum
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
}
PeerEventType;

typedef struct
{
  PeerEventType         eventType;

  uint32_t              pieceIndex;   /* for GOT_BLOCK, GOT_HAVE, CANCEL, ALLOWED, SUGGEST */
  struct tr_bitfield  * bitfield;     /* for GOT_BITFIELD */
  uint32_t              offset;       /* for GOT_BLOCK */
  uint32_t              length;       /* for GOT_BLOCK + GOT_PIECE_DATA */
  int                   err;          /* errno for GOT_ERROR */
  tr_port               port;         /* for GOT_PORT */
}
tr_peer_event;

extern const tr_peer_event TR_PEER_EVENT_INIT;

typedef void (*tr_peer_callback) (struct tr_peer       * peer,
                                  const tr_peer_event  * event,
                                  void                 * client_data);

/***
****
***/

typedef void (*tr_peer_destruct_func)(struct tr_peer * peer);
typedef bool (*tr_peer_is_transferring_pieces_func)(const struct tr_peer * peer,
                                                    uint64_t now,
                                                    tr_direction direction,
                                                    unsigned int * Bps);
struct tr_peer_virtual_funcs
{
  tr_peer_destruct_func destruct;
  tr_peer_is_transferring_pieces_func is_transferring_pieces;
};

/**
 * State information about a connected peer.
 *
 * @see struct peer_atom
 * @see tr_peerMsgs
 */
typedef struct tr_peer
{
  /* whether or not we should free this peer soon.
     NOTE: private to peer-mgr.c */
  bool doPurge;

  /* number of bad pieces they've contributed to */
  uint8_t strikes;

  /* how many requests the peer has made that we haven't responded to yet */
  int pendingReqsToClient;

  /* how many requests we've made and are currently awaiting a response for */
  int pendingReqsToPeer;

  /* Hook to private peer-mgr information */
  struct peer_atom * atom;

  struct tr_swarm * swarm;

  /** how complete the peer's copy of the torrent is. [0.0...1.0] */
  float progress;

  struct tr_bitfield blame;
  struct tr_bitfield have;

  /* the client name.
     For BitTorrent peers, this is the app name derived from the `v' string in LTEP's handshake dictionary */
  tr_quark client;

  tr_recentHistory blocksSentToClient;
  tr_recentHistory blocksSentToPeer;

  tr_recentHistory cancelsSentToClient;
  tr_recentHistory cancelsSentToPeer;

  const struct tr_peer_virtual_funcs * funcs;
}
tr_peer;


void tr_peerConstruct (struct tr_peer * peer, const tr_torrent * tor);

void tr_peerDestruct  (struct tr_peer * peer);


/** Update the tr_peer.progress field based on the 'have' bitset. */
void tr_peerUpdateProgress (tr_torrent * tor, struct tr_peer *);

bool tr_peerIsSeed (const struct tr_peer * peer);

/***
****
***/

typedef struct tr_swarm_stats
{
  int activePeerCount[2];
  int activeWebseedCount;
  int peerCount;
  int peerFromCount[TR_PEER_FROM__MAX];
}
tr_swarm_stats;

extern const tr_swarm_stats TR_SWARM_STATS_INIT;

void tr_swarmGetStats (const struct tr_swarm * swarm, tr_swarm_stats * setme);

void tr_swarmIncrementActivePeers (struct tr_swarm * swarm, tr_direction direction, bool is_active);


/***
****
***/


#ifdef _WIN32
 #undef  EMSGSIZE
 #define EMSGSIZE WSAEMSGSIZE
#endif

/** @} */

#endif
