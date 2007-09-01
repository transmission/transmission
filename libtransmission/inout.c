/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "transmission.h"
#include "completion.h"
#include "fdlimit.h"
#include "inout.h"
#include "net.h"
#include "peer.h"
#include "sha1.h"
#include "utils.h"

struct tr_io_s
{
    tr_torrent_t * tor;
};

/****
*****  Low-level IO functions
****/

enum { TR_IO_READ, TR_IO_WRITE };

static int
readOrWriteBytes ( const tr_torrent_t  * tor,
                   int                   ioMode,
                   int                   fileIndex,
                   uint64_t              fileOffset,
                   void                * buf,
                   size_t                buflen )
{
    const tr_info_t * info = &tor->info;
    const tr_file_t * file = &info->files[fileIndex];
    typedef size_t (* iofunc) ( int, void *, size_t );
    iofunc func = ioMode == TR_IO_READ ? (iofunc)read : (iofunc)write;
    char path[MAX_PATH_LENGTH];
    struct stat sb;
    int fd = -1;
    int ret;

    assert ( 0<=fileIndex && fileIndex<info->fileCount );
    assert ( !file->length || (fileOffset < file->length));
    assert ( fileOffset + buflen <= file->length );

    tr_buildPath ( path, sizeof(path), tor->destination, file->name, NULL );

    if( !file->length )
        return 0;
    else if ((ioMode==TR_IO_READ) && stat( path, &sb ) ) /* fast check to make sure file exists */
        ret = tr_ioErrorFromErrno ();
    else if ((fd = tr_fdFileOpen ( tor->destination, file->name, ioMode==TR_IO_WRITE )) < 0)
        ret = fd;
    else if( lseek( fd, (off_t)fileOffset, SEEK_SET ) == ((off_t)-1) )
        ret = TR_ERROR_IO_OTHER;
    else if( func( fd, buf, buflen ) != buflen )
        ret = tr_ioErrorFromErrno ();
    else
        ret = TR_OK;
 
    if( fd >= 0 )
        tr_fdFileRelease( fd );

    return ret;
}

static void
findFileLocation ( const tr_torrent_t * tor,
                   int                  pieceIndex,
                   int                  pieceOffset,
                   int                * fileIndex,
                   uint64_t           * fileOffset )
{
    const tr_info_t * info = &tor->info;

    int i;
    uint64_t piecePos = ((uint64_t)pieceIndex * info->pieceSize) + pieceOffset;

    assert ( 0<=pieceIndex && pieceIndex < info->pieceCount );
    assert ( 0<=tor->info.pieceSize );
    assert ( pieceOffset < tr_torPieceCountBytes( tor, pieceIndex ) );
    assert ( piecePos < info->totalSize );

    for ( i=0; info->files[i].length<=piecePos; ++i )
      piecePos -= info->files[i].length;

    *fileIndex = i;
    *fileOffset = piecePos;

    assert ( 0<=*fileIndex && *fileIndex<info->fileCount );
    assert ( *fileOffset < info->files[i].length );
}

static int
ensureMinimumFileSize ( const tr_torrent_t  * tor,
                        int                   fileIndex,
                        uint64_t              minSize ) /* in bytes */
{
    int fd;
    int ret;
    struct stat sb;
    const tr_file_t * file = &tor->info.files[fileIndex];

    assert ( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert ( minSize <= file->length );

    fd = tr_fdFileOpen( tor->destination, file->name, TRUE );
    if( fd < 0 ) /* bad fd */
        ret = fd;
    else if (fstat (fd, &sb) ) /* how big is the file? */
        ret = tr_ioErrorFromErrno ();
    else if ((size_t)sb.st_size >= minSize) /* already big enough */
        ret = TR_OK;
    else if (!ftruncate( fd, minSize )) /* grow it */
        ret = TR_OK;
    else /* couldn't grow it */
        ret = tr_ioErrorFromErrno ();

    if( fd >= 0 )
        tr_fdFileRelease( fd );

    return ret;
}

static int
readOrWritePiece ( tr_torrent_t       * tor,
                   int                  ioMode,
                   int                  pieceIndex,
                   int                  pieceOffset,
                   uint8_t            * buf,
                   size_t               buflen )
{
    int ret = 0;
    int fileIndex;
    uint64_t fileOffset;
    const tr_info_t * info = &tor->info;

    assert( 0<=pieceIndex && pieceIndex<tor->info.pieceCount );
    assert( buflen <= (size_t) tr_torPieceCountBytes( tor, pieceIndex ) );

    findFileLocation ( tor, pieceIndex, pieceOffset, &fileIndex, &fileOffset );

    while( buflen && !ret )
    {
        const tr_file_t * file = &info->files[fileIndex];
        const uint64_t bytesThisPass = MIN( buflen, file->length - fileOffset );

        if( ioMode == TR_IO_WRITE )
            ret = ensureMinimumFileSize( tor, fileIndex,
                                         fileOffset + bytesThisPass );
        if( !ret )
            ret = readOrWriteBytes( tor, ioMode,
                                    fileIndex, fileOffset, buf, bytesThisPass );
        buf += bytesThisPass;
        buflen -= bytesThisPass;
        fileIndex++;
        fileOffset = 0;
    }

    return ret;
}

int
tr_ioRead( tr_io_t * io, int pieceIndex, int begin, int len, uint8_t * buf )
{
    return readOrWritePiece ( io->tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

int
tr_ioWrite( tr_io_t * io, int pieceIndex, int begin, int len, uint8_t * buf )
{
    return readOrWritePiece ( io->tor, TR_IO_WRITE, pieceIndex, begin, buf, len );
}

/****
*****
****/

static int
tr_ioRecalculateHash ( tr_torrent_t  * tor,
                       int             pieceIndex,
                       uint8_t       * setme )
{
    int n;
    int ret;
    uint8_t * buf;
    const tr_info_t * info;

    assert( tor != NULL );
    assert( setme != NULL );
    assert( 0<=pieceIndex && pieceIndex<tor->info.pieceCount );

    info = &tor->info;
    n = tr_torPieceCountBytes( tor, pieceIndex );

    buf = malloc( n );
    ret = readOrWritePiece ( tor, TR_IO_READ, pieceIndex, 0, buf, n );
    if( !ret ) {
        SHA1( buf, n, setme );
    }
    free( buf );

    return ret;
}

static int
checkPiece ( tr_torrent_t * tor, int pieceIndex )
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    int ret = tr_ioRecalculateHash( tor, pieceIndex, hash )
           || memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
    tr_dbg ("torrent [%s] piece %d hash check: %s",
            tor->info.name, pieceIndex, (ret?"FAILED":"OK"));
    return ret;
}

void
tr_ioCheckFiles( tr_torrent_t * tor )
{
    assert( tor != NULL );
    assert( tor->completion != NULL );
    assert( tor->info.pieceCount > 0 );

    if( tor->uncheckedPieces != NULL )
    {
        int i;

        /* remove the unchecked pieces from completion... */
        for( i=0; i<tor->info.pieceCount; ++i ) 
            if( tr_bitfieldHas( tor->uncheckedPieces, i ) )
                tr_cpPieceRem( tor->completion, i );

        tr_inf( "Verifying some pieces of \"%s\"", tor->info.name );

        for( i=0; i<tor->info.pieceCount; ++i ) 
        {
            if( !tr_bitfieldHas( tor->uncheckedPieces, i ) )
                continue;

            tr_torrentSetHasPiece( tor, i, !checkPiece( tor, i ) );
            tr_bitfieldRem( tor->uncheckedPieces, i );
        }

        tr_bitfieldFree( tor->uncheckedPieces );
        tor->uncheckedPieces = NULL;
        tor->fastResumeDirty = TRUE;
    }
}

/****
*****  Life Cycle
****/

tr_io_t*
tr_ioNew ( tr_torrent_t * tor )
{
    tr_io_t * io = tr_calloc( 1, sizeof( tr_io_t ) );
    io->tor = tor;
    return io;
}


void
tr_ioSync( tr_io_t * io )
{
    if( io != NULL )
    {
        int i;
        const tr_info_t * info = &io->tor->info;

        for( i=0; i<info->fileCount; ++i )
            tr_fdFileClose( io->tor->destination, info->files[i].name );
    }
}

void
tr_ioClose( tr_io_t * io )
{
    if( io != NULL )
    {
        tr_ioSync( io );
        tr_free( io );
    }
}

int
tr_ioHash( tr_io_t * io, int pieceIndex )
{
    int i;
    int ret;
    tr_torrent_t * tor = io->tor;
    const int success = !checkPiece( tor, pieceIndex );

    if( success )
    {
        tr_dbg( "Piece %d hash OK", pieceIndex );
        tr_cpPieceAdd( tor->completion, pieceIndex );
        ret = TR_OK;
    }
    else
    {
        tr_err( "Piece %d hash FAILED", pieceIndex );
        tr_cpPieceRem( tor->completion, pieceIndex );
        ret = TR_ERROR;
    }

    /* Assign blame or credit to peers */
    for( i=0; i<tor->peerCount; ++i )
        tr_peerBlame( tor->peers[i], pieceIndex, success );

    return ret;
}
