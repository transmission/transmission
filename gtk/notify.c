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

#include <glib/gi18n.h>
#include "conf.h"
#include "notify.h"
#include "tr-prefs.h"
#include "util.h"

void
gtr_notify_torrent_completed( TrCore * core, int torrent_id )
{
    if( gtr_pref_flag_get( PREF_KEY_TORRENT_COMPLETE_SOUND_ENABLED ) )
    {
        const char * cmd = gtr_pref_string_get( PREF_KEY_TORRENT_COMPLETE_SOUND_COMMAND );
        g_spawn_command_line_async( cmd, NULL );
    }

    if( gtr_pref_flag_get( PREF_KEY_TORRENT_COMPLETE_NOTIFICATION_ENABLED ) )
    {
        const tr_torrent * tor = gtr_core_find_torrent( core, torrent_id );
        const char * fmt = gtr_pref_string_get( PREF_KEY_TORRENT_COMPLETE_NOTIFICATION_COMMAND );
        char * cmd = g_strdup_printf( fmt, _( "Torrent Complete" ), ( tor ? tr_torrentName( tor ) : "" ) );
        g_spawn_command_line_async( cmd, NULL );
        g_free( cmd );
    }
}

void
gtr_notify_torrent_added( const char * name )
{
    if( gtr_pref_flag_get( PREF_KEY_TORRENT_ADDED_NOTIFICATION_ENABLED ) )
    {
        const char * fmt = gtr_pref_string_get( PREF_KEY_TORRENT_ADDED_NOTIFICATION_COMMAND );
        char * cmd = g_strdup_printf( fmt, _( "Torrent Added" ), name );
        g_spawn_command_line_async( cmd, NULL );
        g_free( cmd );
    }
}
