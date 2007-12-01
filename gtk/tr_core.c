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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>

#include "conf.h"
#include "tr_core.h"
#include "tr_prefs.h"
#include "tr_torrent.h"
#include "util.h"

static void
tr_core_marshal_err( GClosure * closure, GValue * ret SHUTUP, guint count,
                     const GValue * vals, gpointer hint SHUTUP,
                     gpointer marshal )
{
    typedef void (*TRMarshalErr)
        ( gpointer, enum tr_core_err, const char *, gpointer );
    TRMarshalErr     callback;
    GCClosure      * cclosure = (GCClosure*) closure;
    enum tr_core_err errcode;
    const char     * errstr;
    gpointer         inst, gdata;

    g_return_if_fail( 3 == count );

    inst    = g_value_peek_pointer( vals );
    errcode = g_value_get_int( vals + 1 );
    errstr  = g_value_get_string( vals + 2 );
    gdata   = closure->data;

    callback = (TRMarshalErr) ( NULL == marshal ?
                                cclosure->callback : marshal );
    callback( inst, errcode, errstr, gdata );
}

static void
tr_core_marshal_prompt( GClosure * closure, GValue * ret SHUTUP, guint count,
                        const GValue * vals, gpointer hint SHUTUP,
                        gpointer marshal )
{
    typedef void (*TRMarshalPrompt)
        ( gpointer, GList *, enum tr_torrent_action, gboolean, gpointer );
    TRMarshalPrompt        callback;
    GCClosure            * cclosure = (GCClosure*) closure;
    GList                * paths;
    enum tr_torrent_action action;
    gboolean               paused;
    gpointer               inst, gdata;

    g_return_if_fail( 4 == count );

    inst    = g_value_peek_pointer( vals );
    paths   = g_value_get_pointer( vals + 1 );
    action  = g_value_get_int( vals + 2 );
    paused  = g_value_get_boolean( vals + 3 );
    gdata   = closure->data;

    callback = (TRMarshalPrompt) ( NULL == marshal ?
                                   cclosure->callback : marshal );
    callback( inst, paths, action, paused, gdata );
}

static void
tr_core_marshal_data( GClosure * closure, GValue * ret SHUTUP, guint count,
                      const GValue * vals, gpointer hint SHUTUP,
                      gpointer marshal )
{
    typedef void (*TRMarshalPrompt)
        ( gpointer, uint8_t *, size_t, gboolean, gpointer );
    TRMarshalPrompt        callback;
    GCClosure            * cclosure = (GCClosure*) closure;
    uint8_t              * data;
    size_t                 size;
    gboolean               paused;
    gpointer               inst, gdata;

    g_return_if_fail( 4 == count );

    inst    = g_value_peek_pointer( vals );
    data    = (uint8_t *) g_value_get_string( vals + 1 );
    size    = g_value_get_uint( vals + 2 );
    paused  = g_value_get_boolean( vals + 3 );
    gdata   = closure->data;

    callback = (TRMarshalPrompt) ( NULL == marshal ?
                                   cclosure->callback : marshal );
    callback( inst, data, size, paused, gdata );
}

static void
tr_core_dispose( GObject * obj )
{
    TrCore       * self = (TrCore *) obj;
    GObjectClass * parent;

    if( self->disposed )
        return;

    self->disposed = TRUE;
    pref_save( NULL );
    parent = g_type_class_peek( g_type_parent( TR_CORE_TYPE ) );
    parent->dispose( obj );
}


static void
tr_core_class_init( gpointer g_class, gpointer g_class_data SHUTUP )
{
    GObjectClass * gobject_class;
    TrCoreClass  * core_class;

    gobject_class = G_OBJECT_CLASS( g_class );
    gobject_class->dispose = tr_core_dispose;

    core_class = TR_CORE_CLASS( g_class );
    core_class->errsig = g_signal_new( "error", G_TYPE_FROM_CLASS( g_class ),
                                       G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                       tr_core_marshal_err, G_TYPE_NONE,
                                       2, G_TYPE_INT, G_TYPE_STRING );
    core_class->promptsig = g_signal_new( "directory-prompt",
                                          G_TYPE_FROM_CLASS( g_class ),
                                          G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                          tr_core_marshal_prompt, G_TYPE_NONE,
                                          3, G_TYPE_POINTER, G_TYPE_INT,
                                          G_TYPE_BOOLEAN );
    core_class->promptdatasig = g_signal_new( "directory-prompt-data",
                                              G_TYPE_FROM_CLASS( g_class ),
                                              G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                              tr_core_marshal_data,
                                              G_TYPE_NONE, 3, G_TYPE_STRING,
                                              G_TYPE_UINT, G_TYPE_BOOLEAN );
    core_class->quitsig = g_signal_new( "quit", G_TYPE_FROM_CLASS( g_class ),
                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        g_cclosure_marshal_VOID__VOID,
                                        G_TYPE_NONE, 0 );
    core_class->prefsig = g_signal_new( "prefs-changed",
                                        G_TYPE_FROM_CLASS( g_class ),
                                        G_SIGNAL_RUN_LAST, 0, NULL, NULL,
                                        g_cclosure_marshal_VOID__STRING,
                                        G_TYPE_NONE, 1, G_TYPE_STRING );
}

static int
compareProgress( GtkTreeModel   * model,
                 GtkTreeIter    * a,
                 GtkTreeIter    * b,
                 gpointer         user_data UNUSED )
{
    int ia, ib;
    gfloat rateUpA, rateUpB;
    gfloat rateDownA, rateDownB;
    gfloat percentDoneA, percentDoneB;
    guint64 uploadedEverA, uploadedEverB;

    gtk_tree_model_get( model, a, MC_PROG_D, &percentDoneA,
                                  MC_DRATE, &rateDownA,
                                  MC_URATE, &rateUpA,
                                  MC_UP, &uploadedEverA,
                                  -1 );
    gtk_tree_model_get( model, b, MC_PROG_D, &percentDoneB,
                                  MC_DRATE, &rateDownB,
                                  MC_URATE, &rateUpB,
                                  MC_UP, &uploadedEverB,
                                  -1 );

    ia = (int)( rateUpA + rateDownA );
    ib = (int)( rateUpB + rateDownB );
    if( ia != ib )
        return ia - ib;

    ia = (int)( 100.0 * percentDoneA );
    ib = (int)( 100.0 * percentDoneB );
    if( ia != ib )
        return ia - ib;

    if( uploadedEverA != uploadedEverB )
        return uploadedEverA < uploadedEverB ? -1 : 1;

    return 0;
}

#define STR_REVERSE "reverse-"
#define STR_PROGRESS "progress"
#define STR_NAME "name"

static void
onSortColumnChanged( GtkTreeSortable * sortable, gpointer unused UNUSED )
{
    int column;
    GtkSortType order;
    if( gtk_tree_sortable_get_sort_column_id( sortable, &column, &order ) )
    {
        GString * gstr = g_string_new( NULL );
        switch( column ) {
            case MC_PROG_D: g_string_assign( gstr, STR_PROGRESS ); break;
            default: g_string_assign( gstr, STR_NAME ); break;
        }
        if( order == GTK_SORT_DESCENDING )
            g_string_prepend( gstr, STR_REVERSE );
        pref_string_set( PREF_KEY_SORT_COLUMN, gstr->str );
        g_string_free( gstr, TRUE );
    }
}

void
tr_core_set_sort_column_from_prefs( TrCore * core )
{
    char * val = pref_string_get( PREF_KEY_SORT_COLUMN );
    char * freeme = val;
    gint column;
    GtkSortType order = GTK_SORT_ASCENDING;
    if( g_str_has_prefix( val, STR_REVERSE ) ) {
        order = GTK_SORT_DESCENDING;
        val += strlen( STR_REVERSE );
    }
    if( !strcmp( val, STR_PROGRESS ) )
        column = MC_PROG_D;
    else /* default */
        column = MC_NAME;
    gtk_tree_sortable_set_sort_column_id ( GTK_TREE_SORTABLE( core->model ), column, order );
    g_free( freeme );
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
        /* info->name, info->totalSize, info->hashString, status, */
        G_TYPE_STRING, G_TYPE_UINT64,   G_TYPE_STRING,    G_TYPE_INT,
        /* error,   errorString,   percentComplete, percentDone,  rateDownload, rateUpload, */
        G_TYPE_INT, G_TYPE_STRING, G_TYPE_FLOAT,    G_TYPE_FLOAT, G_TYPE_FLOAT, G_TYPE_FLOAT,
        /* eta,     peersConnected, peersUploading, peersDownloading, seeders, */
        G_TYPE_INT, G_TYPE_INT,     G_TYPE_INT,     G_TYPE_INT,       G_TYPE_INT,
        /* leechers, completedFromTracker, downloaded,    uploaded */
        G_TYPE_INT,  G_TYPE_INT,           G_TYPE_UINT64, G_TYPE_UINT64,
        /* ratio,      left,          TrTorrent object, ID for IPC */
        G_TYPE_FLOAT,  G_TYPE_UINT64, TR_TORRENT_TYPE,  G_TYPE_INT,
    };

#ifdef REFDBG
    fprintf( stderr, "core    %p init\n", self );
#endif

    /* create the model used to store torrent data */
    g_assert( ALEN( types ) == MC_ROW_COUNT );
    store = gtk_list_store_newv( MC_ROW_COUNT, types );
    g_signal_connect( store, "sort-column-changed", G_CALLBACK(onSortColumnChanged), NULL );

    gtk_tree_sortable_set_sort_func( GTK_TREE_SORTABLE(store),
                                     MC_PROG_D,
                                     compareProgress,
                                     NULL, NULL );

    self->model    = GTK_TREE_MODEL( store );
    self->handle   = tr_init( "gtk" );
    self->nextid   = 1;
    self->quitting = FALSE;
    self->disposed = FALSE;
}

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

/**
***
**/

TrCore *
tr_core_new( void )
{
    return g_object_new( TR_CORE_TYPE, NULL );
}

GtkTreeModel *
tr_core_model( TrCore * self )
{
    g_return_val_if_fail (TR_IS_CORE(self), NULL);

    return self->disposed ? NULL : self->model;
}

tr_handle *
tr_core_handle( TrCore * self )
{
    g_return_val_if_fail (TR_IS_CORE(self), NULL);

    return self->disposed ? NULL : self->handle;
}

static void
tr_core_insert( TrCore * self, TrTorrent * tor )
{
    const tr_info * inf = tr_torrent_info( tor );
    gtk_list_store_insert_with_values( GTK_LIST_STORE( self->model ), NULL, 0, 
                                       MC_NAME,    inf->name,
                                       MC_SIZE,    inf->totalSize,
                                       MC_HASH,    inf->hashString,
                                       MC_TORRENT, tor,
                                       MC_ID,      self->nextid,
                                       -1);
    g_object_unref( tor );
    self->nextid++;
}

int
tr_core_load( TrCore * self, gboolean paused )
{
    int i;
    int count = 0;
    tr_torrent ** torrents;
    char * path;

    TR_IS_CORE( self );

    path = getdownloaddir( );

    torrents = tr_loadTorrents ( self->handle, path, paused, &count );
    for( i=0; i<count; ++i )
        tr_core_insert( self, tr_torrent_new_preexisting( torrents[i] ) );
    tr_free( torrents );

    g_free( path );
    return count;
}

gboolean
tr_core_add( TrCore * self, const char * path, enum tr_torrent_action act,
             gboolean paused )
{
    GList * list;
    int     ret;

    TR_IS_CORE( self );

    list = g_list_append( NULL, (void *) path );
    ret  = tr_core_add_list( self, list, act, paused );
    g_list_free( list );

    return 1 == ret;
}

static void
tr_core_errsig( TrCore * self, enum tr_core_err type, const char * msg )
{
    TrCoreClass * class;

    class = g_type_class_peek( TR_CORE_TYPE );
    g_signal_emit( self, class->errsig, 0, type, msg );
}

gboolean
tr_core_add_dir( TrCore * self, const char * path, const char * dir,
                 enum tr_torrent_action act, gboolean paused )
{
    TrTorrent * tor;
    char      * errstr;

    TR_IS_CORE( self );

    errstr = NULL;
    tor = tr_torrent_new( self->handle, path, dir, act, paused, &errstr );
    if( NULL == tor )
    {
        tr_core_errsig( self, TR_CORE_ERR_ADD_TORRENT, errstr );
        g_free( errstr );
        return FALSE;
    }
    g_assert( NULL == errstr );

    tr_core_insert( self, tor );

    return TRUE;
}

int
tr_core_add_list( TrCore * self, GList * paths, enum tr_torrent_action act,
                  gboolean paused )
{
    char * dir;
    int count;

    TR_IS_CORE( self );

    if( pref_flag_get( PREF_KEY_DIR_ASK ) )
    {
        TrCoreClass * class = g_type_class_peek( TR_CORE_TYPE );
        g_signal_emit( self, class->promptsig, 0, paths, act, paused );
        return 0;
    }

    dir = getdownloaddir();
    count = 0;
    for( ; paths; paths=paths->next )
        if( tr_core_add_dir( self, paths->data, dir, act, paused ) )
            count++;

    g_free( dir );
    return count;
}

gboolean
tr_core_add_data( TrCore * self, uint8_t * data, size_t size, gboolean paused )
{
    gboolean ret;
    char * path;
    TR_IS_CORE( self );

    if( pref_flag_get( PREF_KEY_DIR_ASK ) )
    {
        TrCoreClass * class = g_type_class_peek( TR_CORE_TYPE );
        g_signal_emit( self, class->promptdatasig, 0, data, size, paused );
        return FALSE;
    }

    path = getdownloaddir( );
    ret = tr_core_add_data_dir( self, data, size, path, paused );
    g_free( path );
    return ret;
}

gboolean
tr_core_add_data_dir( TrCore * self, uint8_t * data, size_t size,
                      const char * dir, gboolean paused )
{
    TrTorrent * tor;
    char      * errstr = NULL;

    TR_IS_CORE( self );

    tor = tr_torrent_new_with_data( self->handle, data, size, dir,
                                    paused, &errstr );
    if( NULL == tor )
    {
        tr_core_errsig( self, TR_CORE_ERR_ADD_TORRENT, errstr );
        g_free( errstr );
        return FALSE;
    }
    g_assert( NULL == errstr );

    tr_core_insert( self, tor );

    return TRUE;
}

void
tr_core_torrents_added( TrCore * self )
{
    TR_IS_CORE( self );

    tr_core_update( self );
    tr_core_errsig( self, TR_CORE_ERR_NO_MORE_TORRENTS, NULL );
}

void
tr_core_delete_torrent( TrCore * self, GtkTreeIter * iter )
{
    TrTorrent * tor;

    TR_IS_CORE( self );

    gtk_tree_model_get( self->model, iter, MC_TORRENT, &tor, -1 );
    gtk_list_store_remove( GTK_LIST_STORE( self->model ), iter );
    tr_torrentRemoveSaved( tr_torrent_handle( tor ) );

    tr_torrent_sever( tor );
}

static gboolean
update_foreach( GtkTreeModel * model,
                GtkTreePath  * path UNUSED,
                GtkTreeIter  * iter,
                gpointer       data UNUSED)
{
    TrTorrent * tor;
    const tr_stat * st;

    gtk_tree_model_get( model, iter, MC_TORRENT, &tor, -1 );
    st = tr_torrent_stat( tor );
    tr_torrent_check_seeding_cap ( tor );
    g_object_unref( tor );

    gtk_list_store_set( GTK_LIST_STORE( model ), iter,
                        MC_STAT,        st->status,
                        MC_ERR,         st->error,
                        MC_TERR,        st->errorString,
                        MC_PROG_C,      st->percentComplete,
                        MC_PROG_D,      st->percentDone,
                        MC_DRATE,       st->rateDownload,
                        MC_URATE,       st->rateUpload,
                        MC_ETA,         st->eta,
                        MC_PEERS,       st->peersConnected,
                        MC_UPEERS,      st->peersGettingFromUs,
                        MC_DPEERS,      st->peersSendingToUs,
                        MC_SEED,        st->seeders,
                        MC_LEECH,       st->leechers,
                        MC_DONE,        st->completedFromTracker,
                        MC_DOWN,        st->downloadedEver,
                        MC_UP,          st->uploadedEver,
                        MC_RATIO,       st->ratio,
                        MC_LEFT,        st->leftUntilDone,
                        -1 );

    return FALSE;
}

void
tr_core_update( TrCore * self )
{
    int column;
    GtkSortType order;
    GtkTreeSortable * sortable;

    /* pause sorting */
    sortable = GTK_TREE_SORTABLE( self->model );
    gtk_tree_sortable_get_sort_column_id( sortable, &column, &order );
    gtk_tree_sortable_set_sort_column_id( sortable, GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, order );

    /* refresh the model */
    gtk_tree_model_foreach( self->model, update_foreach, NULL );

    /* resume sorting */
    gtk_tree_sortable_set_sort_column_id( sortable, column, order );
}

void
tr_core_quit( TrCore * self )
{
    TrCoreClass * class;

    TR_IS_CORE( self );

    class = g_type_class_peek( TR_CORE_TYPE );
    g_signal_emit( self, class->quitsig, 0 );
}

/**
***  Prefs
**/

static void
commitPrefsChange( TrCore * self, const char * key )
{
    TrCoreClass * class = g_type_class_peek( TR_CORE_TYPE );
    pref_save( NULL );
    g_signal_emit( self, class->prefsig, 0, key );
}

void
tr_core_set_pref( TrCore * self, const char * key, const char * newval )
{
    char * oldval = pref_string_get( key );
    if( tr_strcmp( oldval, newval ) )
    {
        pref_string_set( key, newval );
        commitPrefsChange( self, key );
    }
    g_free( oldval );
}

void
tr_core_set_pref_bool( TrCore * self, const char * key, gboolean newval )
{
    const gboolean oldval = pref_flag_get( key );
    if( oldval != newval )
    {
        pref_flag_set( key, newval );
        commitPrefsChange( self, key );
    }
}

void
tr_core_set_pref_int( TrCore * self, const char * key, int newval )
{
    const int oldval = pref_int_get( key );
    if( oldval != newval )
    {
        pref_int_set( key, newval );
        commitPrefsChange( self, key );
    }
}
