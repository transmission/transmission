/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id$
 */

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <libtransmission/transmission.h>
#include <libtransmission/makemeta.h>

#include "hig.h"
#include "makemeta-ui.h"
#include "tracker-list.h"
#include "util.h"

#define UPDATE_INTERVAL_MSEC 200

#define UI_KEY "ui"

typedef struct
{
    GtkWidget * filename_entry;
    GtkWidget * size_lb;
    GtkWidget * pieces_lb;
    GtkWidget * announce_list;
    GtkWidget * comment_entry;
    GtkWidget * progressbar;
    GtkWidget * private_check;
    GtkWidget * dialog;

    tr_metainfo_builder * builder;
    tr_handle * handle;

    gboolean isBuilding;
}
MakeMetaUI;

static void
freeMetaUI( gpointer p )
{
    MakeMetaUI * ui = p;
    memset( ui, ~0, sizeof(MakeMetaUI) );
    g_free( ui );
}

static void
refreshButtons( MakeMetaUI * ui )
{
    GtkDialog * d = GTK_DIALOG( ui->dialog );
    gtk_dialog_set_response_sensitive( d, GTK_RESPONSE_ACCEPT, !ui->isBuilding && ( ui->builder!=NULL ) );
    gtk_dialog_set_response_sensitive( d, GTK_RESPONSE_CLOSE, !ui->isBuilding );
    gtk_dialog_set_response_sensitive( d, GTK_RESPONSE_CANCEL, ui->isBuilding );
}

static void
setIsBuilding( MakeMetaUI * ui, gboolean isBuilding )
{
    ui->isBuilding = isBuilding;

    if( ui->builder != NULL )
        ui->builder->result = TR_MAKEMETA_OK;

    if( !isBuilding )
        gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR( ui->progressbar ), 0 );

    refreshButtons( ui );
}

static gboolean
refresh_cb ( gpointer user_data )
{
    int denom;
    char buf[1024];
    double fraction;
    MakeMetaUI * ui = user_data;
    GtkProgressBar * p = GTK_PROGRESS_BAR( ui->progressbar );

    denom = ui->builder->pieceCount ? ui->builder->pieceCount : 1;
    fraction = (double)ui->builder->pieceIndex / denom;
    gtk_progress_bar_set_fraction( p, fraction );
    g_snprintf( buf, sizeof(buf), "%s.torrent (%d%%)", ui->builder->top, (int)(fraction*100) );
    gtk_progress_bar_set_text( p, buf );

    if( ui->builder->isDone )
    {
        char * txt = NULL;

        switch( ui->builder->result )
        {
            case TR_MAKEMETA_OK:
                txt = g_strdup( _( "Torrent created!" ) );
                break;

            case TR_MAKEMETA_URL:
                txt = g_strdup_printf( _( "Torrent creation failed: %s" ), _( "Invalid URL" ) );
                break;

            case TR_MAKEMETA_CANCELLED:
                txt = g_strdup_printf( _( "Torrent creation cancelled" ) );
                break;

            case TR_MAKEMETA_IO_READ: {
                char * tmp = g_strdup_printf( _( "Couldn't read \"%1$s\": %2$s" ), ui->builder->errfile, g_strerror( ui->builder->my_errno ) );
                txt = g_strdup_printf( _( "Torrent creation failed: %s" ), tmp );
                g_free( tmp  );
                break;
            }

            case TR_MAKEMETA_IO_WRITE: {
                char * tmp = g_strdup_printf( _( "Couldn't create \"%1$s\": %2$s" ), ui->builder->errfile, g_strerror( ui->builder->my_errno ) );
                txt = g_strdup_printf( _( "Torrent creation failed: %s" ), tmp );
                g_free( tmp  );
                break;
            }
        }

        gtk_progress_bar_set_fraction( p, ui->builder->result==TR_MAKEMETA_OK ? 1.0 : 0.0 );
        gtk_progress_bar_set_text( p, txt );
        setIsBuilding( ui, FALSE );
        g_free( txt );
    }

    return !ui->builder->isDone;
}

static void
remove_tag (gpointer tag)
{
  g_source_remove (GPOINTER_TO_UINT(tag)); /* stop the periodic refresh */
}

static void
response_cb( GtkDialog* d, int response, gpointer user_data )
{
    MakeMetaUI * ui = user_data;
    char *tmp;
    char buf[1024];
    guint tag;
    tr_tracker_info * trackers = NULL;
    int i;
    int trackerCount = 0;

    if( response != GTK_RESPONSE_ACCEPT )
    {
        if( ui->builder == NULL )
        {
            gtk_widget_destroy( GTK_WIDGET( d ) );
            return;
        }

        if( ui->builder->isDone || !ui->isBuilding )
        {
            gtk_widget_destroy( ui->dialog );
        }
        else
        {
            ui->builder->abortFlag = TRUE;
        }
        
        return;
    }

    if( ui->builder == NULL || ui->isBuilding )
        return;

    setIsBuilding( ui, TRUE );

    tmp = g_path_get_basename (ui->builder->top);
    g_snprintf( buf, sizeof(buf), "%s.torrent (%d%%)", ui->builder->top, 0 );

    gtk_progress_bar_set_text( GTK_PROGRESS_BAR(ui->progressbar), buf );
    g_free( tmp );

    trackers = tracker_list_get_trackers( ui->announce_list, &trackerCount );

    tr_makeMetaInfo( ui->builder,
                     NULL, 
                     trackers, trackerCount,
                     gtk_entry_get_text( GTK_ENTRY( ui->comment_entry ) ),
                     gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( ui->private_check ) ) );

    tag = g_timeout_add (UPDATE_INTERVAL_MSEC, refresh_cb, ui);
    g_object_set_data_full (G_OBJECT(d), "tag", GUINT_TO_POINTER(tag), remove_tag);

    /* cleanup */
    for( i=0; i<trackerCount; ++i )
        g_free( trackers[i].announce );
    g_free( trackers );
}

/***
****
***/

static void
refreshFromBuilder( MakeMetaUI * ui )
{
    char sizeStr[128];
    char buf[MAX_PATH_LENGTH];
    tr_metainfo_builder * builder = ui->builder;
    const char * filename = builder ? builder->top : NULL;

    if( !filename )
        g_snprintf( buf, sizeof( buf ), _( "No source selected" ) );
    else
        g_snprintf( buf, sizeof(buf), "%s.torrent (%d%%)", filename, 0 );
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR( ui->progressbar ), buf );
    refreshButtons( ui );

    if( !filename )
        g_snprintf( buf, sizeof( buf ), _( "<i>No source selected</i>" ) );
    else {
        tr_strlsize( sizeStr, builder->totalSize, sizeof(sizeStr) );
        g_snprintf( buf, sizeof( buf ),
                    /* %1$s is the torrent size
                       %2$'d is its number of files */
                    ngettext( "<i>%1$s; %2$'d File</i>",
                              "<i>%1$s; %2$'d Files</i>", builder->fileCount ),
                    sizeStr, builder->fileCount );
    }
    gtk_label_set_markup ( GTK_LABEL(ui->size_lb), buf );

    if( !filename )
        *buf = '\0';
    else {
        tr_strlsize( sizeStr, builder->pieceSize, sizeof(sizeStr) );
        g_snprintf( buf, sizeof( buf ),
                    /* %1$'s is number of pieces;
                       %2$s is how big each piece is */
                    ngettext( "<i>%1$'d Piece @ %2$s</i>",
                              "<i>%1$'d Pieces @ %2$s</i>",
                              builder->pieceCount ),
                    builder->pieceCount, sizeStr );
    }
    gtk_label_set_markup ( GTK_LABEL(ui->pieces_lb), buf );
}

static void
onSourceActivated( GtkEditable * editable, gpointer gui )
{
    const char * filename = gtk_entry_get_text( GTK_ENTRY( editable ) );
    MakeMetaUI * ui = gui;

    if( ui->builder )
        tr_metaInfoBuilderFree( ui->builder );
    ui->builder = tr_metaInfoBuilderCreate( ui->handle, filename );
    refreshFromBuilder( ui );
}

static gboolean
onSourceLostFocus( GtkWidget * w, GdkEventFocus * focus UNUSED, gpointer gui )
{
    onSourceActivated( GTK_EDITABLE( w ), gui );
    return FALSE;
}

static void
onChooseClicked( GtkButton              * button,
                 gpointer                 gui,
                 const char             * title,
                 GtkFileChooserAction     chooserAction )
{
    GtkWidget * top = gtk_widget_get_toplevel( GTK_WIDGET( button ) );
    GtkWidget * d = gtk_file_chooser_dialog_new( title,
                                                 GTK_WINDOW( top ),
                                                 chooserAction,
				                 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				                 GTK_STOCK_ADD, GTK_RESPONSE_ACCEPT,
				                 NULL );
    if( gtk_dialog_run( GTK_DIALOG( d ) ) == GTK_RESPONSE_ACCEPT )
    {
        MakeMetaUI * ui = gui;
        char * filename = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER( d ) );
        gtk_entry_set_text( GTK_ENTRY( ui->filename_entry ), filename );
        onSourceActivated( GTK_EDITABLE( ui->filename_entry ), gui );
        g_free( filename );
    }

    gtk_widget_destroy( d );
}

static void
onChooseDirectoryClicked( GtkButton * b, gpointer gui )
{
    onChooseClicked( b, gui, _( "Choose Directory" ), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER );
}

static void
onChooseFileClicked( GtkButton * b, gpointer gui )
{
    onChooseClicked( b, gui, _( "Choose File" ), GTK_FILE_CHOOSER_ACTION_OPEN );
}

GtkWidget*
make_meta_ui( GtkWindow * parent, tr_handle * handle )
{
    int row = 0;
    GtkWidget *b1, *b2, *d, *t, *w, *h, *h2, *v, *focusMe, *extras;
    GtkBox * main_vbox;
    MakeMetaUI * ui = g_new0 ( MakeMetaUI, 1 );
    ui->handle = handle;
    int width, height;

    d = gtk_dialog_new_with_buttons( _("New Torrent"),
                                     parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT|GTK_DIALOG_NO_SEPARATOR,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     GTK_STOCK_NEW, GTK_RESPONSE_ACCEPT,
                                     GTK_STOCK_STOP, GTK_RESPONSE_CANCEL,
                                     NULL );
    g_signal_connect( d, "response", G_CALLBACK(response_cb), ui );
    g_object_set_data_full( G_OBJECT(d), "ui", ui, freeMetaUI );
    ui->dialog = d;
    main_vbox = GTK_BOX( GTK_DIALOG( d )->vbox );

    t = hig_workarea_create ();

    hig_workarea_add_section_title (t, &row, _( "Source" ));

        h = gtk_hbox_new( FALSE, GUI_PAD );
        v = gtk_vbox_new( FALSE, GUI_PAD_SMALL );
        w = ui->filename_entry = gtk_entry_new( );
        g_signal_connect( w, "activate", G_CALLBACK( onSourceActivated ), ui );
        g_signal_connect( w, "focus-out-event", G_CALLBACK( onSourceLostFocus ), ui );
        gtk_box_pack_start( GTK_BOX( v ), w, FALSE, FALSE, 0 );
        h2 = gtk_hbox_new( FALSE, GUI_PAD_SMALL );
        w = ui->size_lb = gtk_label_new (NULL);
        gtk_label_set_markup ( GTK_LABEL(w), _( "<i>No source selected</i>" ) );
        gtk_box_pack_start( GTK_BOX(h2), w, FALSE, FALSE, GUI_PAD_SMALL );
        w = ui->pieces_lb = gtk_label_new (NULL);
        gtk_box_pack_end( GTK_BOX(h2), w, FALSE, FALSE, GUI_PAD_SMALL );
        w = gtk_alignment_new( 0.0f, 0.0f, 0.0f, 0.0f );
        gtk_widget_set_size_request (w, 2 * GUI_PAD_BIG, 0);
        gtk_box_pack_start_defaults ( GTK_BOX(h2), w );
        gtk_box_pack_start( GTK_BOX( v ), h2, FALSE, FALSE, 0 );
        gtk_box_pack_start_defaults( GTK_BOX( h ), v );
        v = gtk_vbox_new( FALSE, GUI_PAD_SMALL );
        w = b1 = tr_button_new_from_stock( GTK_STOCK_DIRECTORY, _( "F_older" ) );
        focusMe = w;
        g_signal_connect( w, "clicked", G_CALLBACK( onChooseDirectoryClicked ), ui );
        gtk_box_pack_start_defaults( GTK_BOX( v ), w );
        w = b2 = tr_button_new_from_stock( GTK_STOCK_FILE, _( "_File" ) );
        g_signal_connect( w, "clicked", G_CALLBACK( onChooseFileClicked ), ui );
        gtk_box_pack_start_defaults( GTK_BOX( v ), w );
        gtk_box_pack_start( GTK_BOX( h ), v, FALSE, FALSE, 0 );
        hig_workarea_add_wide_control( t, &row, h );

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _( "Trackers" ) );

        w = tracker_list_new( NULL );
        ui->announce_list = w;
        hig_workarea_add_wide_control( t, &row, w );
        tracker_list_get_button_size( w, &width, &height );
        gtk_widget_set_size_request( b1, width, height );
        gtk_widget_set_size_request( b2, width, height );

    hig_workarea_add_section_divider( t, &row );
    w = extras = gtk_expander_new_with_mnemonic( _( "<b>E_xtras</b>" ) );
    gtk_expander_set_use_markup( GTK_EXPANDER( w ), TRUE );
    hig_workarea_add_section_title_widget( t, &row, w );

        {
        int row2 = 0;
        GtkWidget * t2 = hig_workarea_create( );
        w = ui->comment_entry = gtk_entry_new( );
        hig_workarea_add_row( t2, &row2, _( "Commen_t:" ), w, NULL );
        w = hig_workarea_add_wide_checkbutton( t2, &row2, _( "_Private torrent" ), FALSE );
        ui->private_check = w;
        hig_workarea_finish( t2, &row2 );
        gtk_container_add( GTK_CONTAINER( extras ), t2 );
        }

    hig_workarea_finish( t, &row );
    gtk_box_pack_start_defaults( main_vbox, t );

    w = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( w ), GTK_SHADOW_NONE );
    gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD );
    gtk_container_add( GTK_CONTAINER( w ), gtk_hseparator_new( ) );
    gtk_box_pack_start( main_vbox, w, FALSE, FALSE, 0 );

    w = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( w ), GTK_SHADOW_NONE );
    gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD );
    ui->progressbar = gtk_progress_bar_new( );
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR( ui->progressbar), _( "No source selected" ) );
    gtk_container_add( GTK_CONTAINER( w ), ui->progressbar );
    gtk_box_pack_start( main_vbox, w, FALSE, FALSE, 0 );

    gtk_window_set_default_size( GTK_WINDOW(d), 500, 0 );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    setIsBuilding( ui, FALSE );
    gtk_widget_grab_focus( focusMe );
    return d;
}
