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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_getRatio */

#include "file-list.h"
#include "hig.h"
#include "icons.h"

enum
{
    SUB_STATE_HIGH          = ( 1 << 0 ),
    SUB_STATE_NORMAL        = ( 1 << 1 ),
    SUB_STATE_LOW           = ( 1 << 2 ),
    SUB_STATE_PRIORITY_MASK = ( SUB_STATE_HIGH | SUB_STATE_NORMAL | SUB_STATE_LOW ),
    SUB_STATE_DOWNLOAD      = ( 1 << 4 ),
    SUB_STATE_IGNORE        = ( 1 << 5 ),
    SUB_STATE_DOWNLOAD_MASK = ( SUB_STATE_DOWNLOAD | SUB_STATE_IGNORE )
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
    FC_SUB_SIZE,
    FC_SUB_HAVE,
    FC_SUB_STATE,
    N_FILE_COLS
};

typedef struct
{
    int             torrentId;
    TrCore *        core;
    tr_torrent    * tor;
    GtkWidget *     top;
    GtkWidget *     view;
    GtkTreeModel *  model; /* same object as store, but recast */
    GtkTreeStore *  store; /* same object as model, but recast */
    tr_file_stat *  refresh_file_stat;
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
freeData( gpointer gdata )
{
    FileData * data = gdata;

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
    gboolean is_enabled;
    unsigned int index;
    uint64_t size;
    uint64_t old_have;
    int old_sub_state = 0;
    int64_t old_sub_size = 0;
    int64_t old_sub_have = 0;
    int old_prog;
    int old_priority;

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_ENABLED, &is_enabled,
                                     FC_PRIORITY, &old_priority,
                                     FC_INDEX, &index,
                                     FC_HAVE, &old_have,
                                     FC_SIZE, &size,
                                     FC_SUB_STATE, &old_sub_state,
                                     FC_SUB_SIZE, &old_sub_size,
                                     FC_SUB_HAVE, &old_sub_have,
                                     FC_PROG, &old_prog,
                                     -1  );
//g_message ( "is_file {%d} index {%d} name {%s}", (int)is_file, (int)index, name );
    if( is_file )
    {
        int sub_state;
        tr_torrent * tor = data->tor;
        const int download = tr_torrentGetFileDL( tor, index );
        const int priority = tr_torrentGetFilePriority( tor, index );
        const uint64_t have = data->refresh_file_stat[index].bytesCompleted;
        const int prog = (int)((100.0*have)/size);

        switch( priority ) {
            case TR_PRI_HIGH:   sub_state = SUB_STATE_HIGH;   break;
            case TR_PRI_NORMAL: sub_state = SUB_STATE_NORMAL; break;
            case TR_PRI_LOW:    sub_state = SUB_STATE_LOW;    break;
        }
        sub_state |= ( is_enabled ? SUB_STATE_DOWNLOAD : SUB_STATE_IGNORE );

        if( (priority!=old_priority) || (download!=is_enabled)
                                     || (have!=old_have)
                                     || (sub_state!=old_sub_state)
                                     || (prog!=old_prog) )
            gtk_tree_store_set( data->store, iter, FC_PRIORITY, priority,
                                                   FC_ENABLED, download,
                                                   FC_HAVE, have,
                                                   FC_SUB_STATE, sub_state,
                                                   FC_SUB_HAVE, have,
                                                   FC_PROG, (int)((100.0*have)/size),
                                                   -1 );
    }
    else
    {
        GtkTreeIter child;
        int state = 0;
        int64_t size = 0;
        int64_t have = 0;
        int prog;

        /* since gtk_tree_model_foreach() is depth-first, we can
         * get the `sub' info by walking the immediate children */

        if( gtk_tree_model_iter_children( model, &child, iter ) ) do
        {
            int child_state;
            int64_t child_have, child_size;
            gtk_tree_model_get( model, &child, FC_SUB_SIZE, &child_size,
                                               FC_SUB_HAVE, &child_have,
                                               FC_SUB_STATE, &child_state,
                                                -1 );
            size += child_size;
            have += child_have;
            state |= child_state;
        }
        while( gtk_tree_model_iter_next( model, &child ) );

        prog = (int)((100.0*have)/size);

        if( (have!=old_sub_have) || (size!=old_sub_size)
                                 || (state!=old_sub_state)
                                 || (prog!=old_prog) )
            gtk_tree_store_set( data->store, iter, FC_SUB_SIZE, size,
                                                   FC_SUB_HAVE, have,
                                                   FC_SUB_STATE, state,
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

        /* initialize the temporary variables */
        data->tor = tr_torrentFindFromId( tr_core_session( data->core ), data->torrentId );
        data->refresh_file_stat = tr_torrentFiles( tor, &fileCount );

        gtr_tree_model_foreach_postorder( data->model, refreshFilesForeach, data );
        //gtk_tree_model_foreach( data->model, refreshFilesForeach, data );
        //gtk_tree_model_foreach( data->model, addSubForeach, data );

        /* clean up the temporary variables */
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
    GtkTreeSelection *  sel;
    GArray *            array;
};

static gboolean
getSelectedFilesForeach( GtkTreeModel *       model,
                         GtkTreePath   * path UNUSED,
                         GtkTreeIter *        iter,
                         gpointer             gdata )
{
    struct ActiveData * data = gdata;
    unsigned int        i;
    gboolean            is_file = FALSE;
    gboolean            is_active = FALSE;

    /* active == if it's selected, or any ancestor is selected */
    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file, FC_INDEX, &i, -1 );
    if( is_file )
    {
        is_active = gtk_tree_selection_iter_is_selected( data->sel, iter );
        if( !is_active )
        {
            GtkTreeIter walk = *iter;
            GtkTreeIter parent;
            while( !is_active
                 && gtk_tree_model_iter_parent( model, &parent, &walk ) )
            {
                is_active = gtk_tree_selection_iter_is_selected( data->sel,
                                                                 &parent );
                walk = parent;
            }
        }
    }

    if( is_active )
        g_array_append_val( data->array, i );

    return FALSE; /* keep walking */
}

static void
getSelectedFilesAndDescendants( GtkTreeView * view,
                                GArray *      indices )
{
    struct ActiveData data;

    data.sel = gtk_tree_view_get_selection( view );
    data.array  = indices;
    gtk_tree_model_foreach( gtk_tree_view_get_model( view ),
                            getSelectedFilesForeach, &data );
}

struct SubtreeForeachData
{
    GArray * array;
    GtkTreePath * path;
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

    gtk_tree_model_get( model, iter,
                        FC_IS_FILE, &is_file,
                        FC_INDEX, &i, -1 );
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
getActiveFilesForPath( GtkTreeView * view,
                       GtkTreePath * path )
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
    GtkWidget * w;
    tr_torrent * tor;
    GtkTreeIter * iter;
    GtkTreeStore * store;
};

struct row_struct
{
    char * name;
    int index;
    uint64_t length;
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
                                       FC_SUB_SIZE, child_data->length,
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
                                 G_TYPE_BOOLEAN,   /* dl enabled */
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
            g_free( root_data );
        }

        refresh( data );
        data->timeout_tag = gtr_timeout_add_seconds( 2, refreshModel, data );
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW( data->view ), GTK_TREE_MODEL( store ) );
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
                gpointer             data   UNUSED )
{
    char *   filename;
    char *   str;
    int64_t  subsize;
    char     buf[64];

    gtk_tree_model_get( model, iter, FC_LABEL, &filename,
                                     FC_SUB_SIZE, &subsize,
                                     -1 );
    tr_strlsize( buf, subsize, sizeof( buf ) );
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
    int      sub_state;
    gboolean enabled;
    gboolean active = FALSE;
    gboolean inconsistent = FALSE;
    gboolean is_file = FALSE;

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_ENABLED, &enabled,
                                     FC_SUB_STATE, &sub_state,
                                     -1 );
    if( is_file && enabled )
        active = TRUE;
    else if( is_file )
        active = FALSE;
    else switch( sub_state & SUB_STATE_DOWNLOAD_MASK )
        {
            case SUB_STATE_DOWNLOAD:
                active = TRUE; break;

            case SUB_STATE_IGNORE:
                active = FALSE; break;

            default:
                inconsistent = TRUE; break;
        }

    g_object_set( renderer, "inconsistent", inconsistent,
                  "active", active,
                  NULL );
}

static void
renderPriority( GtkTreeViewColumn  * column UNUSED,
                GtkCellRenderer    * renderer,
                GtkTreeModel       * model,
                GtkTreeIter        * iter,
                gpointer             data   UNUSED )
{
    int          priority;
    int          sub_state;
    gboolean     is_file = FALSE;
    const char * text = "";

    gtk_tree_model_get( model, iter, FC_IS_FILE, &is_file,
                                     FC_PRIORITY, &priority,
                                     FC_SUB_STATE, &sub_state,
                                     -1 );
    if( !is_file ) {
        switch( sub_state & SUB_STATE_PRIORITY_MASK ) {
            case SUB_STATE_HIGH: priority = TR_PRI_HIGH; break; 
            case SUB_STATE_NORMAL: priority = TR_PRI_NORMAL; break; 
            case SUB_STATE_LOW: priority = TR_PRI_LOW; break; 
            default: priority = 666; break;
        }
    }

    switch( priority ) {
        case TR_PRI_HIGH: text = _( "High" ); break;
        case TR_PRI_NORMAL: text = _( "Normal" ); break;
        case TR_PRI_LOW: text = _( "Low" ); break;
        default: text = _( "Mixed" ); break;
    }

    g_object_set( renderer, "text", text,
                            "xalign", (gfloat)0.5,
                            "yalign", (gfloat)0.5,
                            NULL );
}

static gboolean
onViewButtonPressed( GtkWidget      * w,
                     GdkEventButton * event,
                     gpointer         gdata )
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
                GArray *           a = getActiveFilesForPath( view, path );
                GtkTreeSelection * sel = gtk_tree_view_get_selection( view );
                const gboolean     isSelected =
                    gtk_tree_selection_path_is_selected( sel, path );
                GtkTreeModel *     model = gtk_tree_view_get_model( view );
                GtkTreeIter        iter;

                gtk_tree_model_get_iter( model, &iter, path );

                if( priorityColumn )
                {
                    gboolean is_file;
                    int      sub_state;
                    int      priority;

                    /* get the `priority' state of the clicked row */
                    gtk_tree_model_get( model, &iter, FC_IS_FILE, &is_file,
                                                      FC_PRIORITY, &priority,
                                                      FC_SUB_STATE, &sub_state,
                                                      -1 );

                    /* twiddle it to the next state */
                    if( !is_file ) switch( sub_state & SUB_STATE_PRIORITY_MASK )
                        {
                            case SUB_STATE_NORMAL:
                                priority = TR_PRI_HIGH; break;

                            case SUB_STATE_HIGH:
                                priority = TR_PRI_LOW; break;

                            default:
                                priority = TR_PRI_NORMAL; break;
                        }
                    else switch( priority )
                        {
                            case TR_PRI_LOW:
                                priority = TR_PRI_NORMAL; break;

                            case TR_PRI_NORMAL:
                                priority = TR_PRI_HIGH; break;

                            case TR_PRI_HIGH:
                                priority = TR_PRI_LOW; break;
                        }

                    /* apply that new state to the active files */
                    tr_torrentSetFilePriorities( tor,
                                                 (tr_file_index_t*)a->data,
                                                 (tr_file_index_t)a->len,
                                                 priority );
                }
                else if( downloadColumn )
                {
                    gboolean is_file;
                    int      sub_state;
                    gboolean enabled;

                    /* get the `enabled' state of the clicked row */
                    gtk_tree_model_get( model, &iter, FC_IS_FILE, &is_file,
                                                      FC_ENABLED, &enabled,
                                                      FC_SUB_STATE, &sub_state, -1 );

                    /* twiddle it to the next state */
                    if( is_file )
                        enabled = !enabled;
                    else
                        enabled = ( sub_state & SUB_STATE_IGNORE ) ? 1 : 0;

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
    const char * title;
    PangoLayout  * pango_layout;
    int width;
    GtkWidget *         ret;
    GtkWidget *         view, * scroll;
    GtkCellRenderer *   rend;
    GtkTreeViewColumn * col;
    GtkTreeSelection *  sel;
    FileData *          data = g_new0( FileData, 1 );

    data->core = core;

    /* create the view */
    view = gtk_tree_view_new( );
    gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW( view ), TRUE );
    gtk_tree_view_set_rules_hint( GTK_TREE_VIEW( view ), TRUE );
    gtk_container_set_border_width( GTK_CONTAINER( view ), GUI_PAD_BIG );
    g_signal_connect( view, "button-press-event",
                      G_CALLBACK( onViewButtonPressed ), data );
    g_signal_connect( view, "button-release-event",
                      G_CALLBACK( on_tree_view_button_released ), NULL );


    /* set up view */
    sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( view ) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_MULTIPLE );
    gtk_tree_view_expand_all( GTK_TREE_VIEW( view ) );
    gtk_tree_view_set_search_column( GTK_TREE_VIEW( view ), FC_LABEL );

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
    gtk_tree_view_append_column( GTK_TREE_VIEW( view ), col );

    /* add "progress" column */
    title = _( "Progress" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += GUI_PAD * 2;
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_progress_new( );
    col = gtk_tree_view_column_new_with_attributes( title, rend, "value", FC_PROG, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    //gtk_tree_view_column_set_cell_data_func( col, rend, renderProgress, NULL, NULL );
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col );

    /* add "enabled" column */
    title = _( "Download" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += GUI_PAD * 2;
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_toggle_new( );
    col = gtk_tree_view_column_new_with_attributes( title, rend, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderDownload, NULL, NULL );
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col );

    /* add priority column */
    title = _( "Priority" );
    pango_layout = gtk_widget_create_pango_layout( view, title );
    pango_layout_get_pixel_size( pango_layout, &width, NULL );
    width += GUI_PAD * 2;
    g_object_unref( G_OBJECT( pango_layout ) );
    rend = gtk_cell_renderer_text_new( );
    col = gtk_tree_view_column_new_with_attributes( title, rend, NULL );
    gtk_tree_view_column_set_fixed_width( col, width );
    gtk_tree_view_column_set_sizing( col, GTK_TREE_VIEW_COLUMN_FIXED );
    gtk_tree_view_column_set_cell_data_func( col, rend, renderPriority, NULL, NULL );
    gtk_tree_view_append_column ( GTK_TREE_VIEW( view ), col );

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
