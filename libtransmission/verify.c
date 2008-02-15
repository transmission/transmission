/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: inout.c 4886 2008-02-01 01:54:04Z charles $
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <openssl/sha.h>

#include "transmission.h"
#include "completion.h"
#include "crypto.h"
#include "fastresume.h"
#include "fdlimit.h"
#include "inout.h"
#include "list.h"
#include "platform.h"
#include "peer-mgr.h"
#include "stats.h"
#include "torrent.h"
#include "utils.h"
#include "verify.h"

/**
***
**/

struct verify_node
{
    tr_torrent * torrent;
    tr_verify_done_cb verify_done_cb;
};

static void
fireCheckDone( tr_torrent          * torrent,
               tr_verify_done_cb    verify_done_cb )
{
    if( verify_done_cb != NULL )
        (*verify_done_cb)( torrent );
}

static struct verify_node currentNode;

static tr_list * verifyList = NULL;

static tr_thread * verifyThread = NULL;

static int stopCurrent = FALSE;

static tr_lock* getVerifyLock( void )
{
    static tr_lock * lock = NULL;
    if( lock == NULL )
        lock = tr_lockNew( );
    return lock;
}

static void
checkFile( tr_torrent   * tor,
           int            fileIndex,
           int          * abortFlag )
{
    int i;
    int nofile;
    struct stat sb;
    char path[MAX_PATH_LENGTH];
    const tr_file * file = &tor->info.files[fileIndex];

    tr_buildPath ( path, sizeof(path), tor->destination, file->name, NULL );
    nofile = stat( path, &sb ) || !S_ISREG( sb.st_mode );

    for( i=file->firstPiece; i<=file->lastPiece && i<tor->info.pieceCount && (!*abortFlag); ++i )
    {
        if( nofile )
        {
            tr_torrentSetHasPiece( tor, i, 0 );
        }
        else if( !tr_torrentIsPieceChecked( tor, i ) )
        {
            const int check = tr_ioTestPiece( tor, i );
            tr_torrentSetHasPiece( tor, i, !check );
            tr_torrentSetPieceChecked( tor, i, TRUE );
        }
    }
}

static void
verifyThreadFunc( void * unused UNUSED )
{
    for( ;; )
    {
        int i;
        tr_torrent * tor;
        struct verify_node * node;

        tr_lockLock( getVerifyLock( ) );
        stopCurrent = FALSE;
        node = (struct verify_node*) verifyList ? verifyList->data : NULL;
        if( node == NULL ) {
            currentNode.torrent = NULL;
            break;
        }

        currentNode = *node;
        tor = currentNode.torrent;
        tr_list_remove_data( &verifyList, node );
        tr_free( node );
        tr_lockUnlock( getVerifyLock( ) );

        tor->verifyState = TR_VERIFY_NOW;

        tr_inf( "Verifying some pieces of \"%s\"", tor->info.name );
        for( i=0; i<tor->info.fileCount && !stopCurrent; ++i )
            checkFile( tor, i, &stopCurrent );

        tor->verifyState = TR_VERIFY_NONE;

        if( !stopCurrent )
        {
            tr_fastResumeSave( tor );
            fireCheckDone( tor, currentNode.verify_done_cb );
        }
    }

    verifyThread = NULL;
    tr_lockUnlock( getVerifyLock( ) );
}

void
tr_verifyAdd( tr_torrent          * tor,
              tr_verify_done_cb    verify_done_cb )
{
    const int uncheckedCount = tr_torrentCountUncheckedPieces( tor );

    if( !uncheckedCount )
    {
        /* doesn't need to be checked... */
        verify_done_cb( tor );
    }
    else
    {
        struct verify_node * node;

        tr_inf( "Queueing %s to verify %d local file pieces", tor->info.name, uncheckedCount );

        node = tr_new( struct verify_node, 1 );
        node->torrent = tor;
        node->verify_done_cb = verify_done_cb;

        tr_lockLock( getVerifyLock( ) );
        tor->verifyState = verifyList ? TR_VERIFY_WAIT : TR_VERIFY_NOW;
        tr_list_append( &verifyList, node );
        if( verifyThread == NULL )
            verifyThread = tr_threadNew( verifyThreadFunc, NULL, "verifyThreadFunc" );
        tr_lockUnlock( getVerifyLock( ) );
    }
}

static int
compareVerifyByTorrent( const void * va, const void * vb )
{
    const struct verify_node * a = va;
    const tr_torrent * b = vb;
    return a->torrent - b;
}

void
tr_verifyRemove( tr_torrent * tor )
{
    tr_lock * lock = getVerifyLock( );
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
        tr_free( tr_list_remove( &verifyList, tor, compareVerifyByTorrent ) );
        tor->verifyState = TR_VERIFY_NONE;
    }

    tr_lockUnlock( lock );
}
