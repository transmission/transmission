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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "dialogs.h"
#include "hig.h"
#include "tr_core.h"
#include "tr_icon.h"
#include "tr_prefs.h"
#include "util.h"

#define UPDATE_INTERVAL         1000
#define PREFNAME                "transmission-dialog-pref-name"
#define FILESWIND_EXTRA_INDENT  4

#define STRIPROOT( path )                                                     \
    ( g_path_is_absolute( (path) ) ? g_path_skip_root( (path) ) : (path) )

struct addcb
{
  GtkWidget * widget;
  TrCore * core;
  gboolean autostart;
  gboolean usingaltdir;
  GtkFileChooser *altdir;
  GtkButtonBox *altbox;
};

struct dirdata
{
    GtkWidget             * widget;
    TrCore                * core;
    GList                 * files;
    uint8_t               * data;
    size_t                  size;
    enum tr_torrent_action  action;
    gboolean                paused;
};

static void
addwindnocore( gpointer gdata, GObject * core );
static void
autoclick(GtkWidget *widget, gpointer gdata);
static void
dirclick(GtkWidget *widget, gpointer gdata);
static void
addresp(GtkWidget *widget, gint resp, gpointer gdata);
static void
promptdirnocore( gpointer gdata, GObject * core );
static void
promptresp( GtkWidget * widget, gint resp, gpointer data );

void
makeaddwind( GtkWindow * parent, TrCore * core )
{
  GtkWidget *wind = gtk_file_chooser_dialog_new(_("Open Torrent"), parent,
    GTK_FILE_CHOOSER_ACTION_OPEN, GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
  struct addcb *data = g_new(struct addcb, 1);
  GtkWidget *vbox = gtk_vbox_new(FALSE, 3);
  GtkWidget *bbox = gtk_hbutton_box_new();
  GtkWidget *autocheck = gtk_check_button_new_with_mnemonic(
    _("Automatically _start torrent"));
  GtkWidget *dircheck = gtk_check_button_new_with_mnemonic(
    _("Use alternate _download directory"));
  GtkFileFilter *filter = gtk_file_filter_new();
  GtkFileFilter *unfilter = gtk_file_filter_new();
  GtkWidget *getdir = gtk_file_chooser_button_new(
    _("Choose a download directory"), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);
  char * pref;

  data->widget = wind;
  data->core = core;
  data->autostart = TRUE;
  data->usingaltdir = FALSE;
  data->altdir = GTK_FILE_CHOOSER(getdir);
  data->altbox = GTK_BUTTON_BOX(bbox);

  g_object_weak_ref( G_OBJECT( core ), addwindnocore, data );

  gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_EDGE);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), dircheck);
  gtk_box_pack_start_defaults(GTK_BOX(bbox), getdir);

  gtk_box_pack_start_defaults(GTK_BOX(vbox), autocheck);
  gtk_box_pack_start_defaults(GTK_BOX(vbox), bbox);

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autocheck), TRUE);
  pref = pref_string_get( PREF_KEY_DIR_DEFAULT );
  if( pref != NULL ) {
      gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( wind ), pref );
      g_free( pref );
  }

  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dircheck), FALSE);
  gtk_widget_set_sensitive(getdir, FALSE);

  gtk_file_filter_set_name(filter, _("Torrent files"));
  gtk_file_filter_add_pattern(filter, "*.torrent");
  gtk_file_filter_set_name(unfilter, _("All files"));
  gtk_file_filter_add_pattern(unfilter, "*");

  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), filter);
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(wind), unfilter);
  gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER(wind), TRUE);
  gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(wind), vbox);

  g_signal_connect(G_OBJECT(autocheck), "clicked", G_CALLBACK(autoclick),data);
  g_signal_connect(G_OBJECT(dircheck), "clicked", G_CALLBACK(dirclick), data);
  g_signal_connect(G_OBJECT(wind), "response", G_CALLBACK(addresp), data);

  gtk_widget_show_all(wind);
}

void
addwindnocore( gpointer gdata, GObject * core UNUSED )
{
    struct addcb * data = gdata;

    /* prevent the response callback from trying to remove the weak
       reference which no longer exists */
    data->core = NULL;

    gtk_dialog_response( GTK_DIALOG( data->widget ), GTK_RESPONSE_NONE );
}

static void
autoclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->autostart = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
}

static void
dirclick(GtkWidget *widget, gpointer gdata) {
  struct addcb *data = gdata;

  data->usingaltdir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
  gtk_widget_set_sensitive(GTK_WIDGET(data->altdir), data->usingaltdir);
}

static void
addresp(GtkWidget *widget, gint resp, gpointer gdata) {
  struct addcb *data = gdata;
  GSList *files, *ii;
  GList *stupidgtk;
  char *dir;
  enum tr_torrent_action action;

  if(GTK_RESPONSE_ACCEPT == resp) {
    dir = NULL;
    if(data->usingaltdir)
      dir = gtk_file_chooser_get_filename(data->altdir);
    files = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(widget));
    action = tr_prefs_get_action( PREF_KEY_ADDSTD );

    if( NULL == dir )
    {
        stupidgtk = NULL;
        for( ii = files; NULL != ii; ii = ii->next )
        {
            stupidgtk = g_list_append( stupidgtk, ii->data );
        }
        tr_core_add_list( data->core, stupidgtk, action, !data->autostart );
        freestrlist(stupidgtk);
    }
    else
    {
        for( ii = files; NULL != ii; ii = ii->next )
        {
            tr_core_add_dir( data->core, ii->data, dir,
                             action, !data->autostart );
            g_free( ii->data );
        }
        g_free( dir );
    }
    tr_core_torrents_added( data->core );
    g_slist_free(files);
  }

  if( NULL != data->core )
  {
      g_object_weak_unref( G_OBJECT( data->core ), addwindnocore, data );
  }

  g_free( data );
  gtk_widget_destroy(widget);
}

void
fmtpeercount( GtkLabel * label, int count )
{
    char str[16];

    if( 0 > count )
    {
        gtk_label_set_text( label, "?" );
    }
    else
    {
        g_snprintf( str, sizeof str, "%i", count );
        gtk_label_set_text( label, str );
    }
}

void
promptfordir( GtkWindow * parent, TrCore * core, GList * files, uint8_t * data,
              size_t size, enum tr_torrent_action act, gboolean paused )
{
    char * path;
    struct dirdata * stuff;
    GtkWidget      * wind;

    stuff = g_new0( struct dirdata, 1 );
    stuff->core    = core;
    if( NULL != files )
    {
        stuff->files = dupstrlist( files );
    }
    if( NULL != data )
    {
        stuff->data = g_new( uint8_t, size );
        memcpy( stuff->data, data, size );
        stuff->size = size;
    }
    stuff->action  = act;
    stuff->paused  = paused;

    g_object_weak_ref( G_OBJECT( core ), promptdirnocore, stuff );

    wind =  gtk_file_chooser_dialog_new( _("Choose a directory"), parent,
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL );
    gtk_file_chooser_set_local_only( GTK_FILE_CHOOSER( wind ), TRUE );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( wind ), FALSE );
    path = getdownloaddir( );
    gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( wind ), path );
    g_free( path );

    stuff->widget = wind;

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( promptresp ), stuff );

    gtk_widget_show_all(wind);
}

void
promptdirnocore( gpointer gdata, GObject * core UNUSED )
{
    struct dirdata * stuff = gdata;

    /* prevent the response callback from trying to remove the weak
       reference which no longer exists */
    stuff->core = NULL;

    gtk_dialog_response( GTK_DIALOG( stuff->widget ), GTK_RESPONSE_NONE );
}

static void
promptresp( GtkWidget * widget, gint resp, gpointer data )
{
    struct dirdata * stuff;
    char           * dir;
    GList          * ii;

    stuff = data;

    if( GTK_RESPONSE_ACCEPT == resp )
    {
        dir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( widget ) );
        /* it seems that we will always get a directory */
        g_assert( NULL != dir );
        for( ii = g_list_first( stuff->files ); NULL != ii; ii = ii->next )
        {
            tr_core_add_dir( stuff->core, ii->data, dir,
                             stuff->action, stuff->paused );
        }
        if( NULL != stuff->data )
        {
            tr_core_add_data_dir( stuff->core, stuff->data, stuff->size, dir,
                                  stuff->paused );
        }
        tr_core_torrents_added( stuff->core );
        g_free( dir );
    }

    if( NULL != stuff->core )
    {
        g_object_weak_unref( G_OBJECT( stuff->core ), promptdirnocore, stuff );
    }

    freestrlist( stuff->files );
    g_free( stuff->data );
    g_free( stuff );
    gtk_widget_destroy( widget );
}

/***
****
***/

struct quitdata
{
    TrCore          * core;
    callbackfunc_t    func;
    void            * cbdata;
    GtkWidget       * dontask;
};

static void
quitresp( GtkWidget * widget, int response, gpointer data )
{
    struct quitdata * stuff = data;

    pref_flag_set( PREF_KEY_ASKQUIT,
                   !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(stuff->dontask) ) );

    if( response == GTK_RESPONSE_ACCEPT )
        stuff->func( stuff->cbdata );

    g_free( stuff );
    gtk_widget_destroy( widget );
}

void
askquit( TrCore          * core,
         GtkWindow       * parent,
         callbackfunc_t    func,
         void            * cbdata )
{
    struct quitdata * stuff;
    GtkWidget * wind;
    GtkWidget * dontask;

    if( !pref_flag_get( PREF_KEY_ASKQUIT ) )
    {
        func( cbdata );
        return;
    }

    stuff          = g_new( struct quitdata, 1 );
    stuff->func    = func;
    stuff->cbdata  = cbdata;
    stuff->core    = core;

    wind = gtk_message_dialog_new_with_markup( parent,
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_QUESTION,
                                               GTK_BUTTONS_NONE,
                                               _("<b>Really Quit %s?</b>"),
                                               g_get_application_name() );

    gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG(wind),
                                              _("This will close all active torrents."));
    gtk_dialog_add_buttons( GTK_DIALOG(wind),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ),
                                     GTK_RESPONSE_ACCEPT );

    dontask = gtk_check_button_new_with_mnemonic( _("_Don't Ask Me This Again") );
    stuff->dontask = dontask;

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wind)->vbox), dontask, FALSE, FALSE, GUI_PAD );

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( quitresp ), stuff );

    gtk_widget_show_all( wind );
}
