/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef TR_PEER_MSGS_H
#define TR_PEER_MSGS_H

#include <inttypes.h>
#include "publish.h"

struct tr_torrent;
struct tr_peer;
struct tr_bitfield;

typedef struct tr_peermsgs tr_peermsgs;

tr_peermsgs* tr_peerMsgsNew( struct tr_torrent  * torrent,
                             struct tr_peer     * peer,
                             tr_delivery_func     func,
                             void               * user,
                             tr_publisher_tag   * setme );


void         tr_peerMsgsSetChoke( tr_peermsgs *, int doChoke );

void         tr_peerMsgsHave( tr_peermsgs * msgs,
                              uint32_t      pieceIndex );

void         tr_peerMsgsCancel( tr_peermsgs * msgs,
                                uint32_t      pieceIndex,
                                uint32_t      offset,
                                uint32_t      length );

void         tr_peerMsgsFree( tr_peermsgs* );


enum {
    TR_ADDREQ_OK=0,
    TR_ADDREQ_FULL,
    TR_ADDREQ_DUPLICATE,
    TR_ADDREQ_MISSING,
    TR_ADDREQ_CLIENT_CHOKED
};

int          tr_peerMsgsAddRequest( tr_peermsgs * peer,
                                    uint32_t      index,
                                    uint32_t      begin,
                                    uint32_t      length );

/**
***  PeerMsgs Publish / Subscribe
**/

typedef enum
{
    TR_PEERMSG_CLIENT_HAVE,
    TR_PEERMSG_CLIENT_BLOCK,
    TR_PEERMSG_PEER_PROGRESS,
    TR_PEERMSG_GOT_ERROR,
    TR_PEERMSG_NEED_REQ
}
PeerMsgsEventType;

typedef struct
{
    PeerMsgsEventType eventType;
    uint32_t pieceIndex; /* for TR_PEERMSG_GOT_BLOCK, TR_PEERMSG_GOT_HAVE */
    uint32_t offset;     /* for TR_PEERMSG_GOT_BLOCK */
    uint32_t length;     /* for TR_PEERMSG_GOT_BLOCK */
    float progress;      /* for TR_PEERMSG_PEER_PROGRESS */
}
tr_peermsgs_event;

tr_publisher_tag  tr_peerMsgsSubscribe   ( tr_peermsgs       * peer,
                                           tr_delivery_func    func,
                                           void              * user );

void              tr_peerMsgsUnsubscribe ( tr_peermsgs       * peer,
                                           tr_publisher_tag    tag );



#endif
