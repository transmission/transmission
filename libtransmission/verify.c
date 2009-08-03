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

#include <unistd.h> /* S_ISREG */
#include <sys/stat.h>

#ifdef HAVE_POSIX_FADVISE
 #define _XOPEN_SOURCE 600
 #include <fcntl.h> /* posix_fadvise() */
#endif

#include <openssl/sha.h>

#include "transmission.h"
#include "completion.h"
#include "fdlimit.h"
#include "resume.h" /* tr_torrentSaveResume() */
#include "inout.h"
#include "list.h"
#include "platform.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */
#include "verify.h"


/***
****
***/

/* #define STOPWATCH */

static tr_bool
verifyTorrent( tr_torrent * tor, tr_bool * stopFlag )
{
    SHA_CTX sha;
    int fd = -1;
    int64_t filePos = 0;
    tr_bool changed = 0;
    tr_bool hadPiece = 0;
    uint32_t piecePos = 0;
    tr_file_index_t fileIndex = 0;
    tr_piece_index_t pieceIndex = 0;
    const int64_t buflen = tor->info.pieceSize;
    uint8_t * buffer = tr_new( uint8_t, buflen );
#ifdef STOPWATCH
    time_t now = time( NULL );
#endif

    SHA1_Init( &sha );

    while( !*stopFlag && ( pieceIndex < tor->info.pieceCount ) )
    {
        int64_t leftInPiece;
        int64_t leftInFile;
        int64_t bytesThisPass;
        const tr_file * file = &tor->info.files[fileIndex];

        /* if we're starting a new piece... */
        if( piecePos == 0 )
        {
            hadPiece = tr_cpPieceIsComplete( &tor->completion, pieceIndex );
            /* fprintf( stderr, "starting piece %d of %d\n", (int)pieceIndex, (int)tor->info.pieceCount ); */
        }

        /* if we're starting a new file... */
        if( !filePos && (fd<0) )
        {
            char * filename = tr_buildPath( tor->downloadDir, file->name, NULL );
            fd = tr_open_file_for_scanning( filename );
            /* fprintf( stderr, "opening file #%d (%s) -- %d\n", fileIndex, filename, fd ); */
            tr_free( filename );
        }

        /* figure out how much we can read this pass */
        leftInPiece = tr_torPieceCountBytes( tor, pieceIndex ) - piecePos;
        leftInFile = file->length - filePos;
        bytesThisPass = MIN( leftInFile, leftInPiece );
        bytesThisPass = MIN( bytesThisPass, buflen );
        /* fprintf( stderr, "reading this pass: %d\n", (int)bytesThisPass ); */

        /* read a bit */
        if( (fd>=0) && tr_lseek( fd, filePos, SEEK_SET ) != -1 ) {
            const int64_t numRead = read( fd, buffer, bytesThisPass );
            if( numRead == bytesThisPass )
                SHA1_Update( &sha, buffer, numRead );
        }

        /* move our offsets */
        leftInPiece -= bytesThisPass;
        leftInFile -= bytesThisPass;
        piecePos += bytesThisPass;
        filePos += bytesThisPass;

        /* if we're finishing a piece... */
        if( leftInPiece == 0 )
        {
            tr_bool hasPiece;
            uint8_t hash[SHA_DIGEST_LENGTH];

            SHA1_Final( hash, &sha );
            hasPiece = !memcmp( hash, tor->info.pieces[pieceIndex].hash, SHA_DIGEST_LENGTH );
            /* fprintf( stderr, "do the hashes match? %s\n", (hasPiece?"yes":"no") ); */

            if( hasPiece ) {
                tr_torrentSetHasPiece( tor, pieceIndex, TRUE );
                if( !hadPiece )
                    changed = TRUE;
            } else if( hadPiece ) {
                tr_torrentSetHasPiece( tor, pieceIndex, FALSE );
                changed = TRUE;
            }
            tr_torrentSetPieceChecked( tor, pieceIndex, TRUE );
            tor->anyDate = time( NULL );

            SHA1_Init( &sha );
            ++pieceIndex;
            piecePos = 0;
        }

        /* if we're finishing a file... */
        if( leftInFile == 0 )
        {
            /* fprintf( stderr, "closing file\n" ); */
            if( fd >= 0 ) { tr_close_file( fd ); fd = -1; }
            ++fileIndex;
            filePos = 0;
        }
    }

    /* cleanup */
    if( fd >= 0 )
        tr_close_file( fd );
    tr_free( buffer );

#ifdef STOPWATCH
{
    time_t now2 = time( NULL );
    fprintf( stderr, "it took %d seconds to verify %"PRIu64" bytes (%"PRIu64" bytes per second)\n",
             (int)(now2-now), tor->info.totalSize, (uint64_t)(tor->info.totalSize/(1+(now2-now))) );
}
#endif

    return changed;
}

/***
****
***/

struct verify_node
{
    tr_torrent *         torrent;
    tr_verify_done_cb    verify_done_cb;
};

static void
fireCheckDone( tr_torrent * tor, tr_verify_done_cb verify_done_cb )
{
    assert( tr_isTorrent( tor ) );

    if( verify_done_cb )
        verify_done_cb( tor );
}

static struct verify_node currentNode;
static tr_list * verifyList = NULL;
static tr_thread * verifyThread = NULL;
static tr_bool stopCurrent = FALSE;

static tr_lock*
getVerifyLock( void )
{
    static tr_lock * lock = NULL;

    if( lock == NULL )
        lock = tr_lockNew( );
    return lock;
}

static void
verifyThreadFunc( void * unused UNUSED )
{
    for( ;; )
    {
        int                  changed = 0;
        tr_torrent         * tor;
        struct verify_node * node;

        tr_lockLock( getVerifyLock( ) );
        stopCurrent = FALSE;
        node = (struct verify_node*) verifyList ? verifyList->data : NULL;
        if( node == NULL )
        {
            currentNode.torrent = NULL;
            break;
        }

        currentNode = *node;
        tor = currentNode.torrent;
        tr_list_remove_data( &verifyList, node );
        tr_free( node );
        tr_lockUnlock( getVerifyLock( ) );

        tr_torinf( tor, "%s", _( "Verifying torrent" ) );
        tr_torrentSetVerifyState( tor, TR_VERIFY_NOW );
        changed = verifyTorrent( tor, &stopCurrent );
        tr_torrentSetVerifyState( tor, TR_VERIFY_NONE );
        assert( tr_isTorrent( tor ) );

        if( !stopCurrent )
        {
            if( changed )
                tr_torrentSaveResume( tor );
            fireCheckDone( tor, currentNode.verify_done_cb );
        }
    }

    verifyThread = NULL;
    tr_lockUnlock( getVerifyLock( ) );
}

void
tr_verifyAdd( tr_torrent *      tor,
              tr_verify_done_cb verify_done_cb )
{
    const int uncheckedCount = tr_torrentCountUncheckedPieces( tor );

    assert( tr_isTorrent( tor ) );

    if( !uncheckedCount )
    {
        /* doesn't need to be checked... */
        fireCheckDone( tor, verify_done_cb );
    }
    else
    {
        struct verify_node * node;

        tr_torinf( tor, "%s", _( "Queued for verification" ) );

        node = tr_new( struct verify_node, 1 );
        node->torrent = tor;
        node->verify_done_cb = verify_done_cb;

        tr_lockLock( getVerifyLock( ) );
        tr_torrentSetVerifyState( tor, TR_VERIFY_WAIT );
        tr_list_append( &verifyList, node );
        if( verifyThread == NULL )
            verifyThread = tr_threadNew( verifyThreadFunc, NULL );
        tr_lockUnlock( getVerifyLock( ) );
    }
}

static int
compareVerifyByTorrent( const void * va,
                        const void * vb )
{
    const struct verify_node * a = va;
    const tr_torrent *         b = vb;

    return a->torrent - b;
}

tr_bool
tr_verifyInProgress( const tr_torrent * tor )
{
    tr_bool found = FALSE;
    tr_lock * lock = getVerifyLock( );
    tr_lockLock( lock );

    assert( tr_isTorrent( tor ) );

    found = ( tor == currentNode.torrent )
         || ( tr_list_find( verifyList, tor, compareVerifyByTorrent ) != NULL );

    tr_lockUnlock( lock );
    return found;
}

void
tr_verifyRemove( tr_torrent * tor )
{
    tr_lock * lock = getVerifyLock( );
    tr_lockLock( lock );

    assert( tr_isTorrent( tor ) );

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
        tr_free( tr_list_remove( &verifyList, tor, compareVerifyByTorrent ) );
        tr_torrentSetVerifyState( tor, TR_VERIFY_NONE );
    }

    tr_lockUnlock( lock );
}

void
tr_verifyClose( tr_session * session UNUSED )
{
    tr_lockLock( getVerifyLock( ) );

    stopCurrent = TRUE;
    tr_list_free( &verifyList, tr_free );

    tr_lockUnlock( getVerifyLock( ) );
}
