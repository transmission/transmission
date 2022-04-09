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

#include "transmission.h"
#include "block-info.h"

class tr_torrent_io
{
public:
    static auto constexpr BlockSize = tr_block_info::BlockSize;
    using tr_torrent_id_t = int;

    virtual ~tr_torrent_io() = default;

    virtual bool putBlock(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t const* block_data) = 0;
    virtual bool getBlock(tr_torrent_id_t tor_id, tr_block_index_t block, uint8_t* block_data) = 0;
    virtual void prefetch(tr_torrent_id_t tor_id, tr_block_index_t block) = 0;
};

class tr_write_cache: public tr_torrent_io
{
public:
    ~tr_write_cache() = default;

    virtual void setMaxBytes(size_t max_bytes) = 0;
    [[nodiscard]] virtual size_t maxBytes() const = 0;

    virtual void flushCompletePieces() = 0;
    virtual void flushTorrent(tr_torrent_id_t tor_id) = 0;
    virtual void flushFile(tr_torrent_id_t tor_id, tr_file_index_t file) = 0;
};

tr_write_cache* tr_writeCacheNew(tr_torrent_io& io, tr_block_info const& info, size_t max_bytes);
