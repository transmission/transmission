/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#ifndef TR_PEER_H
#define TR_PEER_H 1

typedef struct tr_peer_s tr_peer_t; 

void        tr_peerAddOld        ( tr_torrent_t *, char *, int );
void        tr_peerAddCompact    ( tr_torrent_t *, struct in_addr, in_port_t );
tr_peer_t * tr_peerInit          ( struct in_addr, in_port_t, int );
void        tr_peerAttach        ( tr_torrent_t *, tr_peer_t * );
void        tr_peerDestroy       ( tr_fd_t *, tr_peer_t * );
void        tr_peerRem           ( tr_torrent_t *, int );
int         tr_peerRead          ( tr_torrent_t *, tr_peer_t * );
uint64_t    tr_peerDate          ( tr_peer_t * );
uint8_t *   tr_peerId            ( tr_peer_t * );
uint8_t *   tr_peerHash          ( tr_peer_t * );
void        tr_peerPulse         ( tr_torrent_t * );
int         tr_peerIsConnected   ( tr_peer_t * );
int         tr_peerIsUploading   ( tr_peer_t * );
int         tr_peerIsDownloading ( tr_peer_t * );
uint8_t *   tr_peerBitfield      ( tr_peer_t * );
float       tr_peerDownloadRate  ( tr_peer_t * );
int         tr_peerIsUnchoked    ( tr_peer_t * );
int         tr_peerIsInterested  ( tr_peer_t * );
void        tr_peerChoke         ( tr_peer_t * );
void        tr_peerUnchoke       ( tr_peer_t * );
uint64_t    tr_peerLastChoke     ( tr_peer_t * );
void        tr_peerSetOptimistic ( tr_peer_t *, int );
int         tr_peerIsOptimistic  ( tr_peer_t * );
void        tr_peerBlame         ( tr_torrent_t *, tr_peer_t *,
                                   int piece, int success );
struct in_addr * tr_peerAddress  ( tr_peer_t * );

#endif
