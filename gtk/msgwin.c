/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2006 Transmission authors and contributors
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

#include "conf.h"
#include "msgwin.h"
#include "transmission.h"
#include "util.h"

#define COL_LVL 0
#define COL_MSG 1

static void
changelevel( GtkToggleButton * button, gpointer data );
static void
addmsg( int level, const char * msg );


static GMutex * listmutex = NULL;
static GSList * messages = NULL;
static GtkTextBuffer * textbuf = NULL;

void
msgwin_init( void ) {
  if( !g_thread_supported() )
    g_thread_init( NULL );
  listmutex = g_mutex_new();
  tr_setMessageFunction( addmsg );
}

GtkWidget *
msgwin_create( void ) {
  GtkWidget * win, * vbox, * scroll, * text;
  GtkWidget * frame, * bbox, * err, * inf, * dbg;

  if( NULL == textbuf )
    textbuf = gtk_text_buffer_new( NULL );

  win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
  vbox = gtk_vbox_new( FALSE, 0 );
  scroll = gtk_scrolled_window_new( NULL, NULL );
  text = gtk_text_view_new_with_buffer( textbuf );
  frame = gtk_frame_new( NULL );
  bbox = gtk_hbutton_box_new();
  err = gtk_radio_button_new_with_label( NULL, _( "Error" ) );
  inf = gtk_radio_button_new_with_label_from_widget(
    GTK_RADIO_BUTTON( err ), _( "Info" ) );
  dbg = gtk_radio_button_new_with_label_from_widget(
    GTK_RADIO_BUTTON( err ), _( "Debug" ) );

  gtk_text_view_set_editable( GTK_TEXT_VIEW( text ), FALSE );

  gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW( scroll ),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC );

  gtk_container_add( GTK_CONTAINER( scroll ), text );
  gtk_container_add( GTK_CONTAINER( frame ), scroll );

  gtk_frame_set_shadow_type( GTK_FRAME( frame ), GTK_SHADOW_IN );
  gtk_box_pack_start( GTK_BOX( vbox ), frame, TRUE, TRUE, 0 );

  gtk_button_box_set_layout( GTK_BUTTON_BOX( bbox), GTK_BUTTONBOX_SPREAD );

  gtk_container_add( GTK_CONTAINER( bbox ), err );
  gtk_container_add( GTK_CONTAINER( bbox ), inf );
  gtk_container_add( GTK_CONTAINER( bbox ), dbg );
  gtk_box_pack_start( GTK_BOX( vbox ), bbox, FALSE, FALSE, 0 );

  gtk_container_add( GTK_CONTAINER( win ), vbox );

  switch( tr_getMessageLevel() ) {
    case TR_MSG_ERR:
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( err ), TRUE );
      break;
    case TR_MSG_INF:
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( inf ), TRUE );
      break;
    case TR_MSG_DBG:
      gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( dbg ), TRUE );
      break;
  }

  g_signal_connect( err, "toggled", G_CALLBACK( changelevel ),
                    GINT_TO_POINTER( TR_MSG_ERR ) );
  g_signal_connect( inf, "toggled", G_CALLBACK( changelevel ),
                    GINT_TO_POINTER( TR_MSG_INF ) );
  g_signal_connect( dbg, "toggled", G_CALLBACK( changelevel ),
                    GINT_TO_POINTER( TR_MSG_DBG ) );

  gtk_widget_show_all( win );

  return win;
}

static void
changelevel( GtkToggleButton * button, gpointer data ) {
  int    level;
  char * ignored;

  if( gtk_toggle_button_get_active( button ) ) {
    level = GPOINTER_TO_INT( data );
    tr_setMessageLevel( level );
    switch( level ) {
      case TR_MSG_ERR:
        cf_setpref( PREF_MSGLEVEL, "error" );
        break;
      case TR_MSG_INF:
        cf_setpref( PREF_MSGLEVEL, "info" );
        break;
      case TR_MSG_DBG:
        cf_setpref( PREF_MSGLEVEL, "debug" );
        break;
    }
    cf_saveprefs( &ignored );
    g_free( ignored );
    msgwin_update();
  }
}

void
msgwin_loadpref( void ) {
  const char * pref;

  pref = cf_getpref( PREF_MSGLEVEL );
  if( NULL == pref )
    return;

  if( 0 == strcmp( "error", pref ) )
    tr_setMessageLevel( TR_MSG_ERR );
  else if( 0 == strcmp( "info", pref ) )
    tr_setMessageLevel( TR_MSG_INF );
  else if( 0 == strcmp( "debug", pref ) )
    tr_setMessageLevel( TR_MSG_DBG );
}

void
msgwin_update( void ) {
  GSList    * ii;
  GtkTextIter iter;

  if( NULL == textbuf )
    return;

  g_mutex_lock( listmutex );

  if( NULL != messages ) {
    for( ii = messages; NULL != ii; ii = ii->next ) {
      gtk_text_buffer_get_end_iter( textbuf, &iter );
      gtk_text_buffer_insert( textbuf, &iter, ii->data, -1 );
      g_free( ii->data );
    }
    g_slist_free( messages );
    messages = NULL;
  }

  g_mutex_unlock( listmutex );
}

static void
addmsg( int level, const char * msg ) {
  char * str;

  g_mutex_lock( listmutex );

  switch( level )
  {
    case TR_MSG_ERR:
      str = _( "ERR" );
      break;
    case TR_MSG_INF:
      str = _( "INF" );
      break;
    case TR_MSG_DBG:
      str = _( "DBG" );
      break;
    default:
      str = _( "???" );
      break;
  }

  str = g_strdup_printf( "%s: %s\n", str, msg );
  messages = g_slist_append( messages, str );

  g_mutex_unlock( listmutex );
}
