/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "hig.h"
#include "msgwin.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

enum
{
    COL_SEQUENCE,
    COL_NAME,
    COL_MESSAGE,
    COL_TR_MSG,
    N_COLUMNS
};

struct MsgData
{
    TrCore * core;
    GtkTreeView * view;
    GtkListStore * store;
    GtkTreeModel * filter;
    GtkTreeModel * sort;
    int maxLevel;
    gboolean isPaused;
    guint refresh_tag;
};

static struct tr_msg_list * myTail = NULL;
static struct tr_msg_list * myHead = NULL;

/***
****
***/

static void
level_combo_changed_cb( GtkWidget * w, gpointer gdata )
{
    struct MsgData * data = gdata;
    GtkTreeIter iter;
    if( gtk_combo_box_get_active_iter( GTK_COMBO_BOX(w), &iter ) )
    {
        int level = 0;
        GtkTreeModel * m = gtk_combo_box_get_model( GTK_COMBO_BOX( w ) );
        gtk_tree_model_get( m, &iter, 1, &level, -1 );

        tr_setMessageLevel( level );
        tr_core_set_pref_int( data->core, PREF_KEY_MSGLEVEL, level );
        data->maxLevel = level;
        gtk_tree_model_filter_refilter( GTK_TREE_MODEL_FILTER( data->filter ) );
    }
}

static void
doSave( GtkWindow      * parent,
        struct MsgData * data,
        const char     * filename )
{
    FILE * fp = fopen( filename, "w+" );
    if( !fp )
    {
        errmsg( parent,
                _("Couldn't save file \"%s\": %s"),
                filename, g_strerror( errno ) );
    }
    else
    {
        GtkTreeIter iter;
        GtkTreeModel * model = GTK_TREE_MODEL( data->sort );
        if( gtk_tree_model_iter_children( model, &iter, NULL ) ) do
        {
            char * date;
            const char * levelStr;
            const struct tr_msg_list * node;

            gtk_tree_model_get( model, &iter,
                                COL_TR_MSG, &node,
                                -1 );
            date = rfc822date( node->when*1000u );
            switch( node->level ) {
                case TR_MSG_DBG: levelStr = "debug"; break;
                case TR_MSG_ERR: levelStr = "error"; break;
                default:         levelStr = "     "; break;
            }
            fprintf( fp, "%s\t%s\t%s\t%s\n", date, levelStr, node->name, node->message );

            g_free( date );
        }
        while( gtk_tree_model_iter_next( model, &iter ) );
        fclose( fp );
    }
}

static void
onSaveDialogResponse( GtkWidget * d, int response, gpointer data )
{
  if( response == GTK_RESPONSE_ACCEPT )
  {
      char * filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( d ) );
      doSave( GTK_WINDOW( d ), data, filename );
      g_free( filename );
  }

  gtk_widget_destroy( d );
}

static void
onSaveRequest( GtkWidget * w, gpointer data )
{
  GtkWindow * window = GTK_WINDOW( gtk_widget_get_toplevel( w ) );
  GtkWidget * d = gtk_file_chooser_dialog_new( _("Save Log"), window,
                                               GTK_FILE_CHOOSER_ACTION_SAVE,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                               NULL );
  gtk_dialog_set_alternative_button_order( GTK_DIALOG( d ),
                                           GTK_RESPONSE_ACCEPT,
                                           GTK_RESPONSE_CANCEL,
                                           -1 );
  g_signal_connect( d, "response",
                    G_CALLBACK( onSaveDialogResponse ), data );
  gtk_widget_show( d );
}

static void
onClearRequest( GtkWidget * w UNUSED, gpointer gdata )
{
    struct MsgData * data = gdata;
    gtk_list_store_clear( data->store );
    tr_freeMessageList( myHead );
    myHead = myTail = NULL;
}

static void
onPauseToggled( GtkToggleToolButton * w, gpointer gdata )
{
    struct MsgData * data = gdata;
    data->isPaused = gtk_toggle_tool_button_get_active( w );
}

static struct {
  const char * label;
  const char * pref;
  int id;
} trLevels[] = {
  { N_("Error"), "error", TR_MSG_ERR },
  { N_("Information"), "info",  TR_MSG_INF },
  { N_("Debug"), "debug", TR_MSG_DBG },
};

static const char*
getForegroundColor( int msgLevel )
{
    const char * foreground;
    switch( msgLevel ) {
        case TR_MSG_DBG: foreground = "gray"; break;
        case TR_MSG_INF: foreground = "black"; break;
        case TR_MSG_ERR: foreground = "red"; break;
        default: g_assert_not_reached( );
    }
    return foreground;
}

static void
renderText( GtkTreeViewColumn  * column UNUSED,
            GtkCellRenderer    * renderer,
            GtkTreeModel       * tree_model,
            GtkTreeIter        * iter,
            gpointer             gcol )
{
    const int col = GPOINTER_TO_INT( gcol );
    char * str = NULL;
    const struct tr_msg_list * node;
    gtk_tree_model_get( tree_model, iter, col, &str, COL_TR_MSG, &node, -1 );
    g_object_set( renderer, "text", str,
                            "foreground", getForegroundColor( node->level ),
                            "ellipsize", PANGO_ELLIPSIZE_END,
                            NULL );
}

static void
renderTime( GtkTreeViewColumn  * column UNUSED,
            GtkCellRenderer    * renderer,
            GtkTreeModel       * tree_model,
            GtkTreeIter        * iter,
            gpointer             data UNUSED )
{
    struct tm tm;
    char buf[16];
    const struct tr_msg_list * node;

    gtk_tree_model_get(tree_model, iter, COL_TR_MSG, &node, -1 );
    tm = *localtime( &node->when );
    g_snprintf( buf, sizeof( buf ), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec );
    g_object_set (renderer, "text", buf,
                            "foreground", getForegroundColor( node->level ),
                            NULL );
}

static void
appendColumn( GtkTreeView * view, int col )
{
    GtkCellRenderer * r;
    GtkTreeViewColumn * c;
    const char * title = NULL;

    switch( col ) {
        case COL_SEQUENCE: title = _( "Time" ); break;
        /* noun.  column title for a list */
        case COL_NAME: title = _( "Name"); break;
        /* noun.  column title for a list */
        case COL_MESSAGE:  title = _( "Message" ); break;
        default: g_assert_not_reached( );
    }

    switch( col )
    {
        case COL_NAME:
            r = gtk_cell_renderer_text_new( );
            c = gtk_tree_view_column_new_with_attributes( title, r, NULL );
            gtk_tree_view_column_set_cell_data_func( c, r, renderText, GINT_TO_POINTER(col), NULL );
            gtk_tree_view_column_set_sizing( c, GTK_TREE_VIEW_COLUMN_FIXED );
            gtk_tree_view_column_set_fixed_width( c, 200 );
            gtk_tree_view_column_set_resizable( c, TRUE );
            break;

        case COL_MESSAGE:
            r = gtk_cell_renderer_text_new( );
            c = gtk_tree_view_column_new_with_attributes( title, r, NULL );
            gtk_tree_view_column_set_cell_data_func( c, r, renderText, GINT_TO_POINTER(col), NULL );
            gtk_tree_view_column_set_sizing( c, GTK_TREE_VIEW_COLUMN_FIXED );
            gtk_tree_view_column_set_fixed_width( c, 500 );
            gtk_tree_view_column_set_resizable( c, TRUE );
            break;

        case COL_SEQUENCE:
            r = gtk_cell_renderer_text_new( );
            c = gtk_tree_view_column_new_with_attributes( title, r, NULL );
            gtk_tree_view_column_set_cell_data_func( c, r, renderTime, NULL, NULL );
            gtk_tree_view_column_set_resizable( c, TRUE );
            break;

        default:
            g_assert_not_reached( );
            break;
    }

    gtk_tree_view_column_set_sort_column_id( c, col );
    gtk_tree_view_append_column( view, c );
}

static gboolean
isRowVisible( GtkTreeModel * model, GtkTreeIter * iter, gpointer gdata )
{
    const struct MsgData * data = gdata;
    const struct tr_msg_list * node;
    gtk_tree_model_get( model, iter, COL_TR_MSG, &node, -1 );
    return node->level <= data->maxLevel;
}

static void
onWindowDestroyed( gpointer gdata, GObject * deadWindow UNUSED )
{
    struct MsgData * data = gdata;
    g_source_remove( data->refresh_tag );
    g_free( data );
}

static tr_msg_list *
addMessages( GtkListStore * store, struct tr_msg_list * head )
{
    const char * default_name = g_get_application_name( );
    static unsigned int sequence = 1;
    tr_msg_list * i;

    for( i=head; i; i=i->next )
    {
        GtkTreeIter unused;

        gtk_list_store_insert_with_values( store, &unused, 0,
                                           COL_TR_MSG, i,
                                           COL_NAME, ( i->name ? i->name : default_name ),
                                           COL_MESSAGE, i->message,
                                           COL_SEQUENCE, sequence++,
                                           -1 );

         if( !i->next )
             break;
    }

    return i; /* tail */
}

static gboolean
onRefresh( gpointer gdata )
{
    struct MsgData * data = gdata;
    if( !data->isPaused )
    {
        tr_msg_list * msgs = tr_getQueuedMessages( );
        if( msgs )
        {
            /* add the new messages and append them to the end of
             * our persistent list */
            tr_msg_list * tail = addMessages( data->store, msgs );
            if( myTail )
                myTail->next = msgs;
            else
                myHead = msgs;
            myTail = tail;
        }
    }
    return TRUE;
}

static GtkWidget*
debug_level_combo_new( void )
{
    unsigned int i;
    int ii;
    int curlevel;
    GtkWidget * levels;
    GtkListStore * store;
    GtkCellRenderer * renderer;

    store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);

    curlevel = pref_int_get( PREF_KEY_MSGLEVEL );
    for( i=ii=0; i<G_N_ELEMENTS(trLevels); ++i ) {
        GtkTreeIter iter;
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter, 0, _(trLevels[i].label),
                                          1, trLevels[i].id,
                                         -1);
        if( trLevels[i].id == curlevel )
            ii = i;
    }
    levels = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref( G_OBJECT( store ) );
    store = NULL;

    renderer = gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start( GTK_CELL_LAYOUT(levels), renderer, TRUE );
    gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT(levels), renderer,
                                    "text", 0,
                                    NULL );
    gtk_combo_box_set_active( GTK_COMBO_BOX( levels ), ii );

    return levels;
}


/**
***  Public Functions
**/

GtkWidget *
msgwin_new( TrCore * core )
{
    GtkWidget * win;
    GtkWidget * vbox;
    GtkWidget * toolbar;
    GtkWidget * w;
    GtkWidget * view;
    GtkToolItem * item;
    struct MsgData * data;

    data = g_new0( struct MsgData, 1 );
    data->core = core;

    win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW( win ), _( "Message Log" ) );
    gtk_window_set_default_size( GTK_WINDOW( win ), 600, 400 );
    gtk_window_set_role( GTK_WINDOW( win ), "message-log" );
    vbox = gtk_vbox_new( FALSE, 0 );

    /**
    ***  toolbar
    **/

    toolbar = gtk_toolbar_new( );
    gtk_toolbar_set_orientation( GTK_TOOLBAR( toolbar ), GTK_ORIENTATION_HORIZONTAL );
    gtk_toolbar_set_style( GTK_TOOLBAR( toolbar ), GTK_TOOLBAR_BOTH_HORIZ );

      item = gtk_tool_button_new_from_stock( GTK_STOCK_SAVE );
      g_object_set( G_OBJECT( item ), "is-important", TRUE, NULL );
      g_signal_connect( item, "clicked", G_CALLBACK(onSaveRequest), data );
      gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

      item = gtk_tool_button_new_from_stock( GTK_STOCK_CLEAR );
      g_object_set( G_OBJECT( item ), "is-important", TRUE, NULL );
      g_signal_connect( item, "clicked", G_CALLBACK(onClearRequest), data );
      gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

    item = gtk_separator_tool_item_new( );
    gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

      item = gtk_toggle_tool_button_new_from_stock( GTK_STOCK_MEDIA_PAUSE );
      g_object_set( G_OBJECT( item ), "is-important", TRUE, NULL );
      g_signal_connect( item, "toggled", G_CALLBACK(onPauseToggled), data );
      gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

    item = gtk_separator_tool_item_new( );
    gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

      w = gtk_label_new( _( "Level" ) );
      gtk_misc_set_padding( GTK_MISC( w ), GUI_PAD, 0 );
      item = gtk_tool_item_new( );
      gtk_container_add( GTK_CONTAINER( item ), w );
      gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

      w = debug_level_combo_new( );
      g_signal_connect( w, "changed", G_CALLBACK(level_combo_changed_cb), data );
      item = gtk_tool_item_new( );
      gtk_container_add( GTK_CONTAINER( item ), w );
      gtk_toolbar_insert( GTK_TOOLBAR( toolbar ), item, -1 );

    gtk_box_pack_start( GTK_BOX( vbox ), toolbar, FALSE, FALSE, 0 );

    /**
    ***  messages
    **/

    data->store = gtk_list_store_new( N_COLUMNS,
                                      G_TYPE_UINT,       /* sequence */
                                      G_TYPE_POINTER,    /* category */
                                      G_TYPE_POINTER,    /* message */
                                      G_TYPE_POINTER);   /* struct tr_msg_list */

    addMessages( data->store, myHead );
    onRefresh( data ); /* much faster to populate *before* it has listeners */

    data->filter = gtk_tree_model_filter_new( GTK_TREE_MODEL( data->store ), NULL );
    data->sort = gtk_tree_model_sort_new_with_model( data->filter );
    gtk_tree_sortable_set_sort_column_id( GTK_TREE_SORTABLE( data->sort ),
                                          COL_SEQUENCE,
                                          GTK_SORT_ASCENDING );
    data->maxLevel = pref_int_get( PREF_KEY_MSGLEVEL );
    gtk_tree_model_filter_set_visible_func( GTK_TREE_MODEL_FILTER( data->filter ),
                                            isRowVisible, data, NULL );


    view = gtk_tree_view_new_with_model( data->sort );
    data->view = GTK_TREE_VIEW( view );
    gtk_tree_view_set_rules_hint( data->view, TRUE );
    appendColumn( data->view, COL_SEQUENCE );
    appendColumn( data->view, COL_NAME );
    appendColumn( data->view, COL_MESSAGE );
    w = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( w ),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC );
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW( w ),
                                         GTK_SHADOW_IN);
    gtk_container_add( GTK_CONTAINER( w ), view );
    gtk_box_pack_start( GTK_BOX( vbox ), w, TRUE, TRUE, 0 );
    gtk_container_add( GTK_CONTAINER( win ), vbox );

    data->refresh_tag = g_timeout_add( 1666, onRefresh, data );
    g_object_weak_ref( G_OBJECT( win ), onWindowDestroyed, data );

    gtk_widget_show_all( win );
    return win;
}
