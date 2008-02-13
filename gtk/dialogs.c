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
#include "tr_core.h"
#include "tr_prefs.h"

struct dirdata
{
    GtkWidget  * widget;
    TrCore     * core;
    GList      * files;
    tr_ctor    * ctor;
};

static void
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

    stuff = data;

    if( GTK_RESPONSE_ACCEPT == resp )
    {
        char * dir;
        GList * l;

        /* update the destination */
        dir = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( widget ) );
        tr_ctorSetDestination( stuff->ctor, TR_FORCE, dir );
        g_free( dir );

        /* if there's metainfo in the ctor already, use it */
        if( !tr_ctorGetMetainfo( stuff->ctor, NULL ) )
            tr_core_add_ctor( stuff->core, stuff->ctor );

        /* if there's a list of files, use them too */
        for( l=stuff->files; l!=NULL; l=l->next )
            if( !tr_ctorSetMetainfoFromFile( stuff->ctor, l->data ) )
                tr_core_add_ctor( stuff->core, stuff->ctor );
    }

    if( stuff->core )
        g_object_weak_unref( G_OBJECT( stuff->core ), promptdirnocore, stuff );

    gtk_widget_destroy( widget );
    freestrlist( stuff->files );
    tr_ctorFree( stuff->ctor );
    g_free( stuff );
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

static void
deleteToggled( GtkToggleButton * tb, gpointer ctor )
{
    tr_ctorSetDeleteSource( ctor, gtk_toggle_button_get_active( tb ) );
}

static void
startToggled( GtkToggleButton * tb, gpointer ctor )
{
    tr_ctorSetPaused( ctor, TR_FORCE, !gtk_toggle_button_get_active( tb ) );
}

void
promptfordir( GtkWindow * parent, TrCore * core, GList * files, tr_ctor * ctor )
{
    uint8_t          flag = 0;
    const char     * str;
    struct dirdata * stuff;
    GtkWidget      * wind;
    GtkWidget      * v;
    GtkWidget      * w;

    stuff = g_new0( struct dirdata, 1 );
    stuff->core   = core;
    stuff->ctor   = ctor;
    stuff->files  = dupstrlist( files );

    g_object_weak_ref( G_OBJECT( core ), promptdirnocore, stuff );

    wind =  gtk_file_chooser_dialog_new( _("Choose a directory"), parent,
                                         GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL );
    gtk_file_chooser_set_local_only( GTK_FILE_CHOOSER( wind ), TRUE );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( wind ), FALSE );
    if( tr_ctorGetDestination( ctor, TR_FORCE, &str ) )
        g_assert_not_reached( );
    if( !gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( wind ), str ) )
        g_warning( "couldn't set destination '%s'", str );

    v = gtk_vbox_new( FALSE, GUI_PAD );

        flag = 0;
        w = gtk_check_button_new_with_mnemonic( _( "_Delete original torrent file" ) );
        g_signal_connect( w, "toggled", G_CALLBACK( deleteToggled ), ctor );
        if( tr_ctorGetDeleteSource( ctor, &flag ) )
            g_assert_not_reached( );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), flag );
        gtk_box_pack_start( GTK_BOX( v ), w, FALSE, FALSE, 0 );

        flag = 1;
        w = gtk_check_button_new_with_mnemonic( _( "_Start when added" ) );
        g_signal_connect( w, "toggled", G_CALLBACK( startToggled ), ctor );
        if( tr_ctorGetPaused( ctor, TR_FORCE, &flag ) )
            g_assert_not_reached( );
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), !flag );
        gtk_box_pack_start( GTK_BOX( v ), w, FALSE, FALSE, 0 );

    gtk_file_chooser_set_extra_widget( GTK_FILE_CHOOSER( wind ), v );

    stuff->widget = wind;

    g_signal_connect( G_OBJECT( wind ), "response",
                      G_CALLBACK( promptresp ), stuff );

    gtk_widget_show_all(wind);
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
