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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include <event.h>

#include "transmission.h"
#include "fdlimit.h"
#include "list.h"
#include "net.h"
#include "platform.h"
#include "ratecontrol.h"
#include "shared.h"
#include "utils.h"

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
void
tr_peerIdNew ( char * buf, int buflen )
{
    int i;
    assert( buflen == TR_ID_LEN + 1 );

    snprintf( buf, TR_ID_LEN, "%s", PEERID_PREFIX );
    assert( strlen(buf) == 8 );
    for( i=8; i<TR_ID_LEN; ++i ) {
        const int r = tr_rand( 36 );
        buf[i] = ( r < 26 ) ? ( 'a' + r ) : ( '0' + r - 26 ) ;
    }
    buf[TR_ID_LEN] = '\0';
}

static void
libeventThreadFunc( void * unused UNUSED )
{
    tr_dbg( "libevent thread starting" );
    event_init( );
    for ( ;; )
    {
        event_dispatch( );
        tr_wait( 50 ); /* 1/20th of a second */
    }
    tr_dbg( "libevent thread exiting" );
}


/***********************************************************************
 * tr_init
 ***********************************************************************
 * Allocates a tr_handle_t structure and initializes a few things
 **********************************************************************/
tr_handle_t * tr_init( const char * tag )
{
    tr_handle_t * h;
    int           i;

    tr_msgInit();
    tr_threadNew( libeventThreadFunc, NULL, "libeventThreadFunc" );
    tr_netInit();
    tr_netResolveThreadInit();

    h = calloc( 1, sizeof( tr_handle_t ) );
    if( !h )
        return NULL;

    h->tag = strdup( tag );
    if( !h->tag ) {
        free( h );
        return NULL;
    }


    /* Azureus identity */
    for( i=0; i < TR_AZ_ID_LEN; ++i )
        h->azId[i] = tr_rand( 0xff );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    /* Initialize rate and file descripts controls */
    h->upload   = tr_rcInit();
    h->download = tr_rcInit();

    tr_fdInit();
    h->shared = tr_sharedInit( h );

    tr_inf( TR_NAME " " LONG_VERSION_STRING " started" );

    return h;
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/
void tr_setBindPort( tr_handle_t * h, int port )
{
    h->isPortSet = 1;
    tr_sharedSetPort( h->shared, port );
}

void tr_natTraversalEnable( tr_handle_t * h, int enable )
{
    tr_sharedLock( h->shared );
    tr_sharedTraversalEnable( h->shared, enable );
    tr_sharedUnlock( h->shared );
}

tr_handle_status_t * tr_handleStatus( tr_handle_t * h )
{
    tr_handle_status_t * s;

    h->statCur = ( h->statCur + 1 ) % 2;
    s = &h->stats[h->statCur];

    tr_sharedLock( h->shared );

    s->natTraversalStatus = tr_sharedTraversalStatus( h->shared );
    s->publicPort = tr_sharedGetPublicPort( h->shared );

    tr_sharedUnlock( h->shared );

    return s;
}

/***
****
***/

void
tr_setUseGlobalSpeedLimit( tr_handle_t  * h,
                           int            up_or_down,
                           int            use_flag )
{
    char * ch = up_or_down==TR_UP ? &h->useUploadLimit
                                  : &h->useDownloadLimit;
    *ch = use_flag;
}

void
tr_setGlobalSpeedLimit( tr_handle_t  * h,
                        int            up_or_down,
                        int            KiB_sec )
{
    if( up_or_down == TR_DOWN )
        tr_rcSetLimit( h->download, KiB_sec );
    else {
        tr_rcSetLimit( h->upload, KiB_sec );
        tr_sharedSetLimit( h->shared, KiB_sec );
    }
}

void
tr_getGlobalSpeedLimit( tr_handle_t  * h,
                        int            up_or_down,
                        int          * setme_enabled,
                        int          * setme_KiBsec )
{
    if( setme_enabled != NULL )
       *setme_enabled = up_or_down==TR_UP ? h->useUploadLimit
                                          : h->useDownloadLimit;
    if( setme_KiBsec != NULL )
       *setme_KiBsec = tr_rcGetLimit( up_or_down==TR_UP ? h->upload
                                                        : h->download );
}


void tr_torrentRates( tr_handle_t * h, float * dl, float * ul )
{
    tr_torrent_t * tor;

    *dl = 0.0;
    *ul = 0.0;
    tr_sharedLock( h->shared );
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_torrentReaderLock( tor );
        if( tor->cpStatus == TR_CP_INCOMPLETE )
            *dl += tr_rcRate( tor->download );
        *ul += tr_rcRate( tor->upload );
        tr_torrentReaderUnlock( tor );
    }
    tr_sharedUnlock( h->shared );
}

int tr_torrentCount( tr_handle_t * h )
{
    return h->torrentCount;
}

void tr_torrentIterate( tr_handle_t * h, tr_callback_t func, void * d )
{
    tr_torrent_t * tor, * next;

    for( tor = h->torrentList; tor; tor = next )
    {
        next = tor->next;
        func( tor, d );
    }
}

void tr_close( tr_handle_t * h )
{
    tr_rcClose( h->upload );
    tr_rcClose( h->download );
    
    tr_sharedClose( h->shared );
    tr_fdClose();
    free( h->tag );
    free( h );

    tr_netResolveThreadClose();
}

tr_torrent_t **
tr_loadTorrents ( tr_handle_t   * h,
                  const char    * destination,
                  int             flags,
                  int          * setmeCount )
{
    int i, n = 0;
    struct stat sb;
    DIR * odir = NULL;
    const char * torrentDir = tr_getTorrentsDirectory( );
    tr_torrent_t ** torrents;
    tr_list_t *l=NULL, *list=NULL;

    if( !stat( torrentDir, &sb )
        && S_ISDIR( sb.st_mode )
        && (( odir = opendir ( torrentDir ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_torrent_t * tor;
                char path[MAX_PATH_LENGTH];
                tr_buildPath( path, sizeof(path), torrentDir, d->d_name, NULL );
                tor = tr_torrentInit( h, path, destination, flags, NULL );
                if( tor != NULL ) {
                    tr_list_append( &list, tor );
                    //fprintf (stderr, "#%d - %s\n", n, tor->info.name );
                    n++;
                }
            }
        }
        closedir( odir );
    }

    torrents = tr_new( tr_torrent_t*, n );
    for( i=0, l=list; l!=NULL; l=l->next )
        torrents[i++] = (tr_torrent_t*) l->data;
    assert( i==n );

    tr_list_free( &list );

    *setmeCount = n;
    tr_inf( "Loaded %d torrents from disk", *setmeCount );
    return torrents;
}
