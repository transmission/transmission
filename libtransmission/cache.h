// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // intX_t, uintX_t
#include <vector>

#include "transmission.h"

#include "block-info.h"

class tr_torrents;
struct evbuffer;
struct tr_torrent;

class Cache
{
public:
    explicit Cache(tr_torrents& torrents, int64_t max_bytes);
    ~Cache() = default;

    int setLimit(int64_t new_limit);

    [[nodiscard]] size_t getLimit() const
    {
        return max_bytes_;
    }

    int writeBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t length, struct evbuffer* writeme);
    int readBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len, uint8_t* setme);
    int prefetchBlock(tr_torrent* torrent, tr_block_info::Location loc, uint32_t len);
    int flushDone();
    int flushTorrent(tr_torrent* torrent);
    int flushFile(tr_torrent* torrent, tr_file_index_t file);

private:
    struct CacheBlock
    {
        CacheBlock() = default;
        CacheBlock(tr_torrent* _tor, tr_block_info::Location _loc, uint32_t _length);

        tr_torrent* tor = nullptr;

        tr_block_info::Location loc;
        uint32_t length = 0;

        time_t time = 0;

        struct evbuffer* evbuf = nullptr;
    };

    struct RunInfo
    {
        size_t pos = 0;
        size_t rank = 0;
        time_t last_block_time = 0;
        bool is_multi_piece = false;
        bool is_piece_done = false;
        size_t len = 0;
    };

    enum
    {
        MULTIFLAG = 0x1000,
        DONEFLAG = 0x2000,
        SESSIONFLAG = 0x4000,
    };

    tr_torrents& torrents_;

    std::vector<CacheBlock> blocks_;
    size_t max_blocks_ = 0;
    size_t max_bytes_ = 0;

    size_t disk_writes_ = 0;
    size_t disk_write_bytes_ = 0;
    size_t cache_writes_ = 0;
    size_t cache_write_bytes_ = 0;

    size_t getBlockRun(size_t pos, RunInfo* info) const;

    // Descending sort: Higher rank comes before lower rank
    static bool compareRuns(RunInfo& a, RunInfo& b)
    {
        // The value returned by the comparer function indicates whether the element passed as
        // first argument is considered to go before the second
        return b.rank > a.rank;
    }

    size_t calcRuns(std::vector<RunInfo>& runs) const;

    // Returns: result of tr_ioWrite
    int flushContiguous(size_t pos, size_t n);

    // Returns: result of last tr_ioWrite call returned from flushContiguous()
    int flushRuns(std::vector<RunInfo>& runs, size_t n);

    // Returns: result of last tr_ioWrite call returned from flushRuns()
    int cacheTrim();

    static size_t getMaxBlocks(int64_t max_bytes);
    static bool cacheBlockCompare(CacheBlock const& a, CacheBlock const& b);
    // Non-const variant returns a mutable iterator
    std::vector<Cache::CacheBlock>::iterator findBlock(tr_torrent* torrent, tr_block_info::Location loc);
    // Returns max value for size_t (0xFFFF...) if not found
    size_t findBlockPos(tr_torrent* torrent, tr_block_info::Location loc) const;
};
