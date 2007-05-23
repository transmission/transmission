/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2007 Transmission authors and contributors
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "bencode.h"

#include "tr_backend.h"
#include "tr_core.h"
#include "tr_torrent.h"
#include "util.h"

static void
tr_core_init( GTypeInstance * instance, gpointer g_class );
static void
tr_core_class_init( gpointer g_class, gpointer g_class_data );
static void
tr_core_dispose( GObject * obj );

GType
tr_core_get_type( void )
{
    static GType type = 0;

    if( 0 == type )
    {
        static const GTypeInfo info =
        {
            sizeof( TrCoreClass ),
            NULL,                       /* base_init */
            NULL,                       /* base_finalize */
            tr_core_class_init,         /* class_init */
            NULL,                       /* class_finalize */
            NULL,                       /* class_data */
            sizeof( TrCore ),
            0,                          /* n_preallocs */
            tr_core_init,               /* instance_init */
            NULL,
        };
        type = g_type_register_static( G_TYPE_OBJECT, "TrCore", &info, 0 );
    }

    return type;
}

static void
tr_core_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass * gobject_class;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;
}

static void
tr_core_init( GTypeInstance * instance, gpointer g_class SHUTUP )
{
    TrCore * self = (TrCore *) instance;
    GtkListStore * store;

    /* column types for the model used to store torrent information */
    /* keep this in sync with the enum near the bottom of tr_core.h */
    GType types[] =
    {
        /* info->name, info->totalSize, status,     error,      errorString, */
        G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING,
        /* progress,  rateDownload, rateUpload,   eta,        peersTotal, */
        G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_INT, G_TYPE_INT,
        /* peersUploading, peersDownloading, seeders,    leechers */
        G_TYPE_INT,        G_TYPE_INT,       G_TYPE_INT, G_TYPE_INT,
        /* completedFromTracker, downloaded,    uploaded       left */
        G_TYPE_INT,              G_TYPE_UINT64, G_TYPE_UINT64, G_TYPE_UINT64,
        /* tracker,            the TrTorrent object */
        TR_TRACKER_BOXED_TYPE, TR_TORRENT_TYPE,
    };

    /* create the model used to store torrent data */
    g_assert( ALEN( types ) == MC_ROW_COUNT );
    store = gtk_list_store_newv( MC_ROW_COUNT, types );

    self->model    = GTK_TREE_MODEL( store );
    self->backend  = tr_backend_new();
    self->quitting = FALSE;
    self->disposed = FALSE;
}

static void
tr_core_dispose( GObject * obj )
{
    TrCore * self = (TrCore *) obj;
    GObjectClass * parent;

    if( self->disposed )
    {
        return;
    }
    self->disposed = TRUE;

    g_object_unref( self->model );
    g_object_unref( self->backend );

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );
    parent->dispose( obj );
}

TrCore *
tr_core_new( void )
{
    return g_object_new( TR_CORE_TYPE, NULL );
}

GtkTreeModel *
tr_core_model( TrCore * self )
{
    TR_IS_CORE( self );

    return self->model;
}

tr_handle_t *
tr_core_handle( TrCore * self )
{
    TR_IS_CORE( self );

    return tr_backend_handle( self->backend );
}

void
tr_core_quit( TrCore * self )
{
    GtkTreeIter iter;
    TrTorrent * tor;

    TR_IS_CORE( self );

    g_assert( !self->quitting );
    self->quitting = TRUE;

    /*
      Add a reference to all torrents in the list, which will be
      removed when the politely-stopped signal is emitted. This is
      necessary because a reference is added when a torrent is removed
      from the model and tr_torrent_stop_polite() is called on it.
    */
    if( gtk_tree_model_get_iter_first( self->model, &iter) )
    {
        do
        {
            gtk_tree_model_get( self->model, &iter, MC_TORRENT, &tor, -1 );
        }
        while( gtk_tree_model_iter_next( self->model, &iter ) );
    }

    /* try to politely stop all the torrents */
    tr_backend_stop_torrents( self->backend );

    /* shut down nat traversal */
    tr_natTraversalEnable( tr_backend_handle( self->backend ), 0 );
}

gboolean
tr_core_did_quit( TrCore * self )
{
    tr_handle_status_t * hstat;

    TR_IS_CORE( self );
    g_assert( self->quitting );

    hstat = tr_handleStatus( tr_backend_handle( self->backend ) );

    return ( tr_backend_torrents_stopped( self->backend, FALSE ) &&
             TR_NAT_TRAVERSAL_DISABLED == hstat->natTraversalStatus );
}

void
tr_core_force_quit( TrCore * self )
{
    TR_IS_CORE( self );
    g_assert( self->quitting );

    /* time the remaining torrents out so they signal politely-stopped */
    tr_backend_torrents_stopped( self->backend, TRUE );
}

void
tr_core_reap( TrCore * self )
{
    TR_IS_CORE( self );

    tr_backend_torrents_stopped( self->backend, FALSE );
}

GList *
tr_core_load( TrCore * self, benc_val_t * state, GList ** errors )
{
    TR_IS_CORE( self );

    return tr_backend_load_state( self->backend, state, 0, errors );
}

void
tr_core_save( TrCore * self, char ** error )
{
    TR_IS_CORE( self );

    tr_backend_save_state( self->backend, error );
}

TrTorrent *
tr_core_new_torrent( TrCore * self, const char * torrent, const char * dir,
                     guint flags, char ** err )
{
    TR_IS_CORE( self );

    return tr_torrent_new( G_OBJECT( self->backend ),
                           torrent, dir, flags, err );
}
