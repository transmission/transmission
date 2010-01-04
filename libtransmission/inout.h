/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#ifndef __TRANSMISSION__
 #error only libtransmission should #include this header.
#endif

#ifndef TR_IO_H
#define TR_IO_H 1

struct tr_torrent;

/**
 * @addtogroup file_io File IO
 * @{
 */

/**
 * Reads the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioRead( struct tr_torrent   * tor,
               tr_piece_index_t      pieceIndex,
               uint32_t              offset,
               uint32_t              len,
               uint8_t             * setme );

int
tr_ioPrefetch( tr_torrent       * tor,
               tr_piece_index_t   pieceIndex,
               uint32_t           begin,
               uint32_t           len );

/**
 * Writes the block specified by the piece index, offset, and length.
 * @return 0 on success, or an errno value on failure.
 */
int tr_ioWrite( struct tr_torrent  * tor,
                tr_piece_index_t     pieceIndex,
                uint32_t             offset,
                uint32_t             len,
                const uint8_t      * writeme );

/**
 * @brief Test to see if the piece matches its metainfo's SHA1 checksum.
 *
 * @param optionalBuffer if calling tr_ioTestPiece() repeatedly, you can
 *                       get best performance by providing a buffer with
 *                       tor->info.pieceSize bytes.
 */
tr_bool tr_ioTestPiece( tr_torrent       * tor,
                        tr_piece_index_t   piece,
                        void             * optionalBuffer,
                        size_t             optionalBufferLen );


/**
 * Converts a piece index + offset into a file index + offset.
 */
void     tr_ioFindFileLocation( const tr_torrent * tor,
                                tr_piece_index_t   pieceIndex,
                                uint32_t           pieceOffset,
                                tr_file_index_t *  fileIndex,
                                uint64_t *         fileOffset );


/* @} */
#endif
