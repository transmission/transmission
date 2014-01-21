/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_PEER_MSGS_H
#define TR_PEER_MSGS_H

#include <inttypes.h>
#include "peer-common.h"

struct tr_address;
struct tr_bitfield;
struct tr_peer;
struct tr_peerIo;
struct tr_torrent;

/**
 * @addtogroup peers Peers
 * @{
 */

typedef struct tr_peerMsgs tr_peerMsgs;

#define PEER_MSGS(o) (tr_peerMsgsCast(o))

bool         tr_isPeerMsgs                   (const void               * msgs);

tr_peerMsgs* tr_peerMsgsCast                 (void                     * msgs);

tr_peerMsgs* tr_peerMsgsNew                  (struct tr_torrent        * torrent,
                                              struct tr_peerIo         * io,
                                              tr_peer_callback           callback,
                                              void                     * callback_data);

bool         tr_peerMsgsIsPeerChoked         (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsPeerInterested     (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsClientChoked       (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsClientInterested   (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsActive             (const tr_peerMsgs        * msgs,
                                              tr_direction               direction);

void         tr_peerMsgsUpdateActive         (tr_peerMsgs              * msgs,
                                              tr_direction               direction);

time_t       tr_peerMsgsGetConnectionAge     (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsUtpConnection      (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsEncrypted          (const tr_peerMsgs        * msgs);

bool         tr_peerMsgsIsIncomingConnection (const tr_peerMsgs        * msgs);

void         tr_peerMsgsSetChoke             (tr_peerMsgs              * msgs,
                                              bool                       peerIsChoked);

int          tr_peerMsgsIsReadingBlock       (const tr_peerMsgs        * msgs,
                                              tr_block_index_t           block);

void         tr_peerMsgsSetInterested        (tr_peerMsgs              * msgs,
                                              bool                       clientIsInterested);

void         tr_peerMsgsHave                 (tr_peerMsgs              * msgs,
                                              uint32_t                   pieceIndex);

void         tr_peerMsgsPulse                (tr_peerMsgs              * msgs);

void         tr_peerMsgsCancel               (tr_peerMsgs              * msgs,
                                              tr_block_index_t           block);

size_t       tr_generateAllowedSet           (tr_piece_index_t         * setmePieces,
                                              size_t                     desiredSetSize,
                                              size_t                     pieceCount,
                                              const uint8_t            * infohash,
                                              const struct tr_address  * addr);


/* @} */
#endif
