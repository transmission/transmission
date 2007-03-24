/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006-2007 Transmission authors and contributors
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
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "conf.h"
#include "msgwin.h"
#include "tr_prefs.h"
#include "transmission.h"
#include "util.h"

#define MAX_MSGCOUNT 5000

#define COL_LVL 0
#define COL_MSG 1

static void
changelevel( GtkWidget * widget, gpointer data );
static void
asksave( GtkWidget * widget, gpointer data );
static void
dosave( GtkWidget * widget, gint resp, gpointer gdata );
static void
doclear( GtkWidget * widget, gpointer data );

static GtkTextBuffer * textbuf = NULL;

static struct { char * label; char * pref; char * text; int id; } levels[] = {
  { N_("Error"), "error", "ERR", TR_MSG_ERR },
  { N_("Info"),  "info",  "INF", TR_MSG_INF },
  { N_("Debug"), "debug", "DBG", TR_MSG_DBG },
};

GtkWidget *
msgwin_create( void ) {
  GtkWidget * win, * vbox, * scroll, * text;
  GtkWidget * frame, * bbox, * save, * clear, * menu;
  PangoFontDescription * desc;
  int ii, curlevel;

  if( NULL == textbuf )
    textbuf = gtk_text_buffer_new( NULL );

  win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
  vbox = gtk_vbox_new( FALSE, 0 );
  scroll = gtk_scrolled_window_new( NULL, NULL );
  text = gtk_text_view_new_with_buffer( textbuf );
  frame = gtk_frame_new( NULL );
  bbox = gtk_hbutton_box_new();
  save = gtk_button_new_from_stock( GTK_STOCK_SAVE );
  clear = gtk_button_new_from_stock( GTK_STOCK_CLEAR );
  menu = gtk_combo_box_new_text();

  gtk_text_view_set_editable( GTK_TEXT_VIEW( text ), FALSE );
  desc = pango_font_description_new();
  pango_font_description_set_family( desc, "Monospace" );
  gtk_widget_modify_font( text, desc );
  pango_font_description_free( desc );

  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  gtk_container_add( GTK_CONTAINER( scroll ), text );
  gtk_container_add( GTK_CONTAINER( frame ), scroll );

  gtk_frame_set_shadow_type( GTK_FRAME( frame ), GTK_SHADOW_IN );
  gtk_box_pack_start( GTK_BOX( vbox ), frame, TRUE, TRUE, 0 );

  gtk_button_box_set_layout( GTK_BUTTON_BOX( bbox), GTK_BUTTONBOX_SPREAD );

  curlevel = tr_getMessageLevel();
  for( ii = 0; ALEN( levels ) > ii; ii++ ) {
    gtk_combo_box_append_text( GTK_COMBO_BOX( menu ),
                               gettext( levels[ii].label ) );
    if( levels[ii].id == curlevel )
      gtk_combo_box_set_active( GTK_COMBO_BOX( menu ), ii );
  }

  gtk_container_add( GTK_CONTAINER( bbox ), clear );
  gtk_container_add( GTK_CONTAINER( bbox ), save );
  gtk_container_add( GTK_CONTAINER( bbox ), menu );
  gtk_box_pack_start( GTK_BOX( vbox ), bbox, FALSE, FALSE, 0 );

  gtk_container_add( GTK_CONTAINER( win ), vbox );

  g_signal_connect( save, "clicked", G_CALLBACK( asksave ), win );
  g_signal_connect( clear, "clicked", G_CALLBACK( doclear ), NULL );
  g_signal_connect( menu, "changed", G_CALLBACK( changelevel ), NULL );

  gtk_window_set_role( GTK_WINDOW( win ), "tr-messages" );

  gtk_widget_show_all( win );

  return win;
}

static void
changelevel( GtkWidget * widget, gpointer data SHUTUP ) {
  int    index;
  char * ignored;

  index = gtk_combo_box_get_active( GTK_COMBO_BOX( widget ) );
  if( 0 <= index && (int) ALEN( levels ) > index &&
      tr_getMessageLevel() != levels[index].id ) {
    tr_setMessageLevel( levels[index].id );
    cf_setpref( tr_prefs_name( PREF_ID_MSGLEVEL ), levels[index].pref );
    cf_saveprefs( &ignored );
    g_free( ignored );
    msgwin_update();
  }
}

static void
asksave( GtkWidget * widget SHUTUP, gpointer data ) {
  GtkWidget * wind;

  wind = gtk_file_chooser_dialog_new( _("Save Log"), GTK_WINDOW( data ),
                                      GTK_FILE_CHOOSER_ACTION_SAVE,
                                      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                      NULL );
  g_signal_connect( G_OBJECT( wind ), "response", G_CALLBACK( dosave ), NULL );
  gtk_widget_show( wind );
}

static void
dosave( GtkWidget * widget, gint resp, gpointer gdata SHUTUP ) {
  char      * path, * buf;
  FILE      * fptr;
  GtkTextIter front, back;
  size_t      len;

  if( GTK_RESPONSE_ACCEPT == resp ) {
    path = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( widget ) );
    if( NULL != path ) {
      fptr = fopen( path, "w" );
      if( NULL == fptr ) {
        errmsg( GTK_WINDOW( widget ),
                _("Failed to open the file %s for writing:\n%s"),
                path, strerror( errno ) );
      }
      else {
        gtk_text_buffer_get_start_iter( textbuf, &front );
        gtk_text_buffer_get_end_iter( textbuf, &back );
        buf = gtk_text_buffer_get_text( textbuf, &front, &back, FALSE );
        if( NULL != buf ) {
          len = strlen( buf );
          if( len > fwrite( buf, 1, len, fptr ) ) {
            errmsg( GTK_WINDOW( widget ),
                    _("Error while writing to the file %s:\n%s"),
                    path, strerror( errno ) );
          }
          g_free( buf );
        }
        fclose( fptr );
      }
    }
    g_free( path );
  }

  gtk_widget_destroy( widget );
}

static void
doclear( GtkWidget * widget SHUTUP, gpointer data SHUTUP ) {
  GtkTextIter front, back;

  gtk_text_buffer_get_start_iter( textbuf, &front );
  gtk_text_buffer_get_end_iter( textbuf, &back );
  gtk_text_buffer_delete( textbuf, &front, &back );
}

void
msgwin_loadpref( void ) {
  const char * pref;
  int ii;

  tr_setMessageQueuing( 1 );
  pref = tr_prefs_get( PREF_ID_MSGLEVEL );
  if( NULL == pref )
    return;

  for( ii = 0; ALEN( levels ) > ii; ii++ ) {
    if( 0 == strcmp( pref, levels[ii].pref ) ) {
      tr_setMessageLevel( levels[ii].id );
      break;
    }
  }
}

void
msgwin_update( void ) {
  tr_msg_list_t * msgs, * ii;
  GtkTextIter     iter, front;
  char          * label, * line;
  int             count, jj;
  struct tm     * tm;

  if( NULL == textbuf )
    return;

  msgs = tr_getQueuedMessages();
  for( ii = msgs; NULL != ii; ii = ii->next ) {
    label = _("???");
    for( jj = 0; ALEN( levels ) > jj; jj++ ) {
      if( levels[jj].id == ii->level ) {
        label = levels[jj].text;
        break;
      }
    }
    tm = localtime( &ii->when );
    line = g_strdup_printf( "%02i:%02i:%02i %s %s\n", tm->tm_hour, tm->tm_min,
                            tm->tm_sec, label, ii->message );
    gtk_text_buffer_get_end_iter( textbuf, &iter );
    gtk_text_buffer_insert( textbuf, &iter, line, -1 );
    g_free( line );
  }
  tr_freeMessageList( msgs );

  count = gtk_text_buffer_get_line_count( textbuf );
  if( MAX_MSGCOUNT < count ) {
    gtk_text_buffer_get_iter_at_line( textbuf, &front, 0 );
    gtk_text_buffer_get_iter_at_line( textbuf, &iter, count - MAX_MSGCOUNT );
    gtk_text_buffer_delete( textbuf, &front, &iter );
  }
}
