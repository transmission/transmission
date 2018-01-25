/*
 * This file Copyright (C) 2005-2014 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include "transmission.h"
#include "file.h"
#include "net.h"

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
tr_sys_file_t tr_fdFileCheckout(tr_session* session, int torrent_id, tr_file_index_t file_num, char const* filename,
    bool do_write, tr_preallocation_mode preallocation_mode, uint64_t preallocation_file_size);

tr_sys_file_t tr_fdFileGetCached(tr_session* session, int torrent_id, tr_file_index_t file_num, bool doWrite);

bool tr_fdFileGetCachedMTime(tr_session* session, int torrent_id, tr_file_index_t file_num, time_t* mtime);

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
 * Sockets
 **********************************************************************/
tr_socket_t tr_fdSocketCreate(tr_session* session, int domain, int type);

tr_socket_t tr_fdSocketAccept(tr_session* session, tr_socket_t listening_sockfd, tr_address* addr, tr_port* port);

void tr_fdSocketClose(tr_session* session, tr_socket_t s);

/***********************************************************************
 * tr_fdClose
 ***********************************************************************
 * Frees resources allocated by tr_fdInit.
 **********************************************************************/
void tr_fdClose(tr_session* session);

/* @} */
