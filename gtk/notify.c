/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
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
#ifdef HAVE_GIO
 #include <gio/gio.h>
#endif
#include <glib/gi18n.h>
#include "conf.h"
#include "notify.h"
#include "tr-prefs.h"
#include "util.h"

#ifndef HAVE_LIBNOTIFY

void
tr_notify_init( void ) { }
void
tr_notify_send( TrTorrent * tor UNUSED ) { }

#else
 #include <libnotify/notify.h>

void
tr_notify_init( void )
{
    notify_init( "Transmission" );
}

static void
notifyCallback( NotifyNotification * n UNUSED,
                const char *           action,
                gpointer               gdata )
{
    TrTorrent * gtor = TR_TORRENT( gdata );

    if( !strcmp( action, "folder" ) )
    {
        tr_torrent_open_folder( gtor );
    }
    else if( !strcmp( action, "file" ) )
    {
        tr_torrent *    tor = tr_torrent_handle( gtor );
        const tr_info * info = tr_torrent_info( gtor );
        char *          path =
            g_build_filename( tr_torrentGetDownloadDir(
                                  tor ), info->files[0].name, NULL );
        gtr_open_file( path );
        g_free( path );
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

void
tr_notify_send( TrTorrent *tor )
{
    if( pref_flag_get( PREF_KEY_SHOW_DESKTOP_NOTIFICATION ) )
    {
        const tr_info * info = tr_torrent_info( tor );
        NotifyNotification * n;

        n = notify_notification_new( _( "Torrent Complete" ),
                                     info->name,
                                     "transmission", NULL );

        if( can_support_actions( ) )
        {
            if( info->fileCount == 1 )
                notify_notification_add_action(
                    n, "file", _( "Open File" ),
                    NOTIFY_ACTION_CALLBACK( notifyCallback ), tor,
                    NULL );

            notify_notification_add_action(
                n, "folder", _( "Open Folder" ),
                NOTIFY_ACTION_CALLBACK( notifyCallback ), tor, NULL );
        }

        notify_notification_show( n, NULL );
    }
}

#endif
