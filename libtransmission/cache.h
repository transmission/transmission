// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // intX_t, uintX_t

#include "transmission.h"

#include "block-info.h"

struct evbuffer;
struct tr_cache;
struct tr_torrent;

/***
****
***/

tr_cache* tr_cacheNew(int64_t max_bytes);

void tr_cacheFree(tr_cache*);

/***
****
***/

int tr_cacheSetLimit(tr_cache* cache, int64_t max_bytes);

int64_t tr_cacheGetLimit(tr_cache const*);

int tr_cacheWriteBlock(
    tr_cache* cache,
    tr_torrent* torrent,
    tr_block_info::Location loc,
    uint32_t len,
    struct evbuffer* writeme);

int tr_cacheReadBlock(tr_cache* cache, tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme);

int tr_cachePrefetchBlock(tr_cache* cache, tr_torrent* torrent, tr_block_info::Location loc, uint32_t len);

/***
****
***/

int tr_cacheFlushDone(tr_cache* cache);

int tr_cacheFlushTorrent(tr_cache* cache, tr_torrent* torrent);

int tr_cacheFlushFile(tr_cache* cache, tr_torrent* torrent, tr_file_index_t file);
