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
#include "transmission.h"

/* XXX */
#define TR_WANT_TORRENT_PRIVATE

#include "conf.h"
#include "tr_core.h"
#include "tr_torrent.h"
#include "util.h"

static void
tr_core_init( GTypeInstance * instance, gpointer g_class );
static void
tr_core_class_init( gpointer g_class, gpointer g_class_data );
static void
tr_core_dispose( GObject * obj );
static void
tr_core_torrent_finalized( gpointer gdata, GObject * tor );
static int
tr_core_check_torrents( TrCore * self, gboolean timeout );
static void
tr_core_insert( TrCore * self, TrTorrent * tor );

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

void
tr_core_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass * gobject_class;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;
}

void
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
    self->handle   = tr_init( "gtk" );
    self->torrents = NULL;
    self->quitting = FALSE;
    self->disposed = FALSE;
}

void
tr_core_dispose( GObject * obj )
{
    TrCore       * self = (TrCore *) obj;
    GObjectClass * parent;
    GList        * ii;

    if( self->disposed )
    {
        return;
    }
    self->disposed = TRUE;

    if( NULL != self->model )
    {
        g_object_unref( self->model );
        self->model = NULL;
    }

    if( NULL != self->torrents )
    {
        for( ii = g_list_first( self->torrents ); NULL != ii; ii = ii->next )
        {
            g_object_weak_unref( ii->data, tr_core_torrent_finalized, self );
        }
        g_list_free( self->torrents );
        self->torrents = NULL;
    }

    tr_close( self->handle );

    /* Chain up to the parent class */
    parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );
    parent->dispose( obj );
}

void
tr_core_torrent_finalized( gpointer gdata, GObject * tor )
{
    TrCore * self = gdata;

    TR_IS_CORE( self );

    self->torrents = g_list_remove( self->torrents, tor );
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

    return self->handle;
}

void
tr_core_quit( TrCore * self )
{
    GtkTreeIter iter;
    TrTorrent * tor;
    GList     * ii;

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
    for( ii = g_list_first( self->torrents ); NULL != ii; ii = ii->next )
    {
        tr_torrent_stop_politely( ii->data );
    }

    /* shut down nat traversal */
    tr_natTraversalEnable( self->handle, 0 );
}

gboolean
tr_core_did_quit( TrCore * self )
{
    tr_handle_status_t * hstat;

    TR_IS_CORE( self );
    g_assert( self->quitting );

    hstat = tr_handleStatus( self->handle );

    return ( 0 == tr_core_check_torrents( self, FALSE ) &&
             TR_NAT_TRAVERSAL_DISABLED == hstat->natTraversalStatus );
}

void
tr_core_force_quit( TrCore * self )
{
    TR_IS_CORE( self );
    g_assert( self->quitting );

    /* time the remaining torrents out so they signal politely-stopped */
    tr_core_check_torrents( self, TRUE );
}

void
tr_core_clear( TrCore * self )
{
    TR_IS_CORE( self );

    if( NULL != self->model )
    {
        g_object_unref( self->model );
        self->model = NULL;
    }
}

void
tr_core_reap( TrCore * self )
{
    TR_IS_CORE( self );

    tr_core_check_torrents( self, FALSE );
}

int
tr_core_check_torrents( TrCore * self, gboolean timeout )
{
    tr_stat_t * st;
    GList     * ii, * list;
    int         count;

    count = 0;
    list  = g_list_copy( self->torrents );
    for( ii = g_list_first( list ); NULL != ii; ii = ii->next )
    {
        st = tr_torrent_stat_polite( ii->data, timeout );
        if( NULL == st || !( TR_STATUS_PAUSE & st->status ) )
        {
            count++;
        }
    }
    g_list_free( list );

    return count;
}

void
tr_core_save( TrCore * self, char ** error )
{
    benc_val_t state;
    GList    * ii;

    TR_IS_CORE( self );

    tr_bencInit( &state, TYPE_LIST );
    if( tr_bencListReserve( &state, g_list_length( self->torrents ) ) )
    {
        if( NULL != error )
        {
            *error = g_strdup( "malloc failure" );
        }
        return;
    }

    for( ii = g_list_first( self->torrents ); NULL != ii; ii = ii->next )
    {
        tr_torrent_get_state( ii->data, tr_bencListAdd( &state ) );
    }

    cf_savestate( &state, error );
    tr_bencFree( &state );

    for( ii = g_list_first( self->torrents ); NULL != ii; ii = ii->next )
    {
        tr_torrent_state_saved( ii->data );
    }
}

int
tr_core_load( TrCore * self, benc_val_t * state, GList ** errors )
{
    int         ii, count;
    char      * errstr;
    TrTorrent * tor;

    TR_IS_CORE( self );

    if( TYPE_LIST != state->type )
    {
        return 0;
    }

    count = 0;
    for( ii = 0; ii < state->val.l.count; ii++ )
    {
        errstr = NULL;
        tor = tr_torrent_new_with_state( G_OBJECT( self ),
                                         state->val.l.vals + ii, 0, &errstr );
        if( NULL != errstr )
        {
            *errors = g_list_append( *errors, errstr );
        }
        if( NULL != tor )
        {
            tr_core_insert( self, tor );
            count++;
        }
    }

    return count;
}

gboolean
tr_core_add_torrent( TrCore * self, const char * torrent, const char * dir,
                     guint flags, char ** err )
{
    TrTorrent * tor;

    TR_IS_CORE( self );

    tor = tr_torrent_new( G_OBJECT( self ), torrent, dir, flags, err );
    if( NULL == tor )
    {
        return FALSE;
    }

    tr_core_insert( self, tor );

    return TRUE;
}

void
tr_core_delete_torrent( TrCore * self, void * torrent,
                        GtkTreeIter * iter )
{
    TR_IS_CORE( self );

    /* tor will be unref'd in the politely_stopped handler */
    g_object_ref( torrent );
    tr_torrent_stop_politely( torrent );
    if( TR_FLAG_SAVE & tr_torrent_info( torrent )->flags )
    {
        tr_torrentRemoveSaved( tr_torrent_handle( torrent ) );
    }
    gtk_list_store_remove( GTK_LIST_STORE( self->model ), iter );
}

void
tr_core_insert( TrCore * self, TrTorrent * tor )
{
    GtkTreeIter iter;
    tr_info_t * inf;

    g_object_weak_ref( G_OBJECT( tor ), tr_core_torrent_finalized, self );
    self->torrents = g_list_append( self->torrents, tor );

    gtk_list_store_append( GTK_LIST_STORE( self->model ), &iter );
    inf = tr_torrent_info( tor );

    /* inserting the torrent into the model adds a reference */
    gtk_list_store_set( GTK_LIST_STORE( self->model ), &iter,
                        MC_NAME,    inf->name,
                        MC_SIZE,    inf->totalSize,
                        MC_TORRENT, tor,
                        -1);

    /* we will always ref a torrent before politely stopping it */
    g_signal_connect( tor, "politely_stopped",
                      G_CALLBACK( g_object_unref ), NULL );

    g_object_unref( tor );
}

void
tr_core_update( TrCore * self )
{
    GtkTreeIter iter;
    TrTorrent * tor;
    tr_stat_t * st;

    TR_IS_CORE( self );

    if( gtk_tree_model_get_iter_first( self->model, &iter ) )
    {
        do
        {
            gtk_tree_model_get( self->model, &iter, MC_TORRENT, &tor, -1 );
            st = tr_torrent_stat( tor );
            g_object_unref( tor );

            /* XXX find out if setting the same data emits changed signal */
            gtk_list_store_set( GTK_LIST_STORE( self->model ), &iter,
                                MC_STAT,        st->status,
                                MC_ERR,         st->error,
                                MC_TERR,        st->errorString,
                                MC_PROG,        st->progress,
                                MC_DRATE,       st->rateDownload,
                                MC_URATE,       st->rateUpload,
                                MC_ETA,         st->eta,
                                MC_PEERS,       st->peersTotal,
                                MC_UPEERS,      st->peersUploading,
                                MC_DPEERS,      st->peersDownloading,
                                MC_SEED,        st->seeders,
                                MC_LEECH,       st->leechers,
                                MC_DONE,        st->completedFromTracker,
                                MC_TRACKER,     st->tracker,
                                MC_DOWN,        st->downloaded,
                                MC_UP,          st->uploaded,
                                MC_LEFT,        st->left,
                                -1 );
        }
        while( gtk_tree_model_iter_next( self->model, &iter ) );
    }
}
