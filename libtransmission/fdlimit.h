/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
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

#include "transmission.h"
#include "net.h"

/**
 * @addtogroup file_io File IO
 * @{
 */

void tr_fdSetFileLimit( tr_session * session, int limit );

int tr_fdGetFileLimit( tr_session * session );

void tr_fdSetGlobalPeerLimit( tr_session * session, int limit );

/***
****
***/

void tr_set_file_for_single_pass( int fd );

int tr_open_file_for_scanning( const char * filename );

int tr_open_file_for_writing( const char * filename );

void tr_close_file( int fd );

int tr_fsync(int fd);

ssize_t tr_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t tr_pwrite(int fd, const void *buf, size_t count, off_t offset);
int tr_prefetch(int fd, off_t offset, size_t count);


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
 * @param existing_dir An ancestor of filename which must already exist and
 *                     won't be created by tr_fdFileCheckout(). This prevents
 *                     directories from being created in error, such as a mount
 *                     point for an external drive when the drive is unplugged.
 *
 * on success, a file descriptor >= 0 is returned.
 * on failure, a -1 is returned and errno is set.
 *
 * @see tr_fdFileClose
 */
int  tr_fdFileCheckout( tr_session             * session,
                        int                      torrent_id,
                        tr_file_index_t          file_num,
                        const char             * existing_dir,
                        const char             * filename,
                        bool                  do_write,
                        tr_preallocation_mode    preallocation_mode,
                        uint64_t                 preallocation_file_size );

int tr_fdFileGetCached( tr_session             * session,
                        int                      torrent_id,
                        tr_file_index_t          file_num,
                        bool                  doWrite );

/**
 * Closes a file that's being held by our file repository.
 *
 * If the file isn't checked out, it's fsync()ed and close()d immediately.
 * If the file is currently checked out, it will be closed upon its return.
 *
 * @see tr_fdFileCheckout
 */
void tr_fdFileClose( tr_session        * session,
                     const tr_torrent  * tor,
                     tr_file_index_t     file_num );


/**
 * Closes all the files associated with a given torrent id
 */
void tr_fdTorrentClose( tr_session * session, int torrentId );


/***********************************************************************
 * Sockets
 **********************************************************************/
int      tr_fdSocketCreate( tr_session * session, int domain, int type );

int      tr_fdSocketAccept( tr_session  * session,
                            int           listening_sockfd,
                            tr_address  * addr,
                            tr_port     * port );

void     tr_fdSocketClose( tr_session * session, int s );

/***********************************************************************
 * tr_fdClose
 ***********************************************************************
 * Frees resources allocated by tr_fdInit.
 **********************************************************************/
void     tr_fdClose( tr_session * session );


void     tr_fdSetPeerLimit( tr_session * session, int n );

int      tr_fdGetPeerLimit( const tr_session * );

/* @} */
