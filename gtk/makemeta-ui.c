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
#include "util.h"

#define UPDATE_INTERVAL_MSEC 200

typedef struct
{
    GtkWidget * size_lb;
    GtkWidget * pieces_lb;
    GtkWidget * announce_entry;
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
    MakeMetaUI * ui = (MakeMetaUI *) p;
    tr_metaInfoBuilderFree( ui->builder );
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
        ui->builder->failed = FALSE;

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
    MakeMetaUI * ui = (MakeMetaUI *) user_data;
    GtkProgressBar * p = GTK_PROGRESS_BAR( ui->progressbar );

    denom = ui->builder->pieceCount ? ui->builder->pieceCount : 1;
    fraction = (double)ui->builder->pieceIndex / denom;
    gtk_progress_bar_set_fraction( p, fraction );
    g_snprintf( buf, sizeof(buf), "%s.torrent (%d%%)", ui->builder->top, (int)(fraction*100) );
    gtk_progress_bar_set_text( p, buf );

    if( ui->builder->isDone )
    {
        if( ui->builder->failed )
        {
            const char * reason = ui->builder->abortFlag
                ? _("Torrent creation cancelled")
                : _("Torrent creation failed");

            gtk_progress_bar_set_text( p, reason );
            gtk_progress_bar_set_fraction( p, 0 );
        }
        else
        {
            gtk_progress_bar_set_text( p, _("Torrent created") );
        }

        setIsBuilding( ui, FALSE );
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
    MakeMetaUI * ui = (MakeMetaUI*) user_data;
    char *tmp;
    char buf[1024];
    guint tag;

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

    tr_makeMetaInfo( ui->builder,
                     NULL, 
                     gtk_entry_get_text( GTK_ENTRY( ui->announce_entry ) ),
                     gtk_entry_get_text( GTK_ENTRY( ui->comment_entry ) ),
                     gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( ui->private_check ) ) );

    tag = g_timeout_add (UPDATE_INTERVAL_MSEC, refresh_cb, ui);
    g_object_set_data_full (G_OBJECT(d), "tag", GUINT_TO_POINTER(tag), remove_tag);
}

/***
****
***/

static void
file_selection_changed_cb( GtkFileChooser *chooser, gpointer user_data )
{
    MakeMetaUI * ui = (MakeMetaUI *) user_data;
    char * filename;
    char sizeStr[128];
    char buf[MAX_PATH_LENGTH];
    uint64_t totalSize=0;
    int fileCount=0, pieceCount=0, pieceSize=0;

    if( ui->builder ) {
        tr_metaInfoBuilderFree( ui->builder );
        ui->builder = NULL;
    }

    filename = gtk_file_chooser_get_filename( chooser );
    if( !filename )
        *buf = '\0';
    else {
        ui->builder = tr_metaInfoBuilderCreate( ui->handle, filename );
        g_snprintf( buf, sizeof(buf), "%s.torrent (%d%%)", filename, 0 );
        g_free( filename );
        fileCount = ui->builder->fileCount;
        totalSize = ui->builder->totalSize;
        pieceCount = ui->builder->pieceCount;
        pieceSize = ui->builder->pieceSize;
    }
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR( ui->progressbar ), buf );
    refreshButtons( ui );

    tr_strlsize( sizeStr, totalSize, sizeof(sizeStr) );
    g_snprintf( buf, sizeof( buf ),
                /* %1$s is the torrent size, %2$s is its number of files */
                ngettext( "<i>%1$s; %2$d File</i>",
                          "<i>%1$s; %2$d Files</i>", fileCount ),
                sizeStr, fileCount );
    gtk_label_set_markup ( GTK_LABEL(ui->size_lb), buf );

    tr_strlsize( sizeStr, pieceSize, sizeof(sizeStr) );
    g_snprintf( buf, sizeof( buf ),
                /* %1$s is number of pieces; %2$s is how big each piece is */
                ngettext( "<i>%1$d Piece @ %2$s</i>",
                          "<i>%1$d Pieces @ %2$s</i>",
                          pieceCount ),
                pieceCount, sizeStr );
    gtk_label_set_markup ( GTK_LABEL(ui->pieces_lb), buf );
}

static void
file_chooser_shown_cb( GtkWidget *w, gpointer folder_toggle )
{
    const gboolean isFolder = gtk_toggle_button_get_active( folder_toggle );
    gtk_file_chooser_set_action (GTK_FILE_CHOOSER(w), isFolder
        ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER
        : GTK_FILE_CHOOSER_ACTION_OPEN );
}

GtkWidget*
make_meta_ui( GtkWindow * parent, tr_handle * handle )
{
    int row = 0;
    GtkWidget *d, *t, *w, *h, *rb_file, *rb_dir;
    GtkBox * main_vbox;
    char name[256];
    MakeMetaUI * ui = g_new0 ( MakeMetaUI, 1 );
    ui->handle = handle;

    d = gtk_dialog_new_with_buttons( _("New Torrent"),
                                     parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     GTK_STOCK_NEW, GTK_RESPONSE_ACCEPT,
                                     NULL );
    g_signal_connect( d, "response", G_CALLBACK(response_cb), ui );
    g_object_set_data_full( G_OBJECT(d), "ui", ui, freeMetaUI );
    ui->dialog = d;
    main_vbox = GTK_BOX( GTK_DIALOG( d )->vbox );

    t = hig_workarea_create ();

    hig_workarea_add_section_title (t, &row, _("Files"));

        g_snprintf( name, sizeof(name), "%s:", _("File _Type"));
        h = gtk_hbox_new( FALSE, GUI_PAD_SMALL );
        w = rb_dir = gtk_radio_button_new_with_mnemonic( NULL, _("Folder"));
        gtk_box_pack_start ( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = rb_file = gtk_radio_button_new_with_mnemonic_from_widget( GTK_RADIO_BUTTON(w), _("Single File") );
        gtk_box_pack_start ( GTK_BOX(h), w, FALSE, FALSE, 0 );
        hig_workarea_add_row (t, &row, name, h, NULL);

        g_snprintf( name, sizeof(name), "%s:", _("_File"));

        w = gtk_file_chooser_dialog_new ( NULL,
                                          NULL,
                                          GTK_FILE_CHOOSER_ACTION_OPEN,
                                          GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                          GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                          NULL );
        g_signal_connect( w, "map", G_CALLBACK(file_chooser_shown_cb), rb_dir );
        w = gtk_file_chooser_button_new_with_dialog( w );
        g_signal_connect( w, "selection-changed", G_CALLBACK(file_selection_changed_cb), ui );
        hig_workarea_add_row (t, &row, name, w, NULL);

        g_snprintf( name, sizeof(name), "<i>%s</i>", _("No files selected"));
        h = gtk_hbox_new( FALSE, GUI_PAD_SMALL );
        w = ui->size_lb = gtk_label_new (NULL);
        gtk_label_set_markup ( GTK_LABEL(w), name );
        gtk_box_pack_start( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = ui->pieces_lb = gtk_label_new (NULL);
        gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
        gtk_widget_set_size_request (w, 2 * GUI_PAD_BIG, 0);
        gtk_box_pack_start_defaults ( GTK_BOX(h), w );
        hig_workarea_add_row (t, &row, "", h, NULL);
        

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Torrent"));

        g_snprintf( name, sizeof(name), _("Private to this tracker") );
        w = ui->private_check = hig_workarea_add_wide_checkbutton( t, &row, name, FALSE );

        g_snprintf( name, sizeof(name), "%s:", _("Announce _URL"));
        w = ui->announce_entry = gtk_entry_new( );
        gtk_entry_set_text(GTK_ENTRY(w), "http://");
        hig_workarea_add_row (t, &row, name, w, NULL );

        g_snprintf( name, sizeof(name), "%s:", _("Commen_t"));
        w = ui->comment_entry = gtk_entry_new( );
        hig_workarea_add_row (t, &row, name, w, NULL );

    hig_workarea_finish( t, &row );
    gtk_box_pack_start_defaults( main_vbox, t );

    w = gtk_frame_new( NULL );
    gtk_frame_set_shadow_type( GTK_FRAME( w ), GTK_SHADOW_NONE );
    gtk_container_set_border_width( GTK_CONTAINER( w ), GUI_PAD_BIG );
    
        ui->progressbar = gtk_progress_bar_new( );
        gtk_progress_bar_set_text( GTK_PROGRESS_BAR( ui->progressbar), _( "No files selected" ) );
        gtk_container_add( GTK_CONTAINER( w ), ui->progressbar );

    gtk_box_pack_start( main_vbox, w, FALSE, FALSE, GUI_PAD_BIG );

    gtk_window_set_default_size( GTK_WINDOW(d), 400u, 0u );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    setIsBuilding( ui, FALSE );
    return d;
}
