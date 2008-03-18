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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libtransmission/transmission.h>

#include "conf.h"
#include "dialogs.h"
#include "hig.h"
#include "tr-core.h"
#include "tr-prefs.h"

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
    GtkToggleButton * tb = GTK_TOGGLE_BUTTON( stuff->dontask );

    tr_core_set_pref_bool( stuff->core,
                           PREF_KEY_ASKQUIT,
                           !gtk_toggle_button_get_active( tb ) );

    if( response == GTK_RESPONSE_ACCEPT )
        stuff->func( stuff->cbdata );

    g_free( stuff );
    gtk_widget_destroy( widget );
}

static gboolean
countActiveTorrents( GtkTreeModel  * model,
                     GtkTreePath   * path UNUSED,
                     GtkTreeIter   * iter,
                     gpointer        activeTorrentCount )
{
    int status = -1;
    gtk_tree_model_get( model, iter, MC_STATUS, &status, -1 );
    if( status != TR_STATUS_STOPPED )
        *(int*)activeTorrentCount += 1;
    return FALSE; /* keep iterating */
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
    GtkTreeModel * model;
    int activeTorrentCount;

    /* if the user doesn't want to be asked, don't ask */
    if( !pref_flag_get( PREF_KEY_ASKQUIT ) ) {
        func( cbdata );
        return;
    }

    /* if there aren't any active torrents, don't ask */
    model = tr_core_model( core );
    activeTorrentCount = 0;
    gtk_tree_model_foreach( model, countActiveTorrents, &activeTorrentCount );
    if( !activeTorrentCount ) {
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
                                               _("<b>Really Quit?</b>") );

    gtk_dialog_add_buttons( GTK_DIALOG(wind),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_QUIT, GTK_RESPONSE_ACCEPT,
                            NULL );
    gtk_dialog_set_default_response( GTK_DIALOG( wind ),
                                     GTK_RESPONSE_ACCEPT );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( wind ),
                                     GTK_RESPONSE_ACCEPT,
                                     GTK_RESPONSE_CANCEL );

    dontask = gtk_check_button_new_with_mnemonic( _("_Don't ask me this again") );
    stuff->dontask = dontask;

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(wind)->vbox), dontask, FALSE, FALSE, GUI_PAD );

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( quitresp ), stuff );

    gtk_widget_show_all( wind );
}

/***
****
***/

struct DeleteData
{
    gboolean delete_files;
    GSList * torrents;
    TrCore * core;
};

static void
removeTorrents( struct DeleteData * data )
{
    GSList * l;
    for( l=data->torrents; l!=NULL; l=l->next )
        tr_core_remove_torrent( data->core, l->data, data->delete_files );
    g_slist_free( data->torrents );
}


static void
removeResponse( GtkDialog * dialog, gint response, gpointer gdata )
{
    struct DeleteData * data = gdata;

    if( response == GTK_RESPONSE_ACCEPT )
        removeTorrents( data );
    else
        g_slist_foreach( data->torrents, (GFunc)g_object_unref, NULL );

    gtk_widget_destroy( GTK_WIDGET( dialog ) );
    g_free( data );
}

static void
countBusyTorrents( gpointer gtor, gpointer busyCount )
{
    const tr_stat * stat = tr_torrent_stat( gtor );

    if( stat->leftUntilDone || stat->peersConnected )
        ++(*(int*)busyCount);
}

void
confirmRemove( GtkWindow * parent,
               TrCore    * core,
               GSList    * torrents,
               gboolean    delete_files )
{
    GtkWidget * d;
    struct DeleteData * dd;
    int busyCount;
    const int count = g_slist_length( torrents );
    const char * primary_text;
    const char * secondary_text;

    if( !count )
        return;

    dd = g_new0( struct DeleteData, 1 );
    dd->core = core;
    dd->torrents = torrents;
    dd->delete_files = delete_files;

    busyCount = 0;
    g_slist_foreach( torrents, countBusyTorrents, &busyCount );

    if( !busyCount && !delete_files ) /* don't prompt boring torrents */
    {
        removeTorrents( dd );
        g_free( dd );
        return;
    }

    if( !delete_files )
        primary_text = ngettext( "Remove torrent?", "Remove torrents?", count );
    else
        primary_text = ngettext( "Delete this torrent's downloaded files?",
                                 "Delete these torrents' downloaded files?",
                                 count );

    if( busyCount > 1 )
        secondary_text = _( "Some of these torrents are incomplete or connected to peers." );
    else if( busyCount == 0 )
        secondary_text = NULL;
    else
        secondary_text = ngettext( "This torrent is incomplete or connected to peers.",
                                   "One of these torrents is incomplete or connected to peers.",
                                   count );

    d = gtk_message_dialog_new_with_markup( parent,
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            ( delete_files ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION ),
                                            GTK_BUTTONS_NONE,
                                            "<b>%s</b>", primary_text );
    if( secondary_text )
        gtk_message_dialog_format_secondary_markup( GTK_MESSAGE_DIALOG( d ), secondary_text );
    gtk_dialog_add_buttons( GTK_DIALOG( d ),
                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            (delete_files ? GTK_STOCK_DELETE : GTK_STOCK_REMOVE), GTK_RESPONSE_ACCEPT,
                            NULL );
    gtk_dialog_set_default_response( GTK_DIALOG ( d ),
                                     GTK_RESPONSE_CANCEL );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( d ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );
    g_signal_connect( d, "response", G_CALLBACK( removeResponse ), dd );
    gtk_widget_show_all( d );
}
