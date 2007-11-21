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
#include "crypto.h"
#include "fastresume.h"
#include "fdlimit.h"
#include "inout.h"
#include "list.h"
#include "platform.h"
#include "peer-mgr.h"
#include "utils.h"

/****
*****  Low-level IO functions
****/

#ifdef WIN32
#define lseek _lseeki64
#endif

enum { TR_IO_READ, TR_IO_WRITE };

static int
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
    int ret;

    assert( 0<=fileIndex && fileIndex<info->fileCount );
    assert( !file->length || (fileOffset < file->length));
    assert( fileOffset + buflen <= file->length );

    tr_buildPath ( path, sizeof(path), tor->destination, file->name, NULL );

    if( !file->length )
        return 0;
    else if ((ioMode==TR_IO_READ) && stat( path, &sb ) ) /* does file exist? */
        ret = tr_ioErrorFromErrno ();
    else if ((fd = tr_fdFileCheckout ( path, ioMode==TR_IO_WRITE )) < 0)
        ret = fd;
    else if( lseek( fd, (off_t)fileOffset, SEEK_SET ) == ((off_t)-1) )
        ret = TR_ERROR_IO_OTHER;
    else if( func( fd, buf, buflen ) != buflen )
        ret = tr_ioErrorFromErrno( );
    else
        ret = TR_OK;
 
    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return ret;
}

static void
findFileLocation( const tr_torrent * tor,
                  int                pieceIndex,
                  int                pieceOffset,
                  int              * fileIndex,
                  uint64_t         * fileOffset )
{
    const tr_info * info = &tor->info;

    int i;
    uint64_t piecePos = ((uint64_t)pieceIndex * info->pieceSize) + pieceOffset;

    assert( 0<=pieceIndex && pieceIndex < info->pieceCount );
    assert( 0<=tor->info.pieceSize );
    assert( pieceOffset < tr_torPieceCountBytes( tor, pieceIndex ) );
    assert( piecePos < info->totalSize );

    for( i=0; info->files[i].length<=piecePos; ++i )
        piecePos -= info->files[i].length;

    *fileIndex = i;
    *fileOffset = piecePos;

    assert( 0<=*fileIndex && *fileIndex<info->fileCount );
    assert( *fileOffset < info->files[i].length );
}

#ifdef WIN32
static int
ensureMinimumFileSize( const tr_torrent  * tor,
                       int                 fileIndex,
                       uint64_t            minBytes )
{
    int fd;
    int ret;
    struct stat sb;
    const tr_file * file = &tor->info.files[fileIndex];
    char path[MAX_PATH_LENGTH];

    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( minBytes <= file->length );

    tr_buildPath( path, sizeof(path), tor->destination, file->name, NULL );

    fd = tr_fdFileCheckout( path, TRUE );
    if( fd < 0 ) /* bad fd */
        ret = fd;
    else if (fstat (fd, &sb) ) /* how big is the file? */
        ret = tr_ioErrorFromErrno ();
    else if (sb.st_size >= (off_t)minBytes) /* already big enough */
        ret = TR_OK;
    else if (!ftruncate( fd, minBytes )) /* grow it */
        ret = TR_OK;
    else /* couldn't grow it */
        ret = tr_ioErrorFromErrno ();

    if( fd >= 0 )
        tr_fdFileReturn( fd );

    return ret;
}
#endif

static int
readOrWritePiece( tr_torrent  * tor,
                  int           ioMode,
                  int           pieceIndex,
                  int           pieceOffset,
                  uint8_t     * buf,
                  size_t        buflen )
{
    int ret = 0;
    int fileIndex;
    uint64_t fileOffset;
    const tr_info * info = &tor->info;

    assert( 0<=pieceIndex && pieceIndex<tor->info.pieceCount );
    assert( buflen <= (size_t) tr_torPieceCountBytes( tor, pieceIndex ) );

    findFileLocation ( tor, pieceIndex, pieceOffset, &fileIndex, &fileOffset );

    while( buflen && !ret )
    {
        const tr_file * file = &info->files[fileIndex];
        const uint64_t bytesThisPass = MIN( buflen, file->length - fileOffset );

#ifdef WIN32
        if( ioMode == TR_IO_WRITE )
            ret = ensureMinimumFileSize( tor, fileIndex,
                                         fileOffset + bytesThisPass );
        if( !ret )
#endif
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
tr_ioRead( tr_torrent  * tor,
           int           pieceIndex,
           int           begin,
           int           len,
           uint8_t     * buf )
{
    return readOrWritePiece( tor, TR_IO_READ, pieceIndex, begin, buf, len );
}

int
tr_ioWrite( tr_torrent     * tor,
            int              pieceIndex,
            int              begin,
            int              len,
            const uint8_t  * buf )
{
    return readOrWritePiece( tor, TR_IO_WRITE, pieceIndex, begin, (void*)buf, len );
}

/****
*****
****/

static int
tr_ioRecalculateHash( tr_torrent  * tor,
                      int           pieceIndex,
                      uint8_t     * setme )
{
    int n;
    int ret;
    uint8_t * buf;
    const tr_info * info;

    assert( tor != NULL );
    assert( setme != NULL );
    assert( 0<=pieceIndex && pieceIndex<tor->info.pieceCount );

    info = &tor->info;
    n = tr_torPieceCountBytes( tor, pieceIndex );

    buf = tr_new( uint8_t, n );
    ret = tr_ioRead( tor, pieceIndex, 0, n, buf );
    if( !ret )
        tr_sha1( setme, buf, n, NULL );
    tr_free( buf );

    return ret;
}

static int
checkPiece( tr_torrent * tor, int pieceIndex )
{
    uint8_t hash[SHA_DIGEST_LENGTH];
    const int ret = tr_ioRecalculateHash( tor, pieceIndex, hash )
        || memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
    tr_dbg ("torrent [%s] piece %d hash check: %s",
            tor->info.name, pieceIndex, (ret?"FAILED":"OK"));
    return ret;
}

/**
***
**/

int
tr_ioHash( tr_torrent * tor, int pieceIndex )
{
    int ret;
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

    tr_peerMgrSetBlame( tor->handle->peerMgr, tor->info.hash,
                        pieceIndex, success );

    return ret;
}

/**
***
**/

struct recheck_node
{
    tr_torrent * torrent;
    tr_recheck_done_cb recheck_done_cb;
};

static void
fireCheckDone( tr_torrent          * torrent,
               tr_recheck_done_cb    recheck_done_cb )
{
    if( recheck_done_cb != NULL )
        (*recheck_done_cb)( torrent );
}

static struct recheck_node currentNode;

static tr_list * recheckList = NULL;

static tr_thread * recheckThread = NULL;

static int stopCurrent = FALSE;

static tr_lock* getRecheckLock( void )
{
    static tr_lock * lock = NULL;
    if( lock == NULL )
        lock = tr_lockNew( );
    return lock;
}

static void
recheckThreadFunc( void * unused UNUSED )
{
    for( ;; )
    {
        int i;
        tr_torrent * tor;
        struct recheck_node * node;

        tr_lockLock( getRecheckLock( ) );
        stopCurrent = FALSE;
        node = (struct recheck_node*) recheckList ? recheckList->data : NULL;
        if( node == NULL ) {
            currentNode.torrent = NULL;
            break;
        }

        currentNode = *node;
        tor = currentNode.torrent;
        tr_list_remove_data( &recheckList, node );
        tr_free( node );
        tr_lockUnlock( getRecheckLock( ) );

        if( tor->uncheckedPieces == NULL ) {
            tor->recheckState = TR_RECHECK_NONE;
            fireCheckDone( tor, currentNode.recheck_done_cb );
            continue;
        }

        tor->recheckState = TR_RECHECK_NOW;

        /* remove the unchecked pieces from completion... */
        for( i=0; i<tor->info.pieceCount; ++i ) 
            if( tr_bitfieldHas( tor->uncheckedPieces, i ) )
                tr_cpPieceRem( tor->completion, i );

        tr_inf( "Verifying some pieces of \"%s\"", tor->info.name );

        for( i=0; i<tor->info.pieceCount && !stopCurrent; ++i ) 
        {
            if( !tr_bitfieldHas( tor->uncheckedPieces, i ) )
                continue;

            tr_torrentSetHasPiece( tor, i, !checkPiece( tor, i ) );
            tr_bitfieldRem( tor->uncheckedPieces, i );
        }

        tor->recheckState = TR_RECHECK_NONE;

        if( !stopCurrent )
        {
            tr_bitfieldFree( tor->uncheckedPieces );
            tor->uncheckedPieces = NULL;
            tr_fastResumeSave( tor );
            fireCheckDone( tor, currentNode.recheck_done_cb );
        }
    }

    recheckThread = NULL;
    tr_lockUnlock( getRecheckLock( ) );
}

void
tr_ioRecheckAdd( tr_torrent          * tor,
                 tr_recheck_done_cb    recheck_done_cb )
{
    if( !tr_bitfieldCountTrueBits( tor->uncheckedPieces ) )
    {
        /* doesn't need to be checked... */
        recheck_done_cb( tor );
    }
    else
    {
        struct recheck_node * node;

        node = tr_new( struct recheck_node, 1 );
        node->torrent = tor;
        node->recheck_done_cb = recheck_done_cb;

        tr_lockLock( getRecheckLock( ) );
        tor->recheckState = recheckList ? TR_RECHECK_WAIT : TR_RECHECK_NOW;
        tr_list_append( &recheckList, node );
        if( recheckThread == NULL )
            recheckThread = tr_threadNew( recheckThreadFunc, NULL, "recheckThreadFunc" );
        tr_lockUnlock( getRecheckLock( ) );
    }
}

static int
compareRecheckByTorrent( const void * va, const void * vb )
{
    const struct recheck_node * a = va;
    const tr_torrent * b = vb;
    return a->torrent - b;
}

void
tr_ioRecheckRemove( tr_torrent * tor )
{
    tr_lock * lock = getRecheckLock( );
    tr_lockLock( lock );

    if( tor == currentNode.torrent )
    {
        stopCurrent = TRUE;
        while( stopCurrent )
        {
            tr_lockUnlock( lock );
            tr_wait( 100 );
            tr_lockLock( lock );
        }
    }
    else
    {
        tr_free( tr_list_remove( &recheckList, tor, compareRecheckByTorrent ) );
        tor->recheckState = TR_RECHECK_NONE;
    }

    tr_lockUnlock( lock );
}
