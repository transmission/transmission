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

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include "actions.h"
#include "tr-icon.h"
#include "util.h"

#ifndef STATUS_ICON_SUPPORTED

gpointer
tr_icon_new( TrCore * core )
{
    return NULL;
}

#else

static void
activated ( GtkStatusIcon   * self        UNUSED,
            gpointer          user_data   UNUSED )
{
    action_activate ("toggle-main-window");
}

static void
popup ( GtkStatusIcon  * self,
        guint            button,
        guint            when,
        gpointer         data    UNUSED )
{
    GtkWidget * w = action_get_widget( "/icon-popup" );
    gtk_menu_popup (GTK_MENU(w), NULL, NULL,
                    gtk_status_icon_position_menu,
                    self, button, when );
}

static void
core_destroyed( gpointer data, GObject * core UNUSED )
{
    g_source_remove( GPOINTER_TO_UINT( data ) );
}

static gboolean
refresh_tooltip_cb( gpointer data )
{
    GtkStatusIcon * icon = GTK_STATUS_ICON( data );
    TrCore * core = g_object_get_data( G_OBJECT( icon ), "tr-core" );
    const struct core_stats * stats = tr_core_get_stats( core );
    char downStr[32], upStr[32];
    char tip[256];

    tr_strlspeed( downStr, stats->clientDownloadSpeed, sizeof( downStr ) );
    tr_strlspeed( upStr, stats->clientUploadSpeed, sizeof( upStr ) );
    g_snprintf( tip, sizeof( tip ),
                /* %1$d is the number of torrents we're seeding,
                   %2$d is the number of torrents we're downloading,
                   %3$s is our download speed,
                   %4$s is our upload speed */
                _( "%1$d Seeding, %2$d Downloading\nDown: %3$s, Up: %4$s" ), 
                stats->seedingCount,
                stats->downloadCount,
                downStr, upStr );
    gtk_status_icon_set_tooltip( GTK_STATUS_ICON( icon ), tip );

    return TRUE;
}

gpointer
tr_icon_new( TrCore * core )
{
    guint id;
    GtkStatusIcon * icon = gtk_status_icon_new_from_icon_name( "transmission" );
    g_signal_connect( icon, "activate", G_CALLBACK( activated ), NULL );
    g_signal_connect( icon, "popup-menu", G_CALLBACK( popup ), NULL );
    id = g_timeout_add( 1000, refresh_tooltip_cb, icon );
    g_object_weak_ref( G_OBJECT( core ), core_destroyed, GUINT_TO_POINTER( id ) );
    g_object_set_data( G_OBJECT( icon ), "tr-core", core );
    return icon;
}

#endif
