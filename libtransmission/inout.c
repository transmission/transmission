/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h> /* realloc */
#include <string.h> /* memcmp */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "transmission.h"
#include "crypto.h"
#include "fdlimit.h"
#include "inout.h"
#include "platform.h"
#include "stats.h"
#include "torrent.h"
#include "utils.h"

/****
*****  Low-level IO functions
****/

#ifdef WIN32
#define lseek _lseeki64
#endif

enum { TR_IO_READ, TR_IO_WRITE };

static tr_errno
readOrWriteBytes( const tr_torrent  * tor,
                  int                 ioMode,
                  tr_file_index_t     fileIndex,
                  uint64_t            fileOffset,
                  void              * buf,
                  size_t              buflen )
{
    const tr_info * info = &tor->info;
    const tr_file * file = &info->files[fileIndex];
    typedef size_t (* iofunc) ( int, void *, size_t );
    iofunc func = ioMode == TR_IO_READ ? (iofunc)read : (iofunc)write;
    char path[MAX_PATH_LENGTH];
    struct stat sb;
    int fd = -1;
    int err;
    int fileExists;

    assert( fileIndex < info->fileCount );
    assert( !file->length || (fileOffset < file->length));
    assert( fileOffset + buflen <= file->length );

    tr_buildPath ( path, sizeof(path), tor->downloadDir, file->name, NULL );
    fileExists = !stat( path, &sb );

    if( !file->length )
        return TR_OK;

    if ((ioMode==TR_IO_READ) && !fileExists ) /* does file exist? */
        err = tr_ioErrorFromErrno( errno );
    else if ((fd = tr_fdFileCheckout ( tor->downloadDir,
                                       file->name,
                                       ioMode==TR_IO_WRITE )) < 0)
        err = fd;
    else if( lseek( fd, (off_t)fileOffset, SEEK_SET ) == ((off_t)-1) )
        err = tr_ioErrorFromErrno( errno );
    else if( func( fd, buf, buflen ) != buflen )
        err = tr_ioErrorFromErrno( errno );
    else
        err = TR_OK;

    if( ( err==TR_OK ) && ( !fileExists ) && ( ioMode == TR_IO_WRITE) )
        tr_statsFileCreated( tor->handle );
 
    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return err;
}

static int
compareOffsetToFile( const void * a, const void * b )
{
    const uint64_t offset = *(const uint64_t*)a;
    const tr_file * file = b;

    if( offset < file->offset ) return -1;
    if( offset >= file->offset + file->length ) return 1;
    return 0;
}

static void
findFileLocation( const tr_torrent * tor,
                  tr_piece_index_t   pieceIndex,
                  uint32_t           pieceOffset,
                  tr_file_index_t  * fileIndex,
                  uint64_t         * fileOffset )
{
    const uint64_t offset = tr_pieceOffset( tor, pieceIndex, pieceOffset, 0 );
    const tr_file * file;

    file = bsearch( &offset,
                    tor->info.files, tor->info.fileCount, sizeof(tr_file),
                    compareOffsetToFile );
   
    *fileIndex = file - tor->info.files;
    *fileOffset = offset - file->offset;

    assert( *fileIndex < tor->info.fileCount );
    assert( *fileOffset < file->length );
    assert( tor->info.files[*fileIndex].offset + *fileOffset == offset );
}

#ifdef WIN32
static tr_errno
ensureMinimumFileSize( const tr_torrent  * tor,
                       tr_file_index_t     fileIndex,
                       uint64_t            minBytes )
{
    int fd;
    tr_errno err;
    struct stat sb;
    const tr_file * file = &tor->info.files[fileIndex];

    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( minBytes <= file->length );

    fd = tr_fdFileCheckout( tor->downloadDir, file->name, TRUE );
    if( fd < 0 ) /* bad fd */
        err = fd;
    else if (fstat (fd, &sb) ) /* how big is the file? */
        err = tr_ioErrorFromErrno( errno );
    else if (sb.st_size >= (off_t)minBytes) /* already big enough */
        err = TR_OK;
    else if ( !ftruncate( fd, minBytes ) ) /* grow it */
        err = TR_OK;
    else /* couldn't grow it */
        err = tr_ioErrorFromErrno( errno );

    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return err;
}
#endif

static tr_errno
readOrWritePiece( const tr_torrent        * tor,
                  int                       ioMode,
                  tr_piece_index_t          pieceIndex,
                  uint32_t                  pieceOffset,
                  uint8_t                 * buf,
                  size_t                    buflen )
{
    tr_errno err = 0;
    tr_file_index_t fileIndex;
    uint64_t fileOffset;
    const tr_info * info = &tor->info;

    if( pieceIndex >= tor->info.pieceCount )
        return TR_ERROR_ASSERT;
    if( pieceOffset + buflen > tr_torPieceCountBytes( tor, pieceIndex ) )
        return TR_ERROR_ASSERT;

    findFileLocation ( tor, pieceIndex, pieceOffset,
                            &fileIndex, &fileOffset );

    while( buflen && !err )
    {
        const tr_file * file = &info->files[fileIndex];
        const uint64_t bytesThisPass = MIN( buflen, file->length - fileOffset );

#ifdef WIN32
        if( ioMode == TR_IO_WRITE )
            err = ensureMinimumFileSize( tor, fileIndex,
                                         fileOffset + bytesThisPass );
        if( !err )
#endif
            err = readOrWriteBytes( tor, ioMode,
                                    fileIndex, fileOffset, buf, bytesThisPass );
        buf += bytesThisPass;
        buflen -= bytesThisPass;
        ++fileIndex;
        fileOffset = 0;
    }

    return err;
}

tr_errno
tr_ioRead( const tr_torrent  * tor,
           tr_piece_index_t    pieceIndex,
           uint32_t            begin,
           uint32_t            len,
           uint8_t           * buf )
{
    return readOrWritePiece( tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

tr_errno
tr_ioWrite( const tr_torrent  * tor,
            tr_piece_index_t    pieceIndex,
            uint32_t            begin,
            uint32_t            len,
            const uint8_t     * buf )
{
    return readOrWritePiece( tor, TR_IO_WRITE, pieceIndex, begin, (uint8_t*)buf, len );
}

/****
*****
****/

static tr_errno
recalculateHash( const tr_torrent  * tor,
                 tr_piece_index_t    pieceIndex,
                 uint8_t           * setme )
{
    static uint8_t * buf = NULL;
    static int buflen = 0;
    static tr_lock * lock = NULL;

    int n;
    tr_errno err;
    const tr_info * info;

    /* only check one block at a time to prevent disk thrashing.
     * this also lets us reuse the same buffer each time. */
    if( lock == NULL )
        lock = tr_lockNew( );

    tr_lockLock( lock );

    assert( tor != NULL );
    assert( setme != NULL );
    assert( pieceIndex < tor->info.pieceCount );

    info = &tor->info;
    n = tr_torPieceCountBytes( tor, pieceIndex );

    if( buflen < n ) {
        buflen = n;
        buf = tr_renew( uint8_t, buf, buflen );
    }
        
    err = tr_ioRead( tor, pieceIndex, 0, n, buf );
    if( !err )
        tr_sha1( setme, buf, n, NULL );

    tr_lockUnlock( lock );
    return err;
}

tr_errno
tr_ioTestPiece( const tr_torrent * tor, int pieceIndex )
{
    int err;
    uint8_t hash[SHA_DIGEST_LENGTH];

    err  = recalculateHash( tor, pieceIndex, hash );

    if( !err && memcmp( hash, tor->info.pieces[pieceIndex].hash,
                        SHA_DIGEST_LENGTH ) )
        err = TR_ERROR_IO_CHECKSUM;

    tr_tordbg (tor, "piece %d hash check: %s",
            pieceIndex, ( err ? "FAILED" : "OK" ));

    return err;
}
