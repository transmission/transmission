/*
 * This file Copyright (C) 2007-2010 Mnemosyne LLC
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
#ifdef HAVE_LIBAPPINDICATOR
 #include <libappindicator/app-indicator.h>
#endif
#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include "actions.h"
#include "tr-icon.h"
#include "util.h"

#define ICON_NAME "transmission"

#ifndef STATUS_ICON_SUPPORTED
gpointer gtr_icon_new( TrCore * core UNUSED ) { return NULL; }
void gtr_icon_refresh( gpointer vicon UNUSED ) { } 
#else

#ifdef HAVE_LIBAPPINDICATOR
void
gtr_icon_refresh( gpointer vindicator UNUSED )
{
}
#else
static void
activated( GtkStatusIcon * self UNUSED, gpointer user_data UNUSED )
{
    gtr_action_activate( "toggle-main-window" );
}

static void
popup( GtkStatusIcon *       self,
       guint                 button,
       guint                 when,
       gpointer         data UNUSED )
{
    GtkWidget * w = gtr_action_get_widget( "/icon-popup" );

    gtk_menu_popup ( GTK_MENU( w ), NULL, NULL,
                     gtk_status_icon_position_menu,
                     self, button, when );
}

void
gtr_icon_refresh( gpointer vicon )
{
    double KBps;
    double limit;
    char up[64];
    char upLimit[64];
    char down[64];
    char downLimit[64];
    char tip[1024];
    const char * idle = _( "Idle" );
    GtkStatusIcon * icon = GTK_STATUS_ICON( vicon );
    tr_session * session = tr_core_session( g_object_get_data( G_OBJECT( icon ), "tr-core" ) );

    /* up */
    KBps = tr_sessionGetRawSpeed_KBps( session, TR_UP );
    if( KBps < 0.001 )
        g_strlcpy( up, idle, sizeof( up ) );
    else
        tr_formatter_speed_KBps( up, KBps, sizeof( up ) );

    /* up limit */
    if( !tr_sessionGetActiveSpeedLimit_KBps( session, TR_UP, &limit ) )
        *upLimit = '\0';
    else {
        char buf[64];
        tr_formatter_speed_KBps( buf, limit, sizeof( buf ) );
        g_snprintf( upLimit, sizeof( upLimit ), _( "(Limit: %s)" ), buf );
    }

    /* down */
    KBps = tr_sessionGetRawSpeed_KBps( session, TR_DOWN );
    if( KBps < 0.001 )
        g_strlcpy( down, idle, sizeof( down ) );
    else
        tr_formatter_speed_KBps( down, KBps, sizeof( down ) );

    /* down limit */
    if( !tr_sessionGetActiveSpeedLimit_KBps( session, TR_DOWN, &limit ) )
        *downLimit = '\0';
    else {
        char buf[64];
        tr_formatter_speed_KBps( buf, limit, sizeof( buf ) );
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
#endif

static const char *
getIconName( void )
{
    const char * icon_name;

    GtkIconTheme * theme = gtk_icon_theme_get_default( );

    /* if the tray's icon is a 48x48 file, use it;
     * otherwise, use the fallback builtin icon */
    if( !gtk_icon_theme_has_icon( theme, TRAY_ICON ) )
        icon_name = ICON_NAME;
    else {
        GtkIconInfo * icon_info = gtk_icon_theme_lookup_icon( theme, TRAY_ICON, 48, GTK_ICON_LOOKUP_USE_BUILTIN );
        const gboolean icon_is_builtin = gtk_icon_info_get_filename ( icon_info ) == NULL;
        gtk_icon_info_free ( icon_info );
        icon_name = icon_is_builtin ? ICON_NAME : TRAY_ICON;
    }

    return icon_name;
}

#ifdef HAVE_LIBAPPINDICATOR
gpointer
gtr_icon_new( TrCore * core)
{
    GtkWidget * w;
    const char * icon_name = getIconName( );
    AppIndicator * indicator = app_indicator_new( ICON_NAME, icon_name, APP_INDICATOR_CATEGORY_SYSTEM_SERVICES );
    app_indicator_set_status( indicator, APP_INDICATOR_STATUS_ACTIVE );
    w = gtr_action_get_widget( "/icon-popup" );
    app_indicator_set_menu( indicator, GTK_MENU ( w ) );
    g_object_set_data( G_OBJECT( indicator ), "tr-core", core );
    return indicator;
}
#else
gpointer
gtr_icon_new( TrCore * core )
{
    const char * icon_name = getIconName( );
    GtkStatusIcon * icon = gtk_status_icon_new_from_icon_name( icon_name );
    g_signal_connect( icon, "activate", G_CALLBACK( activated ), NULL );
    g_signal_connect( icon, "popup-menu", G_CALLBACK( popup ), NULL );
    g_object_set_data( G_OBJECT( icon ), "tr-core", core );
    return icon;
}

#endif

#endif
