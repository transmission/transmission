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
                                       uint8_t *, int flags, int * error );
static void torrentReallyStop( tr_torrent_t * );
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

tr_torrent_t * tr_torrentInit( tr_handle_t * h, const char * path,
                               uint8_t * hash, int flags, int * error )
{
    tr_torrent_t  * tor = calloc( sizeof( tr_torrent_t ), 1 );
    int             saveCopy = ( TR_FLAG_SAVE & flags );

    /* Parse torrent file */
    if( tr_metainfoParse( &tor->info, h->tag, path, NULL, saveCopy ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    return torrentRealInit( h, tor, hash, flags, error );
}

tr_torrent_t * tr_torrentInitSaved( tr_handle_t * h, const char * hashStr,
                                    int flags, int * error )
{
    tr_torrent_t  * tor = calloc( sizeof( tr_torrent_t ), 1 );

    /* Parse torrent file */
    if( tr_metainfoParse( &tor->info, h->tag, NULL, hashStr, 0 ) )
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
    tor->finished = 0;

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

    tr_lockLock( &tor->lock );

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

    tr_lockUnlock( &tor->lock );

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

    /* Don't return until the files are closed, so the UI can trash
     * them if requested */
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

    tr_trackerClose( tor->tracker );
    tor->tracker = NULL;

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
    int peerCount;
    uint8_t * peerCompact;

    if( !( tor->status & TR_STATUS_ACTIVE ) )
        return;
    
    tr_lockLock( &tor->lock );
    tr_trackerAnnouncePulse( tor->tracker, &peerCount, &peerCompact, 1 );
    if( peerCount > 0 )
    {
        tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                              peerCompact, peerCount );
        free( peerCompact );
    }
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

float * tr_torrentCompletion( tr_torrent_t * tor )
{
    tr_info_t * inf = &tor->info;
    int         piece, file;
    float     * ret, prog, weight;
    uint64_t    piecemax, piecesize;
    uint64_t    filestart, fileoff, filelen, blockend, blockused;

    tr_lockLock( &tor->lock );

    ret       = calloc( inf->fileCount, sizeof( float ) );
    file      = 0;
    piecemax  = inf->pieceSize;
    filestart = 0;
    fileoff   = 0;
    piece     = 0;
    while( inf->pieceCount > piece )
    {
        assert( file < inf->fileCount );
        assert( filestart + fileoff < inf->totalSize );
        filelen    = inf->files[file].length;
        piecesize  = tr_pieceSize( piece );
        blockend   = MIN( filestart + filelen, piecemax * piece + piecesize );
        blockused  = blockend - ( filestart + fileoff );
        weight     = ( filelen ? ( float )blockused / ( float )filelen : 1.0 );
        prog       = tr_cpPercentBlocksInPiece( tor->completion, piece );
        ret[file] += prog * weight;
        fileoff   += blockused;
        assert( -0.1 < prog   && 1.1 > prog );
        assert( -0.1 < weight && 1.1 > weight );
        if( fileoff == filelen )
        {
            ret[file] = MIN( 1.0, ret[file] );
            ret[file] = MAX( 0.0, ret[file] );
            filestart += fileoff;
            fileoff    = 0;
            file++;
        }
        if( filestart + fileoff >= piecemax * piece + piecesize )
        {
            piece++;
        }
    }

    tr_lockUnlock( &tor->lock );

    return ret;
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

void tr_torrentAttachPeer( tr_torrent_t * tor, tr_peer_t * peer )
{
    int i;
    tr_peer_t * otherPeer;

    if( tor->peerCount >= TR_MAX_PEER_COUNT )
    {
        tr_peerDestroy(  peer );
        return;
    }

    /* Don't accept two connections from the same IP */
    for( i = 0; i < tor->peerCount; i++ )
    {
        otherPeer = tor->peers[i];
        if( !memcmp( tr_peerAddress( peer ), tr_peerAddress( otherPeer ), 4 ) )
        {
            tr_peerDestroy(  peer );
            return;
        }
    }

    tr_peerSetPrivate( peer, tor->info.flags & TR_FLAG_PRIVATE ||
                       tor->pexDisabled );
    tr_peerSetTorrent( peer, tor );
    tor->peers[tor->peerCount++] = peer;
}

void tr_torrentAddCompact( tr_torrent_t * tor, int from,
                           uint8_t * buf, int count )
{
    struct in_addr addr;
    in_port_t port;
    int i;
    tr_peer_t * peer;

    for( i = 0; i < count; i++ )
    {
        memcpy( &addr, buf, 4 ); buf += 4;
        memcpy( &port, buf, 2 ); buf += 2;

        peer = tr_peerInit( addr, port, -1, from );
        tr_torrentAttachPeer( tor, peer );
    }
}

/***********************************************************************
 * downloadLoop
 **********************************************************************/
static void downloadLoop( void * _tor )
{
    tr_torrent_t * tor = _tor;
    int            i, ret;
    int            peerCount;
    uint8_t      * peerCompact;
    tr_peer_t    * peer;

    tr_lockLock( &tor->lock );

    tr_cpReset( tor->completion );
    tor->io     = tr_ioInit( tor );
    tor->status = tr_cpIsSeeding( tor->completion ) ?
                      TR_STATUS_SEED : TR_STATUS_DOWNLOAD;

    while( !tor->die )
    {
        tr_lockUnlock( &tor->lock );
        tr_wait( 20 );
        tr_lockLock( &tor->lock );

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

        /* Try to get new peers or to send a message to the tracker */
        tr_trackerPulse( tor->tracker, &peerCount, &peerCompact );
        if( peerCount > 0 )
        {
            tr_torrentAddCompact( tor, TR_PEER_FROM_TRACKER,
                                  peerCompact, peerCount );
            free( peerCompact );
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

