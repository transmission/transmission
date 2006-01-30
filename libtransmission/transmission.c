/******************************************************************************
 * Copyright (c) 2005 Eric Petit
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
static void torrentReallyStop( tr_handle_t * h, int t );
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

    h->bindPort = TR_DEFAULT_PORT;
    h->bindSocket = -1;

#ifndef BEOS_NETSERVER
    /* BeOS net_server seems to be unable to set incoming connections to
       non-blocking. Too bad. */
    if( !tr_fdSocketWillCreate( h->fdlimit, 0 ) )
    {
        /* XXX should handle failure here in a better way */
        h->bindSocket = tr_netBind( h->bindPort );
    }
#endif


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
    int ii, sock;

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

    for( ii = 0; ii < h->torrentCount; ii++ )
    {
        tr_lockLock( &h->torrents[ii]->lock );
        if( NULL != h->torrents[ii]->tracker )
        {
            tr_trackerChangePort( h->torrents[ii]->tracker, port );
        }
        tr_lockUnlock( &h->torrents[ii]->lock );
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
 * tr_torrentRates
 ***********************************************************************
 *
 **********************************************************************/
void tr_torrentRates( tr_handle_t * h, float * dl, float * ul )
{
    *dl = tr_rcRate( h->download );
    *ul = tr_rcRate( h->upload );
}

/***********************************************************************
 * tr_torrentInit
 ***********************************************************************
 * Allocates a tr_torrent_t structure, then relies on tr_metainfoParse
 * to fill it.
 **********************************************************************/
int tr_torrentInit( tr_handle_t * h, const char * path )
{
    tr_torrent_t  * tor;
    tr_info_t     * inf;
    int             i;
    char          * s1, * s2;

    if( h->torrentCount >= TR_MAX_TORRENT_COUNT )
    {
        tr_err( "Maximum number of torrents reached" );
        return 1;
    }

    tor = calloc( sizeof( tr_torrent_t ), 1 );
    inf = &tor->info;

    /* Parse torrent file */
    if( tr_metainfoParse( inf, path ) )
    {
        free( tor );
        return 1;
    }

    /* Make sure this torrent is not already open */
    for( i = 0; i < h->torrentCount; i++ )
    {
        if( !memcmp( tor->info.hash, h->torrents[i]->info.hash,
                     SHA_DIGEST_LENGTH ) )
        {
            tr_err( "Torrent already open" );
            free( tor );
            return 1;
        }
    }

    tor->status = TR_STATUS_PAUSE;
    tor->id     = h->id;
    tor->key    = h->key;
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
    h->torrents[h->torrentCount] = tor;
    (h->torrentCount)++;
    tr_lockUnlock( &h->acceptLock );

    return 0;
}

/***********************************************************************
 * tr_torrentScrape
 ***********************************************************************
 * Allocates a tr_torrent_t structure, then relies on tr_metainfoParse
 * to fill it.
 **********************************************************************/
int tr_torrentScrape( tr_handle_t * h, int t, int * s, int * l )
{
    return tr_trackerScrape( h->torrents[t], s, l );
}

void tr_torrentSetFolder( tr_handle_t * h, int t, const char * path )
{
    tr_torrent_t * tor = h->torrents[t];

    tor->destination = strdup( path );
}

char * tr_torrentGetFolder( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];

    return tor->destination;
}

void tr_torrentStart( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];

    if( tor->status & ( TR_STATUS_STOPPING | TR_STATUS_STOPPED ) )
    {
        /* Join the thread first */
        torrentReallyStop( h, t );
    }

    tor->status   = TR_STATUS_CHECK;
    tor->tracker  = tr_trackerInit( h, tor );

    tor->date = tr_date();
    tor->die = 0;
    tr_threadCreate( &tor->thread, downloadLoop, tor );
}

void tr_torrentStop( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];

    tr_lockLock( &tor->lock );
    tr_trackerStopped( tor->tracker );
    tor->status = TR_STATUS_STOPPING;
    tor->stopDate = tr_date();
    tr_lockUnlock( &tor->lock );
}

/***********************************************************************
 * torrentReallyStop
 ***********************************************************************
 * Joins the download thread and frees/closes everything related to it.
 **********************************************************************/
static void torrentReallyStop( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];

    tor->die = 1;
    tr_threadJoin( &tor->thread );
    tr_dbg( "Thread joined" );

    tr_trackerClose( tor->tracker );

    while( tor->peerCount > 0 )
    {
        tr_peerRem( tor, 0 );
    }

    tor->downloaded = 0;
    tor->uploaded   = 0;
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

int tr_getFinished( tr_handle_t * h, int i)
{
	return h->torrents[i]->finished;
}
void tr_setFinished( tr_handle_t * h, int i, int val)
{
	h->torrents[i]->finished = val;
}

int tr_torrentStat( tr_handle_t * h, tr_stat_t ** stat )
{
    tr_stat_t * s;
    tr_torrent_t * tor;
    tr_info_t * inf;
    int i, j, k, piece;

    if( h->torrentCount < 1 )
    {
        *stat = NULL;
        return 0;
    }

    s = malloc( h->torrentCount * sizeof( tr_stat_t ) );

    for( i = 0; i < h->torrentCount; i++ )
    {
        tor = h->torrents[i];
        inf = &tor->info;

        if( ( tor->status & TR_STATUS_STOPPED ) ||
            ( ( tor->status & TR_STATUS_STOPPING ) &&
              tr_date() > tor->stopDate + 60000 ) )
        {
            torrentReallyStop( h, i );
            tor->status = TR_STATUS_PAUSE;
        }

        tr_lockLock( &tor->lock );

        memcpy( &s[i].info, &tor->info, sizeof( tr_info_t ) );
        s[i].status = tor->status;
        memcpy( s[i].error, tor->error, sizeof( s[i].error ) );

        s[i].peersTotal       = 0;
        s[i].peersUploading   = 0;
        s[i].peersDownloading = 0;

        for( j = 0; j < tor->peerCount; j++ )
        {
            if( tr_peerIsConnected( tor->peers[j] ) )
            {
                (s[i].peersTotal)++;
                if( tr_peerIsUploading( tor->peers[j] ) )
                {
                    (s[i].peersUploading)++;
                }
                if( tr_peerIsDownloading( tor->peers[j] ) )
                {
                    (s[i].peersDownloading)++;
                }
            }
        }

        s[i].progress     = tr_cpCompletionAsFloat( tor->completion );
        s[i].rateDownload = tr_rcRate( tor->download );
        s[i].rateUpload   = tr_rcRate( tor->upload );
        
        s[i].seeders	  = tr_trackerSeeders(tor);
		s[i].leechers	  = tr_trackerLeechers(tor);

        if( s[i].rateDownload < 0.1 )
        {
            s[i].eta = -1;
        }
        else
        {
            s[i].eta = (float) ( 1.0 - s[i].progress ) *
                (float) inf->totalSize / s[i].rateDownload / 1024.0;
            if( s[i].eta > 99 * 3600 + 59 * 60 + 59 )
            {
                s[i].eta = -1;
            }
        }

        for( j = 0; j < 120; j++ )
        {
            piece = j * inf->pieceCount / 120;

            if( tr_cpPieceIsComplete( tor->completion, piece ) )
            {
                s[i].pieces[j] = -1;
                continue;
            }

            s[i].pieces[j] = 0;
            
            for( k = 0; k < tor->peerCount; k++ )
            {
                if( tr_peerBitfield( tor->peers[k] ) &&
                    tr_bitfieldHas( tr_peerBitfield( tor->peers[k] ), piece ) )
                {
                    (s[i].pieces[j])++;
                }
            }
        }

        s[i].downloaded = tor->downloaded;
        s[i].uploaded   = tor->uploaded;

        s[i].folder = tor->destination;

        tr_lockUnlock( &tor->lock );
    }

    *stat = s;
    return h->torrentCount;
}

/***********************************************************************
 * tr_torrentClose
 ***********************************************************************
 * Frees memory allocated by tr_torrentInit.
 **********************************************************************/
void tr_torrentClose( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];
    tr_info_t    * inf = &tor->info;

    if( tor->status & ( TR_STATUS_STOPPING | TR_STATUS_STOPPED ) )
    {
        /* Join the thread first */
        torrentReallyStop( h, t );
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
    free( tor );

    memmove( &h->torrents[t], &h->torrents[t+1],
             ( h->torrentCount - t ) * sizeof( void * ) );

    tr_lockUnlock( &h->acceptLock );
}

void tr_close( tr_handle_t * h )
{
    acceptStop( h );
    tr_chokingClose( h->choking );
    tr_fdClose( h->fdlimit );
    tr_rcClose( h->upload );
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
    int           ii, jj;
    uint8_t     * hash;

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
                for( jj = 0; jj < h->torrentCount; jj++ )
                {
                    tr_lockLock( &h->torrents[jj]->lock );
                    if( 0 == memcmp( h->torrents[jj]->info.hash, hash,
                                     SHA_DIGEST_LENGTH ) )
                    {
                      tr_peerAttach( h->torrents[jj], h->acceptPeers[ii] );
                      tr_lockUnlock( &h->torrents[jj]->lock );
                      goto removePeer;
                    }
                    tr_lockUnlock( &h->torrents[jj]->lock );
                }
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
