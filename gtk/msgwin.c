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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "hig.h"
#include "msgwin.h"
#include "tr-prefs.h"
#include "util.h"

enum
{
    COL_LEVEL,
    COL_LINE,
    COL_FILE,
    COL_TIME,
    COL_CATEGORY,
    COL_MESSAGE,
    COL_SEQUENCE,
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
};

static struct MsgData myData;

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
                _("Couldn't write file \"%s\": %s"),
                filename, g_strerror( errno ) );
    }
    else
    {
        GtkTreeIter iter;
        GtkTreeModel * model = GTK_TREE_MODEL( data->sort );
        if( gtk_tree_model_iter_children( model, &iter, NULL ) ) do
        {
            int level;
            uint64_t time;
            char * category;
            char * message;
            char * date;
            const char * levelStr;

            gtk_tree_model_get( model, &iter,
                                COL_LEVEL, &level,
                                COL_TIME, &time,
                                COL_CATEGORY, &category,
                                COL_MESSAGE, &message,
                                -1 );
            date = rfc822date( time*1000u );
            switch( level ) {
                case TR_MSG_DBG: levelStr = "debug"; break;
                case TR_MSG_ERR: levelStr = "error"; break;
                default:         levelStr = "     "; break;
            }
            fprintf( fp, "%s\t%s\t%s\t%s\n", date, levelStr, category, message );

            /* cleanup */
            g_free( date );
            g_free( message );
            g_free( category );
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
onClearRequest( GtkWidget * w UNUSED, gpointer unused UNUSED )
{
    struct MsgData * data = &myData;
    gtk_list_store_clear( data->store );
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
    int col = GPOINTER_TO_INT( gcol );
    int level;
    char * str = NULL;
    gtk_tree_model_get( tree_model, iter, col, &str, COL_LEVEL, &level, -1 );
    g_object_set( renderer, "text", str,
                            "foreground", getForegroundColor( level ),
                            "ellipsize", PANGO_ELLIPSIZE_END,
                            NULL );
    g_free( str );
}

static void
renderTime( GtkTreeViewColumn  * column UNUSED,
            GtkCellRenderer    * renderer,
            GtkTreeModel       * tree_model,
            GtkTreeIter        * iter,
            gpointer             data UNUSED )
{
    int level;
    uint64_t tmp;
    time_t time;
    struct tm tm;
    char buf[16];

    gtk_tree_model_get(tree_model, iter, COL_TIME, &tmp, COL_LEVEL, &level, -1 );
    time = tmp;
    tm = *localtime( &time );
    g_snprintf( buf, sizeof( buf ), "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec );
    g_object_set (renderer, "text", buf,
                            "foreground", getForegroundColor( level ),
                            NULL );
}

static void
appendColumn( GtkTreeView * view, int col )
{
    GtkCellRenderer * r;
    GtkTreeViewColumn * c;
    int sort_col = col;
    const char * title = NULL;

    switch( col ) {
        case COL_LEVEL:    title = NULL; break;
        case COL_LINE:     title = NULL; break;
        case COL_FILE:     title = _( "Filename" ); break;
        case COL_TIME:     title = _( "Time" ); break;
        case COL_CATEGORY: title = _( "Name"); break;
        case COL_MESSAGE:  title = _( "Message" ); break;
        default: g_assert_not_reached( );
    }

    switch( col )
    {
        case COL_LINE:
            r = gtk_cell_renderer_text_new( );
            c = gtk_tree_view_column_new_with_attributes( title, r, "text", col, NULL );
            gtk_tree_view_column_set_resizable( c, FALSE );
            break;

        case COL_FILE:
        case COL_CATEGORY:
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

        case COL_TIME:
            r = gtk_cell_renderer_text_new( );
            c = gtk_tree_view_column_new_with_attributes( title, r, NULL );
            gtk_tree_view_column_set_cell_data_func( c, r, renderTime, NULL, NULL );
            gtk_tree_view_column_set_resizable( c, TRUE );
            sort_col = COL_SEQUENCE;
            break;

        default:
            g_assert_not_reached( );
            break;
    }

    gtk_tree_view_column_set_sort_column_id( c, sort_col );
    gtk_tree_view_append_column( view, c );
}

static gboolean
isRowVisible( GtkTreeModel * model, GtkTreeIter * iter, gpointer gdata )
{
    struct MsgData * data = gdata;
    int level;
    gtk_tree_model_get( model, iter, COL_LEVEL, &level, -1 );
    return level <= data->maxLevel;
}

/**
***  Public Functions
**/

GtkWidget *
msgwin_create( TrCore * core )
{
  unsigned int i;
  GtkListStore * store;
  GtkWidget * win;
  GtkWidget * vbox;
  GtkWidget * levels;
  GtkWidget * toolbar;
  GtkWidget * w;
  GtkWidget * view;
  GtkWidget * l;
  GtkCellRenderer * renderer;
  int ii, curlevel;
  struct MsgData * data;

  data = &myData;
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
  gtk_toolbar_set_style( GTK_TOOLBAR( toolbar), GTK_TOOLBAR_BOTH_HORIZ );

  gtk_toolbar_insert_stock(GTK_TOOLBAR(toolbar), GTK_STOCK_SAVE,
                           NULL, NULL,
                           G_CALLBACK(onSaveRequest), data, -1);

  gtk_toolbar_insert_stock(GTK_TOOLBAR(toolbar), GTK_STOCK_CLEAR,
                           NULL, NULL,
                           G_CALLBACK(onClearRequest), data, -1);

  gtk_toolbar_insert_space(GTK_TOOLBAR(toolbar), -1);


  l = gtk_label_new( _( "Level" ) );
  gtk_misc_set_padding( GTK_MISC( l ), GUI_PAD, 0 );
  gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                             GTK_TOOLBAR_CHILD_WIDGET, l,
                             NULL, _("Set the verbosity level"),
                             NULL, NULL, NULL, NULL);

  w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
  gtk_widget_set_size_request( w, GUI_PAD_SMALL, GUI_PAD_SMALL );
  gtk_toolbar_append_element( GTK_TOOLBAR(toolbar),
                              GTK_TOOLBAR_CHILD_WIDGET, w,
                              NULL, NULL, NULL, NULL, NULL, NULL);

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
  g_signal_connect( levels, "changed",
                    G_CALLBACK(level_combo_changed_cb), data );

  gtk_toolbar_append_element( GTK_TOOLBAR( toolbar ),
                              GTK_TOOLBAR_CHILD_WIDGET, levels,
                              NULL, _("Set the verbosity level"),
                              NULL, NULL, NULL, NULL);

  gtk_box_pack_start( GTK_BOX( vbox ), toolbar, FALSE, FALSE, 0 );

  /**
  ***  messages
  **/

  view = gtk_tree_view_new_with_model( data->sort );
  data->view = GTK_TREE_VIEW( view );
  gtk_tree_view_set_rules_hint( data->view, TRUE );
  appendColumn( data->view, COL_TIME );
  appendColumn( data->view, COL_CATEGORY );
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


  msgwin_update( );
  gtk_widget_show_all( win );
  return win;
}

void
msgwin_loadpref( void )
{
    struct MsgData * data = &myData;

    data->store = gtk_list_store_new( N_COLUMNS,
                                      G_TYPE_INT,       /* level */
                                      G_TYPE_INT,       /* line number */
                                      G_TYPE_STRING,    /* file */
                                      G_TYPE_UINT64,    /* time */
                                      G_TYPE_STRING,    /* category */
                                      G_TYPE_STRING,    /* message */
                                      G_TYPE_INT );     /* sequence */

    data->filter = gtk_tree_model_filter_new( GTK_TREE_MODEL( data->store ), NULL );
    data->sort = gtk_tree_model_sort_new_with_model( data->filter );
    data->maxLevel = pref_int_get( PREF_KEY_MSGLEVEL );
    gtk_tree_model_filter_set_visible_func( GTK_TREE_MODEL_FILTER( data->filter ),
                                            isRowVisible, data, NULL );
}

void
msgwin_update( void )
{
    tr_msg_list * i;
    tr_msg_list * msgs;
    struct MsgData * data = &myData;
    static int sequence = 1;
    const char * default_category = g_get_application_name( );

    msgs = tr_getQueuedMessages();
    for( i=msgs; i; i=i->next )
    {
        GtkTreeIter unused;

        gtk_list_store_insert_with_values( data->store, &unused, -1,
                                           COL_LEVEL, (int)i->level,
                                           COL_LINE, i->line,
                                           COL_FILE, i->file,
                                           COL_TIME, (uint64_t)i->when,
                                           COL_CATEGORY, ( i->name ? i->name : default_category ),
                                           COL_MESSAGE, i->message,
                                           COL_SEQUENCE, sequence++,
                                           -1 );
    }
    tr_freeMessageList( msgs );
}
