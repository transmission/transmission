/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2 (b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_H
#define TR_PEER_H

/**
 * @addtogroup peers Peers
 * @{
 */

/**
*** Fields common to webseed and bittorrent peers
**/

#include "transmission.h"
#include "bitfield.h"
#include "history.h"
#include "quark.h"

enum
{
  /* when we're making requests from another peer,
     batch them together to send enough requests to
     meet our bandwidth goals for the next N seconds */
  REQUEST_BUF_SECS = 10,

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

/**
 * State information about a connected peer.
 *
 * @see struct peer_atom
 * @see tr_peermsgs
 */
typedef struct tr_peer
{
  /* whether or not we should free this peer soon.
     NOTE: private to peer-mgr.c */
  bool doPurge;

  /* Whether or not we've choked this peer.
     Only applies to BitTorrent peers */
  bool peerIsChoked;

  /* whether or not the peer has indicated it will download from us.
     Only applies to BitTorrent peers */
  bool peerIsInterested;

  /* whether or the peer is choking us.
     Only applies to BitTorrent peers */
  bool clientIsChoked;

  /* whether or not we've indicated to the peer that we would download from them if unchoked.
     Only applies to BitTorrent peers */
  bool clientIsInterested;

  /* number of bad pieces they've contributed to */
  uint8_t strikes;

  uint8_t encryption_preference;

  /* how many requests the peer has made that we haven't responded to yet */
  int pendingReqsToClient;

  /* how many requests we've made and are currently awaiting a response for */
  int pendingReqsToPeer;

  struct tr_peerIo * io;
  struct peer_atom * atom;

  /** how complete the peer's copy of the torrent is. [0.0...1.0] */
  float progress;

  struct tr_bitfield blame;
  struct tr_bitfield have;

  /* the client name.
     For BitTorrent peers, this is the app name derived from the `v' string in LTEP's handshake dictionary */
  tr_quark client;

  time_t chokeChangedAt;

  tr_recentHistory blocksSentToClient;
  tr_recentHistory blocksSentToPeer;

  tr_recentHistory cancelsSentToClient;
  tr_recentHistory cancelsSentToPeer;

  struct tr_peermsgs * msgs;
}
tr_peer;

typedef void tr_peer_callback (struct tr_peer       * peer,
                               const tr_peer_event  * event,
                               void                 * client_data);

/** Update the tr_peer.progress field based on the 'have' bitset. */
void tr_peerUpdateProgress (tr_torrent * tor, struct tr_peer *);


#ifdef WIN32
 #define EMSGSIZE WSAEMSGSIZE
#endif

/** @} */

#endif
