/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <string.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_is_same_file() */

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
        g_snprintf( key, sizeof( key ), "recent-download-dir-%d", i+1 );
        if(( val = gtr_pref_string_get( key )))
            list = g_slist_append( list, (void*)val );
    }
    return list;
}

static void
save_recent_destination( TrCore * core, const char * dir  )
{
    int i;
    GSList * l;
    GSList * list = get_recent_destinations( );

    if( dir == NULL )
        return;

    /* if it was already in the list, remove it */
    if(( l = g_slist_find_custom( list, dir, (GCompareFunc)strcmp )))
        list = g_slist_delete_link( list, l );

    /* add it to the front of the list */
    list = g_slist_prepend( list, (void*)dir );

    /* make local copies of the strings that aren't
     * invalidated by gtr_pref_string_set() */
    for( l=list; l; l=l->next )
        l->data = g_strdup( l->data );

    /* save the first N_RECENT directories */
    for( l=list, i=0; l && ( i<N_RECENT ); ++i, l=l->next ) {
        char key[64];
        g_snprintf( key, sizeof( key ), "recent-download-dir-%d", i + 1 );
        gtr_pref_string_set( key, l->data );
    }
    gtr_pref_save( tr_core_session( core ) );

    /* cleanup */
    g_slist_foreach( list, (GFunc)g_free, NULL );
    g_slist_free( list );
}

/****
*****
****/

struct AddData
{
    TrCore *     core;
    GtkWidget *  file_list;
    GtkWidget *  run_check;
    GtkWidget *  trash_check;
    GtkWidget *  priority_combo;
    char *       filename;
    char *       downloadDir;
    TrTorrent *  gtor;
    tr_ctor *    ctor;
};

static void
removeOldTorrent( struct AddData * data )
{
    if( data->gtor )
    {
        gtr_file_list_clear( data->file_list );
        tr_torrent_set_remove_flag( data->gtor, TRUE );
        g_object_unref( G_OBJECT( data->gtor ) );
        data->gtor = NULL;
    }
}

static void
addResponseCB( GtkDialog * dialog,
               gint        response,
               gpointer    gdata )
{
    struct AddData * data = gdata;

    if( data->gtor )
    {
        if( response != GTK_RESPONSE_ACCEPT )
        {
            removeOldTorrent( data );
        }
        else
        {
            tr_torrent * tor = tr_torrent_handle( data->gtor );

            tr_torrentSetPriority( tor, gtr_priority_combo_get_value( GTK_COMBO_BOX( data->priority_combo ) ) );

            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->run_check ) ) )
                tr_torrentStart( tor );

            tr_core_add_torrent( data->core, data->gtor, FALSE );

            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( data->trash_check ) ) )
                gtr_file_trash_or_remove( data->filename );

            save_recent_destination( data->core, data->downloadDir );
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
    const gboolean isLocalFile = tr_ctorGetSourceFile( o->ctor ) != NULL;
    gtk_widget_set_sensitive( o->trash_check, isLocalFile );

    if( !o->gtor )
    {
        gtr_file_list_clear( o->file_list );
        gtk_widget_set_sensitive( o->file_list, FALSE );
    }
    else
    {
        tr_torrent * tor = tr_torrent_handle( o->gtor );
        tr_torrentSetDownloadDir( tor, o->downloadDir );
        gtk_widget_set_sensitive( o->file_list, tr_torrentHasMetadata( tor ) );
        gtr_file_list_set_torrent( o->file_list, tr_torrentId( tor ) );
        tr_torrentVerify( tor );
    }
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
    if( filename || !data->gtor )
    {
        int err = 0;
        int new_file = 0;
        tr_torrent * torrent;

        if( filename && ( !data->filename || !tr_is_same_file( filename, data->filename ) ) )
        {
            g_free( data->filename );
            data->filename = g_strdup( filename );
            tr_ctorSetMetainfoFromFile( data->ctor, data->filename );
            new_file = 1;
        }

        tr_ctorSetDownloadDir( data->ctor, TR_FORCE, data->downloadDir );
        tr_ctorSetPaused( data->ctor, TR_FORCE, TRUE );
        tr_ctorSetDeleteSource( data->ctor, FALSE );

        if( ( torrent = tr_torrentNew( data->ctor, &err ) ) )
        {
            removeOldTorrent( data );
            data->gtor = tr_torrent_new_preexisting( torrent );
        }
        else if( new_file )
        {
            gtr_add_torrent_error_dialog( GTK_WIDGET( b ), err, data->filename );
        }

        updateTorrent( data );
    }

    g_free( filename );
}

static void
downloadDirChanged( GtkFileChooserButton * b, gpointer gdata )
{
    struct AddData * data = gdata;
    char * fname = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( b ) );

    if( fname && ( !data->downloadDir || !tr_is_same_file( fname, data->downloadDir ) ) )
    {
        g_free( data->downloadDir );
        data->downloadDir = g_strdup( fname );
        updateTorrent( data );
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
gtr_torrent_options_dialog_new( GtkWindow * parent, TrCore * core, tr_ctor * ctor )
{
    int              row;
    int              col;
    const char *     str;
    GtkWidget *      w;
    GtkWidget *      d;
    GtkWidget *      t;
    GtkWidget *      l;
    GtkWidget *      grab;
    GtkWidget *      source_chooser;
    struct AddData * data;
    uint8_t          flag;
    GSList *         list;
    GSList *         walk;

    /* make the dialog */
    d = gtk_dialog_new_with_buttons( _( "Torrent Options" ), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     NULL );
    gtk_dialog_add_button( GTK_DIALOG( d ), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL );
    grab = gtk_dialog_add_button( GTK_DIALOG( d ), GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT );
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
    data->file_list = gtr_file_list_new( core, 0 );
    str = _( "Mo_ve .torrent file to the trash" );
    data->trash_check = gtk_check_button_new_with_mnemonic( str );
    str = _( "_Start when added" );
    data->run_check = gtk_check_button_new_with_mnemonic( str );

    w = data->priority_combo = gtr_priority_combo_new( );
    gtr_priority_combo_set_value( GTK_COMBO_BOX( w ), TR_PRI_NORMAL );

    g_signal_connect( G_OBJECT( d ), "response",
                      G_CALLBACK( addResponseCB ), data );

    t = gtk_table_new( 6, 2, FALSE );
    gtk_container_set_border_width( GTK_CONTAINER( t ), GUI_PAD_BIG );
    gtk_table_set_row_spacings( GTK_TABLE( t ), GUI_PAD );
    gtk_table_set_col_spacings( GTK_TABLE( t ), GUI_PAD_BIG );

    row = col = 0;
    l = gtk_label_new_with_mnemonic( _( "_Torrent file:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col + 1, row, row + 1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Select Source File" ),
                                     GTK_FILE_CHOOSER_ACTION_OPEN );
    source_chooser = w;
    gtk_table_attach( GTK_TABLE( t ), w, col, col + 1, row, row + 1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    addTorrentFilters( GTK_FILE_CHOOSER( w ) );
    g_signal_connect( w, "selection-changed",
                      G_CALLBACK( sourceChanged ), data );

    ++row;
    col = 0;
    l = gtk_label_new_with_mnemonic( _( "_Destination folder:" ) );
    gtk_misc_set_alignment( GTK_MISC( l ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), l, col, col + 1, row, row + 1, GTK_FILL, 0, 0, 0 );
    ++col;
    w = gtk_file_chooser_button_new( _( "Select Destination Folder" ),
                                     GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
    if( !gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( w ),
                                              data->downloadDir ) )
        g_warning( "couldn't select '%s'", data->downloadDir );
    list = get_recent_destinations( );
    for( walk = list; walk; walk = walk->next )
        gtk_file_chooser_add_shortcut_folder( GTK_FILE_CHOOSER( w ), walk->data, NULL );
    g_slist_free( list );
    gtk_table_attach( GTK_TABLE( t ), w, col, col + 1, row, row + 1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( l ), w );
    g_signal_connect( w, "selection-changed",
                      G_CALLBACK( downloadDirChanged ), data );

    ++row;
    col = 0;
    w = data->file_list;
    gtk_widget_set_size_request ( w, 466u, 300u );
    gtk_table_attach_defaults( GTK_TABLE( t ), w, col, col + 2, row, row + 1 );

    ++row;
    col = 0;
    w = gtk_label_new_with_mnemonic( _( "Torrent _priority:" ) );
    gtk_misc_set_alignment( GTK_MISC( w ), 0.0f, 0.5f );
    gtk_table_attach( GTK_TABLE( t ), w, col, col + 1, row, row + 1, ~0, 0, 0, 0 );
    ++col;
    gtk_table_attach( GTK_TABLE( t ), data->priority_combo, col, col + 1, row, row + 1, ~0, 0, 0, 0 );
    gtk_label_set_mnemonic_widget( GTK_LABEL( w ), data->priority_combo );

    ++row;
    col = 0;
    w = data->run_check;
    if( tr_ctorGetPaused( ctor, TR_FORCE, &flag ) )
        g_assert_not_reached( );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), !flag );
    gtk_table_attach( GTK_TABLE( t ), w, col, col + 2, row, row + 1, GTK_FILL, 0, 0, 0 );

    ++row;
    col = 0;
    w = data->trash_check;
    if( tr_ctorGetDeleteSource( ctor, &flag ) )
        g_assert_not_reached( );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( w ), flag );
    gtk_table_attach( GTK_TABLE( t ), w, col, col + 2, row, row + 1, GTK_FILL, 0, 0, 0 );

    /* trigger sourceChanged, either directly or indirectly,
     * so that it creates the tor/gtor objects */
    w = source_chooser;
    if( data->filename )
        gtk_file_chooser_set_filename( GTK_FILE_CHOOSER( w ), data->filename );
    else
        sourceChanged( GTK_FILE_CHOOSER_BUTTON( w ), data );

    gtr_dialog_set_content( GTK_DIALOG( d ), t );
    gtk_widget_grab_focus( grab );
    return d;
}

/****
*****
****/

static void
onAddDialogResponse( GtkDialog * dialog,
                     int         response,
                     gpointer    core )
{
    char * folder;

    /* remember this folder the next time we use this dialog */
    folder = gtk_file_chooser_get_current_folder( GTK_FILE_CHOOSER( dialog ) );
    gtr_pref_string_set( PREF_KEY_OPEN_DIALOG_FOLDER, folder );
    g_free( folder );

    if( response == GTK_RESPONSE_ACCEPT )
    {
        GtkFileChooser  * chooser = GTK_FILE_CHOOSER( dialog );
        GtkWidget       * w = gtk_file_chooser_get_extra_widget( chooser );
        GtkToggleButton * tb = GTK_TOGGLE_BUTTON( w );
        const gboolean    doStart = gtr_pref_flag_get( TR_PREFS_KEY_START );
        const gboolean    doPrompt = gtk_toggle_button_get_active( tb );
        const gboolean    doNotify = FALSE;
        GSList * l = gtk_file_chooser_get_filenames( chooser );

        tr_core_add_list( core, l, doStart, doPrompt, doNotify );
    }

    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

GtkWidget*
gtr_torrent_add_from_file_dialog_new( GtkWindow * parent, TrCore * core )
{
    GtkWidget *  w;
    GtkWidget *  c;
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
    g_signal_connect( w, "response", G_CALLBACK(
                          onAddDialogResponse ), core );

    if( ( folder = gtr_pref_string_get( PREF_KEY_OPEN_DIALOG_FOLDER ) ) )
        gtk_file_chooser_set_current_folder( GTK_FILE_CHOOSER( w ), folder );

    c = gtk_check_button_new_with_mnemonic( _( "Show _options dialog" ) );
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( c ),
                                 gtr_pref_flag_get( PREF_KEY_OPTIONS_PROMPT ) );
    gtk_file_chooser_set_extra_widget( GTK_FILE_CHOOSER( w ), c );
    gtk_widget_show( c );

    return w;
}

/***
****
***/

static void
onAddURLResponse( GtkDialog * dialog, int response, gpointer user_data )
{
    gboolean destroy = TRUE;

    if( response == GTK_RESPONSE_ACCEPT )
    {
        GtkWidget * e = GTK_WIDGET( g_object_get_data( G_OBJECT( dialog ), "url-entry" ) );
        char * url = g_strdup( gtk_entry_get_text( GTK_ENTRY( e ) ) );
        g_strstrip( url );

        if( url && *url )
        {
            TrCore * core = user_data;

            if( gtr_is_supported_url( url ) || gtr_is_magnet_link( url )
                                            || gtr_is_hex_hashcode( url ) )
            {
                tr_core_add_from_url( core, url );
            }
            else
            {
                gtr_unrecognized_url_dialog( GTK_WIDGET( dialog ), url );
                destroy = FALSE;
            }
        }

        g_free( url );
    }

    if( destroy )
        gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

GtkWidget*
gtr_torrent_add_from_url_dialog_new( GtkWindow * parent, TrCore * core )
{
    int row;
    GtkWidget * e;
    GtkWidget * t;
    GtkWidget * w;

    w = gtk_dialog_new_with_buttons( _( "Add URL" ), parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
                                     NULL );
    gtk_dialog_set_alternative_button_order( GTK_DIALOG( w ),
                                             GTK_RESPONSE_ACCEPT,
                                             GTK_RESPONSE_CANCEL,
                                             -1 );
    g_signal_connect( w, "response", G_CALLBACK( onAddURLResponse ), core );

    row = 0;
    t = hig_workarea_create( );
    hig_workarea_add_section_title( t, &row, _( "Add torrent from URL" ) );
    e = gtk_entry_new( );
    gtk_widget_set_size_request( e, 400, -1 );
    gtr_paste_clipboard_url_into_entry( e );
    g_object_set_data( G_OBJECT( w ), "url-entry", e );
    hig_workarea_add_row( t, &row, _( "_URL" ), e, NULL );

    gtr_dialog_set_content( GTK_DIALOG( w ), t );
    return w;
}
