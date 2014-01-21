/*
 * This file Copyright (C) 2010-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_CACHE_H
#define TR_CACHE_H

struct evbuffer;

typedef struct tr_cache tr_cache;

/***
****
***/

tr_cache * tr_cacheNew (int64_t max_bytes);

void tr_cacheFree (tr_cache *);

/***
****
***/

int tr_cacheSetLimit (tr_cache * cache, int64_t max_bytes);

int64_t tr_cacheGetLimit (const tr_cache *);

int tr_cacheWriteBlock (tr_cache         * cache,
                        tr_torrent       * torrent,
                        tr_piece_index_t   piece,
                        uint32_t           offset,
                        uint32_t           len,
                        struct evbuffer  * writeme);

int tr_cacheReadBlock (tr_cache         * cache,
                       tr_torrent       * torrent,
                       tr_piece_index_t   piece,
                       uint32_t           offset,
                       uint32_t           len,
                       uint8_t          * setme);

int tr_cachePrefetchBlock (tr_cache         * cache,
                           tr_torrent       * torrent,
                           tr_piece_index_t   piece,
                           uint32_t           offset,
                           uint32_t           len);

/***
****
***/

int tr_cacheFlushDone (tr_cache * cache);

int tr_cacheFlushTorrent (tr_cache    * cache,
                          tr_torrent  * torrent);

int tr_cacheFlushFile (tr_cache         * cache,
                       tr_torrent       * torrent,
                       tr_file_index_t    file);

#endif
