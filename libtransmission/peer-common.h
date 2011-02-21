/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
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

struct tr_bitfield;

enum
{
    /** when we're making requests from another peer,
        batch them together to send enough requests to
        meet our bandwidth goals for the next N seconds */
    REQUEST_BUF_SECS = 10,

    /** this is the maximum size of a block request.
        most bittorrent clients will reject requests
        larger than this size. */
    MAX_BLOCK_SIZE = ( 1024 * 16 )
};

/**
***  Peer Publish / Subscribe
**/

typedef enum
{
    TR_PEER_CLIENT_GOT_BLOCK,
    TR_PEER_CLIENT_GOT_CHOKE,
    TR_PEER_CLIENT_GOT_DATA,
    TR_PEER_CLIENT_GOT_ALLOWED_FAST,
    TR_PEER_CLIENT_GOT_SUGGEST,
    TR_PEER_CLIENT_GOT_PORT,
    TR_PEER_CLIENT_GOT_REJ,
    TR_PEER_CLIENT_GOT_BITFIELD,
    TR_PEER_CLIENT_GOT_HAVE,
    TR_PEER_CLIENT_GOT_HAVE_ALL,
    TR_PEER_CLIENT_GOT_HAVE_NONE,
    TR_PEER_PEER_GOT_DATA,
    TR_PEER_PEER_PROGRESS,
    TR_PEER_ERROR
}
PeerEventType;

typedef struct
{
    PeerEventType         eventType;

    uint32_t              pieceIndex;   /* for GOT_BLOCK, GOT_HAVE, CANCEL, ALLOWED, SUGGEST */
    struct tr_bitfield  * bitfield;     /* for GOT_BITFIELD */
    uint32_t              offset;       /* for GOT_BLOCK */
    uint32_t              length;       /* for GOT_BLOCK + GOT_DATA */
    float                 progress;     /* for PEER_PROGRESS */
    int                   err;          /* errno for GOT_ERROR */
    tr_bool               wasPieceData; /* for GOT_DATA */
    tr_port               port;         /* for GOT_PORT */
}
tr_peer_event;

extern const tr_peer_event TR_PEER_EVENT_INIT;

struct tr_peer;

typedef void tr_peer_callback( struct tr_peer       * peer,
                               const tr_peer_event  * event,
                               void                 * client_data );

#ifdef WIN32
 #define EMSGSIZE WSAEMSGSIZE
#endif

/** @} */

#endif
