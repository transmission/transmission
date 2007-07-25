/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2007 Transmission authors and contributors
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "transmission.h"
#include "fastresume.h"
#include "trcompat.h" /* for strlcpy */
#include "metainfo.h"
#include "net.h" /* tr_netNtop */
#include "shared.h"

/***
****  LOCKS
***/

void
tr_torrentReaderLock( const tr_torrent_t * tor )
{
    tr_rwReaderLock ( (tr_rwlock_t*)&tor->lock );
}

void
tr_torrentReaderUnlock( const tr_torrent_t * tor )
{
    tr_rwReaderUnlock ( (tr_rwlock_t*)&tor->lock );
}

void
tr_torrentWriterLock( tr_torrent_t * tor )
{
    tr_rwWriterLock ( &tor->lock );
}

void
tr_torrentWriterUnlock( tr_torrent_t * tor )
{
    tr_rwWriterUnlock ( &tor->lock );
}

/***
****  PER-TORRENT UL / DL SPEEDS
***/

void
tr_torrentSetSpeedMode( tr_torrent_t     * tor,
                        int                up_or_down,
                        tr_speedlimit_t    mode )
{
    tr_speedlimit_t * limit = up_or_down==TR_UP
        ? &tor->uploadLimitMode
        : &tor->downloadLimitMode;
    *limit = mode;
}

tr_speedlimit_t
tr_torrentGetSpeedMode( const tr_torrent_t * tor,
                        int                  up_or_down)
{
    return up_or_down==TR_UP ? tor->uploadLimitMode
                             : tor->downloadLimitMode;
}

void
tr_torrentSetSpeedLimit( tr_torrent_t   * tor,
                         int              up_or_down,
                         int              single_KiB_sec )
{
    tr_ratecontrol_t * rc = up_or_down==TR_UP ? tor->upload : tor->download;
    tr_rcSetLimit( rc, single_KiB_sec );
}

int
tr_torrentGetSpeedLimit( const tr_torrent_t  * tor,
                         int                   up_or_down )
{
    tr_ratecontrol_t * rc = up_or_down==TR_UP ? tor->upload : tor->download;
    return tr_rcGetLimit( rc );
}

/***
****
****  TORRENT INSTANTIATION
****
***/

static int
getBytePiece( const tr_info_t * info, uint64_t byteOffset )
{
    assert( info != NULL );
    assert( info->pieceSize != 0 );

    return byteOffset / info->pieceSize;
}

static void
initFilePieces ( tr_info_t * info, int fileIndex )
{
    tr_file_t * file = &info->files[fileIndex];
    uint64_t firstByte, lastByte;

    assert( info != NULL );
    assert( 0<=fileIndex && fileIndex<info->fileCount );

    file = &info->files[fileIndex];
    firstByte = file->offset;
    lastByte = firstByte + (file->length ? file->length-1 : 0);
    file->firstPiece = getBytePiece( info, firstByte );
    file->lastPiece = getBytePiece( info, lastByte );
    tr_dbg( "file #%d is in pieces [%d...%d] (%s)", fileIndex, file->firstPiece, file->lastPiece, file->name );
}

static tr_priority_t
calculatePiecePriority ( const tr_torrent_t * tor,
                         int                  piece )
{
    int i;
    tr_priority_t priority = TR_PRI_NORMAL;

    for( i=0; i<tor->info.fileCount; ++i )
    {
        const tr_file_t * file = &tor->info.files[i];
        if ( file->firstPiece <= piece
          && file->lastPiece  >= piece
          && file->priority   >  priority)
              priority = file->priority;
    }

    return priority;
}

static void
tr_torrentInitFilePieces( tr_torrent_t * tor )
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
    {
        tr_piece_t * piece = tor->info.pieces + i;
        uint64_t lastByte;
        int lastBlock;
        if( i == (tor->info.pieceCount-1))
            lastByte = tor->info.totalSize - 1;
        else {
            lastByte = i + 1;
            lastByte *= tor->info.pieceSize;
            --lastByte;
        }
        lastBlock = lastByte / tor->blockSize;

        piece->priority = calculatePiecePriority( tor, i );
        piece->firstBlock = i * (tor->info.pieceSize / tor->blockSize);
        piece->blockCount = lastBlock + 1 - piece->firstBlock;

        assert( 0 <= piece->firstBlock );
        assert( piece->firstBlock <= lastBlock );
        assert( lastBlock < tor->blockCount );
        assert( piece->firstBlock + piece->blockCount <= tor->blockCount );
    }
}

static void torrentThreadLoop( void * );

static void
torrentRealInit( tr_handle_t   * h,
                 tr_torrent_t  * tor,
                 const char    * destination,
                 int             flags )
{
    int i;
    uint64_t loaded;
    char name[512];
    tr_bitfield_t * uncheckedPieces;
    
    tor->info.flags |= flags;

    tr_sharedLock( h->shared );

    tor->destination = tr_strdup( destination );

    tor->handle   = h;
    tor->key      = h->key;
    tor->azId     = h->azId;
    tor->hasChangedState = -1;
    
    /* Escaped info hash for HTTP queries */
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        snprintf( &tor->escapedHashString[3*i],
                  sizeof( tor->escapedHashString ) - 3 * i,
                  "%%%02x", tor->info.hash[i] );
    }

    tor->pexDisabled = 0;

    /* Block size: usually 16 ko, or less if we have to */
    tor->blockSize  = MIN( tor->info.pieceSize, 1 << 14 );
    tor->blockCount = ( tor->info.totalSize + tor->blockSize - 1 ) /
                        tor->blockSize;
    tor->completion = tr_cpInit( tor );

    tr_torrentInitFilePieces( tor );

    tor->thread = THREAD_EMPTY;
    tr_rwInit( &tor->lock );

    tor->upload         = tr_rcInit();
    tor->download       = tr_rcInit();
    tor->swarmspeed     = tr_rcInit();
 
    /* We have a new torrent */
    tor->publicPort = tr_sharedGetPublicPort( h->shared );

    tr_sharedUnlock( h->shared );

    if( !h->isPortSet )
        tr_setBindPort( h, TR_DEFAULT_PORT );

    assert( !tor->downloadedCur );
    assert( !tor->uploadedCur );

    tor->error   = TR_OK;
    tor->runStatus = flags & TR_FLAG_PAUSED ? TR_RUN_STOPPED : TR_RUN_RUNNING;

    uncheckedPieces = tr_bitfieldNew( tor->info.pieceCount );
    loaded = tr_fastResumeLoad( tor, uncheckedPieces );
    if( tr_bitfieldIsEmpty( uncheckedPieces ) )
        tr_bitfieldFree( uncheckedPieces );
    else
        tor->uncheckedPieces = uncheckedPieces;


    if( !(loaded & TR_FR_SPEEDLIMIT ) ) {
        int limit, enabled;
        tr_getGlobalSpeedLimit( tor->handle, TR_UP, &enabled, &limit );
        tr_torrentSetSpeedLimit( tor, TR_UP, limit );
        tr_getGlobalSpeedLimit( tor->handle, TR_DOWN, &enabled, &limit );
        tr_torrentSetSpeedLimit( tor, TR_DOWN, limit );
    }

    tor->cpStatus = tr_cpGetStatus( tor->completion );

    tr_sharedLock( h->shared );
    tor->next = h->torrentList;
    h->torrentList = tor;
    h->torrentCount++;
    tr_sharedUnlock( h->shared );

    snprintf( name, sizeof( name ), "torrent %p (%s)", tor, tor->info.name );
    tr_threadCreate( &tor->thread, torrentThreadLoop, tor, name );
}

static int
pathIsInUse ( const tr_handle_t   * h,
              const char          * destination,
              const char          * name )
{
    const tr_torrent_t * tor;
    
    for( tor=h->torrentList; tor; tor=tor->next )
        if( !strcmp( destination, tor->destination )
         && !strcmp( name, tor->info.name ) )
            return TRUE;

    return FALSE;
}

static int
hashExists( const tr_handle_t   * h,
            const uint8_t       * hash )
{
    const tr_torrent_t * tor;

    for( tor=h->torrentList; tor; tor=tor->next )
        if( !memcmp( hash, tor->info.hash, SHA_DIGEST_LENGTH ) )
            return TRUE;

    return FALSE;
}

static int
infoCanAdd( const tr_handle_t   * h,
            const char          * destination,
            const tr_info_t     * info )
{
    if( hashExists( h, info->hash ) )
        return TR_EDUPLICATE;

    if( destination && pathIsInUse( h, destination, info->name ) )
        return TR_EDUPLICATE;

    return TR_OK;
}

int
tr_torrentParse( const tr_handle_t  * h,
                 const char         * path,
                 const char         * destination,
                 tr_info_t          * setme_info )
{
    int ret;
    tr_info_t tmp;

    if( setme_info == NULL )
        setme_info = &tmp;

    memset( setme_info, 0, sizeof( tr_info_t ) );
    ret = tr_metainfoParseFile( setme_info, h->tag, path, FALSE );

    if( ret == TR_OK )
        ret = infoCanAdd( h, destination, setme_info );

    if( setme_info == &tmp )
        tr_metainfoFree( &tmp );

    return ret;
}
 
tr_torrent_t *
tr_torrentInit( tr_handle_t   * h,
                const char    * path,
                const char    * destination,
                int             flags,
                int           * error )
{
    int val;
    tr_torrent_t * tor = NULL;

    if(( val = tr_torrentParse( h, path, destination, NULL )))
        *error = val;
    else if(!(( tor = tr_new0( tr_torrent_t, 1 ))))
        *error = TR_EOTHER;
    else {
        tr_metainfoParseFile( &tor->info, h->tag, path, TR_FLAG_SAVE & flags );
        torrentRealInit( h, tor, destination, flags );
    }

    return tor;
}

int
tr_torrentParseHash( const tr_handle_t  * h,
                     const char         * hashStr,
                     const char         * destination,
                     tr_info_t          * setme_info )
{
    int ret;
    tr_info_t tmp;

    if( setme_info == NULL )
        setme_info = &tmp;

    memset( setme_info, 0, sizeof( tr_info_t ) );
    ret = tr_metainfoParseHash( setme_info, h->tag, hashStr );

    if( ret == TR_OK )
        ret = infoCanAdd( h, destination, setme_info );

    if( setme_info == &tmp )
        tr_metainfoFree( &tmp );

    return ret;
}


tr_torrent_t *
tr_torrentInitSaved( tr_handle_t    * h,
                     const char     * hashStr,
                     const char     * destination,
                     int              flags,
                     int            * error )
{
    int val;
    tr_torrent_t * tor = NULL;

    if(( val = tr_torrentParseHash( h, hashStr, destination, NULL )))
        *error = val;
    else if(!(( tor = tr_new0( tr_torrent_t, 1 ))))
        *error = TR_EOTHER;
    else {
        tr_metainfoParseHash( &tor->info, h->tag, hashStr );
        torrentRealInit( h, tor, destination, (TR_FLAG_SAVE|flags) );
    }

    return tor;
}

static int
tr_torrentParseData( const tr_handle_t  * h,
                     const uint8_t      * data,
                     size_t               size,
                     const char         * destination,
                     tr_info_t          * setme_info )
{
    int ret;
    tr_info_t tmp;

    if( setme_info == NULL )
        setme_info = &tmp;

    memset( setme_info, 0, sizeof( tr_info_t ) );
    ret = tr_metainfoParseData( setme_info, h->tag, data, size, FALSE );

    if( ret == TR_OK )
        ret = infoCanAdd( h, destination, setme_info );

    if( setme_info == &tmp )
        tr_metainfoFree( &tmp );

    return ret;
}

tr_torrent_t *
tr_torrentInitData( tr_handle_t    * h,
                    const uint8_t  * data,
                    size_t           size,
                    const char     * destination,
                    int              flags,
                    int            * error )
{
    int val;
    tr_torrent_t * tor = NULL;

    if(( val = tr_torrentParseData( h, data, size, destination, NULL )))
        *error = val;
    else if(!(( tor = tr_new0( tr_torrent_t, 1 ))))
        *error = TR_EOTHER;
    else {
        tr_metainfoParseData( &tor->info, h->tag, data, size, TR_FLAG_SAVE & flags );
        torrentRealInit( h, tor, destination, flags );
    }

    return tor;
}

const tr_info_t *
tr_torrentInfo( const tr_torrent_t * tor )
{
    return &tor->info;
}

/***
****
***/

#if 0
int tr_torrentScrape( tr_torrent_t * tor, int * s, int * l, int * d )
{
    return tr_trackerScrape( tor, s, l, d );
}
#endif

static void
fastResumeSave( tr_torrent_t * tor )
{
    tr_fastResumeSave( tor );
    tor->fastResumeDirty = FALSE;
}

void
tr_torrentSetFolder( tr_torrent_t * tor, const char * path )
{
    tr_free( tor->destination );
    tor->destination = tr_strdup( path );
    fastResumeSave( tor );
}

const char*
tr_torrentGetFolder( const tr_torrent_t * tor )
{
    return tor->destination;
}


/***********************************************************************
 * torrentReallyStop
 ***********************************************************************
 * Joins the download thread and frees/closes everything related to it.
 **********************************************************************/

void tr_torrentDisablePex( tr_torrent_t * tor, int disable )
{
    tr_torrentWriterLock( tor );

    if( ! ( TR_FLAG_PRIVATE & tor->info.flags ) )
    {
        if( tor->pexDisabled != disable )
        {
            int i;
            tor->pexDisabled = disable;
            for( i=0; i<tor->peerCount; ++i )
                tr_peerSetPrivate( tor->peers[i], disable );
        }
    }

    tr_torrentWriterUnlock( tor );
}

static int tr_didStateChangeTo ( tr_torrent_t * tor, int status )
{
    int ret;

    tr_torrentWriterLock( tor );
    if (( ret = tor->hasChangedState == status ))
        tor->hasChangedState = -1;
    tr_torrentWriterUnlock( tor );

    return ret;
}

int tr_getIncomplete( tr_torrent_t * tor )
{
    return tr_didStateChangeTo( tor, TR_CP_INCOMPLETE );
}
int tr_getDone( tr_torrent_t * tor )
{
    return tr_didStateChangeTo( tor, TR_CP_DONE );
}
int tr_getComplete( tr_torrent_t * tor )
{
    return tr_didStateChangeTo( tor, TR_CP_COMPLETE );
}

void tr_manualUpdate( tr_torrent_t * tor UNUSED )
{
#if 0
    int peerCount, new;
    uint8_t * peerCompact;

    if( tor->status != TR_RUN_RUNNING )
        return;
    
    tr_torrentWriterLock( tor );
    tr_trackerAnnouncePulse( tor->tracker, &peerCount, &peerCompact, 1 );
    new = 0;
    if( peerCount > 0 )
    {
        new = tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                                    peerCompact, peerCount );
        free( peerCompact );
    }
    tr_dbg( "got %i peers from manual announce, used %i", peerCount, new );
    tr_torrentWriterUnlock( tor );
#endif
}

const tr_stat_t *
tr_torrentStat( tr_torrent_t * tor )
{
    tr_stat_t * s;
    tr_tracker_t * tc;
    int i;

    tr_torrentReaderLock( tor );

    tor->statCur = ( tor->statCur + 1 ) % 2;
    s = &tor->stats[tor->statCur];

    s->error  = tor->error;
    memcpy( s->errorString, tor->errorString,
            sizeof( s->errorString ) );

    tc = tor->tracker;
    s->cannotConnect = tr_trackerCannotConnect( tc );
    s->tracker = tc
        ? tr_trackerGet( tc )
        : &tor->info.trackerList[0].list[0];

    /* peers... */
    memset( s->peersFrom, 0, sizeof( s->peersFrom ) );
    s->peersTotal          = tor->peerCount;
    s->peersConnected      = 0;
    s->peersSendingToUs    = 0;
    s->peersGettingFromUs  = 0;

    for( i=0; i<tor->peerCount; ++i )
    {
        const tr_peer_t * peer = tor->peers[i];

        if( tr_peerIsConnected( peer ) )
        {
            ++s->peersConnected;
            ++s->peersFrom[tr_peerIsFrom(peer)];

            if( tr_peerDownloadRate( peer ) > 0.01 )
                ++s->peersSendingToUs;

            if( tr_peerUploadRate( peer ) > 0.01 )
                ++s->peersGettingFromUs;
        }
    }

    s->percentDone     = tr_cpPercentDone     ( tor->completion );
    s->percentComplete = tr_cpPercentComplete ( tor->completion );
    s->left            = tr_cpLeftUntilDone   ( tor->completion );


    if( tor->uncheckedPieces )
        s->status = tor->runStatus==TR_RUN_CHECKING
            ? TR_STATUS_CHECK
            : TR_STATUS_CHECK_WAIT;
    else switch( tor->runStatus ) {
        case TR_RUN_STOPPING: /* fallthrough */
        case TR_RUN_STOPPING_NET_WAIT: s->status = TR_STATUS_STOPPING; break;
        case TR_RUN_STOPPED: s->status = TR_STATUS_STOPPED; break;
        case TR_RUN_CHECKING: s->status = TR_STATUS_CHECK; break;
        case TR_RUN_RUNNING: switch( tor->cpStatus ) {
            case TR_CP_INCOMPLETE: s->status = TR_STATUS_DOWNLOAD; break;
            case TR_CP_DONE: s->status = TR_STATUS_DONE; break;
            case TR_CP_COMPLETE: s->status = TR_STATUS_SEED; break;
        }
    }

    s->recheckProgress = (tor->uncheckedPieces == NULL)
        ? 0.0
        : 1.0 - ((double)tr_bitfieldCountTrueBits(tor->uncheckedPieces) / tor->info.pieceCount);

    s->cpStatus = tor->cpStatus;

    /* tr_rcRate() doesn't make the difference between 'piece'
       messages and other messages, which causes a non-zero
       download rate even tough we are not downloading. So we
       force it to zero not to confuse the user. */
    s->rateDownload = tor->runStatus==TR_RUN_RUNNING 
        ? tr_rcRate( tor->download )
        : 0.0;
    s->rateUpload = tr_rcRate( tor->upload );
    
    s->seeders  = tr_trackerSeeders( tc );
    s->leechers = tr_trackerLeechers( tc );
    s->completedFromTracker = tr_trackerDownloaded( tc );

    s->swarmspeed = tr_rcRate( tor->swarmspeed );
    
    s->startDate = tor->startDate;
    s->activityDate = tor->activityDate;

    s->eta = s->rateDownload < 0.1
        ? -1.0f
        : (s->left / s->rateDownload / 1024.0);

    s->uploaded        = tor->uploadedCur   + tor->uploadedPrev;
    s->downloaded      = tor->downloadedCur + tor->downloadedPrev;
    s->downloadedValid = tr_cpDownloadedValid( tor->completion );
   
    s->ratio = s->downloaded || s->downloadedValid
      ? (float)s->uploaded / (float)MAX(s->downloaded, s->downloadedValid)
      : TR_RATIO_NA; 
    
    tr_torrentReaderUnlock( tor );

    return s;
}

/***
****
***/

static uint64_t
fileBytesCompleted ( const tr_torrent_t * tor, int fileIndex )
{
    const tr_file_t * file     =  &tor->info.files[fileIndex];
    const uint64_t firstBlock       =  file->offset / tor->blockSize;
    const uint64_t firstBlockOffset =  file->offset % tor->blockSize;
    const uint64_t lastOffset       =  file->length ? (file->length-1) : 0;
    const uint64_t lastBlock        = (file->offset + lastOffset) / tor->blockSize;
    const uint64_t lastBlockOffset  = (file->offset + lastOffset) % tor->blockSize;
    uint64_t haveBytes = 0;

    assert( tor != NULL );
    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( file->offset + file->length <= tor->info.totalSize );
    assert( (int)firstBlock < tor->blockCount );
    assert( (int)lastBlock < tor->blockCount );
    assert( firstBlock <= lastBlock );
    assert( TOR_BLOCK_PIECE( tor, firstBlock ) == file->firstPiece );
    assert( TOR_BLOCK_PIECE( tor, lastBlock ) == file->lastPiece );

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

tr_file_stat_t *
tr_torrentFiles( const tr_torrent_t * tor, int * fileCount )
{
    int i;
    const int n = tor->info.fileCount;
    tr_file_stat_t * files = tr_new0( tr_file_stat_t, n );
    tr_file_stat_t * walk = files;

    for( i=0; i<n; ++i, ++walk )
    {
        const tr_file_t * file = tor->info.files + i;
        cp_status_t cp;

        walk->bytesCompleted = fileBytesCompleted( tor, i );

        walk->progress = file->length
            ? walk->bytesCompleted / (float)file->length
            : 1.0;

        if( walk->bytesCompleted >= file->length )
            cp = TR_CP_COMPLETE;
        else if( tor->info.files[i].dnd )
            cp = TR_CP_DONE;
        else
            cp = TR_CP_INCOMPLETE;

        walk->completionStatus = cp;
    }

    *fileCount = n;

    return files;
}

void
tr_torrentFilesFree( tr_file_stat_t * files, int fileCount UNUSED )
{
    tr_free( files );
}

/***
****
***/

tr_peer_stat_t *
tr_torrentPeers( const tr_torrent_t * tor, int * peerCount )
{
    tr_peer_stat_t * peers;

    tr_torrentReaderLock( tor );

    *peerCount = tor->peerCount;
   
    peers = tr_new0( tr_peer_stat_t, tor->peerCount ); 
    if (peers != NULL)
    {
        tr_peer_t * peer;
        struct in_addr * addr;
        int i;
        for( i=0; i<tor->peerCount; ++i )
        {
            peer = tor->peers[i];
            
            addr = tr_peerAddress( peer );
            if( NULL != addr )
            {
                tr_netNtop( addr, peers[i].addr,
                           sizeof( peers[i].addr ) );
            }
            
            peers[i].client           =  tr_peerClient( peer );
            peers[i].isConnected      =  tr_peerIsConnected( peer );
            peers[i].from             =  tr_peerIsFrom( peer );
            peers[i].progress         =  tr_peerProgress( peer );
            peers[i].port             =  tr_peerPort( peer );

            peers[i].uploadToRate     =  tr_peerUploadRate( peer );
            peers[i].downloadFromRate =  tr_peerDownloadRate( peer );
            
            peers[i].isDownloading    =  peers[i].uploadToRate > 0.01;
            peers[i].isUploading      =  peers[i].downloadFromRate > 0.01;
        }
    }
    
    tr_torrentReaderUnlock( tor );
    
    return peers;
}

void tr_torrentPeersFree( tr_peer_stat_t * peers, int peerCount UNUSED )
{
    tr_free( peers );
}

void tr_torrentAvailability( const tr_torrent_t * tor, int8_t * tab, int size )
{
    int i, j, piece;
    float interval;

    tr_torrentReaderLock( tor );

    interval = (float)tor->info.pieceCount / (float)size;
    for( i = 0; i < size; i++ )
    {
        piece = i * interval;

        if( tr_cpPieceIsComplete( tor->completion, piece ) )
        {
            tab[i] = -1;
            continue;
        }

        tab[i] = 0;
        for( j = 0; j < tor->peerCount; j++ )
        {
            if( tr_peerHasPiece( tor->peers[j], piece ) )
            {
                (tab[i])++;
            }
        }
    }

    tr_torrentReaderUnlock( tor );
}

void tr_torrentAmountFinished( const tr_torrent_t * tor, float * tab, int size )
{
    int i;
    float interval;
    tr_torrentReaderLock( tor );

    interval = (float)tor->info.pieceCount / (float)size;
    for( i = 0; i < size; i++ )
    {
        int piece = i * interval;
        tab[i] = tr_cpPercentBlocksInPiece( tor->completion, piece );
    }

    tr_torrentReaderUnlock( tor );
}

void
tr_torrentResetTransferStats( tr_torrent_t * tor )
{
    tr_torrentWriterLock( tor );

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;

    tr_torrentWriterUnlock( tor );
}


void
tr_torrentSetHasPiece( tr_torrent_t * tor, int pieceIndex, int has )
{
    tr_torrentWriterLock( tor );

    if( has )
        tr_cpPieceAdd( tor->completion, pieceIndex );
    else
        tr_cpPieceRem( tor->completion, pieceIndex );

    tr_torrentWriterUnlock( tor );
}

void tr_torrentRemoveSaved( tr_torrent_t * tor )
{
    tr_metainfoRemoveSaved( tor->info.hashString, tor->handle->tag );
}

void tr_torrentRecheck( tr_torrent_t * tor )
{
    if( !tor->uncheckedPieces )
        tor->uncheckedPieces = tr_bitfieldNew( tor->info.pieceCount );
    tr_bitfieldAddRange( tor->uncheckedPieces, 0, tor->info.pieceCount );
}


int tr_torrentAttachPeer( tr_torrent_t * tor, tr_peer_t * peer )
{
    int i;
    tr_peer_t * otherPeer;

    assert( tor != NULL );
    assert( peer != NULL );

    if( tor->peerCount >= TR_MAX_PEER_COUNT )
    {
        tr_peerDestroy(  peer );
        return 0;
    }

    /* Don't accept two connections from the same IP */
    for( i = 0; i < tor->peerCount; i++ )
    {
        otherPeer = tor->peers[i];
        if( !memcmp( tr_peerAddress( peer ), tr_peerAddress( otherPeer ), 4 ) )
        {
            tr_peerDestroy(  peer );
            return 0;
        }
    }

    tr_peerSetPrivate( peer, tor->info.flags & TR_FLAG_PRIVATE ||
                       tor->pexDisabled );
    tr_peerSetTorrent( peer, tor );
    tor->peers[tor->peerCount++] = peer;

    return 1;
}

int tr_torrentAddCompact( tr_torrent_t * tor, int from,
                           uint8_t * buf, int count )
{
    struct in_addr addr;
    in_port_t port;
    int i, added;
    tr_peer_t * peer;

    added = 0;
    for( i = 0; i < count; i++ )
    {
        memcpy( &addr, buf, 4 ); buf += 4;
        memcpy( &port, buf, 2 ); buf += 2;

        peer = tr_peerInit( &addr, port, -1, from );
        added += tr_torrentAttachPeer( tor, peer );
    }

    return added;
}

/***
****
***/

static void setRunState( tr_torrent_t * tor, run_status_t run )
{
    tr_torrentWriterLock( tor );
    tor->runStatus = run;
    tr_torrentWriterUnlock( tor );
}

void tr_torrentStart( tr_torrent_t * tor )
{
    setRunState( tor, TR_RUN_RUNNING );
}

void tr_torrentStop( tr_torrent_t * tor )
{
    if((tor->runStatus != TR_RUN_STOPPING) && (tor->runStatus != TR_RUN_STOPPED) )
        setRunState( tor, TR_RUN_STOPPING );
}

void tr_torrentClose( tr_torrent_t * tor )
{
    tr_torrentStop( tor );
    tor->dieFlag = TRUE;
}

static void
tr_torrentFree( tr_torrent_t * tor )
{
    tr_torrent_t * t;
    tr_handle_t * h = tor->handle;
    tr_info_t * inf = &tor->info;

    tr_sharedLock( h->shared );

    tr_rwClose( &tor->lock );
    tr_cpClose( tor->completion );

    tr_rcClose( tor->upload );
    tr_rcClose( tor->download );
    tr_rcClose( tor->swarmspeed );

    tr_free( tor->destination );

    tr_metainfoFree( inf );

    if( tor == h->torrentList )
        h->torrentList = tor->next;
    else for( t=h->torrentList; t!=NULL; t=t->next ) {
        if( t->next == tor ) {
            t->next = tor->next;
            break;
        }
    }

    h->torrentCount--;

    tr_inf( "closing torrent %s... %d torrents left",
            tor->info.name, h->torrentCount );

    tr_free( tor );

    tr_sharedUnlock( h->shared );
}

static void
recheckCpState( tr_torrent_t * tor )
{
    cp_status_t cpStatus;

    tr_torrentWriterLock( tor );

    cpStatus = tr_cpGetStatus( tor->completion );
    if( cpStatus != tor->cpStatus ) {
        tor->cpStatus = cpStatus;
        tor->hasChangedState = tor->cpStatus;  /* tell the client... */
        if( (cpStatus == TR_CP_COMPLETE) /* ...and if we're complete */
            && tor->tracker!=NULL           /* and we have a tracker */
            && tor->downloadedCur ) {        /* and it just happened */
            tr_trackerCompleted( tor->tracker ); /* tell the tracker */
        }
        tr_ioSync( tor->io );
        fastResumeSave( tor );
    }
    tr_torrentWriterUnlock( tor );
}

static void
torrentThreadLoop ( void * _tor )
{
    static tr_lock_t checkFilesLock;
    static int checkFilesLockInited = FALSE;
    tr_torrent_t * tor = _tor;

    /* create the check-files mutex */
    if( !checkFilesLockInited ) {
         checkFilesLockInited = TRUE;
         tr_lockInit( &checkFilesLock );
    }

    /* loop until the torrent is being deleted */
    while( ! ( tor->dieFlag && (tor->runStatus == TR_RUN_STOPPED) ) )
    {
        /* sleep a little while */
        tr_wait( tor->runStatus == TR_RUN_STOPPED ? 1600 : 600 );

        if( tor->fastResumeDirty )
        {
            fastResumeSave( tor );
            recheckCpState( tor );
        }

        /* if we're stopping... */
        if( tor->runStatus == TR_RUN_STOPPING )
        {
            int i;
            int peerCount;
            uint8_t * peerCompact;
            tr_torrentWriterLock( tor );

            /* close the IO */
            tr_ioClose( tor->io );
            tor->io = NULL;
            fastResumeSave( tor );

            /* close the peers */
            for( i=0; i<tor->peerCount; ++i )
                tr_peerDestroy( tor->peers[i] );
            tor->peerCount = 0;

            /* resest the transfer rates */
            tr_rcReset( tor->download );
            tr_rcReset( tor->upload );
            tr_rcReset( tor->swarmspeed );

            /* tell the tracker we're stopping */
            tr_trackerStopped( tor->tracker );
            tr_trackerPulse( tor->tracker, &peerCount, &peerCompact );
            tor->runStatus = TR_RUN_STOPPING_NET_WAIT;
            tor->stopDate = tr_date();
            tr_torrentWriterUnlock( tor );
        }

        if( tor->runStatus == TR_RUN_STOPPING_NET_WAIT )
        {
            uint64_t date;
            int peerCount;
            uint8_t * peerCompact;
            tr_trackerPulse( tor->tracker, &peerCount, &peerCompact );

            /* have we finished telling the tracker that we're stopping? */
            date = tr_trackerLastResponseDate( tor->tracker );
            if( date > tor->stopDate )
            {
                tr_torrentWriterLock( tor );
                tr_trackerClose( tor->tracker );
                tor->tracker = NULL;
                tor->runStatus = TR_RUN_STOPPED;
                tr_torrentWriterUnlock( tor );
            }
            continue;
        }

        /* do we need to check files? */
        if( tor->uncheckedPieces )
        {
            if( !tr_lockTryLock( &checkFilesLock ) )
            {
                run_status_t realStatus;

                tr_torrentWriterLock( tor );
                realStatus = tor->runStatus;
                tor->runStatus = TR_RUN_CHECKING;
                tr_torrentWriterUnlock( tor );

                tr_ioCheckFiles( tor );
                setRunState( tor, realStatus );

                tr_torrentWriterLock( tor );
                tor->cpStatus = tr_cpGetStatus( tor->completion );
                tr_torrentWriterUnlock( tor );

                tr_lockUnlock( &checkFilesLock );
            }
            continue;
        }

        /* if we're paused or stopped, not much to do... */
        if( tor->runStatus == TR_RUN_STOPPED )
            continue;

        /* ping our peers if we're running... */
        if( tor->runStatus == TR_RUN_RUNNING )
        {
            int i;
            int peerCount;
            uint8_t * peerCompact;

            /* starting to run... */
            if( tor->io == NULL ) {
                *tor->errorString = '\0';
                tr_torrentResetTransferStats( tor );
                tor->io = tr_ioNew( tor );
                tr_peerIdNew ( tor->peer_id, sizeof(tor->peer_id) );
                tor->tracker = tr_trackerInit( tor );
                tor->startDate = tr_date();
            }

            /* refresh our completion state */
            recheckCpState( tor );

            /* ping the tracker... */
            tr_trackerPulse( tor->tracker, &peerCount, &peerCompact );
            if( peerCount > 0 ) {
                int used = tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                                                 peerCompact, peerCount );
                tr_dbg( "got %i peers from announce, used %i", peerCount, used );
                free( peerCompact );
            }

            /* Shuffle peers */
            if ( tor->peerCount > 1 ) {
                tr_peer_t * tmp = tor->peers[0];
                memmove( tor->peers, tor->peers+1,
                        (tor->peerCount-1) * sizeof(void*) );
                tor->peers[tor->peerCount - 1] = tmp;
            }

            /* receive/send messages */
            tr_torrentWriterLock( tor );
            for( i=0; i<tor->peerCount; ) {
                tr_peer_t * peer = tor->peers[i];
                int ret = tr_peerPulse( peer );
                if( ret & TR_ERROR_IO_MASK ) {
                    tr_err( "Fatal error, stopping download (%d)", ret );
                    tor->runStatus = TR_RUN_STOPPING;
                    tor->error = ret;
                    strlcpy( tor->errorString,
                             tr_errorString(ret),
                             sizeof(tor->errorString) );
                    break;
                }
                if( ret ) {
                    tr_peerDestroy( peer );
                    tor->peerCount--;
                    memmove( &tor->peers[i], &tor->peers[i+1],
                             (tor->peerCount-i)*sizeof(void*) );
                    continue;
                }
                i++;
            }
            tr_torrentWriterUnlock( tor );
        }
    }

    tr_ioClose( tor->io );
    tr_torrentFree( tor );
}


/**
***  File priorities
**/

void
tr_torrentSetFilePriority( tr_torrent_t   * tor,
                           int              fileIndex,
                           tr_priority_t    priority )
{
    int i;
    tr_file_t * file;

    tr_torrentWriterLock( tor );

    assert( tor != NULL );
    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( priority==TR_PRI_LOW || priority==TR_PRI_NORMAL || priority==TR_PRI_HIGH );

    file = &tor->info.files[fileIndex];
    file->priority = priority;
    for( i=file->firstPiece; i<=file->lastPiece; ++i )
      tor->info.pieces[i].priority = calculatePiecePriority( tor, i );

    tr_dbg ( "Setting file #%d (pieces %d-%d) priority to %d (%s)",
             fileIndex, file->firstPiece, file->lastPiece,
             priority, tor->info.files[fileIndex].name );

    tor->fastResumeDirty = TRUE;

    tr_torrentWriterUnlock( tor );
}

void
tr_torrentSetFilePriorities( tr_torrent_t        * tor,
                             int                 * files,
                             int                   fileCount,
                             tr_priority_t         priority )
{
    int i;
    for( i=0; i<fileCount; ++i )
        tr_torrentSetFilePriority( tor, files[i], priority );
}

tr_priority_t
tr_torrentGetFilePriority( const tr_torrent_t *  tor, int file )
{
    tr_priority_t ret;

    tr_torrentReaderLock( tor );
    assert( tor != NULL );
    assert( 0<=file && file<tor->info.fileCount );
    ret = tor->info.files[file].priority;
    tr_torrentReaderUnlock( tor );

    return ret;
}


tr_priority_t*
tr_torrentGetFilePriorities( const tr_torrent_t * tor )
{
    int i;
    tr_priority_t * p;

    tr_torrentReaderLock( tor );
    p = tr_new0( tr_priority_t, tor->info.fileCount );
    for( i=0; i<tor->info.fileCount; ++i )
        p[i] = tor->info.files[i].priority;
    tr_torrentReaderUnlock( tor );

    return p;
}

/**
***  File DND
**/

int
tr_torrentGetFileDL( const tr_torrent_t * tor,
                     int                  file )
{
    int doDownload;
    tr_torrentReaderLock( tor );

    assert( 0<=file && file<tor->info.fileCount );
    doDownload = !tor->info.files[file].dnd;

    tr_torrentReaderUnlock( tor );
    return doDownload != 0;
}

void
tr_torrentSetFileDL( tr_torrent_t  * tor,
                     int             fileIndex,
                     int             doDownload )
{
    tr_file_t * file;
    const int dnd = !doDownload;
    int firstPiece, firstPieceDND;
    int lastPiece, lastPieceDND;
    int i;

    tr_torrentWriterLock( tor );

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

    tor->fastResumeDirty = TRUE;

    tr_torrentWriterUnlock( tor );
}

void
tr_torrentSetFileDLs ( tr_torrent_t   * tor,
                       int            * files,
                       int              fileCount,
                       int              doDownload )
{
    int i;
    for( i=0; i<fileCount; ++i )
        tr_torrentSetFileDL( tor, files[i], doDownload );
}

/***
****
***/

int _tr_blockSize( const tr_torrent_t * tor, int block )
{
    const tr_info_t * inf = &tor->info;
    int dummy;

    if( block != tor->blockCount - 1 ||
        !( dummy = inf->totalSize % tor->blockSize ) )
    {
        return tor->blockSize;
    }

    return dummy;
}

int _tr_pieceSize( const tr_torrent_t * tor, int piece )
{
    const tr_info_t * inf = &tor->info;
    if( piece < inf->pieceCount - 1 ||
        !( inf->totalSize % inf->pieceSize ) )
    {
        return inf->pieceSize;
    }
    return inf->totalSize % inf->pieceSize;
}

int _tr_block( const tr_torrent_t * tor, int index, int begin )
{
    const tr_info_t * inf = &tor->info;
    return index * ( inf->pieceSize / tor->blockSize ) +
        begin / tor->blockSize;
}
