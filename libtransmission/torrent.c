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

#include "transmission.h"
#include "shared.h"
#define INTERVAL_MSEC 100

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static tr_torrent_t * torrentRealInit( tr_handle_t *, tr_torrent_t * tor,
                                       uint8_t *, int flags, int * error );
static void torrentReallyStop( tr_torrent_t * );

static void ioInitAdd ( tr_torrent_t * );
static int ioInitRemove ( tr_torrent_t * );
static void downloadLoop( void * );

void tr_setUseCustomUpload( tr_torrent_t * tor, int limit )
{
    tor->customUploadLimit = limit;
}

void tr_setUseCustomDownload( tr_torrent_t * tor, int limit )
{
    tor->customDownloadLimit = limit;
}

void tr_setUploadLimit( tr_torrent_t * tor, int limit )
{
    tr_rcSetLimit( tor->upload, limit );
}

void tr_setDownloadLimit( tr_torrent_t * tor, int limit )
{
    tr_rcSetLimit( tor->download, limit );
}

tr_torrent_t *
tr_torrentInit( tr_handle_t * h, const char * path,
                uint8_t * hash, int flags, int * error )
{
    tr_torrent_t * tor;

    tor  = calloc( 1, sizeof *tor );
    if( NULL == tor )
    {
        *error = TR_EOTHER;
        return NULL;
    }

    /* Parse torrent file */
    if( tr_metainfoParseFile( &tor->info, h->tag, path,
                              TR_FLAG_SAVE & flags ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, hash, flags, error );
}

tr_torrent_t *
tr_torrentInitData( tr_handle_t * h, uint8_t * data, size_t size,
                    uint8_t * hash, int flags, int * error )
{
    tr_torrent_t * tor;

    tor  = calloc( 1, sizeof *tor );
    if( NULL == tor )
    {
        *error = TR_EOTHER;
        return NULL;
    }

    /* Parse torrent file */
    if( tr_metainfoParseData( &tor->info, h->tag, data, size,
                              TR_FLAG_SAVE & flags ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, hash, flags, error );
}

tr_torrent_t *
tr_torrentInitSaved( tr_handle_t * h, const char * hashStr,
                     int flags, int * error )
{
    tr_torrent_t * tor;

    tor  = calloc( 1, sizeof *tor );
    if( NULL == tor )
    {
        *error = TR_EOTHER;
        return NULL;
    }

    /* Parse torrent file */
    if( tr_metainfoParseHash( &tor->info, h->tag, hashStr ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, NULL, ( TR_FLAG_SAVE | flags ), error );
}

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Allocates a tr_torrent_t structure, then relies on tr_metainfoParse
 * to fill it.
 **********************************************************************/
static tr_torrent_t * torrentRealInit( tr_handle_t * h, tr_torrent_t * tor,
                                       uint8_t * hash, int flags, int * error )
{
    tr_torrent_t  * tor_tmp;
    tr_info_t     * inf;
    int             i;
    
    inf         = &tor->info;
    inf->flags |= flags;

    tr_sharedLock( h->shared );

    /* Make sure this torrent is not already open */
    for( tor_tmp = h->torrentList; tor_tmp; tor_tmp = tor_tmp->next )
    {
        if( !memcmp( tor->info.hash, tor_tmp->info.hash,
                     SHA_DIGEST_LENGTH ) )
        {
            if( NULL != hash )
            {
                memcpy( hash, tor->info.hash, SHA_DIGEST_LENGTH );
            }
            *error = TR_EDUPLICATE;
            tr_metainfoFree( &tor->info );
            free( tor );
            tr_sharedUnlock( h->shared );
            return NULL;
        }
    }

    tor->handle   = h;
    tor->status   = TR_STATUS_PAUSE;
    tor->id       = h->id;
    tor->key      = h->key;
    tor->azId     = h->azId;
    tor->hasChangedState = -1;

    /* Escaped info hash for HTTP queries */
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        snprintf( &tor->escapedHashString[3*i],
                  sizeof( tor->escapedHashString ) - 3 * i,
                  "%%%02x", inf->hash[i] );
    }

    tor->pexDisabled = 0;

    /* Block size: usually 16 ko, or less if we have to */
    tor->blockSize  = MIN( inf->pieceSize, 1 << 14 );
    tor->blockCount = ( inf->totalSize + tor->blockSize - 1 ) /
                        tor->blockSize;
    tor->completion = tr_cpInit( tor );

    tor->thread = THREAD_EMPTY;
    tr_lockInit( &tor->lock );
    tr_condInit( &tor->cond );

    tor->upload         = tr_rcInit();
    tor->download       = tr_rcInit();
    tor->swarmspeed     = tr_rcInit();
 
    /* We have a new torrent */
    tor->publicPort = tr_sharedGetPublicPort( h->shared );
    tor->prev       = NULL;
    tor->next       = h->torrentList;
    if( tor->next )
    {
        tor->next->prev = tor;
    }
    h->torrentList = tor;
    (h->torrentCount)++;

    tr_sharedUnlock( h->shared );

    if( !h->isPortSet )
    {
        tr_setBindPort( h, TR_DEFAULT_PORT );
    }

    tr_torrentInitFilePieces( tor );

    return tor;
}

tr_info_t * tr_torrentInfo( tr_torrent_t * tor )
{
    return &tor->info;
}

/***********************************************************************
 * tr_torrentScrape     
 **********************************************************************/
int tr_torrentScrape( tr_torrent_t * tor, int * s, int * l, int * d )
{
    return tr_trackerScrape( tor, s, l, d );
}

void tr_torrentSetFolder( tr_torrent_t * tor, const char * path )
{
    tor->destination = strdup( path );
    if ( !tor->ioLoaded )
    {
        tr_ioLoadResume( tor );
    }
}

char * tr_torrentGetFolder( tr_torrent_t * tor )
{
    return tor->destination;
}

int tr_torrentDuplicateDownload( tr_torrent_t * tor )
{
    tr_torrent_t * current;
    
    /* Check if a torrent with the same name and destination is already active */
    for( current = tor->handle->torrentList; current; current = current->next )
    {
        if( current != tor && current->status != TR_STATUS_PAUSE
            && !strcmp( tor->destination, current->destination )
            && !strcmp( tor->info.name, current->info.name ) )
        {
            return 1;
        }
    }
    return 0;
}

void tr_torrentStart( tr_torrent_t * tor )
{
    /* Join the thread first */
    torrentReallyStop( tor );
    
    /* Don't start if a torrent with the same name and destination is already active */
    if( tr_torrentDuplicateDownload( tor ) )
    {
        tor->error = TR_ERROR_IO_DUP_DOWNLOAD;
        snprintf( tor->errorString, sizeof( tor->errorString ),
                    "%s", tr_errorString( TR_ERROR_IO_DUP_DOWNLOAD ) );
        return;
    }

    tr_lockLock( &tor->lock );

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;

    tor->status  = TR_STATUS_CHECK_WAIT;
    tor->error   = TR_OK;
    tor->tracker = tr_trackerInit( tor );

    tor->startDate = tr_date();
    tor->die = 0;
    tor->thread = THREAD_EMPTY;

    tr_lockUnlock( &tor->lock );

    ioInitAdd ( tor );
}

static void torrentStop( tr_torrent_t * tor )
{
    tr_trackerStopped( tor->tracker );
    tr_rcReset( tor->download );
    tr_rcReset( tor->upload );
    tr_rcReset( tor->swarmspeed );
    tor->status = TR_STATUS_STOPPING;
    tor->stopDate = tr_date();
}

void tr_torrentStop( tr_torrent_t * tor )
{
    tr_lockLock( &tor->lock );
    torrentStop( tor );

    /* Don't return until the files are closed, so the UI can trash
     * them if requested */
    if ( ioInitRemove( tor ) ) /* torrent never got started */
        tor->status = TR_STATUS_STOPPED;
    else
        tr_condWait( &tor->cond, &tor->lock );

    tr_lockUnlock( &tor->lock );
}

/***********************************************************************
 * torrentReallyStop
 ***********************************************************************
 * Joins the download thread and frees/closes everything related to it.
 **********************************************************************/
static void torrentReallyStop( tr_torrent_t * tor )
{
    int i;

    tor->die = 1;
    tr_threadJoin( &tor->thread );

    if( tor->tracker )
    {
       tr_trackerClose( tor->tracker );
       tor->tracker = NULL;
    }

    tr_lockLock( &tor->lock );
    for( i = 0; i < tor->peerCount; i++ )
    {
        tr_peerDestroy( tor->peers[i] );
    }
    tor->peerCount = 0;
    tr_lockUnlock( &tor->lock );
}

void tr_torrentDisablePex( tr_torrent_t * tor, int disable )
{
    int ii;

    if( TR_FLAG_PRIVATE & tor->info.flags )
    {
        return;
    }

    tr_lockLock( &tor->lock );

    if( tor->pexDisabled == disable )
    {
        tr_lockUnlock( &tor->lock );
        return;
    }

    tor->pexDisabled = disable;
    for( ii = 0; ii < tor->peerCount; ii++ )
    {
        tr_peerSetPrivate( tor->peers[ii], disable );
    }

    tr_lockUnlock( &tor->lock );
}

static int tr_didStateChangeTo ( tr_torrent_t * tor, int status )
{
    if( tor->hasChangedState == status )
    {
        tor->hasChangedState = -1;
        return 1;
    }
    return 0;
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

void tr_manualUpdate( tr_torrent_t * tor )
{
    int peerCount, new;
    uint8_t * peerCompact;

    if( !( tor->status & TR_STATUS_ACTIVE ) )
        return;
    
    tr_lockLock( &tor->lock );
    tr_trackerAnnouncePulse( tor->tracker, &peerCount, &peerCompact, 1 );
    new = 0;
    if( peerCount > 0 )
    {
        new = tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                              peerCompact, peerCount );
        free( peerCompact );
    }
    tr_dbg( "got %i peers from manual announce, used %i", peerCount, new );
    tr_lockUnlock( &tor->lock );
}

tr_stat_t * tr_torrentStat( tr_torrent_t * tor )
{
    tr_stat_t * s;
    tr_peer_t * peer;
    tr_info_t * inf = &tor->info;
    tr_tracker_t * tc;
    int i;

    tor->statCur = ( tor->statCur + 1 ) % 2;
    s = &tor->stats[tor->statCur];

    if( ( tor->status & TR_STATUS_STOPPED ) ||
        ( ( tor->status & TR_STATUS_STOPPING ) &&
          tr_date() > tor->stopDate + 60000 ) )
    {
        torrentReallyStop( tor );
        tor->status = TR_STATUS_PAUSE;
    }

    tr_lockLock( &tor->lock );

    s->status = tor->status;
    s->error  = tor->error;
    memcpy( s->errorString, tor->errorString,
            sizeof( s->errorString ) );

    tc = tor->tracker;
    s->cannotConnect = tr_trackerCannotConnect( tc );
    s->tracker = ( tc ? tr_trackerGet( tc ) : &inf->trackerList[0].list[0] );

    s->peersTotal       = 0;
    memset( s->peersFrom, 0, sizeof( s->peersFrom ) );
    s->peersUploading   = 0;
    s->peersDownloading = 0;
    
    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];
    
        if( tr_peerIsConnected( peer ) )
        {
            (s->peersTotal)++;
            (s->peersFrom[ tr_peerIsFrom( peer ) ])++;
            if( tr_peerAmInterested( peer ) && !tr_peerIsChoking( peer ) )
            {
                (s->peersUploading)++;
            }
            if( !tr_peerAmChoking( peer ) )
            {
                (s->peersDownloading)++;
            }
        }
    }

    s->percentDone = tr_cpPercentDone( tor->completion );
    s->percentComplete = tr_cpPercentComplete( tor->completion );
    s->cpStatus = tr_cpGetStatus( tor->completion );
    s->left     = tr_cpLeftUntilDone( tor->completion );
    if( tor->status & TR_STATUS_DOWNLOAD )
    {
        s->rateDownload = tr_rcRate( tor->download );
    }
    else
    {
        /* tr_rcRate() doesn't make the difference between 'piece'
           messages and other messages, which causes a non-zero
           download rate even tough we are not downloading. So we
           force it to zero not to confuse the user. */
        s->rateDownload = 0.0;
    }
    s->rateUpload = tr_rcRate( tor->upload );
    
    s->seeders  = tr_trackerSeeders( tc );
    s->leechers = tr_trackerLeechers( tc );
    s->completedFromTracker = tr_trackerDownloaded( tc );

    s->swarmspeed = tr_rcRate( tor->swarmspeed );
    
    s->startDate = tor->startDate;
    s->activityDate = tor->activityDate;

    if( s->rateDownload < 0.1 )
    {
        s->eta = -1;
    }
    else
    {
        s->eta = (float) s->left / s->rateDownload / 1024.0;
    }

    s->uploaded        = tor->uploadedCur   + tor->uploadedPrev;
    s->downloaded      = tor->downloadedCur + tor->downloadedPrev;
    s->downloadedValid = tr_cpDownloadedValid( tor->completion );
    
    if( s->downloaded == 0 && s->percentDone == 0.0 )
    {
        s->ratio = TR_RATIO_NA;
    }
    else
    {
        s->ratio = (float)s->uploaded
                 / (float)MAX(s->downloaded, s->downloadedValid);
    }
    
    tr_lockUnlock( &tor->lock );

    return s;
}

tr_peer_stat_t * tr_torrentPeers( tr_torrent_t * tor, int * peerCount )
{
    tr_peer_stat_t * peers;

    tr_lockLock( &tor->lock );

    *peerCount = tor->peerCount;
    
    peers = (tr_peer_stat_t *) calloc( tor->peerCount, sizeof( tr_peer_stat_t ) );
    if (peers != NULL)
    {
        tr_peer_t * peer;
        struct in_addr * addr;
        int i;
        for( i = 0; i < tor->peerCount; i++ )
        {
            peer = tor->peers[i];
            
            addr = tr_peerAddress( peer );
            if( NULL != addr )
            {
                tr_netNtop( addr, peers[i].addr,
                           sizeof( peers[i].addr ) );
            }
            
            peers[i].client        = tr_peerClient( peer );
            peers[i].isConnected   = tr_peerIsConnected( peer );
            peers[i].from          = tr_peerIsFrom( peer );
            peers[i].progress      = tr_peerProgress( peer );
            peers[i].port          = tr_peerPort( peer );
            
            if( ( peers[i].isDownloading = !tr_peerAmChoking( peer ) ) )
            {
                peers[i].uploadToRate = tr_peerUploadRate( peer );
            }
            if( ( peers[i].isUploading = ( tr_peerAmInterested( peer ) &&
					   !tr_peerIsChoking( peer ) ) ) )
            {
                peers[i].downloadFromRate = tr_peerDownloadRate( peer );
            }
        }
    }
    
    tr_lockUnlock( &tor->lock );
    
    return peers;
}

void tr_torrentPeersFree( tr_peer_stat_t * peers, int peerCount UNUSED )
{
    if (peers == NULL)
        return;

    free( peers );
}

void tr_torrentAvailability( tr_torrent_t * tor, int8_t * tab, int size )
{
    int i, j, piece;
    float interval;

    tr_lockLock( &tor->lock );
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
            if( tr_peerBitfield( tor->peers[j] ) &&
                tr_bitfieldHas( tr_peerBitfield( tor->peers[j] ), piece ) )
            {
                (tab[i])++;
            }
        }
    }
    tr_lockUnlock( &tor->lock );
}

uint64_t
tr_torrentFileBytesCompleted ( const tr_torrent_t * tor, int fileIndex )
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
    assert( tr_blockPiece( firstBlock ) == file->firstPiece );
    assert( tr_blockPiece( lastBlock ) == file->lastPiece );

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

float
tr_torrentFileCompletion ( const tr_torrent_t * tor, int fileIndex )
{
    const uint64_t c = tr_torrentFileBytesCompleted ( tor, fileIndex );
    uint64_t length = tor->info.files[fileIndex].length;
    
    if( !length )
        return 1.0;
    return (double)c / length;
}

float*
tr_torrentCompletion( tr_torrent_t * tor )
{
    int i;
    float * f;

    tr_lockLock( &tor->lock );
    f = calloc ( tor->info.fileCount, sizeof( float ) );
    for( i=0; i<tor->info.fileCount; ++i )
       f[i] = tr_torrentFileCompletion ( tor, i );
    tr_lockUnlock( &tor->lock );

    return f;
}

void tr_torrentAmountFinished( tr_torrent_t * tor, float * tab, int size )
{
    int i, piece;
    float interval;

    tr_lockLock( &tor->lock );
    interval = (float)tor->info.pieceCount / (float)size;
    for( i = 0; i < size; i++ )
    {
        piece = i * interval;
        tab[i] = tr_cpPercentBlocksInPiece( tor->completion, piece );
    }
    tr_lockUnlock( &tor->lock );
}

void tr_torrentRemoveSaved( tr_torrent_t * tor )
{
    tr_metainfoRemoveSaved( tor->info.hashString, tor->handle->tag );
}

void tr_torrentRemoveFastResume( tr_torrent_t * tor )
{
    tr_ioRemoveResume( tor );
}

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit.
 **********************************************************************/
void tr_torrentClose( tr_torrent_t * tor )
{
    tr_handle_t * h = tor->handle;
    tr_info_t * inf = &tor->info;

    /* Join the thread first */
    torrentReallyStop( tor );

    tr_sharedLock( h->shared );

    h->torrentCount--;

    tr_lockClose( &tor->lock );
    tr_condClose( &tor->cond );
    tr_cpClose( tor->completion );

    tr_rcClose( tor->upload );
    tr_rcClose( tor->download );
    tr_rcClose( tor->swarmspeed );

    if( tor->destination )
    {
        free( tor->destination );
    }

    tr_metainfoFree( inf );

    if( tor->prev )
    {
        tor->prev->next = tor->next;
    }
    else
    {
        h->torrentList = tor->next;
    }
    if( tor->next )
    {
        tor->next->prev = tor->prev;
    }
    free( tor );

    tr_sharedUnlock( h->shared );
}

int tr_torrentAttachPeer( tr_torrent_t * tor, tr_peer_t * peer )
{
    int i;
    tr_peer_t * otherPeer;

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

        peer = tr_peerInit( addr, port, -1, from );
        added += tr_torrentAttachPeer( tor, peer );
    }

    return added;
}

/***********************************************************************
 * Push a torrent's call to tr_ioInit through a queue in a worker
 * thread so that only one torrent can be in checkFiles() at a time.
 **********************************************************************/

struct tr_io_init_list_t
{
    tr_torrent_t              * tor;
    struct tr_io_init_list_t  * next;
};

static struct tr_io_init_list_t * ioInitQueue = NULL;

static int ioInitWorkerRunning = 0;

static tr_thread_t ioInitThread;

static tr_lock_t* getIOLock( tr_handle_t * h )
{
    static tr_lock_t * lock = NULL;

    tr_sharedLock( h->shared );
    if( lock == NULL )
    {
        lock = calloc( 1, sizeof( tr_lock_t ) );
        tr_lockInit( lock );
    }
    tr_sharedUnlock( h->shared );

    return lock;
}

static void ioInitWorker( void * user_data )
{
    tr_handle_t * h = (tr_handle_t*) user_data;

    for (;;)
    {
        char name[32];

        /* find the next torrent to process */
        tr_torrent_t * tor = NULL;
        tr_lock_t * lock = getIOLock( h );
        tr_lockLock( lock );
        if( ioInitQueue != NULL )
        {
            struct tr_io_init_list_t * node = ioInitQueue;
            ioInitQueue = node->next;
            tor = node->tor;
            free( node );
        }
        tr_lockUnlock( lock );

        /* if no torrents, this worker thread is done */
        if( tor == NULL )
        {
          break;
        }

        /* check this torrent's files */
        tor->status = TR_STATUS_CHECK;
        
        tr_dbg( "torrent %s checking files", tor->info.name );
        tr_lockLock( &tor->lock );
        tr_cpReset( tor->completion );
        tor->io = tr_ioInit( tor );
        tr_lockUnlock( &tor->lock );

        snprintf( name, sizeof( name ), "torrent %p", tor );
        tr_threadCreate( &tor->thread, downloadLoop, tor, name );
    }

    ioInitWorkerRunning = 0;
}

/* add tor to the queue of torrents waiting for an tr_ioInit */
static void ioInitAdd( tr_torrent_t * tor )
{
    tr_lock_t * lock = getIOLock( tor->handle );
    struct tr_io_init_list_t * node;
    tr_lockLock( lock );

    /* enqueue this torrent to have its io initialized */
    node = malloc( sizeof( struct tr_io_init_list_t ) );
    node->tor = tor;
    node->next = NULL;
    if( ioInitQueue == NULL )
    {
        ioInitQueue = node;
    }
    else
    {
        struct tr_io_init_list_t * l = ioInitQueue;
        while( l->next != NULL )
        {
            l = l->next;
        }
        l->next = node;
    }

    /* ensure there's a worker thread to process the queue */
    if( !ioInitWorkerRunning )
    {
        ioInitWorkerRunning = 1;
        tr_threadCreate( &ioInitThread, ioInitWorker, tor->handle, "ioInit" );
    }
    
    tr_lockUnlock( lock );
}

/* remove tor from the queue of torrents waiting for an tr_ioInit.
   return nonzero if tor was found and removed */
static int ioInitRemove( tr_torrent_t * tor )
{
    tr_lock_t * lock = getIOLock( tor->handle );
    struct tr_io_init_list_t *node, *prev;
    tr_lockLock( lock );

    /* find tor's node */
    for( prev = NULL, node = ioInitQueue;
         node != NULL && node->tor != tor;
         prev=node, node=node->next );

    if( node != NULL )
    {
        if( prev == NULL )
        {
            ioInitQueue = node->next;
        }
        else
        {
            prev->next = node->next;
        }
    }

    tr_lockUnlock( lock );
    return node != NULL;
}

/***********************************************************************
 * downloadLoop
 **********************************************************************/
static void downloadLoop( void * _tor )
{
    tr_torrent_t * tor = _tor;
    int            i, ret;
    int            peerCount, used;
    cp_status_t    cpState, cpPrevState;
    uint8_t      * peerCompact;
    tr_peer_t    * peer;

    tr_lockLock( &tor->lock );

    cpState = cpPrevState = tr_cpGetStatus( tor->completion );
    switch( cpState ) {
        case TR_CP_COMPLETE:   tor->status = TR_STATUS_SEED; break;
        case TR_CP_DONE:       tor->status = TR_STATUS_DONE; break;
        case TR_CP_INCOMPLETE: tor->status = TR_STATUS_DOWNLOAD; break;
    }

    while( !tor->die )
    {
        tr_lockUnlock( &tor->lock );
        tr_wait( INTERVAL_MSEC );
        tr_lockLock( &tor->lock );

        cpState = tr_cpGetStatus( tor->completion );

        if( cpState != cpPrevState )
        {
            switch( cpState ) {
                case TR_CP_COMPLETE:   tor->status = TR_STATUS_SEED; break;
                case TR_CP_DONE:       tor->status = TR_STATUS_DONE; break;
                case TR_CP_INCOMPLETE: tor->status = TR_STATUS_DOWNLOAD; break;
            }

            tor->hasChangedState = cpState;

            if( cpState == TR_CP_COMPLETE )
                tr_trackerCompleted( tor->tracker );

            tr_ioSync( tor->io );

            cpPrevState = cpState;
        }

        /* Try to get new peers or to send a message to the tracker */
        tr_trackerPulse( tor->tracker, &peerCount, &peerCompact );
        if( peerCount > 0 )
        {
            used = tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                                         peerCompact, peerCount );
            free( peerCompact );
            tr_dbg( "got %i peers from announce, used %i", peerCount, used );
        }
        if( tor->status & TR_STATUS_STOPPED )
        {
            break;
        }

        /* Stopping: make sure all files are closed and stop talking
           to peers */
        if( tor->status & TR_STATUS_STOPPING )
        {
            if( tor->io )
            {
                tr_ioClose( tor->io ); tor->io = NULL;
                tr_condSignal( &tor->cond );
            }
            continue;
        }

        /* Shuffle peers */
        if( tor->peerCount > 1 )
        {
            peer = tor->peers[0];
            memmove( &tor->peers[0], &tor->peers[1],
                    ( tor->peerCount - 1 ) * sizeof( void * ) );
            tor->peers[tor->peerCount - 1] = peer;
        }

        /* Receive/send messages */
        for( i = 0; i < tor->peerCount; )
        {
            peer = tor->peers[i];

            ret = tr_peerPulse( peer );
            if( ret & TR_ERROR_IO_MASK )
            {
                tr_err( "Fatal error, stopping download (%d)", ret );
                torrentStop( tor );
                tor->error = ret;
                snprintf( tor->errorString, sizeof( tor->errorString ),
                          "%s", tr_errorString( ret ) );
                break;
            }
            if( ret )
            {
                tr_peerDestroy( peer );
                tor->peerCount--;
                memmove( &tor->peers[i], &tor->peers[i+1],
                         ( tor->peerCount - i ) * sizeof( void * ) );
                continue;
            }
            i++;
        }
    }

    tr_lockUnlock( &tor->lock );

    if( tor->io )
    {
        tr_ioClose( tor->io ); tor->io = NULL;
        tr_condSignal( &tor->cond );
    }

    tor->status = TR_STATUS_STOPPED;
}

/***
****
****  File prioritization
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
    tr_priority_t priority = TR_PRI_DND;

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

void
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
      tor->info.pieces[i].priority = calculatePiecePriority( tor, i );
}

void
tr_torrentSetFilePriority( tr_torrent_t   * tor,
                           int              fileIndex,
                           tr_priority_t    priority )
{
    int i;
    tr_file_t * file;

    assert( tor != NULL );
    assert( 0<=fileIndex && fileIndex<tor->info.fileCount );
    assert( priority==TR_PRI_LOW || priority==TR_PRI_NORMAL
         || priority==TR_PRI_HIGH || priority==TR_PRI_DND );

    file = &tor->info.files[fileIndex];
    file->priority = priority;
    for( i=file->firstPiece; i<=file->lastPiece; ++i )
      tor->info.pieces[i].priority = calculatePiecePriority( tor, i );

    tr_dbg ( "Setting file #%d (pieces %d-%d) priority to %d (%s)",
             fileIndex, file->firstPiece, file->lastPiece,
             priority, tor->info.files[fileIndex].name );
}

tr_priority_t
tr_torrentGetFilePriority( const tr_torrent_t *  tor, int file )
{
    assert( tor != NULL );
    assert( 0<=file && file<tor->info.fileCount );

    return tor->info.files[file].priority;
}


void
tr_torrentSetFilePriorities( tr_torrent_t         * tor,
                             const tr_priority_t  * filePriorities )
{
    int i;
    for( i=0; i<tor->info.pieceCount; ++i )
      tr_torrentSetFilePriority( tor, i, filePriorities[i] );
}

tr_priority_t*
tr_torrentGetFilePriorities( const tr_torrent_t * tor )
{
    int i;
    tr_priority_t * p = malloc( tor->info.fileCount * sizeof(tr_priority_t) );
    for( i=0; i<tor->info.fileCount; ++i )
        p[i] = tor->info.files[i].priority;
    return p;
}
