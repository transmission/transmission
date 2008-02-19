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
#include <string.h> /* memcmp */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "transmission.h"
#include "fdlimit.h"
#include "inout.h"
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
                  int                 fileIndex,
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

    assert( 0<=fileIndex && fileIndex<info->fileCount );
    assert( !file->length || (fileOffset < file->length));
    assert( fileOffset + buflen <= file->length );

    tr_buildPath ( path, sizeof(path), tor->destination, file->name, NULL );
    fileExists = !stat( path, &sb );

    if( !file->length )
        return TR_OK;

    if ((ioMode==TR_IO_READ) && !fileExists ) /* does file exist? */
        err = tr_ioErrorFromErrno( errno );
    else if ((fd = tr_fdFileCheckout ( tor->destination, file->name, ioMode==TR_IO_WRITE )) < 0)
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

static tr_errno
findFileLocation( const tr_torrent * tor,
                  int                pieceIndex,
                  int                pieceOffset,
                  int              * fileIndex,
                  uint64_t         * fileOffset )
{
    const tr_info * info = &tor->info;

    int i;
    uint64_t piecePos = ((uint64_t)pieceIndex * info->pieceSize) + pieceOffset;

    if( pieceIndex < 0 || pieceIndex >= info->pieceCount )
        return TR_ERROR_ASSERT;
    if( pieceOffset >= tr_torPieceCountBytes( tor, pieceIndex ) )
        return TR_ERROR_ASSERT;
    if( piecePos >= info->totalSize )
        return TR_ERROR_ASSERT;

    for( i=0; info->files[i].length<=piecePos; ++i )
        piecePos -= info->files[i].length;

    *fileIndex = i;
    *fileOffset = piecePos;

    assert( 0<=*fileIndex && *fileIndex<info->fileCount );
    assert( *fileOffset < info->files[i].length );
    return 0;
}

#ifdef WIN32
static tr_errno
ensureMinimumFileSize( const tr_torrent  * tor,
                       int                 fileIndex,
                       uint64_t            minBytes )
{
    int fd;
    tr_errno err;
    struct stat sb;
    const tr_file * file = &tor->info.files[fileIndex];

    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( minBytes <= file->length );

    fd = tr_fdFileCheckout( tor->destination, file->name, TRUE );
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
readOrWritePiece( tr_torrent  * tor,
                  int           ioMode,
                  int           pieceIndex,
                  int           pieceOffset,
                  uint8_t     * buf,
                  size_t        buflen )
{
    tr_errno err = 0;
    int fileIndex;
    uint64_t fileOffset;
    const tr_info * info = &tor->info;

    if( pieceIndex < 0 || pieceIndex >= tor->info.pieceCount )
        err = TR_ERROR_ASSERT;
    else if( buflen > ( size_t ) tr_torPieceCountBytes( tor, pieceIndex ) )
        err = TR_ERROR_ASSERT;

    if( !err )
        err = findFileLocation ( tor, pieceIndex, pieceOffset, &fileIndex, &fileOffset );

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
        fileIndex++;
        fileOffset = 0;
    }

    return err;
}

tr_errno
tr_ioRead( const tr_torrent  * tor,
           int                 pieceIndex,
           int                 begin,
           int                 len,
           uint8_t           * buf )
{
    return readOrWritePiece( (tr_torrent*)tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

tr_errno
tr_ioWrite( tr_torrent     * tor,
            int              pieceIndex,
            int              begin,
            int              len,
            const uint8_t  * buf )
{
    return readOrWritePiece( tor, TR_IO_WRITE, pieceIndex, begin, (uint8_t*)buf, len );
}

/****
*****
****/

static tr_errno
tr_ioRecalculateHash( const tr_torrent  * tor,
                      int                 pieceIndex,
                      uint8_t           * setme )
{
    int offset;
    int bytesLeft;
    uint8_t buf[4096];
    const tr_info * info;
    SHA_CTX sha;

    assert( tor != NULL );
    assert( setme != NULL );
    assert( 0<=pieceIndex && pieceIndex<tor->info.pieceCount );

    info = &tor->info;
    offset = 0;
    bytesLeft = tr_torPieceCountBytes( tor, pieceIndex );
    SHA1_Init( &sha );

    while( bytesLeft > 0 )
    {
        const int bytesThisPass = MIN( bytesLeft, (int)sizeof(buf) );
        tr_errno err = tr_ioRead( tor, pieceIndex, offset, bytesThisPass, buf );
        if( err )
            return err;
        SHA1_Update( &sha, buf, bytesThisPass );
        bytesLeft -= bytesThisPass;
        offset += bytesThisPass;
    }

    SHA1_Final( setme, &sha );
    return 0;
}

tr_errno
tr_ioTestPiece( const tr_torrent * tor, int pieceIndex )
{
    int err;
    uint8_t hash[SHA_DIGEST_LENGTH];

    err  = tr_ioRecalculateHash( tor, pieceIndex, hash );

    if( !err && memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH ) )
        err = TR_ERROR;

    tr_dbg ("torrent [%s] piece %d hash check: %s",
            tor->info.name, pieceIndex, ( err ? "FAILED" : "OK" ));

    return err;
}
