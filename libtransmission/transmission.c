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
 * tr_init
 ***********************************************************************
 * Allocates a tr_handle_t structure and initializes a few things
 **********************************************************************/
tr_handle_t * tr_init( const char * tag )
{
    tr_handle_t * h;
    int           i, r;

    tr_msgInit();
    tr_netResolveThreadInit();

    h = calloc( 1, sizeof( tr_handle_t ) );
    if( NULL == h )
    {
        return NULL;
    }

    h->tag = strdup( tag );
    if( NULL == h->tag )
    {
        free( h );
        return NULL;
    }

    /* Generate a peer id : "-TRxxyz-" + 12 random alphanumeric
       characters, where xx is the major version number, y is the
       minor version number, and z is the maintenance number (Azureus-style) */
    snprintf( h->id, sizeof h->id, "-TR%02d%01d%01d-",
              VERSION_MAJOR, VERSION_MINOR, VERSION_MAINTENANCE );
    for( i = 8; i < TR_ID_LEN; i++ )
    {
        r        = tr_rand( 36 );
        h->id[i] = ( r < 26 ) ? ( 'a' + r ) : ( '0' + r - 26 ) ;
    }

    /* Random key */
    for( i = 0; i < TR_KEY_LEN; i++ )
    {
        r         = tr_rand( 36 );
        h->key[i] = ( r < 26 ) ? ( 'a' + r ) : ( '0' + r - 26 ) ;
    }

    /* Azureus identity */
    for( i = 0; i < TR_AZ_ID_LEN; i++ )
    {
        h->azId[i] = tr_rand( 0xff );
    }

    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );

    /* Initialize rate and file descripts controls */
    h->upload   = tr_rcInit();
    h->download = tr_rcInit();

    tr_fdInit();
    h->shared = tr_sharedInit( h );

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

void tr_setGlobalUploadLimit( tr_handle_t * h, int limit )
{
    tr_rcSetLimit( h->upload, limit );
    tr_sharedSetLimit( h->shared, limit );
}

void tr_setGlobalDownloadLimit( tr_handle_t * h, int limit )
{
    tr_rcSetLimit( h->download, limit );
}

void tr_torrentRates( tr_handle_t * h, float * dl, float * ul )
{
    tr_torrent_t * tor;

    *dl = 0.0;
    *ul = 0.0;
    tr_sharedLock( h->shared );
    for( tor = h->torrentList; tor; tor = tor->next )
    {
        tr_lockLock( &tor->lock );
        if( tor->status & TR_STATUS_DOWNLOAD )
            *dl += tr_rcRate( tor->download );
        *ul += tr_rcRate( tor->upload );
        tr_lockUnlock( &tor->lock );
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
