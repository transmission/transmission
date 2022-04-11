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

class tr_write_cache : public tr_torrent_io
{
public:
    ~tr_write_cache() = default;

    virtual void setMaxBlocks(size_t max_blocks) = 0;
    [[nodiscard]] virtual size_t maxBlocks() const noexcept = 0;

    virtual bool saveTorrent(tr_torrent_id_t tor_id) = 0;
    virtual bool saveSpan(tr_torrent_id_t tor_id, tr_block_span_t blocks) = 0;
};

tr_write_cache* tr_writeCacheNew(tr_torrent_io& io, size_t max_blocks);
