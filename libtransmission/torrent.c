/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "crypto.h" /* for tr_sha1 */
#include "fastresume.h"
#include "fdlimit.h" /* tr_fdFileClose */
#include "handshake.h"
#include "inout.h"
#include "metainfo.h"
#include "net.h" /* tr_netNtop */
#include "peer-mgr.h"
#include "platform.h"
#include "ratecontrol.h"
#include "shared.h"
#include "torrent.h"
#include "tracker.h"
#include "trcompat.h" /* for strlcpy */
#include "trevent.h"
#include "utils.h"

/***
****
***/

int
tr_torrentExists( tr_handle       * handle,
                  const uint8_t   * torrentHash )
{
    return tr_torrentFindFromHash( handle, torrentHash ) != NULL;
}

tr_torrent*
tr_torrentFindFromHash( tr_handle      * handle,
                        const uint8_t  * torrentHash )
{
    tr_torrent * tor;

    for( tor = handle->torrentList; tor; tor = tor->next )
        if( !memcmp( tor->info.hash, torrentHash, SHA_DIGEST_LENGTH ) )
            return tor;

    return NULL;
}

tr_torrent*
tr_torrentFindFromObfuscatedHash( tr_handle      * handle,
                                  const uint8_t  * obfuscatedTorrentHash )
{
    tr_torrent * tor;

    for( tor = handle->torrentList; tor; tor = tor->next )
        if( !memcmp( tor->obfuscatedHash, obfuscatedTorrentHash, SHA_DIGEST_LENGTH ) )
            return tor;

    return NULL;
}

/***
****  LOCKS
***/

void
tr_torrentLock( const tr_torrent * tor )
{
    tr_globalLock( tor->handle );
}

void
tr_torrentUnlock( const tr_torrent * tor )
{
    tr_globalUnlock( tor->handle );
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void
tr_torrentSetSpeedMode( tr_torrent   * tor,
                        int            up_or_down,
                        tr_speedlimit  mode )
{
    tr_speedlimit * limit = up_or_down==TR_UP
        ? &tor->uploadLimitMode
        : &tor->downloadLimitMode;
    *limit = mode;
}

tr_speedlimit
tr_torrentGetSpeedMode( const tr_torrent * tor,
                        int                up_or_down)
{
    return up_or_down==TR_UP ? tor->uploadLimitMode
                             : tor->downloadLimitMode;
}

void
tr_torrentSetSpeedLimit( tr_torrent   * tor,
                         int            up_or_down,
                         int            single_KiB_sec )
{
    tr_ratecontrol * rc = up_or_down==TR_UP ? tor->upload : tor->download;
    tr_rcSetLimit( rc, single_KiB_sec );
}

int
tr_torrentGetSpeedLimit( const tr_torrent  * tor,
                         int                 up_or_down )
{
    tr_ratecontrol * rc = up_or_down==TR_UP ? tor->upload : tor->download;
    return tr_rcGetLimit( rc );
}

/***
****
***/

static void
onTrackerResponse( void * tracker UNUSED, void * vevent, void * user_data )
{
    tr_torrent * tor = user_data;
    tr_tracker_event * event = vevent;

    switch( event->messageType )
    {
        case TR_TRACKER_PEERS:
            tr_peerMgrAddPeers( tor->handle->peerMgr,
                                tor->info.hash,
                                TR_PEER_FROM_TRACKER,
                                event->peerCompact,
                                event->peerCount );
            break;

        case TR_TRACKER_WARNING:
            tr_err( "Tracker: Warning - %s", event->text );
            tor->error = TR_ERROR_TC_WARNING;
            strlcpy( tor->errorString, event->text, sizeof(tor->errorString) );
            break;

        case TR_TRACKER_ERROR:
            tr_err( "Tracker: Error - %s", event->text );
            tor->error = TR_ERROR_TC_ERROR;
            strlcpy( tor->errorString, event->text, sizeof(tor->errorString) );
            break;

        case TR_TRACKER_ERROR_CLEAR:
            tor->error = 0;
            tor->errorString[0] = '\0';
            break;
    }
}

/***
****
****  TORRENT INSTANTIATION
****
***/

static int
getBytePiece( const tr_info * info, uint64_t byteOffset )
{
    assert( info != NULL );
    assert( info->pieceSize != 0 );

    return byteOffset / info->pieceSize;
}

static void
initFilePieces ( tr_info * info, int fileIndex )
{
    tr_file * file = &info->files[fileIndex];
    uint64_t firstByte, lastByte;

    assert( info != NULL );
    assert( 0<=fileIndex && fileIndex<info->fileCount );

    file = &info->files[fileIndex];
    firstByte = file->offset;
    lastByte = firstByte + (file->length ? file->length-1 : 0);
    file->firstPiece = getBytePiece( info, firstByte );
    file->lastPiece = getBytePiece( info, lastByte );
}

static int
pieceHasFile( int piece, const tr_file * file )
{
    return ( file->firstPiece <= piece ) && ( piece <= file->lastPiece );
}

static tr_priority_t
calculatePiecePriority ( const tr_torrent * tor,
                         int                piece,
                         int                fileHint )
{
    int i;
    int priority = TR_PRI_LOW;

    /* find the first file that has data in this piece */
    if( fileHint >= 0 ) {
        i = fileHint;
        while( i>0 && pieceHasFile( piece, &tor->info.files[i-1] ) )
            --i;
    } else {
        for( i=0; i<tor->info.fileCount; ++i )
            if( pieceHasFile( piece, &tor->info.files[i] ) )
                break;
    }

    /* the piece's priority is the max of the priorities
     * of all the files in that piece */
    for( ; i<tor->info.fileCount; ++i )
    {
        const tr_file * file = &tor->info.files[i];

        if( !pieceHasFile( piece, file ) )
            break;

        priority = MAX( priority, file->priority );

        /* when dealing with multimedia files, getting the first and
           last pieces can sometimes allow you to preview it a bit
           before it's fully downloaded... */
        if ( file->priority >= TR_PRI_NORMAL )
            if ( file->firstPiece == piece || file->lastPiece == piece )
                priority = TR_PRI_HIGH;
    }

    return priority;
}

static void
tr_torrentInitFilePieces( tr_torrent * tor )
{
    int i;
    uint64_t offset = 0;

    assert( tor != NULL );

    for( i=0; i<tor->info.fileCount; ++i ) {
      tor->info.files[i].offset = offset;
      offset += tor->info.files[i].length;
      initFilePieces( &tor->info, i );
    }

    for( i=0; i<tor->info.pieceCount; ++i )
        tor->info.pieces[i].priority = calculatePiecePriority( tor, i, -1 );
}

static void
torrentRealInit( tr_handle     * h,
                 tr_torrent    * tor,
                 const tr_ctor * ctor )
{
    int doStart;
    uint64_t loaded;
    uint64_t t;
    tr_info * info = &tor->info;
   
    tr_globalLock( h );

    tor->handle   = h;

    /**
     * Decide on a block size.  constraints:
     * (1) most clients decline requests over 16 KiB
     * (2) pieceSize must be a multiple of block size
     */
    tor->blockSize = info->pieceSize;
    while( tor->blockSize > (1024*16) )
        tor->blockSize /= 2;

    tor->lastPieceSize = info->totalSize % info->pieceSize;

    if( !tor->lastPieceSize )
         tor->lastPieceSize = info->pieceSize;

    tor->lastBlockSize = info->totalSize % tor->blockSize;

    if( !tor->lastBlockSize )
         tor->lastBlockSize = tor->blockSize;

    tor->blockCount =
        ( info->totalSize + tor->blockSize - 1 ) / tor->blockSize;

    tor->blockCountInPiece =
        info->pieceSize / tor->blockSize;

    tor->blockCountInLastPiece =
        ( tor->lastPieceSize + tor->blockSize - 1 ) / tor->blockSize;

    /* check our work */
    assert( ( info->pieceSize % tor->blockSize ) == 0 );
    t = info->pieceCount - 1;
    t *= info->pieceSize;
    t += tor->lastPieceSize;
    assert( t == info->totalSize );
    t = tor->blockCount - 1;
    t *= tor->blockSize;
    t += tor->lastBlockSize;
    assert( t == info->totalSize );
    t = info->pieceCount - 1;
    t *= tor->blockCountInPiece;
    t += tor->blockCountInLastPiece;
    assert( t == (uint64_t)tor->blockCount );

    tor->completion = tr_cpInit( tor );

    tr_torrentInitFilePieces( tor );

    tor->upload         = tr_rcInit();
    tor->download       = tr_rcInit();
    tor->swarmspeed     = tr_rcInit();

    tr_sha1( tor->obfuscatedHash, "req2", 4,
                                  info->hash, SHA_DIGEST_LENGTH,
                                  NULL );

    tr_peerMgrAddTorrent( h->peerMgr, tor );

    if( !h->isPortSet )
        tr_setBindPort( h, TR_DEFAULT_PORT );

    assert( !tor->downloadedCur );
    assert( !tor->uploadedCur );

    tor->error   = TR_OK;

    tor->checkedPieces = tr_bitfieldNew( tor->info.pieceCount );
    tr_torrentUncheck( tor );
    loaded = tr_fastResumeLoad( tor, ~0, ctor );
    
    doStart = tor->isRunning;
    tor->isRunning = 0;

    if( !(loaded & TR_FR_SPEEDLIMIT ) ) {
        int limit, enabled;
        tr_getGlobalSpeedLimit( tor->handle, TR_UP, &enabled, &limit );
        tr_torrentSetSpeedLimit( tor, TR_UP, limit );
        tr_getGlobalSpeedLimit( tor->handle, TR_DOWN, &enabled, &limit );
        tr_torrentSetSpeedLimit( tor, TR_DOWN, limit );
    }

    tor->cpStatus = tr_cpGetStatus( tor->completion );

    tor->tracker = tr_trackerNew( tor );
    tor->trackerSubscription = tr_trackerSubscribe( tor->tracker, onTrackerResponse, tor );

    tor->next = h->torrentList;
    h->torrentList = tor;
    h->torrentCount++;

    tr_globalUnlock( h );

    /* maybe save our own copy of the metainfo */
    if( tr_ctorGetSave( ctor ) ) {
        const benc_val_t * val;
        if( !tr_ctorGetMetainfo( ctor, &val ) ) {
            int len;
            uint8_t * text = (uint8_t*) tr_bencSave( val, &len );
            tr_metainfoSave( tor->info.hashString,
                             tor->handle->tag,
                             text, len );
            tr_free( text );
        }
    }

    if( doStart )
        tr_torrentStart( tor );
}

static int
hashExists( const tr_handle   * h,
            const uint8_t     * hash )
{
    const tr_torrent * tor;

    for( tor=h->torrentList; tor; tor=tor->next )
        if( !memcmp( hash, tor->info.hash, SHA_DIGEST_LENGTH ) )
            return TRUE;

    return FALSE;
}

int
tr_torrentParse( const tr_handle  * handle,
                 const tr_ctor    * ctor,
                 tr_info          * setmeInfo )
{
    int err = 0;
    int doFree;
    tr_info tmp;
    const benc_val_t * metainfo;

    if( setmeInfo == NULL )
        setmeInfo = &tmp;
    memset( setmeInfo, 0, sizeof( tr_info ) );

    if( !err && tr_ctorGetMetainfo( ctor, &metainfo ) )
        return TR_EINVALID;

    err = tr_metainfoParse( setmeInfo, metainfo, handle->tag );
    doFree = !err && ( setmeInfo == &tmp );

    if( !err && hashExists( handle, setmeInfo->hash ) )
        err = TR_EDUPLICATE;

    if( doFree )
        tr_metainfoFree( setmeInfo );

    return err;
}

tr_torrent *
tr_torrentNew( tr_handle      * handle,
               const tr_ctor  * ctor,
               int            * setmeError )
{
    int err;
    tr_info tmpInfo;
    tr_torrent * tor = NULL;

    err = tr_torrentParse( handle, ctor, &tmpInfo );
    if( !err ) {
        tor = tr_new0( tr_torrent, 1 );
        tor->info = tmpInfo;
        torrentRealInit( handle, tor, ctor );
    } else if( setmeError ) {
        *setmeError = err;
    }

    return tor;
}

/***
****
***/

static void
saveFastResumeNow( tr_torrent * tor )
{
    tr_fastResumeSave( tor );
}

/**
***
**/

void
tr_torrentSetFolder( tr_torrent * tor, const char * path )
{
    tr_free( tor->destination );
    tor->destination = tr_strdup( path );
    saveFastResumeNow( tor );
}

const char*
tr_torrentGetFolder( const tr_torrent * tor )
{
    return tor->destination;
}

void
tr_torrentChangeMyPort( tr_torrent * tor )
{
    if( tor->tracker )
        tr_trackerChangeMyPort( tor->tracker );
}

int
tr_torrentIsPrivate( const tr_torrent * tor )
{
    return tor
        && tor->info.isPrivate;
}

int
tr_torrentAllowsPex( const tr_torrent * tor )
{
    return tor
        && tor->handle->isPexEnabled
        && !tr_torrentIsPrivate( tor );
}

static void
tr_manualUpdateImpl( void * vtor )
{
    tr_torrent * tor = vtor;
    if( tor->isRunning )
        tr_trackerReannounce( tor->tracker );
}
void
tr_manualUpdate( tr_torrent * tor )
{
    tr_runInEventThread( tor->handle, tr_manualUpdateImpl, tor );
}
int
tr_torrentCanManualUpdate( const tr_torrent * tor )
{
    return ( tor != NULL )
        && ( tor->isRunning )
        && ( tr_trackerCanManualAnnounce( tor->tracker ) );
}

/* rcRate's averaging code can make it appear that we're
 * still sending bytes after a torrent stops or all the
 * peers disconnect, so short-circuit that appearance here */
void
tr_torrentGetRates( const tr_torrent * tor,
                    float            * toClient,
                    float            * toPeer)
{
    const int showSpeed = tor->isRunning
        && tr_peerMgrHasConnections( tor->handle->peerMgr, tor->info.hash );

    if( toClient )
        *toClient = showSpeed ? tr_rcRate( tor->download ) : 0.0;
    if( toPeer )
        *toPeer = showSpeed ? tr_rcRate( tor->upload ) : 0.0;
}
const tr_info *
tr_torrentInfo( const tr_torrent * tor )
{
    return &tor->info;
}

const tr_stat *
tr_torrentStatCached( tr_torrent * tor )
{
    const time_t now = time( NULL );

    return now == tor->lastStatTime
        ? &tor->stats[tor->statCur]
        : tr_torrentStat( tor );
}

const tr_stat *
tr_torrentStat( tr_torrent * tor )
{
    tr_stat * s;
    struct tr_tracker * tc;

    tr_torrentLock( tor );

    tor->lastStatTime = time( NULL );
    tr_torrentRecheckCompleteness( tor );

    tor->statCur = !tor->statCur;
    s = &tor->stats[tor->statCur];

    s->error  = tor->error;
    memcpy( s->errorString, tor->errorString,
            sizeof( s->errorString ) );

    tc = tor->tracker;
    s->tracker = tr_trackerGetAddress( tor->tracker );

    tr_peerMgrTorrentStats( tor->handle->peerMgr,
                            tor->info.hash,
                            &s->peersKnown,
                            &s->peersConnected,
                            &s->peersSendingToUs,
                            &s->peersGettingFromUs,
                             s->peersFrom );

    s->percentComplete = tr_cpPercentComplete ( tor->completion );

    s->percentDone = tr_cpPercentDone( tor->completion );
    s->leftUntilDone = tr_cpLeftUntilDone( tor->completion );

    if( tor->recheckState == TR_RECHECK_NOW )
        s->status = TR_STATUS_CHECK;
    else if( tor->recheckState == TR_RECHECK_WAIT )
        s->status = TR_STATUS_CHECK_WAIT;
    else if( !tor->isRunning )
        s->status = TR_STATUS_STOPPED;
    else if( tor->cpStatus == TR_CP_INCOMPLETE )
        s->status = TR_STATUS_DOWNLOAD;
    else if( tor->cpStatus == TR_CP_DONE )
        s->status = TR_STATUS_DONE;
    else
        s->status = TR_STATUS_SEED;

    s->recheckProgress =
        1.0 - (tr_torrentCountUncheckedPieces( tor ) / (double) tor->info.pieceCount);

    tr_torrentGetRates( tor, &s->rateDownload, &s->rateUpload );
   
    tr_trackerGetCounts( tc,
                         &s->completedFromTracker,
                         &s->leechers, 
                         &s->seeders );

    s->swarmspeed = tr_rcRate( tor->swarmspeed );
    
    s->startDate = tor->startDate;
    s->activityDate = tor->activityDate;

    s->eta = s->rateDownload < 0.1
        ? -1.0f
        : (s->leftUntilDone / s->rateDownload / 1024.0);

    s->corruptEver     = tor->corruptCur    + tor->corruptPrev;
    s->downloadedEver  = tor->downloadedCur + tor->downloadedPrev;
    s->uploadedEver    = tor->uploadedCur   + tor->uploadedPrev;
    s->haveValid       = tr_cpHaveValid( tor->completion );
    s->haveUnchecked   = tr_cpHaveTotal( tor->completion ) - s->haveValid;


    {
        int i;
        tr_bitfield * availablePieces = tr_peerMgrGetAvailable( tor->handle->peerMgr,
                                                                tor->info.hash );
        s->desiredSize = 0;
        s->desiredAvailable = 0;

        for( i=0; i<tor->info.pieceCount; ++i ) {
            if( !tor->info.pieces[i].dnd ) {
                const uint64_t byteCount = tr_torPieceCountBytes( tor, i );
                s->desiredSize += byteCount;
                if( tr_bitfieldHas( availablePieces, i ) )
                    s->desiredAvailable += byteCount;
            }
        }

        /* "availablePieces" can miss our unverified blocks... */
        if( s->desiredAvailable < s->haveValid + s->haveUnchecked )
            s->desiredAvailable = s->haveValid + s->haveUnchecked;

        tr_bitfieldFree( availablePieces );
    }
   
    s->ratio = ( s->downloadedEver || s->haveValid )
      ? s->uploadedEver / (float)(MAX(s->downloadedEver,s->haveValid))
      : TR_RATIO_NA;
    
    tr_torrentUnlock( tor );

    return s;
}

/***
****
***/

static uint64_t
fileBytesCompleted ( const tr_torrent * tor, int fileIndex )
{
    const tr_file * file     =  &tor->info.files[fileIndex];
    const uint64_t firstBlock       =  file->offset / tor->blockSize;
    const uint64_t firstBlockOffset =  file->offset % tor->blockSize;
    const uint64_t lastOffset       =  file->length ? (file->length-1) : 0;
    const uint64_t lastBlock        = (file->offset + lastOffset) / tor->blockSize;
    const uint64_t lastBlockOffset  = (file->offset + lastOffset) % tor->blockSize;
    uint64_t haveBytes = 0;

    assert( tor != NULL );
    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( file->offset + file->length <= tor->info.totalSize );
    assert( ( (int)firstBlock < tor->blockCount ) || (!file->length && file->offset==tor->info.totalSize) );
    assert( ( (int)lastBlock < tor->blockCount ) || (!file->length && file->offset==tor->info.totalSize) );
    assert( firstBlock <= lastBlock );
    assert( (int)tr_torBlockPiece( tor, firstBlock ) == file->firstPiece );
    assert( (int)tr_torBlockPiece( tor, lastBlock ) == file->lastPiece );

    if( firstBlock == lastBlock )
    {
        if( tr_cpBlockIsComplete( tor->completion, firstBlock ) )
            haveBytes += lastBlockOffset + 1 - firstBlockOffset;
    }
    else
    {
        uint64_t i;

        if( tr_cpBlockIsComplete( tor->completion, firstBlock ) )
            haveBytes += tor->blockSize - firstBlockOffset;

        for( i=firstBlock+1; i<lastBlock; ++i )
            if( tr_cpBlockIsComplete( tor->completion, i ) )
               haveBytes += tor->blockSize;

        if( tr_cpBlockIsComplete( tor->completion, lastBlock ) )
            haveBytes += lastBlockOffset + 1;
    }

    return haveBytes;
}

tr_file_stat *
tr_torrentFiles( const tr_torrent * tor, int * fileCount )
{
    int i;
    const int n = tor->info.fileCount;
    tr_file_stat * files = tr_new0( tr_file_stat, n );
    tr_file_stat * walk = files;

    for( i=0; i<n; ++i, ++walk )
    {
        const tr_file * file = tor->info.files + i;

        walk->bytesCompleted = fileBytesCompleted( tor, i );

        walk->progress = file->length
            ? walk->bytesCompleted / (float)file->length
            : 1.0;
    }

    *fileCount = n;

    return files;
}

void
tr_torrentFilesFree( tr_file_stat * files, int fileCount UNUSED )
{
    tr_free( files );
}

/***
****
***/

tr_peer_stat *
tr_torrentPeers( const tr_torrent * tor, int * peerCount )
{
    tr_peer_stat * ret = NULL;

    if( tor != NULL )
        ret = tr_peerMgrPeerStats( tor->handle->peerMgr,
                                   tor->info.hash, peerCount );

    return ret;
}

void
tr_torrentPeersFree( tr_peer_stat * peers, int peerCount UNUSED )
{
    tr_free( peers );
}

void tr_torrentAvailability( const tr_torrent * tor, int8_t * tab, int size )
{
    return tr_peerMgrTorrentAvailability( tor->handle->peerMgr,
                                          tor->info.hash,
                                          tab, size );
}

void tr_torrentAmountFinished( const tr_torrent * tor, float * tab, int size )
{
    int i;
    float interval;
    tr_torrentLock( tor );

    interval = (float)tor->info.pieceCount / (float)size;
    for( i = 0; i < size; i++ )
    {
        int piece = i * interval;
        tab[i] = tr_cpPercentBlocksInPiece( tor->completion, piece );
    }

    tr_torrentUnlock( tor );
}

void
tr_torrentResetTransferStats( tr_torrent * tor )
{
    tr_torrentLock( tor );

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;
    tor->corruptPrev    += tor->corruptCur;
    tor->corruptCur      = 0;

    tr_torrentUnlock( tor );
}


void
tr_torrentSetHasPiece( tr_torrent * tor, int pieceIndex, int has )
{
    tr_torrentLock( tor );

    assert( tor != NULL );
    assert( pieceIndex >= 0 );
    assert( pieceIndex < tor->info.pieceCount );

    if( has )
        tr_cpPieceAdd( tor->completion, pieceIndex );
    else
        tr_cpPieceRem( tor->completion, pieceIndex );

    tr_torrentUnlock( tor );
}

void tr_torrentRemoveSaved( tr_torrent * tor )
{
    tr_metainfoRemoveSaved( tor->info.hashString, tor->handle->tag );
}

/***
****
***/

static void
freeTorrent( tr_torrent * tor )
{
    tr_torrent * t;
    tr_handle * h = tor->handle;
    tr_info * inf = &tor->info;

    assert( tor != NULL );
    assert( !tor->isRunning );

    tr_globalLock( h );

    tr_peerMgrRemoveTorrent( h->peerMgr, tor->info.hash );

    tr_cpClose( tor->completion );

    tr_rcClose( tor->upload );
    tr_rcClose( tor->download );
    tr_rcClose( tor->swarmspeed );

    tr_trackerUnsubscribe( tor->tracker, tor->trackerSubscription );
    tr_trackerFree( tor->tracker );
    tor->tracker = NULL;

    tr_bitfieldFree( tor->checkedPieces );

    tr_free( tor->destination );

    if( tor == h->torrentList )
        h->torrentList = tor->next;
    else for( t=h->torrentList; t!=NULL; t=t->next ) {
        if( t->next == tor ) {
            t->next = tor->next;
            break;
        }
    }

    assert( h->torrentCount >= 1 );
    h->torrentCount--;

    tr_inf( "closing torrent %s... %d torrents left",
            tor->info.name, h->torrentCount );

    tr_metainfoFree( inf );
    tr_free( tor );

    tr_globalUnlock( h );
}

enum
{
    AFTER_RECHECK_NONE,
    AFTER_RECHECK_START,
    AFTER_RECHECK_STOP,
    AFTER_RECHECK_CLOSE
};

static void
checkAndStartImpl( void * vtor )
{
    tr_torrent * tor = vtor;

    tr_globalLock( tor->handle );

    tor->isRunning  = 1;
    *tor->errorString = '\0';
    tr_torrentResetTransferStats( tor );
    tor->cpStatus = tr_cpGetStatus( tor->completion );
    saveFastResumeNow( tor );
    tor->startDate = tr_date( );
    tr_trackerStart( tor->tracker );
    tr_peerMgrStartTorrent( tor->handle->peerMgr, tor->info.hash );

    tr_globalUnlock( tor->handle );
}

static void
checkAndStartCB( tr_torrent * tor )
{
    tr_runInEventThread( tor->handle, checkAndStartImpl, tor );
}

void
tr_torrentStart( tr_torrent * tor )
{
    tr_globalLock( tor->handle );

    if( !tor->isRunning )
    {
        tr_fastResumeLoad( tor, TR_FR_PROGRESS, NULL );
        tor->isRunning = 1;
        tr_ioRecheckAdd( tor, checkAndStartCB );
    }

    tr_globalUnlock( tor->handle );
}

static void
torrentRecheckDoneImpl( void * vtor )
{
    tr_torrentRecheckCompleteness( vtor );
}
static void
torrentRecheckDoneCB( tr_torrent * tor )
{
    tr_runInEventThread( tor->handle, torrentRecheckDoneImpl, tor );
}
void
tr_torrentRecheck( tr_torrent * tor )
{
    tr_globalLock( tor->handle );

    tr_torrentUncheck( tor );
    tr_ioRecheckAdd( tor, torrentRecheckDoneCB );

    tr_globalUnlock( tor->handle );
}


static void
stopTorrent( void * vtor )
{
    int i;

    tr_torrent * tor = vtor;
    tr_ioRecheckRemove( tor );
    tr_peerMgrStopTorrent( tor->handle->peerMgr, tor->info.hash );
    tr_trackerStop( tor->tracker );

    for( i=0; i<tor->info.fileCount; ++i )
    {
        char path[MAX_PATH_LENGTH];
        const tr_file * file = &tor->info.files[i];
        tr_buildPath( path, sizeof(path), tor->destination, file->name, NULL );
        tr_fdFileClose( path );
    }
}

void
tr_torrentStop( tr_torrent * tor )
{
    tr_globalLock( tor->handle );

    saveFastResumeNow( tor );
    tor->isRunning = 0;
    tr_runInEventThread( tor->handle, stopTorrent, tor );

    tr_globalUnlock( tor->handle );
}

static void
closeTorrent( void * vtor )
{
    tr_torrent * tor = vtor;
    saveFastResumeNow( tor );
    tor->isRunning = 0;
    stopTorrent( tor );
    freeTorrent( tor );
}

void
tr_torrentClose( tr_torrent * tor )
{
    tr_globalLock( tor->handle );

    tr_torrentClearStatusCallback( tor );
    tr_runInEventThread( tor->handle, closeTorrent, tor );

    tr_globalUnlock( tor->handle );
}

/**
***  Completeness
**/

static void
fireStatusChange( tr_torrent * tor, cp_status_t status )
{
    assert( tor != NULL );
    assert( status==TR_CP_INCOMPLETE || status==TR_CP_DONE || status==TR_CP_COMPLETE );

    if( tor->status_func != NULL )
        (tor->status_func)( tor, status, tor->status_func_user_data );
}

void
tr_torrentSetStatusCallback( tr_torrent             * tor,
                             tr_torrent_status_func   func,
                             void                   * user_data )
{
    assert( tor != NULL );
    tor->status_func = func;
    tor->status_func_user_data = user_data;
}

void
tr_torrentClearStatusCallback( tr_torrent * torrent )
{
    tr_torrentSetStatusCallback( torrent, NULL, NULL );
}

void
tr_torrentRecheckCompleteness( tr_torrent * tor )
{
    cp_status_t cpStatus;

    tr_torrentLock( tor );

    cpStatus = tr_cpGetStatus( tor->completion );
    if( cpStatus != tor->cpStatus ) {
        tor->cpStatus = cpStatus;
        fireStatusChange( tor, cpStatus );
        if( (cpStatus == TR_CP_COMPLETE) /* ...and if we're complete */
            && tor->downloadedCur ) {        /* and it just happened */
            tr_trackerCompleted( tor->tracker ); /* tell the tracker */
        }
        saveFastResumeNow( tor );
    }
    tr_torrentUnlock( tor );
}

int
tr_torrentIsSeed( const tr_torrent * tor )
{
    return tor->cpStatus==TR_CP_COMPLETE || tor->cpStatus==TR_CP_DONE;
}

/**
***  File priorities
**/

void
tr_torrentInitFilePriority( tr_torrent   * tor,
                            int            fileIndex,
                            tr_priority_t  priority )
{
    int i;
    tr_file * file;

    assert( tor != NULL );
    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( priority==TR_PRI_LOW || priority==TR_PRI_NORMAL || priority==TR_PRI_HIGH );

    file = &tor->info.files[fileIndex];
    file->priority = priority;
    for( i=file->firstPiece; i<=file->lastPiece; ++i )
      tor->info.pieces[i].priority = calculatePiecePriority( tor, i, fileIndex );
}

void
tr_torrentSetFilePriorities( tr_torrent     * tor,
                             int            * files,
                             int              fileCount,
                             tr_priority_t    priority )
{
    int i;
    tr_torrentLock( tor );

    for( i=0; i<fileCount; ++i )
        tr_torrentInitFilePriority( tor, files[i], priority );

    saveFastResumeNow( tor );
    tr_torrentUnlock( tor );
}

tr_priority_t
tr_torrentGetFilePriority( const tr_torrent *  tor, int file )
{
    tr_priority_t ret;

    tr_torrentLock( tor );
    assert( tor != NULL );
    assert( 0<=file && file<tor->info.fileCount );
    ret = tor->info.files[file].priority;
    tr_torrentUnlock( tor );

    return ret;
}

tr_priority_t*
tr_torrentGetFilePriorities( const tr_torrent * tor )
{
    int i;
    tr_priority_t * p;

    tr_torrentLock( tor );
    p = tr_new0( tr_priority_t, tor->info.fileCount );
    for( i=0; i<tor->info.fileCount; ++i )
        p[i] = tor->info.files[i].priority;
    tr_torrentUnlock( tor );

    return p;
}

/**
***  File DND
**/

int
tr_torrentGetFileDL( const tr_torrent * tor,
                     int                file )
{
    int doDownload;
    tr_torrentLock( tor );

    assert( 0<=file && file<tor->info.fileCount );
    doDownload = !tor->info.files[file].dnd;

    tr_torrentUnlock( tor );
    return doDownload != 0;
}

static void
setFileDND( tr_torrent  * tor,
            int           fileIndex,
            int           doDownload )
{
    tr_file * file;
    const int dnd = !doDownload;
    int firstPiece, firstPieceDND;
    int lastPiece, lastPieceDND;
    int i;

    file = &tor->info.files[fileIndex];
    file->dnd = dnd;
    firstPiece = file->firstPiece;
    lastPiece = file->lastPiece;

    /* can't set the first piece to DND unless
       every file using that piece is DND */
    firstPieceDND = dnd;
    for( i=fileIndex-1; firstPieceDND && i>=0; --i ) {
        if( tor->info.files[i].lastPiece != firstPiece )
            break;
        firstPieceDND = tor->info.files[i].dnd;
    }

    /* can't set the last piece to DND unless
       every file using that piece is DND */
    lastPieceDND = dnd;
    for( i=fileIndex+1; lastPieceDND && i<tor->info.fileCount; ++i ) {
        if( tor->info.files[i].firstPiece != lastPiece )
            break;
        lastPieceDND = tor->info.files[i].dnd;
    }

    if( firstPiece == lastPiece )
    {
        tor->info.pieces[firstPiece].dnd = firstPieceDND && lastPieceDND;
    }
    else
    {
        tor->info.pieces[firstPiece].dnd = firstPieceDND;
        tor->info.pieces[lastPiece].dnd = lastPieceDND;
        for( i=firstPiece+1; i<lastPiece; ++i )
            tor->info.pieces[i].dnd = dnd;
    }
}

void
tr_torrentInitFileDLs ( tr_torrent   * tor,
                        int          * files,
                        int            fileCount,
                        int            doDownload )
{
    int i;
    tr_torrentLock( tor );

    for( i=0; i<fileCount; ++i )
        setFileDND( tor, files[i], doDownload );
    tr_cpInvalidateDND ( tor->completion );

    tr_torrentUnlock( tor );
}

void
tr_torrentSetFileDLs ( tr_torrent  * tor,
                       int         * files,
                       int           fileCount,
                       int           doDownload )
{
    tr_torrentLock( tor );
    tr_torrentInitFileDLs( tor, files, fileCount, doDownload );
    saveFastResumeNow( tor );
    tr_torrentUnlock( tor );
}

/***
****
***/

void
tr_torrentSetMaxConnectedPeers( tr_torrent  * tor,
                                uint16_t      maxConnectedPeers )
{
    tor->maxConnectedPeers = maxConnectedPeers;
}

void
tr_torrentSetMaxUnchokedPeers( tr_torrent  * tor,
                               uint8_t       maxUnchokedPeers )
{
    tor->maxUnchokedPeers = maxUnchokedPeers;
}

uint16_t
tr_torrentGetMaxConnectedPeers( const tr_torrent  * tor )
{
    return tor->maxConnectedPeers;
}

uint8_t
tr_torrentGetMaxUnchokedPeers( const tr_torrent  * tor )
{
    return tor->maxUnchokedPeers;
}

/***
****
***/

int _tr_block( const tr_torrent * tor, int index, int begin )
{
    const tr_info * inf = &tor->info;
    return index * ( inf->pieceSize / tor->blockSize ) +
        begin / tor->blockSize;
}

uint64_t
tr_pieceOffset( const tr_torrent * tor, int index, int begin, int length )
{
    uint64_t ret;
    ret = tor->info.pieceSize;
    ret *= index;
    ret += begin;
    ret += length;
    return ret;
}

/***
****
***/

int
tr_torrentIsPieceChecked( const tr_torrent * tor, int piece )
{
    return tr_bitfieldHas( tor->checkedPieces, piece );
}

void
tr_torrentSetPieceChecked( tr_torrent * tor, int piece, int isChecked )
{
    if( isChecked )
        tr_bitfieldAdd( tor->checkedPieces, piece );
    else
        tr_bitfieldRem( tor->checkedPieces, piece );
}

void
tr_torrentSetFileChecked( tr_torrent * tor, int fileIndex, int isChecked )
{
    const tr_file * file = &tor->info.files[fileIndex];
    const size_t begin = file->firstPiece;
    const size_t end = file->lastPiece + 1;

    if( isChecked )
        tr_bitfieldAddRange ( tor->checkedPieces, begin, end );
    else
        tr_bitfieldRemRange ( tor->checkedPieces, begin, end );
}

void
tr_torrentUncheck( tr_torrent * tor )
{
    tr_bitfieldRemRange ( tor->checkedPieces, 0, tor->info.pieceCount );
}

int
tr_torrentCountUncheckedPieces( const tr_torrent * tor )
{
    return tor->info.pieceCount - tr_bitfieldCountTrueBits( tor->checkedPieces );
}
