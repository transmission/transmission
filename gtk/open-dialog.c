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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "file-list.h"
#include "hig.h"
#include "open-dialog.h"

struct OpenData
{
    TrCore * core;
    GtkWidget * list;
    GtkWidget * run_check;
    GtkWidget * trash_check;
    char * filename;
    char * destination;
    TrTorrent * gtor;
    tr_ctor * ctor;
};

static void
deleteOldTorrent( struct OpenData * data )
{
    if( data->gtor )
    {
        file_list_set_torrent( data->list, NULL );

        tr_torrent_set_delete_flag( data->gtor, TRUE );
        g_object_unref( G_OBJECT( data->gtor ) );
        data->gtor = NULL;
    }
}

static void
openResponseCB( GtkDialog * dialog, gint response, gpointer gdata )
{
    struct OpenData * data = gdata;

    if( data->gtor )
    {
        if( response != GTK_RESPONSE_ACCEPT )
            deleteOldTorrent( data );
        else {
            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->run_check ) ) )
                tr_torrentStart( tr_torrent_handle( data->gtor ) );
            tr_core_add_torrent( data->core, data->gtor );
            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->trash_check ) ) )
                tr_file_trash_or_unlink( data->filename );
        }
    }

    tr_ctorFree( data->ctor );
    g_free( data->filename );
    g_free( data->destination );
    g_free( data );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

static void
updateTorrent( struct OpenData * o )
{
    if( o->gtor )
        tr_torrentSetFolder( tr_torrent_handle( o->gtor ), o->destination );

    file_list_set_torrent( o->list, o->gtor );
}

static void
sourceChanged( GtkFileChooserButton * b, gpointer gdata )
{
    struct OpenData * data = gdata;

    deleteOldTorrent( data );

    g_free( data->filename );
    data->filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( b ) );

    /* maybe instantiate a torrent */
    if( data->filename ) {
        int err = 0;
        tr_torrent * torrent;
        tr_handle * handle = tr_core_handle( data->core );
        tr_ctorSetMetainfoFromFile( data->ctor, data->filename );
        tr_ctorSetDestination( data->ctor, TR_FORCE, data->destination );
        tr_ctorSetPaused( data->ctor, TR_FORCE, TRUE );
        tr_ctorSetDeleteSource( data->ctor, FALSE );
        if(( torrent = tr_torrentNew( handle, data->ctor, &err )))
            data->gtor = tr_torrent_new_preexisting( torrent );
    }

    updateTorrent( data );
}

static void
verifyRequested( GtkButton * button UNUSED, gpointer gdata )
{
    struct OpenData * data = gdata;
    if( data->gtor )
        tr_torrentVerify( tr_torrent_handle( data->gtor ) );
}

static void
destinationChanged( GtkFileChooserButton * b, gpointer gdata )
{
    struct OpenData * data = gdata;

    g_free( data->destination );
    data->destination = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( b ) );

    updateTorrent( data );
    verifyRequested( NULL, data );
}

/****
*****
****/

GtkWidget*
openSingleTorrentDialog( GtkWindow  * parent,
                         TrCore     * core,
                         tr_ctor    * ctor )
{
    int row;
    int col;
    const char * str;
    GtkWidget * w;
    GtkWidget * d;
    GtkWidget * t;
    GtkWidget * l;
    GtkFileFilter * filter;
    struct OpenData * data;
    uint8_t flag;

    /* make the dialog */
    d = gtk_dialog_new_with_buttons( _( "Open Torrent" ), parent,
            GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
            NULL );
    gtk_dialog_set_default_response( GTK_DIALOG( d ),
                                     GTK_RESPONSE_ACCEPT );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( d ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );

    if( tr_ctorGetDestination( ctor, TR_FORCE, &str ) )
        g_assert_not_reached( );

    data = g_new0( struct OpenData, 1 );
    data->core = core;
    data->ctor = ctor;
    data->filename = g_strdup( tr_ctorGetSourceFile( ctor ) );
    data->destination = g_strdup( str );
    data->list = file_list_new( NULL );
    data->trash_check = gtk_check_button_new_with_mnemonic( _( "Mo_ve source file to Trash" ) );
    data->run_check = gtk_check_button_new_with_mnemonic( _( "_Start when opened" ) );

    g_signal_connect( G_OBJECT( d ), "response",
                      G_CALLBACK( openResponseCB ), data );

    t = gtk_table_new( 6, 2, FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( t ), GUI_PAD_BIG );
    gtk_table_set_row_spacings( GTK_TABLE( t ), GUI_PAD );
    gtk_table_set_col_spacings( GTK_TABLE( t ), GUI_PAD_BIG );

    row = col = 0;
    l = gtk_label_new_with_mnemonic( _( "_Source file:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col+1, row, row+1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Select Torrent" ),
                                     GTK_FILE_CHOOSER_ACTION_OPEN );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+1, row, row+1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "Torrent files" ) );
    gtk_file_filter_add_pattern( filter, "*.torrent" );
    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER( w ), filter );
    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "All files" ) );
    gtk_file_filter_add_pattern( filter, "*" );
    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER( w ), filter );
    g_signal_connect( w, "selection-changed",
                      G_CALLBACK( sourceChanged ), data );
    if( data->filename )
        if( !gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), data->filename ) )
            g_warning( "couldn't select '%s'", data->filename );

    ++row;
    col = 0;
    l = gtk_label_new_with_mnemonic( _( "Destination _folder:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col+1, row, row+1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Destination" ), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    if( !gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), data->destination ) )
        g_warning( "couldn't select '%s'", data->destination );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+1, row, row+1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    g_signal_connect( w, "selection-changed", G_CALLBACK( destinationChanged ), data );

    ++row;
    col = 0;
    w = data->list;
    gtk_widget_set_size_request ( w, 466u, 300u );
    gtk_table_attach_defaults( GTK_TABLE( t ), w, col, col+2, row, row+1 );

    ++row;
    col = 0;
    w = gtk_button_new_with_mnemonic( _( "Verify Local Data" ) );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+1, row, row+1, GTK_FILL, 0, 0, 0 );
    g_signal_connect( w, "clicked", G_CALLBACK( verifyRequested ), data );

    ++row;
    col = 0;
    w = data->run_check;
    if( tr_ctorGetPaused( ctor, TR_FORCE, &flag ) )
        g_assert_not_reached( );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), !flag );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+2, row, row+1, GTK_FILL, 0, 0, 0 );

    ++row;
    col = 0;
    w = data->trash_check;
    if( tr_ctorGetDeleteSource( ctor, &flag ) )
        g_assert_not_reached( );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), flag );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+2, row, row+1, GTK_FILL, 0, 0, 0 );

    gtk_box_pack_start_defaults( GTK_BOX( GTK_DIALOG( d )->vbox ), t );
    gtk_widget_show_all( d );
    return d;
}

/****
*****
****/

static void
onOpenDialogResponse( GtkDialog * dialog, int response, gpointer core )
{
    if( response == GTK_RESPONSE_ACCEPT )
    {
        GSList * l = gtk_file_chooser_get_filenames( GTK_FILE_CHOOSER( dialog ) );
        tr_core_add_list( core, l, FALSE );
    }

    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

GtkWidget*
openDialog( GtkWindow * parent,
            TrCore    * core )
{
    GtkWidget * w;
    GtkFileFilter * filter;


    w = gtk_file_chooser_dialog_new( _( "Select Torrents" ), parent,
                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                     NULL );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( w ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( w ), TRUE );

    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "Torrent files" ) );
    gtk_file_filter_add_pattern( filter, "*.torrent" );
    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER( w ), filter );

    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "All files" ) );
    gtk_file_filter_add_pattern( filter, "*" );
    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER( w ), filter );

    g_signal_connect( w, "response", G_CALLBACK(onOpenDialogResponse), core );

    gtk_widget_show( w );
    return w;
}
