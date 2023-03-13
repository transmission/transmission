// This file Copyright © 2007-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // uint8_t, uint32_t

#include "transmission.h"

#include "block-info.h"

struct tr_torrent;

/**
 * @addtogroup file_io File IO
 * @{
 */

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] int tr_ioRead(struct tr_torrent* tor, tr_block_info::Location loc, size_t len, uint8_t* setme);

int tr_ioPrefetch(tr_torrent* tor, tr_block_info::Location loc, size_t len);

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] int tr_ioWrite(struct tr_torrent* tor, tr_block_info::Location loc, size_t len, uint8_t const* writeme);

/**
 * @brief Test to see if the piece matches its metainfo's SHA1 checksum.
 */
bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece);

/* @} */
