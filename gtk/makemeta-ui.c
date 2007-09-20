/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
    char torrent_name[2048];
    GtkWidget * size_lb;
    GtkWidget * pieces_lb;
    GtkWidget * announce_entry;
    GtkWidget * comment_entry;
    GtkWidget * progressbar;
    GtkWidget * private_check;
    GtkWidget * dialog;
    GtkWidget * progress_dialog;
    tr_metainfo_builder * builder;
    tr_handle * handle;
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
progress_response_cb ( GtkDialog *d UNUSED, int response, gpointer user_data )
{
    MakeMetaUI * ui = (MakeMetaUI *) user_data;

    if( response == GTK_RESPONSE_CANCEL )
    {
        ui->builder->abortFlag = TRUE;
    }
    else
    {
        gtk_widget_destroy( ui->dialog );
    }
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
    g_snprintf( buf, sizeof(buf), "%s (%d%%)", ui->torrent_name, (int)(fraction*100 + 0.5));
    gtk_progress_bar_set_text( p, buf );

    if( ui->builder->isDone )
    {
        GtkWidget * w;

        if( ui->builder->failed )
        {
            const char * reason = ui->builder->abortFlag
                ? _("Torrent creation aborted.")
                : _("Torrent creation failed.");
            w = gtk_message_dialog_new (GTK_WINDOW(ui->progress_dialog),
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_CLOSE, reason );
            gtk_dialog_run( GTK_DIALOG( w ) );
            gtk_widget_destroy( ui->progress_dialog );
        }
        else
        {
            GtkWidget * w = ui->progress_dialog;
            gtk_window_set_title (GTK_WINDOW(ui->progress_dialog), _("Torrent Created"));
            gtk_dialog_set_response_sensitive (GTK_DIALOG(w), GTK_RESPONSE_CANCEL, FALSE);
            gtk_dialog_set_response_sensitive (GTK_DIALOG(w), GTK_RESPONSE_CLOSE, TRUE);
            gtk_progress_bar_set_text( p, buf );
        }
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
    GtkWidget *w, *p, *fr;
    char *tmp;
    char buf[1024];
    guint tag;

    if( response != GTK_RESPONSE_ACCEPT )
    {
        gtk_widget_destroy( GTK_WIDGET( d ) );
        return;
    }

    w = gtk_dialog_new_with_buttons( _("Making Torrent..."), 
                                     GTK_WINDOW(d),
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    g_signal_connect( w, "response", G_CALLBACK(progress_response_cb), ui );
    ui->progress_dialog = w;
    gtk_dialog_set_response_sensitive (GTK_DIALOG(w), GTK_RESPONSE_CLOSE, FALSE);

    tmp = g_path_get_basename (ui->builder->top);
    g_snprintf( ui->torrent_name, sizeof(ui->torrent_name), "%s.torrent", tmp );
    g_snprintf( buf, sizeof(buf), "%s (%d%%)", ui->torrent_name, 0);
    p = ui->progressbar = gtk_progress_bar_new ();
    gtk_progress_bar_set_text( GTK_PROGRESS_BAR(p), buf );
    fr = gtk_frame_new (NULL);
    gtk_frame_set_shadow_type (GTK_FRAME(fr), GTK_SHADOW_NONE);
    gtk_container_set_border_width( GTK_CONTAINER(fr), 20 );
    gtk_container_add (GTK_CONTAINER(fr), p);
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(w)->vbox), fr );
    gtk_widget_show_all ( w );
    g_free( tmp );

    tr_makeMetaInfo( ui->builder,
                     NULL, 
                     gtk_entry_get_text( GTK_ENTRY( ui->announce_entry ) ),
                     gtk_entry_get_text( GTK_ENTRY( ui->comment_entry ) ),
                     gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( ui->private_check ) ) );

    tag = g_timeout_add (UPDATE_INTERVAL_MSEC, refresh_cb, ui);
    g_object_set_data_full (G_OBJECT(w), "tag", GUINT_TO_POINTER(tag), remove_tag);
}

/***
****
***/

static void
file_selection_changed_cb( GtkFileChooser *chooser, gpointer user_data )
{
    MakeMetaUI * ui = (MakeMetaUI *) user_data;
    char * pch;
    char * filename;
    char buf[512];
    uint64_t totalSize=0;
    int fileCount=0, pieceCount=0, pieceSize=0;

    if( ui->builder ) {
        tr_metaInfoBuilderFree( ui->builder );
        ui->builder = NULL;
    }

    filename = gtk_file_chooser_get_filename( chooser );
    if( filename ) {
        ui->builder = tr_metaInfoBuilderCreate( ui->handle, filename );
        g_free( filename );
        fileCount = ui->builder->fileCount;
        totalSize = ui->builder->totalSize;
        pieceCount = ui->builder->pieceCount;
        pieceSize = ui->builder->pieceSize;
    }

    pch = readablesize( totalSize );
    g_snprintf( buf, sizeof(buf), "<i>%s; %d %s</i>",
                pch, fileCount,
                ngettext("file", "files", fileCount) );
    gtk_label_set_markup ( GTK_LABEL(ui->size_lb), buf );
    g_free( pch );

    pch = readablesize( pieceSize );
    g_snprintf( buf, sizeof(buf), "<i>%d %s @ %s</i>",
                pieceCount,
                ngettext("piece", "pieces", pieceCount),
                pch );
    gtk_label_set_markup ( GTK_LABEL(ui->pieces_lb), buf );
    g_free( pch );
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
    char name[256];
    MakeMetaUI * ui = g_new0 ( MakeMetaUI, 1 );
    ui->handle = handle;

    d = gtk_dialog_new_with_buttons( _("Make a New Torrent"),
                                     parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_NEW, GTK_RESPONSE_ACCEPT,
                                     NULL );
    g_signal_connect( d, "response", G_CALLBACK(response_cb), ui );
    g_object_set_data_full( G_OBJECT(d), "ui", ui, freeMetaUI );
    ui->dialog = d;

    t = hig_workarea_create ();

    hig_workarea_add_section_title (t, &row, _("Files"));
    hig_workarea_add_section_spacer (t, row, 3);

        g_snprintf( name, sizeof(name), "%s:", _("File _Type"));
        h = gtk_hbox_new( FALSE, GUI_PAD_SMALL );
        w = rb_dir = gtk_radio_button_new_with_mnemonic( NULL, _("Directory"));
        gtk_box_pack_start ( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = rb_file = gtk_radio_button_new_with_mnemonic_from_widget( GTK_RADIO_BUTTON(w), _("Single File") );
        gtk_box_pack_start ( GTK_BOX(h), w, FALSE, FALSE, 0 );
        hig_workarea_add_row (t, &row, name, h, NULL);

        g_snprintf( name, sizeof(name), "%s:", _("_File"));

        w = gtk_file_chooser_dialog_new (_("File or Directory to Add to the New Torrent"),
                                         NULL,
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                         NULL);
        g_signal_connect( w, "map", G_CALLBACK(file_chooser_shown_cb), rb_dir );
        w = gtk_file_chooser_button_new_with_dialog( w );
        g_signal_connect( w, "selection-changed", G_CALLBACK(file_selection_changed_cb), ui );
        hig_workarea_add_row (t, &row, name, w, NULL);

        g_snprintf( name, sizeof(name), "<i>%s</i>", _("No Files Selected"));
        h = gtk_hbox_new( FALSE, GUI_PAD_SMALL );
        w = ui->size_lb = gtk_label_new (NULL);
        gtk_label_set_markup ( GTK_LABEL(w), name );
        gtk_box_pack_start( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = ui->pieces_lb = gtk_label_new (NULL);
        gtk_box_pack_end( GTK_BOX(h), w, FALSE, FALSE, 0 );
        w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
        gtk_widget_set_usize (w, 2 * GUI_PAD_BIG, 0);
        gtk_box_pack_start_defaults ( GTK_BOX(h), w );
        hig_workarea_add_row (t, &row, "", h, NULL);
        

    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title (t, &row, _("Torrent"));
    hig_workarea_add_section_spacer (t, row, 3);

        g_snprintf( name, sizeof(name), _("Private to this Tracker") );
        w = ui->private_check = hig_workarea_add_wide_checkbutton( t, &row, name, FALSE );

        g_snprintf( name, sizeof(name), "%s:", _("Announce _URL"));
        w = ui->announce_entry = gtk_entry_new( );
        gtk_entry_set_text(GTK_ENTRY(w), "http://");
        hig_workarea_add_row (t, &row, name, w, NULL );

        g_snprintf( name, sizeof(name), "%s:", _("Commen_t"));
        w = ui->comment_entry = gtk_entry_new( );
        hig_workarea_add_row (t, &row, name, w, NULL );


    gtk_window_set_default_size( GTK_WINDOW(d), 400u, 0u );
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), t );
    gtk_widget_show_all( GTK_DIALOG(d)->vbox );
    return d;
}
