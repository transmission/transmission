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

#include "tr-prefs.h"
#include "tr-torrent.h"
#include "conf.h"
#include "notify.h"
#include "util.h"

struct TrTorrentPrivate
{
   tr_torrent * handle;
   gboolean do_remove;
};


static void
tr_torrent_init(GTypeInstance *instance, gpointer g_class UNUSED )
{
    TrTorrent * self = TR_TORRENT( instance );
    struct TrTorrentPrivate * p;

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                                                  TR_TORRENT_TYPE,
                                                  struct TrTorrentPrivate );
    p->handle = NULL;

#ifdef REFDBG
    g_message( "torrent %p init", self );
#endif
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
    TrTorrent * self = TR_TORRENT( o );

    if( !isDisposed( self ) )
    {
        if( self->priv->handle )
        {
            if( self->priv->do_remove )
                tr_torrentRemove( self->priv->handle );
            else
                tr_torrentFree( self->priv->handle );
        }

        self->priv = NULL;
    }

    parent = g_type_class_peek(g_type_parent(TR_TORRENT_TYPE));
    parent->dispose( o );
}

void
tr_torrent_clear( TrTorrent * tor )
{
    g_return_if_fail( tor );
    g_return_if_fail( tor->priv );

    tor->priv->handle = NULL;
}

static void
tr_torrent_class_init( gpointer g_class, gpointer g_class_data UNUSED )
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(g_class);
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
      sizeof (TrTorrentClass),
      NULL,   /* base_init */
      NULL,   /* base_finalize */
      tr_torrent_class_init,   /* class_init */
      NULL,   /* class_finalize */
      NULL,   /* class_data */
      sizeof (TrTorrent),
      0,      /* n_preallocs */
      tr_torrent_init, /* instance_init */
      NULL,
    };
    type = g_type_register_static(G_TYPE_OBJECT, "TrTorrent", &info, 0);
  }
  return type;
}

tr_torrent *
tr_torrent_handle(TrTorrent *tor)
{
    return isDisposed( tor ) ? NULL : tor->priv->handle;
}

const tr_stat *
tr_torrent_stat(TrTorrent *tor)
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
statusChangedCallback( tr_torrent   * tor UNUSED,
                       cp_status_t    status,
                       void         * user_data )
{
    if( status == TR_CP_COMPLETE )
        g_idle_add( notifyInMainThread, user_data );
}
static TrTorrent *
maketorrent( tr_torrent * handle )
{
    TrTorrent * tor = g_object_new( TR_TORRENT_TYPE, NULL );
    tor->priv->handle = handle;
    tr_torrentSetStatusCallback( handle, statusChangedCallback, tor );
    return tor;
}

TrTorrent*
tr_torrent_new_preexisting( tr_torrent * tor )
{
    return maketorrent( tor );
}

TrTorrent *
tr_torrent_new_ctor( tr_handle  * handle,
                     tr_ctor    * ctor,
                     char      ** err )
{
    tr_torrent * tor;
    int errcode;
    uint8_t doTrash = FALSE;

    errcode = -1;
    *err = NULL;

    /* let the gtk client handle the removal, since libT
     * doesn't have any concept of the glib trash API */
    tr_ctorGetDeleteSource( ctor, &doTrash );
    tr_ctorSetDeleteSource( ctor, FALSE );
    tor = tr_torrentNew( handle, ctor, &errcode );

    if( tor && doTrash )
        tr_file_trash_or_unlink( tr_ctorGetSourceFile( ctor ) );
  
    if( !tor )
    {
        const char * filename = tr_ctorGetSourceFile( ctor );
        if( !filename )
            filename = "(null)";

        switch( errcode )
        {
            case TR_EINVALID:
                *err = g_strdup_printf( _( "File \"%s\" isn't a valid torrent" ), filename );
                 break;
            case TR_EDUPLICATE:
                *err = g_strdup_printf( _( "File \"%s\" is already open" ), filename );
                break;
            default:
                *err = g_strdup( filename );
                break;
        }

        return NULL;
    }

    return maketorrent( tor );
}

char *
tr_torrent_status_str ( TrTorrent * gtor )
{
    char * top = 0;

    const tr_stat * st = tr_torrent_stat( gtor );

    const int tpeers = MAX (st->peersConnected, 0);
    const int upeers = MAX (st->peersGettingFromUs, 0);
    const int eta = st->eta;
    double prog = st->percentDone * 100.0; /* [0...100] */

    switch( st->status )
    {
        case TR_STATUS_CHECK_WAIT:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Waiting to verify local data (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_CHECK:
            prog = st->recheckProgress * 100.0; /* [0...100] */
            top = g_strdup_printf( _("Verifying local data (%.1f%% tested)"), prog );
            break;

        case TR_STATUS_DOWNLOAD:

            if( eta == TR_ETA_NOT_AVAIL )
                top = g_strdup_printf( _("Data not fully available (%.1f%%)" ), prog );
            else if( eta == TR_ETA_UNKNOWN )
                top = g_strdup_printf( _( "Stalled (%.1f%%)" ), prog );
            else {
                char timestr[128];
                tr_strltime( timestr, eta, sizeof( timestr ) );
                /* %1$s is # of minutes
                   %2$.1f is a percentage of how much of the torrent is done */
                top = g_strdup_printf( _("%1$s remaining (%2$.1f%%)"), timestr, prog );
            }
            break;

        case TR_STATUS_SEED:
            top = g_strdup_printf(
                ngettext( "Seeding to %1$'d of %2$'d connected peer",
                          "Seeding to %1$'d of %2$'d connected peers", tpeers ),
                          upeers, tpeers );
            break;

        case TR_STATUS_STOPPED:
            top = g_strdup_printf( _("Stopped (%.1f%%)"), prog );
            break;

        default:
            top = g_strdup_printf( "???" );
            break;

    }

    return top;
}

void
tr_torrent_set_remove_flag( TrTorrent * gtor, gboolean do_remove )
{
    if( !isDisposed( gtor ) )
        gtor->priv->do_remove = do_remove;
}

void
tr_torrent_delete_files( TrTorrent * gtor )
{
    tr_file_index_t i;
    const tr_info * info = tr_torrent_info( gtor );
    const char * stop = tr_torrentGetDownloadDir( tr_torrent_handle( gtor ) );

    for( i=0; info && i<info->fileCount; ++i )
    {
        char * file = g_build_filename( stop, info->files[i].name, NULL );
        while( strcmp( stop, file ) && strlen(stop) < strlen(file) )
        {
            char * swap = g_path_get_dirname( file );
            tr_file_trash_or_unlink( file );
            g_free( file );
            file = swap;
        }
        g_free( file );
    }
}

void
tr_torrent_open_folder( TrTorrent * gtor )
{
    tr_torrent * tor = tr_torrent_handle( gtor );
    const tr_info * info = tr_torrent_info( gtor );
    char * path = info->fileCount == 1
        ? g_build_filename( tr_torrentGetDownloadDir(tor), NULL )
        : g_build_filename( tr_torrentGetDownloadDir(tor), info->name, NULL );
    gtr_open_file( path );
    g_free( path );
}
