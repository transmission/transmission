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

#include "transmission.h"
#include "fdlimit.h"
#include "list.h"
#include "net.h"
#include "peer-mgr.h"
#include "platform.h"
#include "ratecontrol.h"
#include "shared.h"
#include "trevent.h"
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

const char*
getPeerId( void )
{
    static char * peerId = NULL;
    if( !peerId ) {
        peerId = tr_new0( char, TR_ID_LEN + 1 );
        tr_peerIdNew( peerId, TR_ID_LEN + 1 );
    }
    return peerId;
}

/***
****
***/

tr_encryption_mode
tr_getEncryptionMode( tr_handle * handle )
{
    assert( handle != NULL );

    return handle->encryptionMode;
}

void
tr_setEncryptionMode( tr_handle * handle, tr_encryption_mode mode )
{
    assert( handle != NULL );
    assert( mode==TR_ENCRYPTION_PREFERRED || mode==TR_ENCRYPTION_REQUIRED );

    handle->encryptionMode = mode;
}

/***
****
***/


/***********************************************************************
 * tr_init
 ***********************************************************************
 * Allocates a tr_handle structure and initializes a few things
 **********************************************************************/
tr_handle * tr_init( const char * tag )
{
    tr_handle * h;
    int         i;

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    tr_msgInit();

    h = tr_new0( tr_handle, 1 );
    if( !h )
        return NULL;

    h->encryptionMode = TR_ENCRYPTION_PREFERRED;

    tr_eventInit( h );
    while( !h->events )
        tr_wait( 50 );

    tr_netInit();

    h->tag = strdup( tag );
    if( !h->tag ) {
        free( h );
        return NULL;
    }

    h->peerMgr = tr_peerMgrNew( h );

    /* Azureus identity */
    for( i=0; i < TR_AZ_ID_LEN; ++i )
        h->azId[i] = tr_rand( 0xff );

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
void tr_setBindPort( tr_handle * h, int port )
{
    h->isPortSet = 1;
    tr_sharedSetPort( h->shared, port );
}

int
tr_getPublicPort( const tr_handle * h )
{
    assert( h != NULL );
    return tr_sharedGetPublicPort( h->shared );
}

void tr_natTraversalEnable( tr_handle * h, int enable )
{
    tr_sharedLock( h->shared );
    tr_sharedTraversalEnable( h->shared, enable );
    tr_sharedUnlock( h->shared );
}

tr_handle_status * tr_handleStatus( tr_handle * h )
{
    tr_handle_status * s;

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
tr_setUseGlobalSpeedLimit( tr_handle  * h,
                           int          up_or_down,
                           int          use_flag )
{
    char * ch = up_or_down==TR_UP ? &h->useUploadLimit
                                  : &h->useDownloadLimit;
    *ch = use_flag;
}

void
tr_setGlobalSpeedLimit( tr_handle  * h,
                        int          up_or_down,
                        int          KiB_sec )
{
    if( up_or_down == TR_DOWN )
        tr_rcSetLimit( h->download, KiB_sec );
    else
        tr_rcSetLimit( h->upload, KiB_sec );
}

void
tr_getGlobalSpeedLimit( tr_handle  * h,
                        int          up_or_down,
                        int        * setme_enabled,
                        int          * setme_KiBsec )
{
    if( setme_enabled != NULL )
       *setme_enabled = up_or_down==TR_UP ? h->useUploadLimit
                                          : h->useDownloadLimit;
    if( setme_KiBsec != NULL )
       *setme_KiBsec = tr_rcGetLimit( up_or_down==TR_UP ? h->upload
                                                        : h->download );
}


void tr_torrentRates( tr_handle * h, float * dl, float * ul )
{
    tr_torrent * tor;

    *dl = 0.0;
    *ul = 0.0;
    tr_sharedLock( h->shared );
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_torrentLock( tor );
        if( tor->cpStatus == TR_CP_INCOMPLETE )
            *dl += tr_rcRate( tor->download );
        *ul += tr_rcRate( tor->upload );
        tr_torrentUnlock( tor );
    }
    tr_sharedUnlock( h->shared );
}

int tr_torrentCount( tr_handle * h )
{
    return h->torrentCount;
}

void tr_torrentIterate( tr_handle * h, tr_callback_t func, void * d )
{
    tr_torrent * tor, * next;

    for( tor = h->torrentList; tor; tor = next )
    {
        next = tor->next;
        func( tor, d );
    }
}

static void
tr_closeImpl( void * vh )
{
    tr_handle * h = vh;
fprintf( stderr, "in tr_closeImpl\n" );
    tr_peerMgrFree( h->peerMgr );
fprintf( stderr, "calling mgr free\n" );

    tr_rcClose( h->upload );
    tr_rcClose( h->download );

fprintf( stderr, "calling shared close\n" );
    tr_sharedClose( h->shared );
fprintf( stderr, "calling fd close\n" );
    tr_fdClose();

fprintf( stderr, "setting h->closed to TRUE\n" );
    h->isClosed = TRUE;
}
void
tr_close( tr_handle * h )
{
    fprintf( stderr, "torrentCount is %d\n", h->torrentCount );
    assert( tr_torrentCount( h ) == 0 );

fprintf( stderr, "here I am in tr_close...\n" );
    tr_runInEventThread( h, tr_closeImpl, h );
    while( !h->isClosed )
        tr_wait( 200 );

    tr_eventClose( h );
    while( h->events != NULL ) {
        fprintf( stderr, "waiting for libevent thread to close...\n" );
        tr_wait( 200 );
    }

    free( h->tag );
    free( h );
}

tr_torrent **
tr_loadTorrents ( tr_handle   * h,
                  const char  * destination,
                  int           flags,
                  int         * setmeCount )
{
    int i, n = 0;
    struct stat sb;
    DIR * odir = NULL;
    const char * torrentDir = tr_getTorrentsDirectory( );
    tr_torrent ** torrents;
    tr_list *l=NULL, *list=NULL;

    if( !stat( torrentDir, &sb )
        && S_ISDIR( sb.st_mode )
        && (( odir = opendir ( torrentDir ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_torrent * tor;
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

    torrents = tr_new( tr_torrent*, n );
    for( i=0, l=list; l!=NULL; l=l->next )
        torrents[i++] = (tr_torrent*) l->data;
    assert( i==n );

    tr_list_free( &list );

    *setmeCount = n;
    tr_inf( "Loaded %d torrents from disk", *setmeCount );
    return torrents;
}
