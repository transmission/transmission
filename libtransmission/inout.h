// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // uint8_t
#include <span>

#include "libtransmission/error-types.h"
#include "libtransmission/block-info.h"
#include "libtransmission/types.h"

class tr_open_files;
struct tr_torrent;

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] tr_error_code_t tr_ioRead(
    tr_torrent const& tor,
    tr_open_files& open_files,
    tr_block_info::Location const& loc,
    std::span<uint8_t> setme);

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
[[nodiscard]] tr_error_code_t tr_ioWrite(
    tr_torrent& tor,
    tr_open_files& open_files,
    tr_block_info::Location const& loc,
    std::span<uint8_t const> writeme);
