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

#ifndef TR_PEER_MGR_PRIVATE_H
#define TR_PEER_MGR_PRIVATE_H

#include <inttypes.h> /* uint16_t */

#ifdef WIN32
#include <winsock2.h> /* struct in_addr */
#else
#include <netinet/in.h> /* struct in_addr */
#endif

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

/**
*** The "SWIFT" system is described by Karthik Tamilmani,
*** Vinay Pai, and Alexander Mohr of Stony Brook University
*** in their paper "SWIFT: A System With Incentives For Trading"
*** http://citeseer.ist.psu.edu/tamilmani04swift.html
***
*** More SWIFT constants are defined in peer-mgr.c
**/

/**
 * Use SWIFT?
 */
static const int SWIFT_ENABLED = 1;

/**
 * For every byte the peer uploads to us,
 * allow them to download this many bytes from us
 */
static const double SWIFT_REPAYMENT_RATIO = 1.33;


typedef struct tr_peer
{
    unsigned int  peerIsChoked : 1;
    unsigned int  peerIsInterested : 1;
    unsigned int  clientIsChoked : 1;
    unsigned int  clientIsInterested : 1;
    unsigned int  doPurge : 1;

    tr_peer_status status;

    /* number of bad pieces they've contributed to */
    uint8_t strikes;

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

    time_t peerSentPieceDataAt;
    time_t chokeChangedAt;
    time_t pieceDataActivityDate;

    struct tr_peermsgs * msgs;
    tr_publisher_tag msgsTag;

    struct tr_ratecontrol * rcToClient;
    struct tr_ratecontrol * rcToPeer;

    double rateToClient;
    double rateToPeer;

    int64_t credit;
}
tr_peer;

#endif
