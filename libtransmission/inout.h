/*
 * This file Copyright (C) 2007-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

struct tr_torrent;

/**
 * @addtogroup file_io File IO
 * @{
 */

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioRead(struct tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t offset, uint32_t len, uint8_t* setme);

int tr_ioPrefetch(tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t begin, uint32_t len);

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioWrite(struct tr_torrent* tor, tr_piece_index_t pieceIndex, uint32_t offset, uint32_t len, uint8_t const* writeme);

/**
 * @brief Test to see if the piece matches its metainfo's SHA1 checksum.
 */
bool tr_ioTestPiece(tr_torrent* tor, tr_piece_index_t piece);

/**
 * Converts a piece index + offset into a file index + offset.
 */
void tr_ioFindFileLocation(tr_torrent const* tor, tr_piece_index_t pieceIndex, uint32_t pieceOffset, tr_file_index_t* fileIndex,
    uint64_t* fileOffset);

/* @} */
