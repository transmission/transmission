// This file Copyright © 2005-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0), GPLv3 (SPDX: GPL-3.0),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <cstdint> // uint64_t

#include "transmission.h"

#include "file.h"

/**
 * @addtogroup file_io File IO
 * @{
 */

/***
****
***/

/**
 * Returns an fd to the specified filename.
 *
 * A small pool of open files is kept to avoid the overhead of
 * continually opening and closing the same files when downloading
 * piece data.
 *
 * - if do_write is true, subfolders in torrentFile are created if necessary.
 * - if do_write is true, the target file is created if necessary.
 *
 * on success, a file descriptor >= 0 is returned.
 * on failure, a TR_BAD_SYS_FILE is returned and errno is set.
 *
 * @see tr_fdFileClose
 */
tr_sys_file_t tr_fdFileCheckout(
    tr_session* session,
    int torrent_id,
    tr_file_index_t file_num,
    char const* filename,
    bool do_write,
    tr_preallocation_mode preallocation_mode,
    uint64_t preallocation_file_size);

tr_sys_file_t tr_fdFileGetCached(tr_session* session, int torrent_id, tr_file_index_t file_num, bool doWrite);

/**
 * Closes a file that's being held by our file repository.
 *
 * If the file isn't checked out, it's fsync()ed and close()d immediately.
 * If the file is currently checked out, it will be closed upon its return.
 *
 * @see tr_fdFileCheckout
 */
void tr_fdFileClose(tr_session* session, tr_torrent const* tor, tr_file_index_t file_num);

/**
 * Closes all the files associated with a given torrent id
 */
void tr_fdTorrentClose(tr_session* session, int torrentId);

/***********************************************************************
 * tr_fdClose
 ***********************************************************************
 * Frees resources allocated by tr_fdInit.
 **********************************************************************/
void tr_fdClose(tr_session* session);

/* @} */
