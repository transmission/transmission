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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "hig.h"
#include "msgwin.h"
#include "tr-prefs.h"
#include "util.h"

#define MAX_MSGCOUNT 5000

#define COL_LVL 0
#define COL_MSG 1

static GtkTextBuffer * textbuf = NULL;

static GtkTextTag*
get_or_create_tag (GtkTextTagTable * table, const char * key)
{
  GtkTextTag * tag;

  g_assert (table);
  g_assert (key && *key);

  tag = gtk_text_tag_table_lookup (table, key);
  if (!tag) {
    tag = gtk_text_tag_new (key);
    gtk_text_tag_table_add (table, tag);
    g_object_unref (tag); /* table refs it */
  }

  return tag;
}

static GtkTextBuffer*
debug_window_text_buffer_new ( void )
{
  GtkTextBuffer * buffer = gtk_text_buffer_new ( NULL );

  GtkTextTagTable * table = gtk_text_buffer_get_tag_table (buffer);

  g_object_set (get_or_create_tag(table,"bold"),
      "weight", PANGO_WEIGHT_BOLD,
      NULL);
  
  g_object_set (get_or_create_tag (table, "info"),
      "foreground", "black",
      NULL);
 
  g_object_set (get_or_create_tag (table, "error"),
      "foreground", "red",
      NULL);

  g_object_set (get_or_create_tag (table, "debug"),
      "foreground", "gray",
      NULL);

  return buffer;
}

void
msgwin_update( void )
{
  tr_msg_list * msgs, * ii;

  g_assert( textbuf != NULL );

  msgs = tr_getQueuedMessages();
  for( ii = msgs; NULL != ii; ii = ii->next )
  {
    int len;
    char * line;
    const char * tag = NULL;
    struct tm * tm = localtime( &ii->when );
    GtkTextIter mark_start, mark_end;

    switch( ii->level ) {
      case TR_MSG_ERR: tag = "error"; break;
      case TR_MSG_INF: tag = "info"; break;
      case TR_MSG_DBG: tag = "debug"; break;
    }

    line = ( ii->name != NULL )
        ? g_strdup_printf( "%02i:%02i:%02i [%s] %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, ii->name, ii->message )
        : g_strdup_printf( "%02i:%02i:%02i %s\n", tm->tm_hour, tm->tm_min, tm->tm_sec, ii->message );
    len = strlen( line );

    gtk_text_buffer_get_end_iter( textbuf, &mark_end );
    gtk_text_buffer_insert( textbuf, &mark_end, line, len );
    mark_start = mark_end;
    gtk_text_iter_backward_chars( &mark_start, len );
    gtk_text_buffer_apply_tag_by_name (textbuf, tag, &mark_start, &mark_end);

    g_free( line );
  }
  tr_freeMessageList( msgs );

#if 0
  count = gtk_text_buffer_get_line_count( textbuf );
  if( MAX_MSGCOUNT < count ) {
    gtk_text_buffer_get_iter_at_line( textbuf, &front, 0 );
    gtk_text_buffer_get_iter_at_line( textbuf, &iter, count - MAX_MSGCOUNT );
    gtk_text_buffer_delete( textbuf, &front, &iter );
  }
#endif
}

static void
level_combo_changed_cb( GtkWidget * w, TrCore * core )
{
    GtkTreeIter iter;
    if( gtk_combo_box_get_active_iter( GTK_COMBO_BOX(w), &iter ) ) {
        int id = 0;
        GtkTreeModel * m = gtk_combo_box_get_model( GTK_COMBO_BOX(w) );
        gtk_tree_model_get( m, &iter, 1, &id, -1 );
        tr_setMessageLevel( id );
        tr_core_set_pref_int( core, PREF_KEY_MSGLEVEL, id );
        msgwin_update( );
    }
}

static void
save_dialog_response_cb( GtkWidget * d, int response, GtkTextBuffer * textbuf )
{
  if( response == GTK_RESPONSE_ACCEPT )
  {
      char * filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( d ) );
      FILE * fp = fopen( filename, "w+" );
      if( !fp )
      {
          errmsg( GTK_WINDOW(d),
                  _("Couldn't write file \"%s\": %s"),
                  filename, g_strerror( errno ) );
      }
      else
      {
          char * buf;
          GtkTextIter front, back;
          gtk_text_buffer_get_start_iter( textbuf, &front );
          gtk_text_buffer_get_end_iter( textbuf, &back );
          buf = gtk_text_buffer_get_text( textbuf, &front, &back, FALSE );
          if( buf ) {
              const size_t len = strlen( buf );
              if( len > fwrite( buf, 1, len, fp ) ) {
                  errmsg( GTK_WINDOW( d ),
                          _("Couldn't write file \"%s\": %s"),
                          filename, g_strerror( errno ) );
              }
              g_free( buf );
          }
          fclose( fp );
      }
      g_free( filename );
  }

  gtk_widget_destroy( d );
}

static void
save_cb( GtkWidget * w, GtkTextBuffer * textbuf )
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
                    G_CALLBACK( save_dialog_response_cb ), textbuf );
  gtk_widget_show( d );
}

static void
clear_cb( GtkWidget * w UNUSED, GtkTextBuffer * textbuf )
{
  GtkTextIter front, back;
  gtk_text_buffer_get_start_iter( textbuf, &front );
  gtk_text_buffer_get_end_iter( textbuf, &back );
  gtk_text_buffer_delete( textbuf, &front, &back );
}

static struct {
  const char * label;
  const char * pref;
  const char * text;
  int id;
} trLevels[] = {
  { N_("Error"), "error", "ERR", TR_MSG_ERR },
  { N_("Information"),  "info",  "INF", TR_MSG_INF },
  { N_("Debug"), "debug", "DBG", TR_MSG_DBG },
};

GtkWidget *
msgwin_create( TrCore * core )
{
  unsigned int i;
  GtkListStore * store;
  GtkWidget * win, * vbox, * scroll, * text;
  GtkWidget * levels;
  GtkWidget * toolbar;
  GtkWidget * w;
  GtkWidget * l;
  GtkCellRenderer * renderer;
  int ii, curlevel;

  win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
  gtk_window_set_title( GTK_WINDOW( win ), _( "Message Log" ) );
  gtk_window_set_default_size( GTK_WINDOW( win ), 600, 400 );
  gtk_window_set_role( GTK_WINDOW( win ), "message-log" );
  vbox = gtk_vbox_new( FALSE, 0 );

  /**
  ***  toolbar
  **/

  toolbar = gtk_toolbar_new ();
  gtk_toolbar_set_style( GTK_TOOLBAR( toolbar), GTK_TOOLBAR_BOTH_HORIZ );

  gtk_toolbar_insert_stock(GTK_TOOLBAR(toolbar), GTK_STOCK_SAVE,
                           NULL, NULL,
                           G_CALLBACK(save_cb), textbuf, -1);

  gtk_toolbar_insert_stock(GTK_TOOLBAR(toolbar), GTK_STOCK_CLEAR,
                           NULL, NULL,
                           G_CALLBACK(clear_cb), textbuf, -1);

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
  g_object_unref (G_OBJECT(store));
  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(levels), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT(levels), renderer, "text", 0, NULL);
  gtk_combo_box_set_active( GTK_COMBO_BOX( levels ), ii );
  g_signal_connect( levels, "changed", G_CALLBACK(level_combo_changed_cb), core );

  gtk_toolbar_append_element(GTK_TOOLBAR(toolbar),
                             GTK_TOOLBAR_CHILD_WIDGET, levels,
                             NULL, _("Set the verbosity level"),
                             NULL, NULL, NULL, NULL);

  gtk_box_pack_start( GTK_BOX( vbox ), toolbar, FALSE, FALSE, 0 );

  /**
  ***  text area
  **/

  text = gtk_text_view_new_with_buffer( textbuf );
  gtk_text_view_set_editable( GTK_TEXT_VIEW( text ), FALSE );

  scroll = gtk_scrolled_window_new( NULL, NULL );
  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC );
  gtk_container_add( GTK_CONTAINER( scroll ), text );

  gtk_box_pack_start( GTK_BOX( vbox ), scroll, TRUE, TRUE, 0 );

  msgwin_update( );
  gtk_container_add( GTK_CONTAINER( win ), vbox );
  gtk_widget_show_all( win );
  return win;
}

void
msgwin_loadpref( void )
{
    textbuf = debug_window_text_buffer_new ( );
}
