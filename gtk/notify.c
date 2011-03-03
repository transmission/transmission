/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <string.h>

#ifdef HAVE_LIBCANBERRA
 #include <canberra-gtk.h>
#endif

#ifdef HAVE_GIO
 #include <gio/gio.h>
#endif

#include <glib/gi18n.h>
#include "actions.h" /* NOTIFICATION_ICON */
#include "conf.h"
#include "notify.h"
#include "tr-prefs.h"
#include "util.h"

#ifndef HAVE_LIBNOTIFY

void gtr_notify_init( void ) { }
void gtr_notify_torrent_completed( TrCore * core UNUSED, int torrent_id UNUSED ) { }
void gtr_notify_torrent_added( const char * name UNUSED ) { }

#else
 #include <libnotify/notify.h>

void
gtr_notify_init( void )
{
    notify_init( "Transmission" );
}

struct notify_callback_data
{
    TrCore * core;
    int torrent_id;
};

static void
notify_callback_data_free( gpointer gdata )
{
    struct notify_callback_data * data = gdata;
    g_object_unref( G_OBJECT( data->core ) );
    g_free( data );
}

static struct notify_callback_data *
notify_callback_data_new( TrCore * core, int torrent_id )
{
    struct notify_callback_data * data = g_new( struct notify_callback_data, 1 );
    data->core = core;
    data->torrent_id = torrent_id;
    g_object_ref( G_OBJECT( data->core ) );
    return data;
}

static void
notifyCallback( NotifyNotification  * n UNUSED,
                const char          * action,
                gpointer              gdata )
{
    struct notify_callback_data * data = gdata;
    tr_torrent * tor = gtr_core_find_torrent( data->core, data->torrent_id );

    if( tor != NULL )
    {
        if( !strcmp( action, "folder" ) )
        {
            gtr_core_open_folder( data->core, data->torrent_id );
        }
        else if( !strcmp( action, "file" ) )
        {
            const tr_info * inf = tr_torrentInfo( tor );
            const char * dir = tr_torrentGetDownloadDir( tor );
            char * path = g_build_filename( dir, inf->files[0].name, NULL );
            gtr_open_file( path );
            g_free( path );
        }
    }
}

static gboolean
can_support_actions( void )
{
    static gboolean supported;
    static gboolean have_checked = FALSE;

    if( !have_checked )
    {
        GList * c;
        GList * caps = notify_get_server_caps( );

        have_checked = TRUE;

        for( c=caps; c && !supported; c=c->next )
            supported = !strcmp( "actions", (char*)c->data );

        g_list_foreach( caps, (GFunc)g_free, NULL );
        g_list_free( caps );
    }

    return supported;
}

static void
addIcon( NotifyNotification * notify )
{
    int size = 32;
    GtkIconTheme * theme;
    GdkPixbuf * icon;

    gtk_icon_size_lookup( GTK_ICON_SIZE_DIALOG, &size, &size );
    theme = gtk_icon_theme_get_default( );
    icon = gtk_icon_theme_load_icon( theme, NOTIFICATION_ICON, size, 0, NULL );

    if( icon != NULL )
    {
        notify_notification_set_icon_from_pixbuf( notify, icon );
        g_object_unref( icon );
    }
}

static NotifyNotification *
tr_notify_notification_new( const char * summary,
                            const char * body,
                            const char * icon )
{
    NotifyNotification * n = notify_notification_new( summary, body, icon
/* the fourth argument was removed in libnotify 0.7.0 */
#if !defined(NOTIFY_VERSION_MINOR) || (NOTIFY_VERSION_MAJOR == 0 && NOTIFY_VERSION_MINOR < 7)
                                                     , NULL
#endif
                                                            );
    return n;
}

void
gtr_notify_torrent_completed( TrCore * core, int torrent_id )
{
#ifdef HAVE_LIBCANBERRA
    if( gtr_pref_flag_get( PREF_KEY_PLAY_DOWNLOAD_COMPLETE_SOUND ) )
    {
        /* play the sound, using sounds from the naming spec */
        ca_context_play( ca_gtk_context_get (), 0,
                         CA_PROP_EVENT_ID, "complete-download",
                         CA_PROP_APPLICATION_NAME, g_get_application_name,
                         CA_PROP_EVENT_DESCRIPTION, _("Download complete"),
                         NULL);
    }
#endif

    if( gtr_pref_flag_get( PREF_KEY_SHOW_DESKTOP_NOTIFICATION ) )
    {
        NotifyNotification * n;
        tr_torrent * tor = gtr_core_find_torrent( core, torrent_id );

        n = tr_notify_notification_new( _( "Torrent Complete" ), tr_torrentName( tor ), NULL );
        addIcon( n );

        if( can_support_actions( ) )
        {
            const tr_info * inf = tr_torrentInfo( tor );
            if( inf->fileCount == 1 )
                notify_notification_add_action(
                    n, "file", _( "Open File" ),
                    NOTIFY_ACTION_CALLBACK( notifyCallback ),
                    notify_callback_data_new( core, torrent_id ),
                    notify_callback_data_free );

            notify_notification_add_action(
                n, "folder", _( "Open Folder" ),
                NOTIFY_ACTION_CALLBACK( notifyCallback ),
                notify_callback_data_new( core, torrent_id ),
                notify_callback_data_free );
        }

        notify_notification_show( n, NULL );
    }
}

void
gtr_notify_torrent_added( const char * name )
{
    if( gtr_pref_flag_get( PREF_KEY_SHOW_DESKTOP_NOTIFICATION ) )
    {
        NotifyNotification * n = tr_notify_notification_new( _( "Torrent Added" ), name, NULL );
        addIcon( n );
        notify_notification_set_timeout( n, NOTIFY_EXPIRES_DEFAULT );
        notify_notification_show( n, NULL );
    }
}

#endif
