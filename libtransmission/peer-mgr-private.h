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

#ifndef TR_PEER_MGR_PRIVATE_H
#define TR_PEER_MGR_PRIVATE_H

#include <inttypes.h> /* uint16_t */
#include <arpa/inet.h> /* struct in_addr */
#include "publish.h" /* tr_publisher_tag */

struct tr_bitfield;
struct tr_peerIo;
struct tr_peermsgs;

enum
{
    ENCRYPTION_PREFERENCE_UNKNOWN,
    ENCRYPTION_PREFERENCE_YES,
    ENCRYPTION_PREFERENCE_NO
};

typedef struct tr_peer
{
    unsigned int  peerIsChoked : 1;
    unsigned int  peerIsInterested : 1;
    unsigned int  clientIsChoked : 1;
    unsigned int  clientIsInterested : 1;
    unsigned int  doPurge : 1;

    uint8_t encryption_preference;
    uint16_t port;
    struct in_addr in_addr;
    struct tr_peerIo * io;

    struct tr_bitfield * banned;
    struct tr_bitfield * blame;
    struct tr_bitfield * have;
    float progress;

    /* the client name from the `v' string in LTEP's handshake dictionary */
    char * client;

    time_t clientSentPieceDataAt;
    time_t peerSentPieceDataAt;
    time_t chokeChangedAt;

    struct tr_peermsgs * msgs;
    tr_publisher_tag msgsTag;

    struct tr_ratecontrol * rateToClient;
    struct tr_ratecontrol * rateToPeer;
}
tr_peer;

#endif
