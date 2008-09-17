/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include "peer-common.h"
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

void         tr_peerMsgsPulse( tr_peermsgs * msgs );

#if 0
void         tr_peerMsgsCancel( tr_peermsgs * msgs,
                                uint32_t      pieceIndex,
                                uint32_t      offset,
                                uint32_t      length );
#endif

void         tr_peerMsgsFree( tr_peermsgs* );

tr_addreq_t  tr_peerMsgsAddRequest( tr_peermsgs      * peer,
                                    tr_piece_index_t   piece );

void         tr_peerMsgsUnsubscribe ( tr_peermsgs       * peer,
                                      tr_publisher_tag    tag );

#endif
