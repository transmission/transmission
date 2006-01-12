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
static void  downloadLoop( void * );
static float rateDownload( tr_torrent_t * );
static float rateUpload( tr_torrent_t * );

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

    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );

    /* Initialize rate and file descripts controls */
    h->upload  = tr_uploadInit();
    h->fdlimit = tr_fdInit();

    h->bindPort = 9090;
    
    return h;
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setBindPort( tr_handle_t * h, int port )
{
    /* FIXME multithread safety */
    h->bindPort = port;
}

/***********************************************************************
 * tr_setUploadLimit
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setUploadLimit( tr_handle_t * h, int limit )
{
    tr_uploadSetLimit( h->upload, limit );
}

/***********************************************************************
 * tr_torrentRates
 ***********************************************************************
 *
 **********************************************************************/
void tr_torrentRates( tr_handle_t * h, float * dl, float * ul )
{
    int            i;
    tr_torrent_t * tor;

    *dl = 0.0;
    *ul = 0.0;

    for( i = 0; i < h->torrentCount; i++ )
    {
        tor = h->torrents[i];
        tr_lockLock( tor->lock );
        *dl += rateDownload( tor );
        *ul += rateUpload( tor );
        tr_lockUnlock( tor->lock );
    }
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
    tor->blockHave  = calloc( tor->blockCount, 1 );
    tor->bitfield   = calloc( ( inf->pieceCount + 7 ) / 8, 1 );

    tr_lockInit( &tor->lock );

    tor->upload  = h->upload;
    tor->fdlimit = h->fdlimit;
 
    /* We have a new torrent */
    h->torrents[h->torrentCount] = tor;
    (h->torrentCount)++;

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
    uint64_t       now;
    int            i;

    tor->status   = TR_STATUS_CHECK;
    tor->tracker  = tr_trackerInit( h, tor );
    tor->bindPort = h->bindPort;
#ifndef BEOS_NETSERVER
    /* BeOS net_server seems to be unable to set incoming connections to
       non-blocking. Too bad. */
    if( !tr_fdSocketWillCreate( h->fdlimit, 0 ) )
    {
        tor->bindSocket = tr_netBind( &tor->bindPort );
    }
#endif

    now = tr_date();
    for( i = 0; i < 10; i++ )
    {
        tor->dates[i] = now;
    }

    tor->die = 0;
    tr_threadCreate( &tor->thread, downloadLoop, tor );
}

void tr_torrentStop( tr_handle_t * h, int t )
{
    tr_torrent_t * tor = h->torrents[t];

    tr_lockLock( tor->lock );
    tr_trackerStopped( tor->tracker );
    tor->status = TR_STATUS_STOPPING;
    tr_lockUnlock( tor->lock );
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
    tr_threadJoin( tor->thread );
    tr_dbg( "Thread joined" );

    tr_trackerClose( tor->tracker );

    while( tor->peerCount > 0 )
    {
        tr_peerRem( tor, 0 );
    }
    if( tor->bindSocket > -1 )
    {
        tr_netClose( tor->bindSocket );
        tr_fdSocketClosed( h->fdlimit, 0 );
    }

    memset( tor->downloaded, 0, sizeof( tor->downloaded ) );
    memset( tor->uploaded,   0, sizeof( tor->uploaded ) );
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

        tr_lockLock( tor->lock );

        if( tor->status & TR_STATUS_STOPPED )
        {
            torrentReallyStop( h, i );
            tor->status = TR_STATUS_PAUSE;
        }

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

        s[i].progress = (float) tor->blockHaveCount / (float) tor->blockCount;

        s[i].rateDownload = rateDownload( tor );
        s[i].rateUpload   = rateUpload( tor );

        if( s[i].rateDownload < 0.1 )
        {
            s[i].eta = -1;
        }
        else
        {
            s[i].eta = (float) (tor->blockCount - tor->blockHaveCount ) *
                (float) tor->blockSize / s[i].rateDownload / 1024.0;
            if( s[i].eta > 99 * 3600 + 59 * 60 + 59 )
            {
                s[i].eta = -1;
            }
        }

        for( j = 0; j < 120; j++ )
        {
            piece = j * inf->pieceCount / 120;

            if( tr_bitfieldHas( tor->bitfield, piece ) )
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

        s[i].downloaded = tor->downloaded[9];
        s[i].uploaded   = tor->uploaded[9];

        s[i].folder = tor->destination;

        tr_lockUnlock( tor->lock );
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
        tr_lockLock( tor->lock );
        torrentReallyStop( h, t );
        tr_lockUnlock( tor->lock );
    }

    h->torrentCount--;

    tr_lockClose( tor->lock );

    if( tor->destination )
    {
        free( tor->destination );
    }
    free( inf->pieces );
    free( inf->files );
    free( tor->blockHave );
    free( tor->bitfield );
    free( tor );

    memmove( &h->torrents[t], &h->torrents[t+1],
             ( h->torrentCount - t ) * sizeof( void * ) );
}

void tr_close( tr_handle_t * h )
{
    tr_fdClose( h->fdlimit );
    tr_uploadClose( h->upload );
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

    tor->io     = tr_ioInit( tor );
    tor->status = ( tor->blockHaveCount < tor->blockCount ) ?
                      TR_STATUS_DOWNLOAD : TR_STATUS_SEED;
    
    while( !tor->die )
    {
        date1 = tr_date();

        tr_lockLock( tor->lock );

        /* Are we finished ? */
        if( ( tor->status & TR_STATUS_DOWNLOAD ) &&
            tor->blockHaveCount >= tor->blockCount )
        {
            /* Done */
            tor->status = TR_STATUS_SEED;
            tr_trackerCompleted( tor->tracker );
        }

        /* Receive/send messages */
        if( !( tor->status & TR_STATUS_STOPPING ) )
        {
            tr_peerPulse( tor );
        }

        /* Try to get new peers or to send a message to the tracker */
        tr_trackerPulse( tor->tracker );

        tr_lockUnlock( tor->lock );

        if( tor->status & TR_STATUS_STOPPED )
        {
            break;
        }

        /* Wait up to 20 ms */
        date2 = tr_date();
        if( date2 < date1 + 20 )
        {
            tr_wait( date1 + 20 - date2 );
        }
    }

    tr_ioClose( tor->io );

    tor->status = TR_STATUS_STOPPED;

    tr_dbg( "Thread exited" );
}

/***********************************************************************
 * rateDownload, rateUpload
 **********************************************************************/
static float rateGeneric( uint64_t * dates, uint64_t * counts )
{
    float ret;
    int i;

    ret = 0.0;
    for( i = 0; i < 9; i++ )
    {
        if( dates[i+1] == dates[i] )
        {
            continue;
        }
        ret += (float) ( i + 1 ) * 1000.0 / 1024.0 *
            (float) ( counts[i+1] - counts[i] ) /
            (float) ( dates[i+1] - dates[i] );
    }
    ret *= 1000.0 / 1024.0 / 45.0;

    return ret;
}
static float rateDownload( tr_torrent_t * tor )
{
    return rateGeneric( tor->dates, tor->downloaded );
}
static float rateUpload( tr_torrent_t * tor )
{
    return rateGeneric( tor->dates, tor->uploaded );
}
