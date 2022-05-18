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
#include "ptrarray.h"

struct evbuffer;
struct tr_torrent;

class tr_cache
{
public: // TODO: private more of these
    tr_ptrArray blocks; // TODO: std::vector
    int max_blocks = 0;
    size_t max_bytes = 0;

    size_t disk_writes = 0;
    size_t disk_write_bytes = 0;
    size_t cache_writes = 0;
    size_t cache_write_bytes = 0;

    explicit tr_cache(int64_t max_bytes);
    ~tr_cache();

    int setLimit(int64_t new_limit);

    [[nodiscard]] size_t getLimit() const
    {
        return max_bytes;
    }

    int writeBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t length, struct evbuffer* writeme);
    int readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme);
    int prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len);
    int flushDone();
    int flushTorrent(tr_torrent* torrent);
    int flushFile(tr_torrent* torrent, tr_file_index_t file);

private:
    struct cache_block
    {
        tr_torrent* tor = nullptr;

        tr_block_info::Location loc;
        uint32_t length = 0;

        time_t time = 0;

        struct evbuffer* evbuf = nullptr;
    };

    struct run_info
    {
        int pos = 0;
        int rank = 0;
        time_t last_block_time = 0;
        bool is_multi_piece = false;
        bool is_piece_done = false;
        unsigned int len = 0;
    };

    enum
    {
        MULTIFLAG = 0x1000,
        DONEFLAG = 0x2000,
        SESSIONFLAG = 0x4000,
    };

    // TODO: size_t return
    int getBlockRun(int pos, run_info* info) const;
    static int compareRuns(void const* va, void const* vb);
    int calcRuns(run_info* runs) const;
    int flushContiguous(int pos, int n);
    int flushRuns(struct run_info* runs, int n);
    int cacheTrim();
    static int getMaxBlocks(int64_t max_bytes);
    static int cache_block_compare(void const* va, void const* vb);
    cache_block* findBlock(tr_torrent* torrent, tr_block_info::Location loc);
    int findBlockPos(tr_torrent* torrent, tr_block_info::Location loc) const;
};
