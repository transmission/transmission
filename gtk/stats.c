/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id:$
 */

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "hig.h"
#include "stats.h"
#include "tr_core.h"

struct stat_ui
{
    GtkWidget * one_up_lb;
    GtkWidget * one_down_lb;
    GtkWidget * one_ratio_lb;
    GtkWidget * one_time_lb;
    GtkWidget * all_up_lb;
    GtkWidget * all_down_lb;
    GtkWidget * all_ratio_lb;
    GtkWidget * all_time_lb;
    GtkWidget * all_sessions_lb;
    TrCore * core;
};

static void
setLabel( GtkWidget * w, const char * str )
{
    gtk_label_set_text( GTK_LABEL(w), str );
}

static void
setLabelFromRatio( GtkWidget * w, double d )
{
    char buf[128];
    tr_strlratio( buf, d, sizeof( buf ) );
    setLabel( w, buf );
}

static gboolean
updateStats( gpointer gdata )
{
    char buf[128];

    struct stat_ui * ui = gdata;
    tr_session_stats one, all;
    tr_getSessionStats( tr_core_handle( ui->core ), &one );
    tr_getCumulativeSessionStats( tr_core_handle( ui->core ), &all );

    setLabel( ui->one_up_lb, tr_strlsize( buf, one.uploadedBytes, sizeof(buf) ) );
    setLabel( ui->one_down_lb, tr_strlsize( buf, one.downloadedBytes, sizeof(buf) ) );
    setLabel( ui->one_time_lb, tr_strltime( buf, one.secondsActive, sizeof(buf) ) );
    setLabelFromRatio( ui->one_ratio_lb, one.ratio );
    setLabel( ui->all_sessions_lb, g_strdup_printf( _("Started %d times"), (int)all.sessionCount ) );
    setLabel( ui->all_up_lb, tr_strlsize( buf, all.uploadedBytes, sizeof(buf) ) );
    setLabel( ui->all_down_lb, tr_strlsize( buf, all.downloadedBytes, sizeof(buf) ) );
    setLabel( ui->all_time_lb, tr_strltime( buf, all.secondsActive, sizeof(buf) ) );
    setLabelFromRatio( ui->all_ratio_lb, all.ratio );

    return TRUE;
}

static void
dialogResponse( GtkDialog * dialog, gint response UNUSED, gpointer unused UNUSED )
{
    g_source_remove( GPOINTER_TO_UINT( g_object_get_data( G_OBJECT(dialog), "TrTimer" ) ) );
    gtk_widget_destroy( GTK_WIDGET( dialog ) );
}

GtkWidget*
stats_dialog_create( GtkWindow * parent, TrCore * core )
{
    guint i;
    int row = 0;
    GtkWidget * d;
    GtkWidget * t;
    GtkWidget * l;
    struct stat_ui * ui = g_new0( struct stat_ui, 1 );

    d = gtk_dialog_new_with_buttons( _("Statistics"),
                                     parent,
                                     GTK_DIALOG_DESTROY_WITH_PARENT,
                                     GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                     NULL );
    t = hig_workarea_create( );
    gtk_box_pack_start_defaults( GTK_BOX(GTK_DIALOG(d)->vbox), t );
    ui->core = core;

    hig_workarea_add_section_title( t, &row, _( "Current Session" ) );
    hig_workarea_add_section_spacer( t, row, 4 );
        l = ui->one_up_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Uploaded:"), l, NULL );
        l = ui->one_down_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Downloaded:"), l, NULL );
        l = ui->one_ratio_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Ratio:"), l, NULL );
        l = ui->one_time_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Duration:"), l, NULL );
    hig_workarea_add_section_divider( t, &row );
    hig_workarea_add_section_title( t, &row, _("Cumulative") );
    hig_workarea_add_section_spacer( t, row, 5 );
        l = ui->all_sessions_lb = gtk_label_new( _("Program started %d times") );
        hig_workarea_add_label_w( t, row++, l );
        l = ui->all_up_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Uploaded:"), l, NULL );
        l = ui->all_down_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Downloaded:"), l, NULL );
        l = ui->all_ratio_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Ratio:"), l, NULL );
        l = ui->all_time_lb = gtk_label_new( NULL );
        hig_workarea_add_row( t, &row, _("Duration:"), l, NULL );
    hig_workarea_finish( t, &row );
    gtk_widget_show_all( t );

    updateStats( ui );
    g_object_set_data_full( G_OBJECT(d), "data", ui, g_free );
    g_signal_connect( d, "response", G_CALLBACK(dialogResponse), NULL );
    i = g_timeout_add( 1000, updateStats, ui );
    g_object_set_data( G_OBJECT(d), "TrTimer", GUINT_TO_POINTER(i) );
    return d;
}
