/******************************************************************************
 * $Id: torrent-inspector.c 4962 2008-02-07 17:44:26Z charles $
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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "file-list.h"
#include "hig.h"

enum
{
  FC_STOCK,
  FC_LABEL,
  FC_PROG,
  FC_KEY,
  FC_INDEX,
  FC_SIZE,
  FC_PRIORITY,
  FC_ENABLED,
  N_FILE_COLS
};

typedef struct
{
  TrTorrent * gtor;
  GtkWidget * top;
  GtkWidget * view;
  GtkTreeModel * model; /* same object as store, but recast */
  GtkTreeStore * store; /* same object as model, but recast */
  guint timeout_tag;
}
FileData;

static const char*
priorityToString( const int priority )
{
    switch( priority ) {
        case TR_PRI_HIGH:   return _("High");
        case TR_PRI_NORMAL: return _("Normal");
        case TR_PRI_LOW:    return _("Low");
        default:            return "BUG!";
    }
}

static tr_priority_t
stringToPriority( const char* str )
{
    if( !strcmp( str, _( "High" ) ) ) return TR_PRI_HIGH;
    if( !strcmp( str, _( "Low" ) ) ) return TR_PRI_LOW;
    return TR_PRI_NORMAL;
}

static void
parsepath( const tr_torrent  * tor,
           GtkTreeStore      * store,
           GtkTreeIter       * ret,
           const char        * path,
           int                 index,
           uint64_t            size )
{
    GtkTreeModel * model;
    GtkTreeIter  * parent, start, iter;
    char         * file, * lower, * mykey, *escaped=0;
    const char   * stock;
    int            priority = 0;
    gboolean       enabled = TRUE;

    model  = GTK_TREE_MODEL( store );
    parent = NULL;
    file   = g_path_get_basename( path );
    if( 0 != strcmp( file, path ) )
    {
        char * dir = g_path_get_dirname( path );
        parsepath( tor, store, &start, dir, index, size );
        parent = &start;
        g_free( dir );
    }

    lower = g_utf8_casefold( file, -1 );
    mykey = g_utf8_collate_key( lower, -1 );
    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
        gboolean stop;
        char * modelkey;
        gtk_tree_model_get( model, &iter, FC_KEY, &modelkey, -1 );
        stop = (modelkey!=NULL) && !strcmp(mykey,modelkey);
        g_free (modelkey);
        if (stop) goto done;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    gtk_tree_store_append( store, &iter, parent );
    if( NULL == ret )
    {
        stock = GTK_STOCK_FILE;
    }
    else
    {
        stock = GTK_STOCK_DIRECTORY;
        size  = 0;
        index = -1;
    }

    if (index != -1) {
        priority = tr_torrentGetFilePriority( tor, index );
        enabled  = tr_torrentGetFileDL( tor, index );
    }

    escaped = g_markup_escape_text (file, -1); 
    gtk_tree_store_set( store, &iter, FC_INDEX, index,
                                      FC_LABEL, escaped,
                                      FC_KEY, mykey,
                                      FC_STOCK, stock,
                                      FC_PRIORITY, priorityToString(priority),
                                      FC_ENABLED, enabled,
                                      FC_SIZE, size, -1 );
  done:
    g_free( escaped );
    g_free( mykey );
    g_free( lower );
    g_free( file );
    if( NULL != ret )
      *ret = iter;
}

static uint64_t
getdirtotals( GtkTreeStore * store, GtkTreeIter * parent )
{
    GtkTreeModel * model;
    GtkTreeIter    iter;
    uint64_t       mysize, subsize;
    char         * name, * label;

    model  = GTK_TREE_MODEL( store );
    mysize = 0;
    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
         char sizeStr[64];
        if( gtk_tree_model_iter_has_child( model, &iter ) )
        {
            subsize = getdirtotals( store, &iter );
            gtk_tree_store_set( store, &iter, FC_SIZE, subsize, -1 );
        }
        else
        {
            gtk_tree_model_get( model, &iter, FC_SIZE, &subsize, -1 );
        }
        gtk_tree_model_get( model, &iter, FC_LABEL, &name, -1 );
        tr_strlsize( sizeStr, subsize, sizeof( sizeStr ) );
        label = g_markup_printf_escaped( "<small>%s (%s)</small>",
                                          name, sizeStr );
        g_free( name );
        gtk_tree_store_set( store, &iter, FC_LABEL, label, -1 );
        g_free( label );
        mysize += subsize;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    return mysize;
}

static void
updateprogress( GtkTreeModel   * model,
                GtkTreeStore   * store,
                GtkTreeIter    * parent,
                tr_file_stat   * fileStats,
                guint64        * setmeGotSize,
                guint64        * setmeTotalSize)
{
    GtkTreeIter iter;
    guint64 gotSize=0, totalSize=0;

    if( gtk_tree_model_iter_children( model, &iter, parent ) ) do
    {
        int oldProg, newProg;
        guint64 subGot, subTotal;

        if (gtk_tree_model_iter_has_child( model, &iter ) )
        {
            updateprogress( model, store, &iter, fileStats, &subGot, &subTotal);
        }
        else
        {
            int index, percent;
            gtk_tree_model_get( model, &iter, FC_SIZE, &subTotal,
                                              FC_INDEX, &index,
                                              -1 );
            g_assert( 0 <= index );
            percent = (int)(fileStats[index].progress * 100.0); /* [0...100] */
            subGot = (guint64)(subTotal * percent/100.0);
        }

        if (!subTotal) subTotal = 1; /* avoid div by zero */
        g_assert (subGot <= subTotal);

        /* why not just set it every time?
           because that causes the "priorities" combobox to pop down */
        gtk_tree_model_get (model, &iter, FC_PROG, &oldProg, -1);
        newProg = (int)(100.0*subGot/subTotal);
        if (oldProg != newProg)
          gtk_tree_store_set (store, &iter,
                              FC_PROG, (int)(100.0*subGot/subTotal), -1);

        gotSize += subGot;
        totalSize += subTotal;
    }
    while( gtk_tree_model_iter_next( model, &iter ) );

    *setmeGotSize = gotSize;
    *setmeTotalSize = totalSize;
}

static GtkTreeModel*
priority_model_new (void)
{
  GtkTreeIter iter;
  GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("High"), 1, TR_PRI_HIGH, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Normal"), 1, TR_PRI_NORMAL, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Low"), 1, TR_PRI_LOW, -1);
  return GTK_TREE_MODEL (store);
}

static void
subtree_walk_dnd( GtkTreeStore   * store,
                  GtkTreeIter    * iter,
                  tr_torrent     * tor,
                  gboolean         enabled,
                  GArray         * indices )
{
    int index;
    GtkTreeIter child;
 
    /* update this node */ 
    gtk_tree_model_get( GTK_TREE_MODEL(store), iter, FC_INDEX, &index, -1  );
    if (index >= 0)
      g_array_append_val( indices, index );
    gtk_tree_store_set( store, iter, FC_ENABLED, enabled, -1 );

    /* visit the children */
    if( gtk_tree_model_iter_children( GTK_TREE_MODEL(store), &child, iter ) ) do
      subtree_walk_dnd( store, &child, tor, enabled, indices );
    while( gtk_tree_model_iter_next( GTK_TREE_MODEL(store), &child ) );
}

static void
set_subtree_dnd( GtkTreeStore   * store,
                 GtkTreeIter    * iter,
                 tr_torrent     * tor,
                 gboolean         enabled )
{
    GArray * indices = g_array_new( FALSE, FALSE, sizeof(int) );
    subtree_walk_dnd( store, iter, tor, enabled, indices );
    tr_torrentSetFileDLs( tor, (int*)indices->data, (int)indices->len, enabled );
    g_array_free( indices, TRUE );
}

static void
subtree_walk_priority( GtkTreeStore   * store,
                       GtkTreeIter    * iter,
                       tr_torrent     * tor,
                       int              priority,
                       GArray         * indices )
{
    int index;
    GtkTreeIter child;

    /* update this node */ 
    gtk_tree_model_get( GTK_TREE_MODEL(store), iter, FC_INDEX, &index, -1  );
    if( index >= 0 )
        g_array_append_val( indices, index );
    gtk_tree_store_set( store, iter, FC_PRIORITY, priorityToString(priority), -1 );

    /* visit the children */
    if( gtk_tree_model_iter_children( GTK_TREE_MODEL(store), &child, iter ) ) do
        subtree_walk_priority( store, &child, tor, priority, indices );
    while( gtk_tree_model_iter_next( GTK_TREE_MODEL(store), &child ) );

}

static void
set_subtree_priority( GtkTreeStore * store,
                      GtkTreeIter * iter,
                      tr_torrent * tor,
                      int priority )
{
    GArray * indices = g_array_new( FALSE, FALSE, sizeof(int) );
    subtree_walk_priority( store, iter, tor, priority, indices );
    tr_torrentSetFilePriorities( tor, (int*)indices->data, (int)indices->len, priority );
    g_array_free( indices, TRUE );
}

static void
priority_changed_cb (GtkCellRendererText * cell UNUSED,
                     const gchar         * path,
		     const gchar         * value,
		     void                * file_data)
{
    GtkTreeIter iter;
    FileData * d = file_data;
    if (gtk_tree_model_get_iter_from_string (d->model, &iter, path))
    {
        tr_torrent  * tor = tr_torrent_handle( d->gtor );
        const tr_priority_t priority = stringToPriority( value );
        set_subtree_priority( d->store, &iter, tor, priority );
    }
}

static void
enabled_toggled (GtkCellRendererToggle  * cell UNUSED,
	         const gchar            * path_str,
	         gpointer                 data_gpointer)
{
  FileData * data = data_gpointer;
  GtkTreePath * path = gtk_tree_path_new_from_string( path_str );
  GtkTreeModel * model = data->model;
  GtkTreeIter iter;
  gboolean enabled;

  gtk_tree_model_get_iter( model, &iter, path );
  gtk_tree_model_get( model, &iter, FC_ENABLED, &enabled, -1 );
  enabled = !enabled;
  set_subtree_dnd( GTK_TREE_STORE(model),
                   &iter,
                   tr_torrent_handle( data->gtor ),
                   enabled );

  gtk_tree_path_free( path );
}

static void
freeData( gpointer gdata )
{
    FileData * data = gdata;

    if( data->timeout_tag ) {
        g_source_remove( data->timeout_tag );
        data->timeout_tag = 0;
    }

    g_free( data );
}

static void
torrentDestroyed( gpointer gdata, GObject * deadTorrent UNUSED )
{
    FileData * data = gdata;
    file_list_set_torrent( data->top, NULL );
}

static gboolean
refreshModel( gpointer gdata )
{
    FileData * data  = gdata;

    g_assert( data != NULL );

    if( data->gtor )
    {
        guint64 foo, bar;
        int fileCount;
        tr_torrent * tor;
        tr_file_stat * fileStats;

        tor = tr_torrent_handle( data->gtor );
        fileCount = 0;
        fileStats = tr_torrentFiles( tor, &fileCount );
        updateprogress (data->model, data->store, NULL, fileStats, &foo, &bar);
        tr_torrentFilesFree( fileStats, fileCount );
    }

    return TRUE;
}

void
file_list_set_torrent( GtkWidget * w, TrTorrent * gtor )
{
    GtkTreeStore        * store;
    FileData            * data;

    data = g_object_get_data( G_OBJECT( w ), "file-data" );

    /* instantiate the model */
    store = gtk_tree_store_new ( N_FILE_COLS,
                                 G_TYPE_STRING,    /* stock */
                                 G_TYPE_STRING,    /* label */
                                 G_TYPE_INT,       /* prog [0..100] */
                                 G_TYPE_STRING,    /* key */
                                 G_TYPE_INT,       /* index */
                                 G_TYPE_UINT64,    /* size */
                                 G_TYPE_STRING,    /* priority */
                                 G_TYPE_BOOLEAN ); /* dl enabled */
    data->store = store;
    data->model = GTK_TREE_MODEL( store );
    data->gtor = gtor;

    if( data->timeout_tag ) {
        g_source_remove( data->timeout_tag );
        data->timeout_tag = 0;
    }

    /* populate the model */
    if( gtor )
    {
        int i;
        const tr_info * inf = tr_torrent_info( gtor );
        tr_torrent * tor = tr_torrent_handle( gtor );
        g_object_weak_ref( G_OBJECT( gtor ), torrentDestroyed, data );

        for( i=0; inf && i<inf->fileCount; ++i )
        {
            const char * path = inf->files[i].name;
            const char * base = g_path_is_absolute( path ) ? g_path_skip_root( path ) : path;
            parsepath( tor, store, NULL, base, i, inf->files[i].length );
        }

        getdirtotals( store, NULL );

        data->timeout_tag = g_timeout_add( 1000, refreshModel, data );
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW( data->view ), GTK_TREE_MODEL( store ) );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( data->view ) );
}

GtkWidget *
file_list_new( TrTorrent * gtor )
{
    GtkWidget           * ret;
    FileData            * data;
    GtkWidget           * view, * scroll;
    GtkCellRenderer     * rend;
    GtkCellRenderer     * priority_rend;
    GtkCellRenderer     * enabled_rend;
    GtkTreeViewColumn   * col;
    GtkTreeSelection    * sel;
    GtkTreeModel        * model;

    /* create the view */
    view = gtk_tree_view_new( );
    gtk_container_set_border_width( GTK_CONTAINER( view ), GUI_PAD_BIG );

    /* add file column */
    
    col = GTK_TREE_VIEW_COLUMN (g_object_new (GTK_TYPE_TREE_VIEW_COLUMN,
        "expand", TRUE,
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. */ 				      
        "title", Q_("filedetails|File"),
        NULL));
    rend = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "stock-id", FC_STOCK );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new();
    g_object_set( rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_add_attribute( col, rend, "markup", FC_LABEL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* add progress column */
    rend = gtk_cell_renderer_progress_new();
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. */ 
    col = gtk_tree_view_column_new_with_attributes (Q_("filedetails|Progress"),
						    rend,
						    "value", FC_PROG,
						    NULL);
    gtk_tree_view_column_set_sort_column_id( col, FC_PROG );
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );
    /* set up view */
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( view ) );
    gtk_tree_view_set_search_column( GTK_TREE_VIEW( view ), FC_LABEL );

    /* add "download" checkbox column */
    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_sort_column_id( col, FC_ENABLED );
    rend = enabled_rend = gtk_cell_renderer_toggle_new  ();
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. 
       Please note the items for this column are checkboxes (yes/no) */ 
    col = gtk_tree_view_column_new_with_attributes (Q_("filedetails|Download"),
                                                    rend,
                                                    "active", FC_ENABLED,
                                                    NULL);
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* add priority column */
    model = priority_model_new ();
    col = gtk_tree_view_column_new ();
    gtk_tree_view_column_set_sort_column_id( col, FC_PRIORITY );
    /* Translators: this is a column header in Files tab, Details dialog;
       Don't include the prefix "filedetails|" in the translation. */ 
    gtk_tree_view_column_set_title (col, Q_("filedetails|Priority"));
    rend = priority_rend = gtk_cell_renderer_combo_new ();
    gtk_tree_view_column_pack_start (col, rend, TRUE);
    g_object_set (G_OBJECT(rend), "model", model,
                                  "editable", TRUE,
                                  "has-entry", FALSE,
                                  "text-column", 0,
                                  NULL);
    g_object_unref (G_OBJECT(model));
    gtk_tree_view_column_add_attribute (col, rend, "text", FC_PRIORITY);
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                    GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll),
                                         GTK_SHADOW_IN);
    gtk_container_add( GTK_CONTAINER( scroll ), view );
    gtk_widget_set_size_request (scroll, 0u, 200u);

    ret = scroll;
    data = g_new0( FileData, 1 );
    data->view = view;
    data->top = scroll;
    g_signal_connect (G_OBJECT(priority_rend), "edited", G_CALLBACK(priority_changed_cb), data);
    g_signal_connect(enabled_rend, "toggled", G_CALLBACK(enabled_toggled), data );
    g_object_set_data_full( G_OBJECT( ret ), "file-data", data, freeData );
    file_list_set_torrent( ret, gtor );

    return ret;
}
