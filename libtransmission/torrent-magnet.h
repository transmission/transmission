/*
 * This file Copyright (C) 2012-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_TORRENT_MAGNET_H
#define TR_TORRENT_MAGNET_H 1

#include <inttypes.h>
#include <time.h>

enum
{
    /* defined by BEP #9 */
    METADATA_PIECE_SIZE = (1024 * 16)
};

void* tr_torrentGetMetadataPiece (tr_torrent * tor, int piece, size_t * len);

void tr_torrentSetMetadataPiece (tr_torrent * tor, int piece, const void * data, int len);

bool tr_torrentGetNextMetadataRequest (tr_torrent * tor, time_t now, int * setme);

bool tr_torrentSetMetadataSizeHint (tr_torrent * tor, int64_t metadata_size);

double tr_torrentGetMetadataPercent (const tr_torrent * tor);

#endif
