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
#include "add-dialog.h"
#include "conf.h"
#include "file-list.h"
#include "hig.h"
#include "tr-prefs.h"

/****
*****
****/

#define N_RECENT 4

static GSList*
get_recent_destinations( void )
{
    int i;
    GSList * list = NULL;
    for( i=0; i<N_RECENT; ++i )
    {
        char key[64];
        const char * val;
        g_snprintf( key, sizeof(key), "recent-download-dir-%d", i+1 );
        if(( val = pref_string_get( key )))
            list = g_slist_append( list, (void*)val );
    }
    return list;
}

static void
save_recent_destination( const char * dir  )
{
    int i;
    GSList * list = get_recent_destinations( );
    GSList * l;

    if( !dir )
        return;

    /* if it was already in the list, remove it */
    if(( l = g_slist_find_custom( list, dir, (GCompareFunc)strcmp )))
        list = g_slist_delete_link( list, l );

    /* add it to the front of the list */
    list = g_slist_prepend( list, (void*)dir );

    /* make local copies of the strings that aren't
     * invalidated by pref_string_set() */
    for( l=list; l; l=l->next )
         l->data = g_strdup( l->data );

    /* save the first N_RECENT directories */
    for( l=list, i=0; l && (i<N_RECENT); ++i, l=l->next ) {
        char key[64];
        g_snprintf( key, sizeof(key), "recent-download-dir-%d", i+1 );
        pref_string_set( key, l->data );
    }
    pref_save( );

    /* cleanup */
    g_slist_foreach( list, (GFunc)g_free, NULL );
    g_slist_free( list );
}

/****
*****
****/

struct AddData
{
    TrCore * core;
    GtkWidget * list;
    GtkWidget * run_check;
    GtkWidget * trash_check;
    char * filename;
    char * downloadDir;
    TrTorrent * gtor;
    tr_ctor * ctor;
};

static void
removeOldTorrent( struct AddData * data )
{
    if( data->gtor )
    {
        file_list_set_torrent( data->list, NULL );

        tr_torrent_set_remove_flag( data->gtor, TRUE );
        g_object_unref( G_OBJECT( data->gtor ) );
        data->gtor = NULL;
    }
}

static void
addResponseCB( GtkDialog * dialog, gint response, gpointer gdata )
{
    struct AddData * data = gdata;

    if( data->gtor )
    {
        if( response != GTK_RESPONSE_ACCEPT )
            removeOldTorrent( data );
        else {
            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->run_check ) ) )
                tr_torrentStart( tr_torrent_handle( data->gtor ) );
            tr_core_add_torrent( data->core, data->gtor );
            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->trash_check ) ) )
                tr_file_trash_or_unlink( data->filename );
            save_recent_destination( data->downloadDir );
        }
    }

    tr_ctorFree( data->ctor );
    g_free( data->filename );
    g_free( data->downloadDir );
    g_free( data );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

static void
updateTorrent( struct AddData * o )
{
    if( o->gtor )
        tr_torrentSetDownloadDir( tr_torrent_handle( o->gtor ), o->downloadDir );

    file_list_set_torrent( o->list, o->gtor );
}

/**
 * When the source .torrent file is deleted
 * (such as, if it was a temp file that a web browser passed to us),
 * gtk invokes this callback and `filename' will be NULL.
 * The `filename' tests here are to prevent us from losing the current
 * metadata when that happens.
 */
static void
sourceChanged( GtkFileChooserButton * b, gpointer gdata )
{
    struct AddData * data = gdata;
    char * filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( b ) );

    /* maybe instantiate a torrent */
    if( data->filename || !data->gtor )
    {
        int err = 0;
        int new_file = 0;
        tr_torrent * torrent;
        tr_handle * handle = tr_core_handle( data->core );

        if( filename && ( !data->filename || strcmp( filename, data->filename ) ) )
        {
            g_free( data->filename );
            data->filename = g_strdup( filename );
            tr_ctorSetMetainfoFromFile( data->ctor, data->filename );
            new_file = 1;
        }

        tr_ctorSetDownloadDir( data->ctor, TR_FORCE, data->downloadDir );
        tr_ctorSetPaused( data->ctor, TR_FORCE, TRUE );
        tr_ctorSetDeleteSource( data->ctor, FALSE );

        if(( torrent = tr_torrentNew( handle, data->ctor, &err ))) {
            removeOldTorrent( data );
            data->gtor = tr_torrent_new_preexisting( torrent );
        } else if( new_file ) {
            addTorrentErrorDialog( GTK_WIDGET(b), err, data->filename );
        }

        updateTorrent( data );
    }

    g_free( filename );
}

static void
verifyRequested( GtkButton * button UNUSED, gpointer gdata )
{
    struct AddData * data = gdata;
    if( data->gtor )
        tr_torrentVerify( tr_torrent_handle( data->gtor ) );
}

static void
downloadDirChanged( GtkFileChooserButton * b, gpointer gdata )
{
    char * fname = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( b ) );
    struct AddData * data = gdata;

    if( fname && ( !data->downloadDir || strcmp( fname, data->downloadDir ) ) )
    {
        g_free( data->downloadDir );
        data->downloadDir = g_strdup( fname );

        updateTorrent( data );
        verifyRequested( NULL, data );
    }

    g_free( fname );
}

static void
addTorrentFilters( GtkFileChooser * chooser )
{
    GtkFileFilter * filter;

    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "Torrent files" ) );
    gtk_file_filter_add_pattern( filter, "*.torrent" );
    gtk_file_chooser_add_filter( chooser, filter );

    filter = gtk_file_filter_new( );
    gtk_file_filter_set_name( filter, _( "All files" ) );
    gtk_file_filter_add_pattern( filter, "*" );
    gtk_file_chooser_add_filter( chooser, filter );
}

/****
*****
****/

GtkWidget*
addSingleTorrentDialog( GtkWindow  * parent,
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
    struct AddData * data;
    uint8_t flag;
    GSList * list;
    GSList * walk;

    /* make the dialog */
    d = gtk_dialog_new_with_buttons( _( "Torrent Options" ), parent,
            GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
            GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
            NULL );
    gtk_dialog_set_default_response( GTK_DIALOG( d ),
                                     GTK_RESPONSE_ACCEPT );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( d ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );

    if( tr_ctorGetDownloadDir( ctor, TR_FORCE, &str ) )
        g_assert_not_reached( );
    g_assert( str );

    data = g_new0( struct AddData, 1 );
    data->core = core;
    data->ctor = ctor;
    data->filename = g_strdup( tr_ctorGetSourceFile( ctor ) );
    data->downloadDir = g_strdup( str );
    data->list = file_list_new( NULL );
    data->trash_check = gtk_check_button_new_with_mnemonic( _( "_Move source file to Trash" ) );
    data->run_check = gtk_check_button_new_with_mnemonic( _( "_Start when added" ) );

    g_signal_connect( G_OBJECT( d ), "response",
                      G_CALLBACK( addResponseCB ), data );

    t = gtk_table_new( 6, 2, FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( t ), GUI_PAD_BIG );
    gtk_table_set_row_spacings( GTK_TABLE( t ), GUI_PAD );
    gtk_table_set_col_spacings( GTK_TABLE( t ), GUI_PAD_BIG );

    row = col = 0;
    l = gtk_label_new_with_mnemonic( _( "_Torrent file:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col+1, row, row+1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Select Source File" ),
                                     GTK_FILE_CHOOSER_ACTION_OPEN );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+1, row, row+1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    addTorrentFilters( GTK_FILE_CHOOSER( w ) );
    if( data->filename )
        if( !gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), data->filename ) )
            g_warning( "couldn't select '%s'", data->filename );
    g_signal_connect( w, "selection-changed",
                      G_CALLBACK( sourceChanged ), data );

    ++row;
    col = 0;
    l = gtk_label_new_with_mnemonic( _( "_Destination folder:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col+1, row, row+1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Select Destination Folder" ), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    if( !gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), data->downloadDir ) )
        g_warning( "couldn't select '%s'", data->downloadDir );
    list = get_recent_destinations( );
    for( walk=list; walk; walk=walk->next )
        gtk_file_chooser_add_shortcut_folder( GTK_FILE_CHOOSER( w ), walk->data, NULL );
    g_slist_free( list );
    gtk_table_attach( GTK_TABLE( t ), w, col, col+1, row, row+1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    g_signal_connect( w, "selection-changed", G_CALLBACK( downloadDirChanged ), data );

    ++row;
    col = 0;
    w = data->list;
    gtk_widget_set_size_request ( w, 466u, 300u );
    gtk_table_attach_defaults( GTK_TABLE( t ), w, col, col+2, row, row+1 );

    ++row;
    col = 0;
    w = gtk_button_new_with_mnemonic( _( "_Verify Local Data" ) );
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
onAddDialogResponse( GtkDialog * dialog, int response, gpointer core )
{
    char * folder;

    /* remember this folder the next time we use this dialog */
    folder = gtk_file_chooser_get_current_folder( GTK_FILE_CHOOSER( dialog ) );
    pref_string_set( PREF_KEY_OPEN_DIALOG_FOLDER, folder );
    g_free( folder );

    if( response == GTK_RESPONSE_ACCEPT )
    {
        GtkWidget * w = gtk_file_chooser_get_extra_widget( GTK_FILE_CHOOSER( dialog ) );
        const gboolean showOptions = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON ( w ) );
        const pref_flag_t start = PREF_FLAG_DEFAULT;
        const pref_flag_t prompt = showOptions ? PREF_FLAG_TRUE : PREF_FLAG_FALSE;
        GSList * l = gtk_file_chooser_get_filenames( GTK_FILE_CHOOSER( dialog ) );
        tr_core_add_list( core, l, start, prompt );
    }

    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

GtkWidget*
addDialog( GtkWindow * parent,
           TrCore    * core )
{
    GtkWidget * w;
    GtkWidget * c;
    const char * folder;

    w = gtk_file_chooser_dialog_new( _( "Add a Torrent" ), parent,
                                     GTK_FILE_CHOOSER_ACTION_OPEN,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
                                     NULL );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( w ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );
    gtk_file_chooser_set_select_multiple( GTK_FILE_CHOOSER( w ), TRUE );
    addTorrentFilters( GTK_FILE_CHOOSER( w ) );
    g_signal_connect( w, "response", G_CALLBACK(onAddDialogResponse), core );

    if(( folder = pref_string_get( PREF_KEY_OPEN_DIALOG_FOLDER )))
        gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( w ), folder );

    c = gtk_check_button_new_with_mnemonic( _( "Display _options dialog" ) );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( c ), pref_flag_get( PREF_KEY_OPTIONS_PROMPT ) );
    gtk_file_chooser_set_extra_widget( GTK_FILE_CHOOSER( w ), c );
    gtk_widget_show( c );

    gtk_widget_show( w );
    return w;
}
