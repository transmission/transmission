/*
 * This file Copyright (C) 2009-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <libtransmission/transmission.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "conf.h" /* pref_string_get */
#include "hig.h"
#include "relocate.h"
#include "util.h"

static char * previousLocation = NULL;

struct UpdateData
{
    GtkDialog * dialog;
    int done;
};

/* every once in awhile, check to see if the move is done.
 * if so, delete the dialog */
static gboolean
onTimer( gpointer gdata )
{
    struct UpdateData * data = gdata;
    const tr_bool done = data->done;

    if( done != TR_LOC_MOVING )
    {
        gtk_widget_destroy( GTK_WIDGET( data->dialog ) );
        g_free( data );
    }

    return !done;
}

static void
onResponse( GtkDialog * dialog, int response, gpointer unused UNUSED )
{
    if( response == GTK_RESPONSE_APPLY )
    {
        struct UpdateData * updateData;

        GtkWidget * w;
        GObject * d = G_OBJECT( dialog );
        tr_torrent * tor = g_object_get_data( d, "torrent" );
        GtkFileChooser * chooser = g_object_get_data( d, "chooser" );
        GtkToggleButton * move_tb = g_object_get_data( d, "move_rb" );
        char * location = gtk_file_chooser_get_filename( chooser );
        const gboolean do_move = gtk_toggle_button_get_active( move_tb );

        /* pop up a dialog saying that the work is in progress */
        w = gtk_message_dialog_new( GTK_WINDOW( dialog ),
                                    GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_INFO,
                                    GTK_BUTTONS_CLOSE,
                                    _( "Moving \"%s\"" ),
                                    tr_torrentInfo(tor)->name );
        gtk_message_dialog_format_secondary_text( GTK_MESSAGE_DIALOG( w ), _( "This may take a moment..." ) );
        gtk_dialog_set_response_sensitive( GTK_DIALOG( w ), GTK_RESPONSE_CLOSE, FALSE );
        gtk_widget_show( w );

        /* start the move and periodically check its status */
        updateData = g_new( struct UpdateData, 1 );
        updateData->dialog = dialog;
        updateData->done = FALSE;
        tr_torrentSetLocation( tor, location, do_move, NULL, &updateData->done );
        gtr_timeout_add_seconds( 1, onTimer, updateData );

        /* remember this location so that it can be the default next time */
        g_free( previousLocation );
        previousLocation = location;
    }
    else
    {
        gtk_widget_destroy( GTK_WIDGET( dialog ) );
    }
}

GtkWidget*
gtr_relocate_dialog_new( GtkWindow * parent, tr_torrent * tor )
{
    int row;
    GtkWidget * w;
    GtkWidget * d;
    GtkWidget * t;

    d = gtk_dialog_new_with_buttons( _( "Set Torrent Location" ), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT |
                                     GTK_DIALOG_MODAL |
                                     GTK_DIALOG_NO_SEPARATOR,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_APPLY, GTK_RESPONSE_APPLY,
                                     NULL );
    g_object_set_data( G_OBJECT( d ), "torrent", tor );
    gtk_dialog_set_default_response( GTK_DIALOG( d ),
                                     GTK_RESPONSE_CANCEL );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( d ),
                                             GTK_RESPONSE_APPLY,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );
    g_signal_connect( d, "response", G_CALLBACK( onResponse ), NULL );

    row = 0;
    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Location" ) );

    if( previousLocation == NULL )
        previousLocation = g_strdup( pref_string_get( TR_PREFS_KEY_DOWNLOAD_DIR ) );
    w = gtk_file_chooser_button_new( _( "Set Torrent Location" ), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), previousLocation );
    g_object_set_data( G_OBJECT( d ), "chooser", w );
    hig_workarea_add_row( t, &row, _( "Torrent _location:" ), w, NULL );
    w = gtk_radio_button_new_with_mnemonic( NULL, _( "_Move from the current folder" ) );
    g_object_set_data( G_OBJECT( d ), "move_rb", w );
    hig_workarea_add_wide_control( t, &row, w );
    w = gtk_radio_button_new_with_mnemonic_from_widget( GTK_RADIO_BUTTON( w ), _( "Local data is _already there" ) );
    hig_workarea_add_wide_control( t, &row, w );
    hig_workarea_finish( t, &row );
    gtk_widget_show_all( t );
    gtk_box_pack_start( GTK_BOX( GTK_DIALOG( d )->vbox ), t, TRUE, TRUE, 0 );

    return d;
}
