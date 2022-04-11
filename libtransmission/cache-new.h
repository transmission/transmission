// This file Copyright © 2010-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <functional>

#include "transmission.h"

#include "block-info.h"

/**
 * An interface for reading / writing torrent blocks
 */
class tr_torrent_io
{
public:
    static auto constexpr BlockSize = tr_block_info::BlockSize;

    virtual ~tr_torrent_io() = default;

    virtual bool put(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t const* block_data) = 0;
    virtual bool get(tr_torrent_id_t tor_id, tr_block_span_t span, uint8_t* block_data) = 0;
    virtual void prefetch(tr_torrent_id_t tor_id, tr_block_span_t span) = 0;

    bool put(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t const* block_data)
    {
        return put(tor_id, { block, block + 1 }, block_data);
    }

    bool get(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t* block_data)
    {
        return get(tor_id, { block, block + 1 }, block_data);
    }

    void prefetch(tr_torrent_id_t tor_id, tr_block_index_t block)
    {
        return prefetch(tor_id, { block, block + 1 });
    }

    virtual uint32_t blockSize(tr_block_index_t block) const = 0;
};

/**
 * A tr_torrent_io decorator that batches data passed to it via put() in
 * memory for a short time before put()ting it along to its wrapped io.
 *
 * The intent is fewer disk writes: blocks that arrive from peers
 * are often contiguous and can be folded into fewer disk writes.
 */
class tr_write_cache : public tr_torrent_io
{
public:
    ~tr_write_cache() = default;

    // How many blocks can be held in the memory cache
    virtual void setMaxBlocks(size_t max_blocks) = 0;
    [[nodiscard]] virtual size_t maxBlocks() const noexcept = 0;

    // Flush a torrent from the memory cache to disk.
    // Typically used when a torrent becomes complete, e.g. so its
    // files can be moved from an incompleteDir to a completeDir
    virtual bool saveTorrent(tr_torrent_id_t tor_id) = 0;

    // Flush a torrent's span from the memory cache to disk.
    // Typically used when a file becomes complete, e.g. so
    // the file can be reopened in read-only mode
    virtual bool saveSpan(tr_torrent_id_t tor_id, tr_block_span_t blocks) = 0;
};

tr_write_cache* tr_writeCacheNew(tr_torrent_io& io, size_t max_blocks);
