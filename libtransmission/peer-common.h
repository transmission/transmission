/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_PEER_H
#define TR_PEER_H

/**
*** Fields common to webseed and bittorrent peers
**/

#include "transmission.h"
#include "publish.h"

typedef enum
{
    TR_ADDREQ_OK=0,
    TR_ADDREQ_FULL,
    TR_ADDREQ_DUPLICATE,
    TR_ADDREQ_MISSING,
    TR_ADDREQ_CLIENT_CHOKED
} tr_addreq_t;

/**
***  Peer Publish / Subscribe
**/

typedef enum
{
    TR_PEER_CLIENT_GOT_BLOCK,
    TR_PEER_CLIENT_GOT_DATA,
    TR_PEER_PEER_GOT_DATA,
    TR_PEER_PEER_PROGRESS,
    TR_PEER_ERROR,
    TR_PEER_CANCEL,
    TR_PEER_NEED_REQ
}
PeerEventType;

typedef struct
{
    PeerEventType eventType;
    uint32_t pieceIndex; /* for GOT_BLOCK, CANCEL */
    uint32_t offset;     /* for GOT_BLOCK */
    uint32_t length;     /* for GOT_BLOCK + GOT_DATA */
    float progress;      /* for TR_PEER_PEER_PROGRESS */
    tr_errno err;        /* for TR_PEER_GOT_ERROR */
}
tr_peer_event;

#endif
