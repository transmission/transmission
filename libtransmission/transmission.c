/******************************************************************************
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

/***********************************************************************
 * Local prototypes
 **********************************************************************/
static void torrentReallyStop( tr_torrent_t * );
static void  downloadLoop( void * );
static void  acceptLoop( void * );
static void acceptStop( tr_handle_t * h );

/***********************************************************************
 * tr_init
 ***********************************************************************
 * Allocates a tr_handle_t structure and initializes a few things
 **********************************************************************/
tr_handle_t * tr_init()
{
    tr_handle_t * h;
    int           i, r;

    h = calloc( sizeof( tr_handle_t ), 1 );

    /* Generate a peer id : "-TRxxyy-" + 12 random alphanumeric
       characters, where xx is the major version number and yy the
       minor version number (Azureus-style) */
    sprintf( h->id, "-TR%02d%02d-", VERSION_MAJOR, VERSION_MINOR );
    for( i = 8; i < 20; i++ )
    {
        r        = tr_rand( 36 );
        h->id[i] = ( r < 26 ) ? ( 'a' + r ) : ( '0' + r - 26 ) ;
    }

    /* Random key */
    for( i = 0; i < 20; i++ )
    {
        r         = tr_rand( 36 );
        h->key[i] = ( r < 26 ) ? ( 'a' + r ) : ( '0' + r - 26 ) ;
    }

    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );

    /* Initialize rate and file descripts controls */
    h->upload   = tr_rcInit();
    h->download = tr_rcInit();
    h->fdlimit  = tr_fdInit();
    h->choking  = tr_chokingInit( h );

    h->bindPort = -1;
    h->bindSocket = -1;

    h->acceptDie = 0;
    tr_lockInit( &h->acceptLock );
    tr_threadCreate( &h->acceptThread, acceptLoop, h );

    return h;
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setBindPort( tr_handle_t * h, int port )
{
    int sock = -1;
    tr_torrent_t * tor;

    if( h->bindPort == port )
      return;

#ifndef BEOS_NETSERVER
    /* BeOS net_server seems to be unable to set incoming connections to
       non-blocking. Too bad. */
    if( !tr_fdSocketWillCreate( h->fdlimit, 0 ) )
    {
        /* XXX should handle failure here in a better way */
        sock = tr_netBind( port );
    }
#else
    return;
#endif

    tr_lockLock( &h->acceptLock );

    h->bindPort = port;

    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_lockLock( &tor->lock );
        if( NULL != tor->tracker )
        {
            tr_trackerChangePort( tor->tracker, port );
        }
        tr_lockUnlock( &tor->lock );
    }

    if( h->bindSocket > -1 )
    {
        tr_netClose( h->bindSocket );
        tr_fdSocketClosed( h->fdlimit, 0 );
    }

    h->bindSocket = sock;

    tr_lockUnlock( &h->acceptLock );
}

/***********************************************************************
 * tr_setUploadLimit
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setUploadLimit( tr_handle_t * h, int limit )
{
    tr_rcSetLimit( h->upload, limit );
    tr_chokingSetLimit( h->choking, limit );
}

/***********************************************************************
 * tr_setDownloadLimit
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setDownloadLimit( tr_handle_t * h, int limit )
{
    tr_rcSetLimit( h->download, limit );
}

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 *
 **********************************************************************/
void tr_torrentRates( tr_handle_t * h, float * dl, float * ul )
{
    tr_torrent_t * tor;

    *dl = 0.0;
    *ul = 0.0;
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_lockLock( &tor->lock );
        if( tor->status & TR_STATUS_DOWNLOAD )
            *dl += tr_rcRate( tor->download );
        *ul += tr_rcRate( tor->upload );
        tr_lockUnlock( &tor->lock );
    }
}

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Allocates a tr_torrent_t structure, then relies on tr_metainfoParse
 * to fill it.
 **********************************************************************/
tr_torrent_t * tr_torrentInit( tr_handle_t * h, const char * path,
                               int * error )
{
    tr_torrent_t  * tor, * tor_tmp;
    tr_info_t     * inf;
    int             i;
    char          * s1, * s2;

    tor = calloc( sizeof( tr_torrent_t ), 1 );
    inf = &tor->info;

    /* Parse torrent file */
    if( tr_metainfoParse( inf, path ) )
    {
        *error = TR_EINVALID;
        free( tor );
        return NULL;
    }

    /* Make sure this torrent is not already open */
    for( tor_tmp = h->torrentList; tor_tmp; tor_tmp = tor_tmp->next )
    {
        if( !memcmp( tor->info.hash, tor_tmp->info.hash,
                     SHA_DIGEST_LENGTH ) )
        {
            *error = TR_EDUPLICATE;
            free( tor );
            return NULL;
        }
    }

    tor->status = TR_STATUS_PAUSE;
    tor->id     = h->id;
    tor->key    = h->key;
    tor->bindPort = &h->bindPort;
	tor->finished = 0;


    /* Guess scrape URL */
    s1 = strchr( inf->trackerAnnounce, '/' );
    while( ( s2 = strchr( s1 + 1, '/' ) ) )
    {
        s1 = s2;
    }
    s1++;
    if( !strncmp( s1, "announce", 8 ) )
    {
        int pre  = (long) s1 - (long) inf->trackerAnnounce;
        int post = strlen( inf->trackerAnnounce ) - pre - 8;
        memcpy( tor->scrape, inf->trackerAnnounce, pre );
        sprintf( &tor->scrape[pre], "scrape" );
        memcpy( &tor->scrape[pre+6], &inf->trackerAnnounce[pre+8], post );
    }

    /* Escaped info hash for HTTP queries */
    for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
    {
        sprintf( &tor->hashString[3*i], "%%%02x", inf->hash[i] );
    }

    /* Block size: usually 16 ko, or less if we have to */
    tor->blockSize  = MIN( inf->pieceSize, 1 << 14 );
    tor->blockCount = ( inf->totalSize + tor->blockSize - 1 ) /
                        tor->blockSize;
    tor->completion = tr_cpInit( tor );

    tr_lockInit( &tor->lock );

    tor->globalUpload   = h->upload;
    tor->globalDownload = h->download;
    tor->fdlimit        = h->fdlimit;
    tor->upload         = tr_rcInit();
    tor->download       = tr_rcInit();
 
    /* We have a new torrent */
    tr_lockLock( &h->acceptLock );
    tor->prev = NULL;
    tor->next = h->torrentList;
    if( tor->next )
    {
        tor->next->prev = tor;
    }
    h->torrentList = tor;
    (h->torrentCount)++;
    tr_lockUnlock( &h->acceptLock );

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
int tr_torrentScrape( tr_torrent_t * tor, int * s, int * l )
{
    return tr_trackerScrape( tor, s, l );
}

void tr_torrentSetFolder( tr_torrent_t * tor, const char * path )
{
    tor->destination = strdup( path );
}

char * tr_torrentGetFolder( tr_torrent_t * tor )
{
    return tor->destination;
}

void tr_torrentStart( tr_torrent_t * tor )
{
    if( tor->status & ( TR_STATUS_STOPPING | TR_STATUS_STOPPED ) )
    {
        /* Join the thread first */
        torrentReallyStop( tor );
    }

    tor->status  = TR_STATUS_CHECK;
    tor->tracker = tr_trackerInit( tor );

    tor->date = tr_date();
    tor->die = 0;
    tr_threadCreate( &tor->thread, downloadLoop, tor );
}

void tr_torrentStop( tr_torrent_t * tor )
{
    tr_lockLock( &tor->lock );
    tr_trackerStopped( tor->tracker );
    tr_rcReset( tor->download );
    tr_rcReset( tor->upload );
    tor->status = TR_STATUS_STOPPING;
    tor->stopDate = tr_date();
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
    tr_dbg( "Thread joined" );

    tr_trackerClose( tor->tracker );

    while( tor->peerCount > 0 )
    {
        tr_peerRem( tor, 0 );
    }
}

/***********************************************************************
 * tr_torrentCount
 ***********************************************************************
 *
 **********************************************************************/
int tr_torrentCount( tr_handle_t * h )
{
    return h->torrentCount;
}

void tr_torrentIterate( tr_handle_t * h, tr_callback_t func, void * d )
{
    tr_torrent_t * tor;

    for( tor = h->torrentList; tor; tor = tor->next )
    {
        func( tor, d );
    }
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

tr_stat_t * tr_torrentStat( tr_torrent_t * tor )
{
    tr_stat_t * s;
    tr_info_t * inf = &tor->info;
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
    memcpy( s->trackerError, tor->trackerError,
            sizeof( s->trackerError ) );

    s->peersTotal       = 0;
    s->peersUploading   = 0;
    s->peersDownloading = 0;

    for( i = 0; i < tor->peerCount; i++ )
    {
        if( tr_peerIsConnected( tor->peers[i] ) )
        {
            (s->peersTotal)++;
            if( tr_peerIsUploading( tor->peers[i] ) )
            {
                (s->peersUploading)++;
            }
            if( tr_peerIsDownloading( tor->peers[i] ) )
            {
                (s->peersDownloading)++;
            }
        }
    }

    s->progress = tr_cpCompletionAsFloat( tor->completion );
    if( tor->status & TR_STATUS_DOWNLOAD )
        s->rateDownload = tr_rcRate( tor->download );
    else
        /* tr_rcRate() doesn't make the difference between 'piece'
           messages and other messages, which causes a non-zero
           download rate even tough we are not downloading. So we
           force it to zero not to confuse the user. */
        s->rateDownload = 0.0;
    s->rateUpload = tr_rcRate( tor->upload );
    
    s->seeders	  = tr_trackerSeeders(tor);
	s->leechers	  = tr_trackerLeechers(tor);

    if( s->rateDownload < 0.1 )
    {
        s->eta = -1;
    }
    else
    {
        s->eta = (float) ( 1.0 - s->progress ) *
            (float) inf->totalSize / s->rateDownload / 1024.0;
        if( s->eta > 99 * 3600 + 59 * 60 + 59 )
        {
            s->eta = -1;
        }
    }

    s->downloaded = tor->downloaded;
    s->uploaded   = tor->uploaded;

    tr_lockUnlock( &tor->lock );

    return s;
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

    tr_lockLock( &h->acceptLock );

    h->torrentCount--;

    tr_lockClose( &tor->lock );
    tr_cpClose( tor->completion );

    tr_rcClose( tor->upload );
    tr_rcClose( tor->download );

    if( tor->destination )
    {
        free( tor->destination );
    }
    free( inf->pieces );
    free( inf->files );

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

    tr_lockUnlock( &h->acceptLock );
}

void tr_close( tr_handle_t * h )
{
    acceptStop( h );
    tr_chokingClose( h->choking );
    tr_fdClose( h->fdlimit );
    tr_rcClose( h->upload );
    tr_rcClose( h->download );
    free( h );
}

/***********************************************************************
 * downloadLoop
 **********************************************************************/
static void downloadLoop( void * _tor )
{
    tr_torrent_t * tor = _tor;
    uint64_t       date1, date2;

    tr_dbg( "Thread started" );

#ifdef SYS_BEOS
    /* This is required because on BeOS, SIGINT is sent to each thread,
       which kills them not nicely */
    signal( SIGINT, SIG_IGN );
#endif

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
            tr_ioSaveResume( tor->io );
        }

        /* Receive/send messages */
        tr_peerPulse( tor );

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

    tr_dbg( "Thread exited" );
}

/***********************************************************************
 * acceptLoop
 **********************************************************************/
static void acceptLoop( void * _h )
{
    tr_handle_t * h = _h;
    uint64_t      date1, date2, lastchoke = 0;
    int           ii;
    uint8_t     * hash;
    tr_torrent_t * tor;

    tr_dbg( "Accept thread started" );

#ifdef SYS_BEOS
    /* This is required because on BeOS, SIGINT is sent to each thread,
       which kills them not nicely */
    signal( SIGINT, SIG_IGN );
#endif

    tr_lockLock( &h->acceptLock );

    while( !h->acceptDie )
    {
        date1 = tr_date();

        /* Check for incoming connections */
        if( h->bindSocket > -1 &&
            h->acceptPeerCount < TR_MAX_PEER_COUNT &&
            !tr_fdSocketWillCreate( h->fdlimit, 0 ) )
        {
            int            s;
            struct in_addr addr;
            in_port_t      port;
            s = tr_netAccept( h->bindSocket, &addr, &port );
            if( s > -1 )
            {
                h->acceptPeers[h->acceptPeerCount++] = tr_peerInit( addr, port, s );
            }
            else
            {
                tr_fdSocketClosed( h->fdlimit, 0 );
            }
        }

        for( ii = 0; ii < h->acceptPeerCount; )
        {
            if( tr_peerRead( NULL, h->acceptPeers[ii] ) )
            {
                tr_peerDestroy( h->fdlimit, h->acceptPeers[ii] );
                goto removePeer;
            }
            if( NULL != ( hash = tr_peerHash( h->acceptPeers[ii] ) ) )
            {
                for( tor = h->torrentList; tor; tor = tor->next )
                {
                    tr_lockLock( &tor->lock );
                    if( 0 == memcmp( tor->info.hash, hash,
                                     SHA_DIGEST_LENGTH ) )
                    {
                      tr_peerAttach( tor, h->acceptPeers[ii] );
                      tr_lockUnlock( &tor->lock );
                      goto removePeer;
                    }
                    tr_lockUnlock( &tor->lock );
                }
                tr_peerDestroy( h->fdlimit, h->acceptPeers[ii] );
                goto removePeer;
            }
            if( date1 > tr_peerDate( h->acceptPeers[ii] ) + 10000 )
            {
                /* Give them 10 seconds to send the handshake */
                tr_peerDestroy( h->fdlimit, h->acceptPeers[ii] );
                goto removePeer;
            }
            ii++;
            continue;
           removePeer:
            h->acceptPeerCount--;
            memmove( &h->acceptPeers[ii], &h->acceptPeers[ii+1],
                     ( h->acceptPeerCount - ii ) * sizeof( tr_peer_t * ) );
        }

        if( date1 > lastchoke + 2000 )
        {
            tr_chokingPulse( h->choking );
            lastchoke = date1;
        }

        /* Wait up to 20 ms */
        date2 = tr_date();
        if( date2 < date1 + 20 )
        {
            tr_lockUnlock( &h->acceptLock );
            tr_wait( date1 + 20 - date2 );
            tr_lockLock( &h->acceptLock );
        }
    }

    tr_lockUnlock( &h->acceptLock );

    tr_dbg( "Accept thread exited" );
}

/***********************************************************************
 * acceptStop
 ***********************************************************************
 * Joins the accept thread and frees/closes everything related to it.
 **********************************************************************/
static void acceptStop( tr_handle_t * h )
{
    int ii;

    h->acceptDie = 1;
    tr_threadJoin( &h->acceptThread );
    tr_lockClose( &h->acceptLock );
    tr_dbg( "Accept thread joined" );

    for( ii = 0; ii < h->acceptPeerCount; ii++ )
    {
        tr_peerDestroy( h->fdlimit, h->acceptPeers[ii] );
    }

    if( h->bindSocket > -1 )
    {
        tr_netClose( h->bindSocket );
        tr_fdSocketClosed( h->fdlimit, 0 );
    }
}
