/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
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

void
tr_icon_refresh( gpointer vicon UNUSED )
{
}

#else

static void
activated( GtkStatusIcon   * self      UNUSED,
           gpointer          user_data UNUSED )
{
    action_activate ( "toggle-main-window" );
}

static void
popup( GtkStatusIcon *       self,
       guint                 button,
       guint                 when,
       gpointer         data UNUSED )
{
    GtkWidget * w = action_get_widget( "/icon-popup" );

    gtk_menu_popup ( GTK_MENU( w ), NULL, NULL,
                     gtk_status_icon_position_menu,
                     self, button, when );
}

void
tr_icon_refresh( gpointer vicon )
{
    double d;
    int limit;
    char up[64];
    char upLimit[64];
    char down[64];
    char downLimit[64];
    char tip[1024];
    const char * idle = _( "Idle" );
    GtkStatusIcon * icon = GTK_STATUS_ICON( vicon );
    tr_session * session = tr_core_session( g_object_get_data( G_OBJECT( icon ), "tr-core" ) );

    /* up */
    if(((d = tr_sessionGetRawSpeed( session, TR_UP ))) < 0.1 )
        g_strlcpy( up, idle, sizeof( up ) );
    else
        tr_strlspeed( up, d, sizeof( up ) );

    /* up limit */
    if( !tr_sessionGetActiveSpeedLimit( session, TR_UP, &limit ) )
        *upLimit = '\0';
    else {
        char buf[64];
        tr_strlspeed( buf, limit, sizeof( buf ) );
        g_snprintf( upLimit, sizeof( upLimit ), _( "(Limit: %s)" ), buf );
    }

    /* down */
    if(((d = tr_sessionGetRawSpeed( session, TR_DOWN ))) < 0.1 )
        g_strlcpy( down, idle, sizeof( down ) );
    else
        tr_strlspeed( down, d, sizeof( down ) );

    /* down limit */
    if( !tr_sessionGetActiveSpeedLimit( session, TR_DOWN, &limit ) )
        *downLimit = '\0';
    else {
        char buf[64];
        tr_strlspeed( buf, limit, sizeof( buf ) );
        g_snprintf( downLimit, sizeof( downLimit ), _( "(Limit: %s)" ), buf );
    }

    /* %1$s: current upload speed
     * %2$s: current upload limit, if any
     * %3$s: current download speed
     * %4$s: current download limit, if any */
    g_snprintf( tip, sizeof( tip ), _( "Transmission\nUp: %1$s %2$s\nDown: %3$s %4$s" ), up, upLimit, down, downLimit );

#if GTK_CHECK_VERSION( 2,16,0 )
    gtk_status_icon_set_tooltip_text( GTK_STATUS_ICON( icon ), tip );
#else
    gtk_status_icon_set_tooltip( GTK_STATUS_ICON( icon ), tip );
#endif
}

gpointer
tr_icon_new( TrCore * core )
{
    GtkStatusIcon * icon = gtk_status_icon_new_from_icon_name(
        "transmission" );

    g_signal_connect( icon, "activate", G_CALLBACK( activated ), NULL );
    g_signal_connect( icon, "popup-menu", G_CALLBACK( popup ), NULL );
    g_object_set_data( G_OBJECT( icon ), "tr-core", core );
    return icon;
}

#endif
