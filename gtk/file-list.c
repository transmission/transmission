/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "file-list.h"
#include "hig.h"
#include "icons.h"

enum
{
    /* these two fields could be any number at all so long as they're not
     * TR_PRI_LOW, TR_PRI_NORMAL, TR_PRI_HIGH, TRUE, or FALSE */
    NOT_SET = 1000,
    MIXED = 1001
};

enum
{
    FC_ICON,
    FC_LABEL,
    FC_PROG,
    FC_INDEX,
    FC_SIZE,
    FC_HAVE,
    FC_PRIORITY,
    FC_ENABLED,
    FC_IS_FILE,
    N_FILE_COLS
};

typedef struct
{
    TrCore        * core;
    tr_torrent    * tor;
    GtkWidget     * top;
    GtkWidget     * view;
    GtkTreeModel  * model; /* same object as store, but recast */
    GtkTreeStore  * store; /* same object as model, but recast */
    tr_file_stat  * refresh_file_stat;
    int             torrentId;
    guint           timeout_tag;
}
FileData;

static void
clearData( FileData * data )
{
    data->torrentId = -1;

    if( data->timeout_tag ) {
        g_source_remove( data->timeout_tag );
        data->timeout_tag = 0;
    }
}

static void
freeData( gpointer data )
{
    clearData( data );
    g_free( data );
}

/***
****
***/

static gboolean
refreshFilesForeach( GtkTreeModel * model,
                     GtkTreePath  * path UNUSED,
                     GtkTreeIter  * iter,
                     gpointer       gdata )
{
    FileData * data = gdata;
    gboolean is_file;
    unsigned int index;
    uint64_t size;
    uint64_t old_have;
    int old_prog;
    int old_priority;
    int old_enabled;

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_ENABLED, &old_enabled,
                                     FC_PRIORITY, &old_priority,
                                     FC_INDEX, &index,
                                     FC_HAVE, &old_have,
                                     FC_SIZE, &size,
                                     FC_PROG, &old_prog,
                                     -1  );

    if( is_file )
    {
        tr_torrent * tor = data->tor;
        const int enabled = tr_torrentGetFileDL( tor, index );
        const int priority = tr_torrentGetFilePriority( tor, index );
        const uint64_t have = data->refresh_file_stat[index].bytesCompleted;
        const int prog = (int)((100.0*have)/size);

        if( (priority!=old_priority) || (enabled!=old_enabled) || (have!=old_have) || (prog!=old_prog) )
            gtk_tree_store_set( data->store, iter, FC_PRIORITY, priority,
                                                   FC_ENABLED, enabled,
                                                   FC_HAVE, have,
                                                   FC_PROG, prog,
                                                   -1 );
    }
    else
    {
        GtkTreeIter child;
        uint64_t sub_size = 0;
        uint64_t have = 0;
        int prog;
        int enabled = NOT_SET;
        int priority = NOT_SET;

        /* since gtk_tree_model_foreach() is depth-first, we can
         * get the `sub' info by walking the immediate children */

        if( gtk_tree_model_iter_children( model, &child, iter ) ) do
        {
            int child_enabled;
            int child_priority;
            int64_t child_have, child_size;

            gtk_tree_model_get( model, &child, FC_SIZE, &child_size,
                                               FC_HAVE, &child_have,
                                               FC_PRIORITY, &child_priority,
                                               FC_ENABLED, &child_enabled,
                                               -1 );

            sub_size += child_size;
            have += child_have;

            if( enabled == NOT_SET )
                enabled = child_enabled;
            else if( enabled != child_enabled )
                enabled = MIXED;

            if( priority == NOT_SET )
                priority = child_priority;
            else if( priority != child_priority )
                priority = MIXED;
        }
        while( gtk_tree_model_iter_next( model, &child ) );

        prog = (int)((100.0*have)/sub_size);

        if( (size!=sub_size) || (have!=old_have)
                             || (priority!=old_priority)
                             || (enabled!=old_enabled)
                             || (prog!=old_prog) )
            gtk_tree_store_set( data->store, iter, FC_SIZE, sub_size,
                                                   FC_HAVE, have,
                                                   FC_PRIORITY, priority,
                                                   FC_ENABLED, enabled,
                                                   FC_PROG, prog,
                                                   -1 );
    }

    return FALSE; /* keep walking */
}

static void
gtr_tree_model_foreach_postorder_subtree( GtkTreeModel            * model,
                                          GtkTreeIter             * parent,
                                          GtkTreeModelForeachFunc   func,
                                          gpointer                  data )
{
    GtkTreeIter child;
    if( gtk_tree_model_iter_children( model, &child, parent ) ) do
        gtr_tree_model_foreach_postorder_subtree( model, &child, func, data );
    while( gtk_tree_model_iter_next( model, &child ) );
    if( parent )
        func( model, NULL, parent, data );
}

static void
gtr_tree_model_foreach_postorder( GtkTreeModel            * model,
                                  GtkTreeModelForeachFunc   func,
                                  gpointer                  data )
{
    GtkTreeIter iter;
    if( gtk_tree_model_get_iter_first( model, &iter ) ) do
        gtr_tree_model_foreach_postorder_subtree( model, &iter, func, data );
    while( gtk_tree_model_iter_next( model, &iter ) );
}

static void
refresh( FileData * data )
{
    tr_torrent * tor = tr_torrentFindFromId( tr_core_session( data->core ), data->torrentId );

    if( tor == NULL )
    {
        file_list_clear( data->top );
    }
    else
    {
        tr_file_index_t fileCount;
        data->tor = tr_torrentFindFromId( tr_core_session( data->core ), data->torrentId );
        data->refresh_file_stat = tr_torrentFiles( tor, &fileCount );

        gtr_tree_model_foreach_postorder( data->model, refreshFilesForeach, data );

        tr_torrentFilesFree( data->refresh_file_stat, fileCount );
        data->refresh_file_stat = NULL;
        data->tor = NULL;
    }
}

static gboolean
refreshModel( gpointer file_data )
{
    refresh( file_data );
    return TRUE;
}

/***
****
***/

struct ActiveData
{
    GtkTreeSelection  * sel;
    GArray            * array;
};

static gboolean
getSelectedFilesForeach( GtkTreeModel * model,
                         GtkTreePath  * path UNUSED,
                         GtkTreeIter  * iter,
                         gpointer       gdata )
{
    struct ActiveData * data = gdata;
    unsigned int        i;
    gboolean            is_file = FALSE;
    gboolean            is_active = FALSE;

    /* active == if it's selected, or any ancestor is selected */
    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_INDEX, &i,
                                     -1 );
    if( is_file )
    {
        is_active = gtk_tree_selection_iter_is_selected( data->sel, iter );
        if( !is_active )
        {
            GtkTreeIter walk = *iter;
            GtkTreeIter parent;
            while( !is_active && gtk_tree_model_iter_parent( model, &parent, &walk ) )
            {
                is_active = gtk_tree_selection_iter_is_selected( data->sel, &parent );
                walk = parent;
            }
        }
    }

    if( is_active )
        g_array_append_val( data->array, i );

    return FALSE; /* keep walking */
}

static void
getSelectedFilesAndDescendants( GtkTreeView * view, GArray * indices )
{
    struct ActiveData data;

    data.sel = gtk_tree_view_get_selection( view );
    data.array  = indices;
    gtk_tree_model_foreach( gtk_tree_view_get_model( view ),
                            getSelectedFilesForeach, &data );
}

struct SubtreeForeachData
{
    GArray       * array;
    GtkTreePath  * path;
};

static gboolean
getSubtreeForeach( GtkTreeModel   * model,
                   GtkTreePath    * path,
                   GtkTreeIter    * iter,
                   gpointer         gdata )
{
    struct SubtreeForeachData * data = gdata;
    unsigned int i;
    gboolean is_file = FALSE;

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_INDEX, &i,
                                     -1 );
    if( is_file )
        if( !gtk_tree_path_compare( path, data->path ) || gtk_tree_path_is_descendant( path, data->path ) )
            g_array_append_val( data->array, i );

    return FALSE; /* keep walking */
}

static void
getSubtree( GtkTreeView * view, GtkTreePath * path, GArray * indices )
{
    struct SubtreeForeachData tmp;
    tmp.array = indices;
    tmp.path = path;
    gtk_tree_model_foreach( gtk_tree_view_get_model( view ), getSubtreeForeach, &tmp );
}

/* if `path' is a selected row, all selected rows are returned.
 * otherwise, only the row indicated by `path' is returned.
 * this is for toggling all the selected rows' states in a batch.
 */
static GArray*
getActiveFilesForPath( GtkTreeView * view, GtkTreePath * path )
{
    GtkTreeSelection * sel = gtk_tree_view_get_selection( view );
    GArray * indices = g_array_new( FALSE, FALSE, sizeof( tr_file_index_t ) );

    if( gtk_tree_selection_path_is_selected( sel, path ) )
    {
        /* clicked in a selected row... use the current selection */
        getSelectedFilesAndDescendants( view, indices );
    }
    else
    {
        /* clicked OUTSIDE of the selected row... just use the clicked row */
        getSubtree( view, path, indices );
    }

    return indices;
}

/***
****
***/

void
file_list_clear( GtkWidget * w )
{
    file_list_set_torrent( w, -1 );
}

struct build_data
{
    GtkWidget    * w;
    tr_torrent   * tor;
    GtkTreeIter  * iter;
    GtkTreeStore * store;
};

struct row_struct
{
    uint64_t    length;
    char      * name;
    int         index;
};

static void
buildTree( GNode * node, gpointer gdata )
{
    GtkTreeIter child_iter;
    struct build_data * build = gdata;
    struct row_struct *child_data = node->data;
    const gboolean isLeaf = node->children == NULL;

    const char * mime_type = isLeaf ? get_mime_type_from_filename( child_data->name ) : DIRECTORY_MIME_TYPE;
    GdkPixbuf * icon = get_mime_type_icon( mime_type, GTK_ICON_SIZE_MENU, build->w ); 
    const int priority = isLeaf ? tr_torrentGetFilePriority( build->tor, child_data->index ) : 0;
    const gboolean enabled = isLeaf ? tr_torrentGetFileDL( build->tor, child_data->index ) : TRUE;
#if GTK_CHECK_VERSION(2,10,0)
    gtk_tree_store_insert_with_values( build->store, &child_iter, build->iter, INT_MAX,
                                       FC_INDEX, child_data->index,
                                       FC_LABEL, child_data->name,
                                       FC_SIZE, child_data->length,
                                       FC_ICON, icon,
                                       FC_PRIORITY, priority,
                                       FC_ENABLED, enabled,
                                       FC_IS_FILE, isLeaf,
                                       -1 );
#else
    gtk_tree_store_append( build->store, &child_iter, build->iter );
    gtk_tree_store_set( build->store, &child_iter,
                        FC_INDEX, child_data->index,
                        FC_LABEL, child_data->name,
                        FC_SIZE, child_data->length,
                        FC_ICON, icon,
                        FC_PRIORITY, priority,
                        FC_ENABLED, enabled,
                        FC_IS_FILE, isLeaf,
                        -1 );
#endif

    if( !isLeaf )
    {
        struct build_data b = *build;
        b.iter = &child_iter;
        g_node_children_foreach( node, G_TRAVERSE_ALL, buildTree, &b );
    }

    g_object_unref( icon );
 
    /* we're done with this node */
    g_free( child_data->name );
    g_free( child_data );
}

static GNode*
find_child( GNode* parent, const char * name )
{
    GNode * child = parent->children;
    while( child ) {
        const struct row_struct * child_data = child->data;
        if( ( *child_data->name == *name ) && !strcmp( child_data->name, name ) )
            break;
        child = child->next;
    }
    return child;
}

void
file_list_set_torrent( GtkWidget * w, int torrentId )
{
    GtkTreeStore * store;
    FileData * data = g_object_get_data( G_OBJECT( w ), "file-data" );

    /* unset the old fields */
    clearData( data );

    /* instantiate the model */
    store = gtk_tree_store_new ( N_FILE_COLS,
                                 GDK_TYPE_PIXBUF,  /* icon */
                                 G_TYPE_STRING,    /* label */
                                 G_TYPE_INT,       /* prog [0..100] */
                                 G_TYPE_UINT,      /* index */
                                 G_TYPE_UINT64,    /* size */
                                 G_TYPE_UINT64,    /* have */
                                 G_TYPE_INT,       /* priority */
                                 G_TYPE_INT,       /* dl enabled */
                                 G_TYPE_BOOLEAN,   /* is file */
                                 G_TYPE_UINT64,    /* sub size */
                                 G_TYPE_UINT64,    /* sub have */
                                 G_TYPE_INT );     /* sub state */

    data->store = store;
    data->model = GTK_TREE_MODEL( store );
    data->torrentId = torrentId;

    /* populate the model */
    if( torrentId > 0 )
    {
        tr_session * session = tr_core_session( data->core );
        tr_torrent * tor = tr_torrentFindFromId( session, torrentId );
        if( tor != NULL )
        {
            tr_file_index_t i;
            const tr_info * inf = tr_torrentInfo( tor );
            struct row_struct * root_data;
            GNode * root;
            struct build_data build;

            /* build a GNode tree of the files */
            root_data = g_new0( struct row_struct, 1 );
            root_data->name = g_strdup( inf->name );
            root_data->index = -1;
            root_data->length = 0;
            root = g_node_new( root_data );
            for( i=0; i<inf->fileCount; ++i ) {
                int j;
                GNode * parent = root;
                const tr_file * file = &inf->files[i];
                char ** tokens = g_strsplit( file->name, G_DIR_SEPARATOR_S, 0 );
                for( j=0; tokens[j]; ++j ) {
                    const gboolean isLeaf = tokens[j+1] == NULL;
                    const char * name = tokens[j];
                    GNode * node = find_child( parent, name );
                    if( node == NULL ) {
                        struct row_struct * row = g_new( struct row_struct, 1 );
                        row->name = g_strdup( name );
                        row->index = isLeaf ? (int)i : -1;
                        row->length = isLeaf ? file->length : 0;
                        node = g_node_new( row );
                        g_node_append( parent, node );
                    }
                    parent = node;
                }
                g_strfreev( tokens );
            }

            /* now, add them to the model */
            build.w = w;
            build.tor = tor;
            build.store = data->store;
            build.iter = NULL;
            g_node_children_foreach( root, G_TRAVERSE_ALL, buildTree, &build );

            /* cleanup */
            g_node_destroy( root );
            g_free( root_data->name );
            g_free( root_data );
        }

        refresh( data );
        data->timeout_tag = gtr_timeout_add_seconds( 2, refreshModel, data );
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW( data->view ), data->model );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( data->view ) );
}

/***
****
***/

static void
renderFilename( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data UNUSED )
{
    char *   filename;
    char *   str;
    int64_t  size;
    char     buf[64];

    gtk_tree_model_get( model, iter, FC_LABEL, &filename,
                                     FC_SIZE, &size,
                                     -1 );
    tr_strlsize( buf, size, sizeof( buf ) );
    str = g_markup_printf_escaped( "<small>%s (%s)</small>", filename, buf );
    g_object_set( renderer, "markup", str, NULL );
    g_free( str );
    g_free( filename );
}

static void
renderDownload( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data   UNUSED )
{
    gboolean enabled;
    gtk_tree_model_get( model, iter, FC_ENABLED, &enabled, -1 );
    g_object_set( renderer, "inconsistent", (enabled==MIXED),
                            "active", (enabled==TRUE),
                            NULL );
}

static void
renderPriority( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data   UNUSED )
{
    int priority;
    const char * text;
    gtk_tree_model_get( model, iter, FC_PRIORITY, &priority, -1 );
    switch( priority ) {
        case TR_PRI_HIGH:   text = _( "High" ); break;
        case TR_PRI_NORMAL: text = _( "Normal" ); break;
        case TR_PRI_LOW:    text = _( "Low" ); break;
        default:            text = _( "Mixed" ); break;
    }
    g_object_set( renderer, "text", text, NULL );
}

static gboolean
onViewButtonPressed( GtkWidget * w, GdkEventButton * event, gpointer gdata )
{
    FileData * data = gdata;
    gboolean   handled = FALSE;
    tr_torrent * tor = tr_torrentFindFromId( tr_core_session( data->core ), data->torrentId );

    if( tor == NULL )
        return handled;

    if( ( event->type == GDK_BUTTON_PRESS ) && ( event->button == 1 )
      && !( event->state & ( GDK_SHIFT_MASK | GDK_CONTROL_MASK ) ) )
    {
        GtkTreeView *       view = GTK_TREE_VIEW( w );
        GtkTreePath *       path;
        GtkTreeViewColumn * column;
        int                 cell_x;
        int                 cell_y;
        if( gtk_tree_view_get_path_at_pos( view, event->x, event->y,
                                           &path, &column, &cell_x, &cell_y ) )
        {
            const char * column_title = gtk_tree_view_column_get_title( column );
            const gboolean downloadColumn = !strcmp( column_title, _( "Download" ) );
            const gboolean priorityColumn = !strcmp( column_title, _( "Priority" ) );
            if( downloadColumn || priorityColumn )
            {
                GArray * a = getActiveFilesForPath( view, path );
                GtkTreeSelection * sel = gtk_tree_view_get_selection( view );
                const gboolean isSelected = gtk_tree_selection_path_is_selected( sel, path );
                GtkTreeModel * model = data->model;
                GtkTreeIter iter;

                gtk_tree_model_get_iter( model, &iter, path );

                if( priorityColumn )
                {
                    int priority;

                    /* get the `priority' state of the clicked row */
                    gtk_tree_model_get( model, &iter, FC_PRIORITY, &priority, -1 );

                    /* twiddle it to the next state */
                    switch( priority ) {
                        case TR_PRI_NORMAL: priority = TR_PRI_HIGH; break;
                        case TR_PRI_HIGH:   priority = TR_PRI_LOW; break;
                        default:            priority = TR_PRI_NORMAL; break;
                    }

                    /* apply that new state to the active files */
                    tr_torrentSetFilePriorities( tor,
                                                 (tr_file_index_t*)a->data,
                                                 (tr_file_index_t)a->len,
                                                 priority );
                }
                else if( downloadColumn )
                {
                    int enabled;

                    /* get the `enabled' state of the clicked row */
                    gtk_tree_model_get( model, &iter, FC_ENABLED, &enabled, -1 );

                    /* twiddle it to the next state */
                    enabled = !enabled;

                    /* apply that new state to the active files */
                    tr_torrentSetFileDLs( tor,
                                          (tr_file_index_t*)a->data,
                                          (tr_file_index_t)a->len,
                                          enabled );
                }

                refresh( data );

                /* the click was meant to change the priority or enabled state,
                   not to alter which rows were selected, so don't pass this
                   event on to the other handlers. */
                handled = isSelected;

                /* cleanup */
                g_array_free( a, TRUE );
            }

            gtk_tree_path_free( path );
        }
    }

    return handled;
}

GtkWidget *
file_list_new( TrCore * core, int torrentId )
{
    int width;
    GtkWidget * ret;
    GtkWidget * view;
    GtkWidget * scroll;
    GtkCellRenderer * rend;
    GtkTreeSelection * sel;
    GtkTreeViewColumn * col;
    GtkTreeView * tree_view;
    const char * title;
    PangoLayout * pango_layout;
    FileData * data = g_new0( FileData, 1 );

    data->core = core;

    /* create the view */
    view = gtk_tree_view_new( );
    tree_view = GTK_TREE_VIEW( view );
    gtk_tree_view_set_fixed_height_mode( tree_view, TRUE );
    gtk_tree_view_set_rules_hint( tree_view, TRUE );
    gtk_container_set_border_width( GTK_CONTAINER( view ), GUI_PAD_BIG );
    g_signal_connect( view, "button-press-event",
                      G_CALLBACK( onViewButtonPressed ), data );
    g_signal_connect( view, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );


    /* set up view */
    sel = gtk_tree_view_get_selection( tree_view );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );
    gtk_tree_view_expand_all( tree_view );
    gtk_tree_view_set_search_column( tree_view, FC_LABEL );

    /* add file column */

    col = GTK_TREE_VIEW_COLUMN ( g_object_new ( GTK_TYPE_TREE_VIEW_COLUMN,
                                                "expand", TRUE,
                                                "title", _( "File" ),
                                                NULL ) );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    rend = gtk_cell_renderer_pixbuf_new( );
    gtk_tree_view_column_pack_start( col, rend, FALSE );
    gtk_tree_view_column_add_attribute( col, rend, "pixbuf", FC_ICON );
    /* add text renderer */
    rend = gtk_cell_renderer_text_new( );
    g_object_set( rend, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_tree_view_column_pack_start( col, rend, TRUE );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderFilename, NULL, NULL );
    gtk_tree_view_column_set_sort_column_id( col, FC_LABEL );
    gtk_tree_view_append_column( tree_view, col );

    /* add "progress" column */
    title = _( "Progress" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += 30; /* room for the sort indicator */
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_progress_new( );
    col = gtk_tree_view_column_new_with_attributes( title, rend, "value", FC_PROG, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_sort_column_id( col, FC_PROG );
    gtk_tree_view_append_column( tree_view, col );

    /* add "enabled" column */
    title = _( "Download" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += 30; /* room for the sort indicator */
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_toggle_new( );
    col = gtk_tree_view_column_new_with_attributes( title, rend, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderDownload, NULL, NULL );
    gtk_tree_view_column_set_sort_column_id( col, FC_ENABLED );
    gtk_tree_view_append_column( tree_view, col );

    /* add priority column */
    title = _( "Priority" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += 30; /* room for the sort indicator */
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_text_new( );
    g_object_set( rend, "xalign", (gfloat)0.5, "yalign", (gfloat)0.5, NULL );
    col = gtk_tree_view_column_new_with_attributes( title, rend, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_sort_column_id( col, FC_PRIORITY );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderPriority, NULL, NULL );
    gtk_tree_view_append_column( tree_view, col );

    /* create the scrolled window and stick the view in it */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type ( GTK_SCROLLED_WINDOW( scroll ),
                                          GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER( scroll ), view );
    gtk_widget_set_size_request ( scroll, -1, 200 );

    ret = scroll;
    data->view = view;
    data->top = scroll;
    g_object_set_data_full( G_OBJECT( ret ), "file-data", data, freeData );
    file_list_set_torrent( ret, torrentId );

    return ret;
}
