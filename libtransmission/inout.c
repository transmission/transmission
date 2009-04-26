/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
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

int64_t
tr_lseek( int fd, int64_t offset, int whence )
{
#if defined(_LARGEFILE_SOURCE)
    return lseek64( fd, (off64_t)offset, whence );
#elif defined(WIN32)
    return _lseeki64( fd, offset, whence );
#else
    return lseek( fd, (off_t)offset, whence );
#endif
}

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
    tr_preallocation_mode preallocationMode;

    typedef size_t ( *iofunc )( int, void *, size_t );
    iofunc          func = ioMode == TR_IO_READ ? (iofunc)read : (iofunc)write;
    struct stat     sb;
    int             fd = -1;
    int             err;
    int             fileExists;

    assert( tor->downloadDir && *tor->downloadDir );
    assert( fileIndex < info->fileCount );
    assert( !file->length || ( fileOffset < file->length ) );
    assert( fileOffset + buflen <= file->length );

    {
        char path[MAX_PATH_LENGTH];
        tr_snprintf( path, sizeof( path ), "%s%c%s", tor->downloadDir, TR_PATH_DELIMITER, file->name );
        fileExists = !stat( path, &sb );
    }

    if( !file->length )
        return 0;

    if( ( file->dnd ) || ( ioMode != TR_IO_WRITE ) )
        preallocationMode = TR_PREALLOCATE_NONE;
    else
        preallocationMode = tor->session->preallocationMode;

    if( ( ioMode == TR_IO_READ ) && !fileExists ) /* does file exist? */
        err = errno;
    else if( ( fd = tr_fdFileCheckout ( tor->downloadDir, file->name, ioMode == TR_IO_WRITE, preallocationMode, file->length ) ) < 0 )
        err = errno;
    else if( tr_lseek( fd, (int64_t)fileOffset, SEEK_SET ) == -1 )
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
                       tr_file_index_t  * fileIndex,
                       uint64_t         * fileOffset )
{
    const uint64_t  offset = tr_pieceOffset( tor, pieceIndex, pieceOffset, 0 );
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
        const uint64_t  bytesThisPass = MIN( buflen, file->length - fileOffset );

        err = readOrWriteBytes( tor, ioMode, fileIndex, fileOffset, buf, bytesThisPass );
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

static tr_bool
recalculateHash( const tr_torrent * tor,
                 tr_piece_index_t   pieceIndex,
                 void             * buffer,
                 size_t             buflen,
                 uint8_t *          setme )
{
    size_t   bytesLeft;
    uint32_t offset = 0;
    tr_bool  success = TRUE;
    uint8_t  stackbuf[MAX_STACK_ARRAY_SIZE];
    SHA_CTX  sha;

    /* fallback buffer */
    if( ( buffer == NULL ) || ( buflen < 1 ) )
    {
        buffer = stackbuf;
        buflen = sizeof( stackbuf );
    }

    assert( tor != NULL );
    assert( pieceIndex < tor->info.pieceCount );
    assert( buffer != NULL );
    assert( buflen > 0 );
    assert( setme != NULL );

    SHA1_Init( &sha );
    bytesLeft = tr_torPieceCountBytes( tor, pieceIndex );

    while( bytesLeft )
    {
        const int len = MIN( bytesLeft, buflen );
        success = !tr_ioRead( tor, pieceIndex, offset, len, buffer );
        if( !success )
            break;
        SHA1_Update( &sha, buffer, len );
        offset += len;
        bytesLeft -= len;
    }

    if( success )
        SHA1_Final( setme, &sha );

    return success;
}

tr_bool
tr_ioTestPiece( const tr_torrent  * tor,
                tr_piece_index_t    pieceIndex,
                void              * buffer,
                size_t              buflen )
{
    uint8_t hash[SHA_DIGEST_LENGTH];

    return recalculateHash( tor, pieceIndex, buffer, buflen, hash )
           && !memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
}
