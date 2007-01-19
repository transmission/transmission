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

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static tr_torrent_t * torrentRealInit( tr_handle_t *, tr_torrent_t * tor,
                                       int flags, int * error );
static void torrentReallyStop( tr_torrent_t * );
static void downloadLoop( void * );

void tr_setUseCustomLimit( tr_torrent_t * tor, int limit )
{
    tor->customSpeedLimit = limit;
}

void tr_setUploadLimit( tr_torrent_t * tor, int limit )
{
    tr_rcSetLimit( tor->upload, limit );
}

void tr_setDownloadLimit( tr_torrent_t * tor, int limit )
{
    tr_rcSetLimit( tor->download, limit );
}

tr_torrent_t * tr_torrentInit( tr_handle_t * h, const char * path,
                               int flags, int * error )
{
    tr_torrent_t  * tor = calloc( sizeof( tr_torrent_t ), 1 );
    int             saveCopy = ( TR_FSAVEPRIVATE & flags );

    /* Parse torrent file */
    if( tr_metainfoParse( &tor->info, path, NULL, saveCopy ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, flags, error );
}

tr_torrent_t * tr_torrentInitSaved( tr_handle_t * h, const char * hashStr,
                                    int flags, int * error )
{
    tr_torrent_t  * tor = calloc( sizeof( tr_torrent_t ), 1 );

    /* Parse torrent file */
    if( tr_metainfoParse( &tor->info, NULL, hashStr, 0 ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, ( TR_FSAVEPRIVATE | flags ), error );
}

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Allocates a tr_torrent_t structure, then relies on tr_metainfoParse
 * to fill it.
 **********************************************************************/
static tr_torrent_t * torrentRealInit( tr_handle_t * h, tr_torrent_t * tor,
                                       int flags, int * error )
{
    tr_torrent_t  * tor_tmp;
    tr_info_t     * inf;
    int             i;
    
    inf        = &tor->info;
    inf->flags = flags;

    /* Make sure this torrent is not already open */
    for( tor_tmp = h->torrentList; tor_tmp; tor_tmp = tor_tmp->next )
    {
        if( !memcmp( tor->info.hash, tor_tmp->info.hash,
                     SHA_DIGEST_LENGTH ) )
        {
            *error = TR_EDUPLICATE;
            tr_metainfoFree( &tor->info );
            free( tor );
            return NULL;
        }
    }

    tor->handle = h;
    tor->status = TR_STATUS_PAUSE;
    tor->id     = h->id;
    tor->key    = h->key;
    tor->bindPort = &h->bindPort;
	tor->finished = 0;

    /* Escaped info hash for HTTP queries */
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        sprintf( &tor->escapedHashString[3*i], "%%%02x", inf->hash[i] );
    }

    /* Block size: usually 16 ko, or less if we have to */
    tor->blockSize  = MIN( inf->pieceSize, 1 << 14 );
    tor->blockCount = ( inf->totalSize + tor->blockSize - 1 ) /
                        tor->blockSize;
    tor->completion = tr_cpInit( tor );

    tr_lockInit( &tor->lock );

    tor->fdlimit        = h->fdlimit;
    tor->upload         = tr_rcInit();
    tor->download       = tr_rcInit();
    tor->swarmspeed     = tr_rcInit();
 
    /* We have a new torrent */
    tr_sharedLock( h->shared );
    tor->prev = NULL;
    tor->next = h->torrentList;
    if( tor->next )
    {
        tor->next->prev = tor;
    }
    h->torrentList = tor;
    (h->torrentCount)++;
    tr_sharedUnlock( h->shared );

    if( 0 > h->bindPort )
    {
        tr_setBindPort( h, TR_DEFAULT_PORT );
    }

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
    tr_ioLoadResume( tor );
}

char * tr_torrentGetFolder( tr_torrent_t * tor )
{
    return tor->destination;
}

void tr_torrentStart( tr_torrent_t * tor )
{
    char name[32];

    if( tor->status & ( TR_STATUS_STOPPING | TR_STATUS_STOPPED ) )
    {
        /* Join the thread first */
        torrentReallyStop( tor );
    }

    tor->downloadedPrev += tor->downloadedCur;
    tor->downloadedCur   = 0;
    tor->uploadedPrev   += tor->uploadedCur;
    tor->uploadedCur     = 0;

    tor->status  = TR_STATUS_CHECK;
    tor->error   = TR_OK;
    tor->tracker = tr_trackerInit( tor );

    tor->date = tr_date();
    tor->die = 0;
    snprintf( name, sizeof( name ), "torrent %p", tor );
    tr_threadCreate( &tor->thread, downloadLoop, tor, name );
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
    tr_lockUnlock( &tor->lock );
}

/***********************************************************************
 * torrentReallyStop
 ***********************************************************************
 * Joins the download thread and frees/closes everything related to it.
 **********************************************************************/
static void torrentReallyStop( tr_torrent_t * tor )
{
    tor->die = 1;
    tr_threadJoin( &tor->thread );

    tr_trackerClose( tor->tracker );
    tor->tracker = NULL;

    tr_lockLock( &tor->lock );
    while( tor->peerCount > 0 )
    {
        tr_peerRem( tor, 0 );
    }
    tr_lockUnlock( &tor->lock );
}

int tr_getFinished( tr_torrent_t * tor )
{
    if( tor->finished )
    {
        tor->finished = 0;
        return 1;
    }
    return 0;
}

void tr_manualUpdate( tr_torrent_t * tor )
{
    if( !( tor->status & TR_STATUS_ACTIVE ) )
        return;
    
    tr_lockLock( &tor->lock );
    tr_trackerAnnouncePulse( tor->tracker, 1 );
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
    
    if( tc )
    {
        s->trackerAddress  = tr_trackerAddress(  tc );
        s->trackerPort     = tr_trackerPort(     tc );
        s->trackerAnnounce = tr_trackerAnnounce( tc );
    }
    else
    {
        s->trackerAddress  = inf->trackerList[0].list[0].address;
        s->trackerPort     = inf->trackerList[0].list[0].port;
        s->trackerAnnounce = inf->trackerList[0].list[0].announce;
    }

    s->peersTotal       = 0;
    s->peersIncoming    = 0;
    s->peersUploading   = 0;
    s->peersDownloading = 0;
    
    for( i = 0; i < tor->peerCount; i++ )
    {
        peer = tor->peers[i];
    
        if( tr_peerIsConnected( peer ) )
        {
            (s->peersTotal)++;
            
            if( tr_peerIsIncoming( peer ) )
            {
                (s->peersIncoming)++;
            }
            if( !tr_peerIsChoking( peer ) )
            {
                (s->peersUploading)++;
            }
            if( tr_peerIsUnchoked( peer ) )
            {
                (s->peersDownloading)++;
            }
        }
    }

    s->progress = tr_cpCompletionAsFloat( tor->completion );
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

    if( s->rateDownload < 0.1 )
    {
        s->eta = -1;
    }
    else
    {
        s->eta = ( 1.0 - s->progress ) *
            (float) inf->totalSize / s->rateDownload / 1024.0;
    }

    s->downloaded = tor->downloadedCur + tor->downloadedPrev;
    s->uploaded   = tor->uploadedCur   + tor->uploadedPrev;
    
    if( s->downloaded == 0 )
    {
        s->ratio = s->uploaded == 0 ? TR_RATIO_NA : TR_RATIO_INF;
    }
    else
    {
        s->ratio = (float)s->uploaded / (float)s->downloaded;
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
            
            peers[i].client = tr_clientForId(tr_peerId(peer));
            
            peers[i].isConnected   = tr_peerIsConnected( peer );
            peers[i].isIncoming    = tr_peerIsIncoming( peer );
            peers[i].progress      = tr_peerProgress( peer );
            peers[i].port          = tr_peerPort( peer );
            
            if( ( peers[i].isDownloading = tr_peerIsUnchoked( peer ) ) )
            {
                peers[i].uploadToRate = tr_peerUploadRate( peer );
            }
            if( ( peers[i].isUploading = !tr_peerIsChoking( peer ) ) )
            {
                peers[i].downloadFromRate = tr_peerDownloadRate( peer );
            }
        }
    }
    
    tr_lockUnlock( &tor->lock );
    
    return peers;
}

void tr_torrentPeersFree( tr_peer_stat_t * peers, int peerCount )
{
    int i;

    if (peers == NULL)
        return;

    for (i = 0; i < peerCount; i++)
        free( peers[i].client );

    free( peers );
}

void tr_torrentAvailability( tr_torrent_t * tor, int8_t * tab, int size )
{
    int i, j, piece;

    tr_lockLock( &tor->lock );
    for( i = 0; i < size; i++ )
    {
        piece = i * tor->info.pieceCount / size;

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

void tr_torrentAmountFinished( tr_torrent_t * tor, float * tab, int size )
{
    int i, piece;

    tr_lockLock( &tor->lock );
    for( i = 0; i < size; i++ )
    {
        piece = i * tor->info.pieceCount / size;
        tab[i] = tr_cpPercentBlocksInPiece( tor->completion, piece );
    }
    tr_lockUnlock( &tor->lock );
}

void tr_torrentRemoveSaved( tr_torrent_t * tor ) {
    tr_metainfoRemoveSaved( tor->info.hashString );
}

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit.
 **********************************************************************/
void tr_torrentClose( tr_handle_t * h, tr_torrent_t * tor )
{
    tr_info_t * inf = &tor->info;

    if( tor->status & ( TR_STATUS_STOPPING | TR_STATUS_STOPPED ) )
    {
        /* Join the thread first */
        torrentReallyStop( tor );
    }

    tr_sharedLock( h->shared );

    h->torrentCount--;

    tr_lockClose( &tor->lock );
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

/***********************************************************************
 * downloadLoop
 **********************************************************************/
static void downloadLoop( void * _tor )
{
    tr_torrent_t * tor = _tor;
    uint64_t       date1, date2;
    int            ret;

    tr_lockLock( &tor->lock );

    tr_cpReset( tor->completion );
    tor->io     = tr_ioInit( tor );
    tor->status = tr_cpIsSeeding( tor->completion ) ?
                      TR_STATUS_SEED : TR_STATUS_DOWNLOAD;

    while( !tor->die )
    {
        date1 = tr_date();

        /* Are we finished ? */
        if( ( tor->status & TR_STATUS_DOWNLOAD ) &&
            tr_cpIsSeeding( tor->completion ) )
        {
            /* Done */
            tor->status = TR_STATUS_SEED;
			tor->finished = 1;
            tr_trackerCompleted( tor->tracker );
            tr_ioSync( tor->io );
        }

        /* Receive/send messages */
        if( ( ret = tr_peerPulse( tor ) ) )
        {
            tr_err( "Fatal error, stopping download (%d)", ret );
            torrentStop( tor );
            tor->error = ret;
            snprintf( tor->errorString, sizeof( tor->errorString ),
                      "%s", tr_errorString( ret ) );
        }

        /* Try to get new peers or to send a message to the tracker */
        tr_trackerPulse( tor->tracker );

        if( tor->status & TR_STATUS_STOPPED )
        {
            break;
        }

        /* Wait up to 20 ms */
        date2 = tr_date();
        if( date2 < date1 + 20 )
        {
            tr_lockUnlock( &tor->lock );
            tr_wait( date1 + 20 - date2 );
            tr_lockLock( &tor->lock );
        }
    }

    tr_lockUnlock( &tor->lock );

    tr_ioClose( tor->io );

    tor->status = TR_STATUS_STOPPED;
}

