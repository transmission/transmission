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

#include <openssl/sha.h>

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
 #if defined(read)
    #undef read
 #endif
 #define read  _read
 
 #if defined(write)
    #undef write
 #endif
 #define write _write
#endif

enum { TR_IO_READ, TR_IO_WRITE };

/* returns 0 on success, or an errno on failure */
static int
readOrWriteBytes( const tr_torrent * tor,
                  int                ioMode,
                  tr_file_index_t    fileIndex,
                  uint64_t           fileOffset,
                  void *             buf,
                  size_t             buflen )
{
    const tr_info * info = &tor->info;
    const tr_file * file = &info->files[fileIndex];

    typedef size_t ( *iofunc )( int, void *, size_t );
    iofunc          func = ioMode ==
                           TR_IO_READ ? (iofunc)read : (iofunc)write;
    char          * path;
    struct stat     sb;
    int             fd = -1;
    int             err;
    int             fileExists;

    assert( fileIndex < info->fileCount );
    assert( !file->length || ( fileOffset < file->length ) );
    assert( fileOffset + buflen <= file->length );

    path = tr_buildPath( tor->downloadDir, file->name, NULL );
    fileExists = !stat( path, &sb );
    tr_free( path );

    if( !file->length )
        return 0;

    if( ( ioMode == TR_IO_READ ) && !fileExists ) /* does file exist? */
        err = errno;
    else if( ( fd = tr_fdFileCheckout ( tor->downloadDir,
                                        file->name,
                                        ioMode == TR_IO_WRITE ) ) < 0 )
        err = errno;
    else if( lseek( fd, (off_t)fileOffset, SEEK_SET ) == ( (off_t)-1 ) )
        err = errno;
    else if( func( fd, buf, buflen ) != buflen )
        err = errno;
    else
        err = 0;

    if( ( !err ) && ( !fileExists ) && ( ioMode == TR_IO_WRITE ) )
        tr_statsFileCreated( tor->session );

    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return err;
}

static int
compareOffsetToFile( const void * a,
                     const void * b )
{
    const uint64_t  offset = *(const uint64_t*)a;
    const tr_file * file = b;

    if( offset < file->offset ) return -1;
    if( offset >= file->offset + file->length ) return 1;
    return 0;
}

void
tr_ioFindFileLocation( const tr_torrent * tor,
                       tr_piece_index_t   pieceIndex,
                       uint32_t           pieceOffset,
                       tr_file_index_t *  fileIndex,
                       uint64_t *         fileOffset )
{
    const uint64_t  offset = tr_pieceOffset( tor, pieceIndex, pieceOffset,
                                             0 );
    const tr_file * file;

    file = bsearch( &offset,
                    tor->info.files, tor->info.fileCount, sizeof( tr_file ),
                    compareOffsetToFile );

    *fileIndex = file - tor->info.files;
    *fileOffset = offset - file->offset;

    assert( *fileIndex < tor->info.fileCount );
    assert( *fileOffset < file->length );
    assert( tor->info.files[*fileIndex].offset + *fileOffset == offset );
}

#ifdef WIN32
/* return 0 on success, or an errno on failure */
static int
ensureMinimumFileSize( const tr_torrent * tor,
                       tr_file_index_t    fileIndex,
                       uint64_t           minBytes )
{
    int             fd;
    int             err;
    struct stat     sb;
    const tr_file * file = &tor->info.files[fileIndex];

    assert( 0 <= fileIndex && fileIndex < tor->info.fileCount );
    assert( minBytes <= file->length );

    fd = tr_fdFileCheckout( tor->downloadDir, file->name, TRUE );
    if( fd < 0 ) /* bad fd */
        err = errno;
    else if( fstat ( fd, &sb ) ) /* how big is the file? */
        err = errno;
    else if( sb.st_size >= (off_t)minBytes ) /* already big enough */
        err = 0;
    else if( !ftruncate( fd, minBytes ) )  /* grow it */
        err = 0;
    else /* couldn't grow it */
        err = errno;

    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return err;
}

#endif

/* returns 0 on success, or an errno on failure */
static int
readOrWritePiece( const tr_torrent * tor,
                  int                ioMode,
                  tr_piece_index_t   pieceIndex,
                  uint32_t           pieceOffset,
                  uint8_t *          buf,
                  size_t             buflen )
{
    int             err = 0;
    tr_file_index_t fileIndex;
    uint64_t        fileOffset;
    const tr_info * info = &tor->info;

    if( pieceIndex >= tor->info.pieceCount )
        return EINVAL;
    if( pieceOffset + buflen > tr_torPieceCountBytes( tor, pieceIndex ) )
        return EINVAL;

    tr_ioFindFileLocation( tor, pieceIndex, pieceOffset,
                           &fileIndex, &fileOffset );

    while( buflen && !err )
    {
        const tr_file * file = &info->files[fileIndex];
        const uint64_t  bytesThisPass = MIN( buflen,
                                             file->length - fileOffset );

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

int
tr_ioRead( const tr_torrent * tor,
           tr_piece_index_t   pieceIndex,
           uint32_t           begin,
           uint32_t           len,
           uint8_t *          buf )
{
    return readOrWritePiece( tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

int
tr_ioWrite( const tr_torrent * tor,
            tr_piece_index_t   pieceIndex,
            uint32_t           begin,
            uint32_t           len,
            const uint8_t *    buf )
{
    return readOrWritePiece( tor, TR_IO_WRITE, pieceIndex, begin,
                             (uint8_t*)buf,
                             len );
}

/****
*****
****/

static int
recalculateHash( const tr_torrent * tor,
                 tr_piece_index_t   pieceIndex,
                 uint8_t *          setme )
{
    size_t   bytesLeft;
    size_t   n;
    uint32_t offset = 0;
    int      success = TRUE;
    SHA_CTX  sha;

    assert( tor );
    assert( setme );
    assert( pieceIndex < tor->info.pieceCount );

    SHA1_Init( &sha );
    n = bytesLeft = tr_torPieceCountBytes( tor, pieceIndex );

    while( bytesLeft )
    {
        uint8_t   buf[8192];
        const int len = MIN( bytesLeft, sizeof( buf ) );
        success = !tr_ioRead( tor, pieceIndex, offset, len, buf );
        if( !success )
            break;
        SHA1_Update( &sha, buf, len );
        offset += len;
        bytesLeft -= len;
    }

    if( success )
        SHA1_Final( setme, &sha );

    return success;
}

int
tr_ioTestPiece( const tr_torrent * tor,
                int                pieceIndex )
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    const int recalculated = recalculateHash( tor, pieceIndex, hash );
    return recalculated && !memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
}

