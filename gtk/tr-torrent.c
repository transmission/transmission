/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2008 Transmission authors and contributors
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

#include <string.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_truncd() */

#include "tr-prefs.h"
#include "tr-torrent.h"
#include "conf.h"
#include "notify.h"
#include "util.h"

struct TrTorrentPrivate
{
    tr_torrent *  handle;
    gboolean      do_remove;
    gboolean      delete_local_data;
};


static void
tr_torrent_init( GTypeInstance * instance, gpointer g_class UNUSED )
{
    struct TrTorrentPrivate * p;
    TrTorrent * self = TR_TORRENT( instance );

    p = G_TYPE_INSTANCE_GET_PRIVATE( self, TR_TORRENT_TYPE, struct TrTorrentPrivate );
    p->handle = NULL;
    p->do_remove = FALSE;
    p->delete_local_data = FALSE;

    self->priv = p;
}

static int
isDisposed( const TrTorrent * tor )
{
    return !tor || !TR_IS_TORRENT( tor ) || !tor->priv;
}

static void
tr_torrent_dispose( GObject * o )
{
    GObjectClass * parent;
    TrTorrent *    self = TR_TORRENT( o );

    if( !isDisposed( self ) )
    {
        if( self->priv->handle )
        {
            if( self->priv->do_remove )
                tr_torrentRemove( self->priv->handle, self->priv->delete_local_data, gtr_file_trash_or_remove );
            else
                tr_torrentFree( self->priv->handle );
        }

        self->priv = NULL;
    }

    parent = g_type_class_peek( g_type_parent( TR_TORRENT_TYPE ) );
    parent->dispose( o );
}

static void
tr_torrent_class_init( gpointer              g_class,
                       gpointer g_class_data UNUSED )
{
    GObjectClass *gobject_class = G_OBJECT_CLASS( g_class );

    gobject_class->dispose = tr_torrent_dispose;
    g_type_class_add_private( g_class, sizeof( struct TrTorrentPrivate ) );
}

GType
tr_torrent_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info = {
            sizeof ( TrTorrentClass ),
            NULL,                     /* base_init */
            NULL,                     /* base_finalize */
            tr_torrent_class_init,    /* class_init */
            NULL,                     /* class_finalize */
            NULL,                     /* class_data */
            sizeof ( TrTorrent ),
            0,                        /* n_preallocs */
            tr_torrent_init,          /* instance_init */
            NULL,
        };
        type = g_type_register_static( G_TYPE_OBJECT, "TrTorrent", &info, 0 );
    }
    return type;
}

tr_torrent *
tr_torrent_handle( TrTorrent *tor )
{
    return isDisposed( tor ) ? NULL : tor->priv->handle;
}

const tr_stat *
tr_torrent_stat( TrTorrent *tor )
{
    tr_torrent * handle = tr_torrent_handle( tor );

    return handle ? tr_torrentStatCached( handle ) : NULL;
}

const tr_info *
tr_torrent_info( TrTorrent * tor )
{
    tr_torrent * handle = tr_torrent_handle( tor );

    return handle ? tr_torrentInfo( handle ) : NULL;
}

static gboolean
notifyInMainThread( gpointer user_data )
{
    tr_notify_send( TR_TORRENT( user_data ) );
    return FALSE;
}

static void
completenessChangedCallback( tr_torrent       * tor,
                             tr_completeness    completeness,
                             tr_bool            wasRunning,
                             void *             user_data )
{
    if( wasRunning && ( completeness != TR_LEECH ) && ( tr_torrentStat( tor )->sizeWhenDone != 0 ) )
        gtr_idle_add( notifyInMainThread, user_data );
}

static TrTorrent *
maketorrent( tr_torrent * tor )
{
    TrTorrent * gtor = g_object_new( TR_TORRENT_TYPE, NULL );

    gtor->priv->handle = tor;
    tr_torrentSetCompletenessCallback( tor, completenessChangedCallback, gtor );
    return gtor;
}

TrTorrent*
tr_torrent_new_preexisting( tr_torrent * tor )
{
    return maketorrent( tor );
}

TrTorrent *
tr_torrent_new_ctor( tr_session   * session,
                     tr_ctor      * ctor,
                     int          * errcode )
{
    tr_torrent * tor;
    uint8_t      doTrash = FALSE;

    /* let the gtk client handle the removal, since libT
     * doesn't have any concept of the glib trash API */
    tr_ctorGetDeleteSource( ctor, &doTrash );
    tr_ctorSetDeleteSource( ctor, FALSE );
    tor = tr_torrentNew( ctor, errcode );

    if( tor && doTrash )
    {
        const char * config = tr_sessionGetConfigDir( session );
        const char * source = tr_ctorGetSourceFile( ctor );
        const int is_internal = source && ( strstr( source, config ) == source );

        /* #1294: don't delete the source .torrent file if it's our internal copy */
        if( !is_internal )
            gtr_file_trash_or_remove( source );
    }

    return tor ? maketorrent( tor ) : NULL;
}

void
tr_torrent_set_remove_flag( TrTorrent * gtor, gboolean flag )
{
    if( !isDisposed( gtor ) )
        gtor->priv->do_remove = flag;
}

void
tr_torrent_set_delete_local_data_flag( TrTorrent * gtor, gboolean flag )
{
    if( !isDisposed( gtor ) )
        gtor->priv->delete_local_data = flag;
}


void
tr_torrent_open_folder( TrTorrent * gtor )
{
    const tr_torrent * tor =  tr_torrent_handle( gtor );

    if( tor != NULL )
    {
        const tr_info * info = tr_torrent_info( gtor );
        const gboolean single = info->fileCount == 1;
        const char * currentDir = tr_torrentGetCurrentDir( tor );
        if( single )
            gtr_open_file( currentDir );
        else {
            char * path = g_build_filename( currentDir, info->name, NULL );
            gtr_open_file( path );
            g_free( path );
        }
    }
}
