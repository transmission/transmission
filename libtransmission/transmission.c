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
#include <stdlib.h>
#include <string.h> /* memcpy */

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
#include "platform.h" /* tr_lock */
#include "ratecontrol.h"
#include "shared.h"
#include "stats.h"
#include "torrent.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
uint8_t*
tr_peerIdNew( void )
{
    int i;
    int val;
    int total = 0;
    uint8_t * buf = tr_new( uint8_t, 21 );
    const char * pool = "0123456789abcdefghijklmnopqrstuvwxyz";
    const int base = 36;

    memcpy( buf, PEERID_PREFIX, 8 );

    for( i=8; i<19; ++i ) {
        val = tr_rand( base );
        total += val;
        buf[i] = pool[val];
    }

    val = total % base ? base - (total % base) : 0;
    total += val;
    buf[19] = pool[val];
    buf[20] = '\0';

    return buf;
}

const uint8_t*
tr_getPeerId( void )
{
    static uint8_t * id = NULL;
    if( id == NULL )
        id = tr_peerIdNew( );
    return id;
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
    assert( mode==TR_ENCRYPTION_PREFERRED
         || mode==TR_ENCRYPTION_REQUIRED
         || mode==TR_PLAINTEXT_PREFERRED );

    handle->encryptionMode = mode;
}

/***
****
***/

tr_handle *
tr_initFull( const char * tag,
             int          isPexEnabled,
             int          isNatEnabled,
             int          publicPort,
             int          encryptionMode,
             int          isUploadLimitEnabled,
             int          uploadLimit,
             int          isDownloadLimitEnabled,
             int          downloadLimit,
             int          globalPeerLimit,
             int          messageLevel,
             int          isMessageQueueingEnabled )
{
    tr_handle * h;
    char buf[128];

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    tr_msgInit( );
    tr_setMessageLevel( messageLevel );
    tr_setMessageQueuing( isMessageQueueingEnabled );

    h = tr_new0( tr_handle, 1 );
    h->lock = tr_lockNew( );
    h->isPexEnabled = isPexEnabled ? 1 : 0;
    h->encryptionMode = encryptionMode;

    tr_netInit(); /* must go before tr_eventInit */

    tr_eventInit( h );
    while( !h->events )
        tr_wait( 50 );

    h->tag = strdup( tag );
    if( !h->tag ) {
        free( h );
        return NULL;
    }

    h->peerMgr = tr_peerMgrNew( h );

    /* Initialize rate and file descripts controls */

    h->upload = tr_rcInit();
    tr_rcSetLimit( h->upload, uploadLimit );
    h->useUploadLimit = isUploadLimitEnabled;

    h->download = tr_rcInit();
    tr_rcSetLimit( h->download, downloadLimit );
    h->useDownloadLimit = isDownloadLimitEnabled;

    tr_fdInit( globalPeerLimit );
    h->shared = tr_sharedInit( h, isNatEnabled, publicPort );
    h->isPortSet = publicPort >= 0;

    /* first %s is the application name
       second %s is the version number */
    snprintf( buf, sizeof( buf ), _( "%s %s started" ),
              TR_NAME, LONG_VERSION_STRING );
    tr_inf( "%s", buf );

    tr_statsInit( h );

    return h;
}

tr_handle * tr_init( const char * tag )
{
    return tr_initFull( tag,
                        TRUE, /* pex enabled */
                        FALSE, /* nat enabled */
                        -1, /* public port */
                        TR_ENCRYPTION_PREFERRED, /* encryption mode */
                        FALSE, /* use upload speed limit? */ 
                        -1, /* upload speed limit */
                        FALSE, /* use download speed limit? */
                        -1, /* download speed limit */
                        200, /* globalPeerLimit */
                        TR_MSG_INF, /* message level */
                        FALSE ); /* is message queueing enabled? */
}

/***
****
***/

void
tr_globalLock( struct tr_handle * handle )
{
    tr_lockLock( handle->lock );
}

void
tr_globalUnlock( struct tr_handle * handle )
{
    tr_lockUnlock( handle->lock );
}

int
tr_globalIsLocked( const struct tr_handle * handle )
{
    return handle && tr_lockHave( handle->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/

struct bind_port_data
{
    tr_handle * handle;
    int port;
};

static void
tr_setBindPortImpl( void * vdata )
{
    struct bind_port_data * data = vdata;
    tr_handle * handle = data->handle;
    const int port = data->port;

    handle->isPortSet = 1;
    tr_sharedSetPort( handle->shared, port );

    tr_free( data );
}

void
tr_setBindPort( tr_handle * handle, int port )
{
    struct bind_port_data * data = tr_new( struct bind_port_data, 1 );
    data->handle = handle;
    data->port = port;
    tr_runInEventThread( handle, tr_setBindPortImpl, data );
}

int
tr_getPublicPort( const tr_handle * h )
{
    assert( h != NULL );
    return tr_sharedGetPublicPort( h->shared );
}

void tr_natTraversalEnable( tr_handle * h, int enable )
{
    tr_globalLock( h );
    tr_sharedTraversalEnable( h->shared, enable );
    tr_globalUnlock( h );
}

const tr_handle_status *
tr_handleStatus( tr_handle * h )
{
    tr_handle_status * s;

    h->statCur = ( h->statCur + 1 ) % 2;
    s = &h->stats[h->statCur];

    tr_globalLock( h );

    s->natTraversalStatus = tr_sharedTraversalStatus( h->shared );
    s->publicPort = tr_sharedGetPublicPort( h->shared );

    tr_globalUnlock( h );

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


void
tr_setGlobalPeerLimit( tr_handle * handle UNUSED,
                       uint16_t    maxGlobalPeers )
{
    tr_fdSetPeerLimit( maxGlobalPeers );
}

uint16_t
tr_getGlobalPeerLimit( const tr_handle * handle UNUSED )
{
    return tr_fdGetPeerLimit( );
}

void
tr_torrentRates( tr_handle * h, float * toClient, float * toPeer )
{
    const tr_torrent * tor;
    tr_globalLock( h );

    *toClient = *toPeer = 0.0;
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        float c, p;
        tr_torrentGetRates( tor, &c, &p );
        *toClient += c;
        *toPeer += p;
    }

    tr_globalUnlock( h );
}

int
tr_torrentCount( const tr_handle * h )
{
    return h->torrentCount;
}

static void
tr_closeImpl( void * vh )
{
    tr_handle * h = vh;
    tr_torrent * t;

    tr_sharedShuttingDown( h->shared );
    tr_trackerShuttingDown( h );

    for( t=h->torrentList; t!=NULL; ) {
        tr_torrent * tmp = t;
        t = t->next;
        tr_torrentClose( tmp );
    }

    tr_peerMgrFree( h->peerMgr );

    tr_rcClose( h->upload );
    tr_rcClose( h->download );
    
    h->isClosed = TRUE;
}

static int
deadlineReached( const uint64_t deadline )
{
    return tr_date( ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 30

void
tr_close( tr_handle * h )
{
    const int maxwait_msec = SHUTDOWN_MAX_SECONDS * 1000;
    const uint64_t deadline = tr_date( ) + maxwait_msec;

    tr_statsClose( h );

    tr_runInEventThread( h, tr_closeImpl, h );
    while( !h->isClosed && !deadlineReached( deadline ) )
        tr_wait( 100 );

    tr_eventClose( h );
    while( h->events && !deadlineReached( deadline ) )
        tr_wait( 100 );

    tr_fdClose( );
    tr_lockFree( h->lock );
    free( h->tag );
    free( h );
}

tr_torrent **
tr_loadTorrents ( tr_handle   * h,
                  tr_ctor     * ctor,
                  int         * setmeCount )
{
    int i, n = 0;
    struct stat sb;
    DIR * odir = NULL;
    const char * dirname = tr_getTorrentsDirectory( );
    tr_torrent ** torrents;
    tr_list *l=NULL, *list=NULL;

    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */

    if( !stat( dirname, &sb )
        && S_ISDIR( sb.st_mode )
        && (( odir = opendir ( dirname ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_torrent * tor;
                char filename[MAX_PATH_LENGTH];
                tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, filename );
                tor = tr_torrentNew( h, ctor, NULL );
                if( tor != NULL ) {
                    tr_list_append( &list, tor );
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

    tr_list_free( &list, NULL );

    *setmeCount = n;
    tr_inf( _( "Loaded %d torrents" ), *setmeCount );
    return torrents;
}

/***
****
***/

void
tr_setPexEnabled( tr_handle * handle, int isPexEnabled )
{
    handle->isPexEnabled = isPexEnabled ? 1 : 0;
}

int
tr_isPexEnabled( const tr_handle * handle )
{
    return handle->isPexEnabled;
}
