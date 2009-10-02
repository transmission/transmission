/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2008 Transmission authors and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *****************************************************************************/

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/platform.h>

#include "conf.h"
#include "tr-prefs.h"
#include "util.h"

#define MY_NAME "transmission"

static char * gl_confdir = NULL;
static char * gl_lockpath = NULL;

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_init( const char   * configDir,
         char        ** errstr )
{
    if( errstr != NULL )
        *errstr = NULL;

    gl_confdir = g_strdup( configDir );

    if( gtr_mkdir_with_parents( gl_confdir, 0755 ) )
        return TRUE;

    if( errstr != NULL )
        *errstr = g_strdup_printf( _( "Couldn't create \"%1$s\": %2$s" ),
                                  gl_confdir, g_strerror( errno ) );

    return FALSE;
}

/***
****
****  Lockfile
****
***/

/* errstr may be NULL, this might be called before GTK is initialized */
static gboolean
lockfile( const char             * filename,
          gtr_lockfile_state_t   * tr_state,
          char                  ** errstr )
{
    const gtr_lockfile_state_t state = gtr_lockfile( filename );
    const gboolean success = state == GTR_LOCKFILE_SUCCESS;

    if( errstr ) switch( state )
        {
            case GTR_LOCKFILE_EOPEN:
                *errstr =
                    g_strdup_printf( _( "Couldn't open \"%1$s\": %2$s" ),
                                    filename, g_strerror( errno ) );
                break;

            case GTR_LOCKFILE_ELOCK:
                *errstr = g_strdup_printf( _( "%s is already running." ),
                                          g_get_application_name( ) );
                break;

            case GTR_LOCKFILE_SUCCESS:
                *errstr = NULL;
                break;
        }

    if( tr_state != NULL )
        *tr_state = state;

    return success;
}

static char*
getLockFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "lock", NULL );
}

static void
cf_removelocks( void )
{
    if( gl_lockpath )
    {
        g_unlink( gl_lockpath );
        g_free( gl_lockpath );
    }
}

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_lock( gtr_lockfile_state_t * tr_state, char ** errstr )
{
    char *         path = getLockFilename( );
    const gboolean didLock = lockfile( path, tr_state, errstr );

    if( didLock )
        gl_lockpath = g_strdup( path );
    g_atexit( cf_removelocks );
    g_free( path );
    return didLock;
}

/***
****
****  Preferences
****
***/

static void cf_check_older_configs( void );

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
static void
tr_prefs_init_defaults( tr_benc * d )
{
    const char * str;

    cf_check_older_configs( );

#ifdef HAVE_GIO
    str = NULL;
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DESKTOP );
    if( !str ) str = tr_getDefaultDownloadDir( );
    tr_bencDictAddStr( d, PREF_KEY_DIR_WATCH, str );
    tr_bencDictAddBool( d, PREF_KEY_DIR_WATCH_ENABLED, FALSE );
#endif

    tr_bencDictAddBool( d, PREF_KEY_LEGAL_DIALOG_ON_STARTUP, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_INHIBIT_HIBERNATION, FALSE );
    tr_bencDictAddBool( d, PREF_KEY_BLOCKLIST_UPDATES_ENABLED, TRUE );

    tr_bencDictAddStr( d, PREF_KEY_OPEN_DIALOG_FOLDER, g_get_home_dir( ) );

    tr_bencDictAddBool( d, PREF_KEY_TOOLBAR, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_FILTERBAR, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_STATUSBAR, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_SHOW_TRAY_ICON, FALSE );
    tr_bencDictAddBool( d, PREF_KEY_SHOW_DESKTOP_NOTIFICATION, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_SHOW_MORE_TRACKER_INFO, FALSE );
    tr_bencDictAddBool( d, PREF_KEY_SHOW_BACKUP_TRACKERS, FALSE );
    tr_bencDictAddStr( d, PREF_KEY_STATUSBAR_STATS, "total-ratio" );

    tr_bencDictAddBool( d, PREF_KEY_OPTIONS_PROMPT, TRUE );

    tr_bencDictAddBool( d, PREF_KEY_MAIN_WINDOW_IS_MAXIMIZED, FALSE );
    tr_bencDictAddInt( d, PREF_KEY_MAIN_WINDOW_HEIGHT, 500 );
    tr_bencDictAddInt( d, PREF_KEY_MAIN_WINDOW_WIDTH, 300 );
    tr_bencDictAddInt( d, PREF_KEY_MAIN_WINDOW_X, 50 );
    tr_bencDictAddInt( d, PREF_KEY_MAIN_WINDOW_Y, 50 );
    tr_bencDictAddStr( d, PREF_KEY_MAIN_WINDOW_LAYOUT_ORDER, "menu,toolbar,filter,list,statusbar" );

    str = NULL;
#if GLIB_CHECK_VERSION( 2, 14, 0 )
    if( !str ) str = g_get_user_special_dir( G_USER_DIRECTORY_DOWNLOAD );
#endif
    if( !str ) str = tr_getDefaultDownloadDir( );
    tr_bencDictAddStr( d, TR_PREFS_KEY_DOWNLOAD_DIR, str );

    tr_bencDictAddBool( d, PREF_KEY_ASKQUIT, TRUE );

    tr_bencDictAddStr( d, PREF_KEY_FILTER_MODE, "show-all" );
    tr_bencDictAddStr( d, PREF_KEY_SORT_MODE, "sort-by-name" );
    tr_bencDictAddBool( d, PREF_KEY_SORT_REVERSED, FALSE );
    tr_bencDictAddBool( d, PREF_KEY_MINIMAL_VIEW, FALSE );

    tr_bencDictAddBool( d, PREF_KEY_START, TRUE );
    tr_bencDictAddBool( d, PREF_KEY_TRASH_ORIGINAL, FALSE );
}

static char*
getPrefsFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "settings.json", NULL );
}

static tr_benc*
getPrefs( void )
{
    static tr_benc settings;
    static gboolean loaded = FALSE;

    if( !loaded )
    {
        tr_bencInitDict( &settings, 0 );
        tr_prefs_init_defaults( &settings );
        tr_sessionLoadSettings( &settings, gl_confdir, MY_NAME );
        loaded = TRUE;
    }

    return &settings;
}

/***
****
***/

tr_benc*
pref_get_all( void )
{
    return getPrefs( );
}

int64_t
pref_int_get( const char * key )
{
    int64_t i = 0;

    tr_bencDictFindInt( getPrefs( ), key, &i );
    return i;
}

void
pref_int_set( const char * key,
              int64_t      value )
{
    tr_bencDictAddInt( getPrefs( ), key, value );
}

double
pref_double_get( const char * key )
{
    double d = 0.0;
    tr_bencDictFindReal( getPrefs( ), key, &d );
    return d;
}

void
pref_double_set( const char * key,
                 double       value )
{
    tr_bencDictAddReal( getPrefs( ), key, value );
}

/***
****
***/

gboolean
pref_flag_get( const char * key )
{
    tr_bool boolVal;
    tr_bencDictFindBool( getPrefs( ), key, &boolVal );
    return boolVal != 0;
}

gboolean
pref_flag_eval( pref_flag_t  val,
                const char * key )
{
    switch( val )
    {
        case PREF_FLAG_TRUE:
            return TRUE;

        case PREF_FLAG_FALSE:
            return FALSE;

        default:
            return pref_flag_get( key );
    }
}

void
pref_flag_set( const char * key,
               gboolean     value )
{
    tr_bencDictAddBool( getPrefs( ), key, value );
}

/***
****
***/

const char*
pref_string_get( const char * key )
{
    const char * str = NULL;
    tr_bencDictFindStr( getPrefs( ), key, &str );
    return str;
}

void
pref_string_set( const char * key, const char * value )
{
    tr_bencDictAddStr( getPrefs( ), key, value );
}

/***
****
***/

void
pref_save( tr_session * session )
{
    tr_sessionSaveSettings( session, gl_confdir, getPrefs( ) );
}

/***
****
***/

#if !GLIB_CHECK_VERSION( 2, 8, 0 )
static void
tr_file_set_contents( const char *   filename,
                      const void *   out,
                      size_t         len,
                      GError* unused UNUSED )
{
    FILE * fp = fopen( filename, "wb+" );

    if( fp != NULL )
    {
        fwrite( out, 1, len, fp );
        fclose( fp );
    }
}

 #define g_file_set_contents tr_file_set_contents
#endif

static char*
getCompat090PrefsFilename( void )
{
    assert( gl_confdir != NULL );

    return g_build_filename( g_get_home_dir( ), ".transmission", "gtk", "prefs.ini", NULL );
}

static char*
getCompat121PrefsFilename( void )
{
    return g_build_filename( g_get_user_config_dir( ), "transmission", "gtk", "prefs.ini", NULL );
}

static void
translate_keyfile_to_json( const char * old_file,
                           const char * new_file )
{
    tr_benc    dict;
    GKeyFile * keyfile;
    gchar **   keys;
    gsize      i;
    gsize      length;

    static struct pref_entry {
        const char*   oldkey;
        const char*   newkey;
    } renamed[] = {
        { "default-download-directory", "download-dir"             },
        { "encrypted-connections-only", "encryption"               },
        { "listening-port",             "peer-port"                },
        { "nat-traversal-enabled",      "port-forwarding-enabled"  },
        { "open-dialog-folder",         "open-dialog-dir"          },
        { "watch-folder",               "watch-dir"                },
        { "watch-folder-enabled",       "watch-dir-enabled"        }
    };

    keyfile = g_key_file_new( );
    g_key_file_load_from_file( keyfile, old_file, 0, NULL );
    length = 0;
    keys = g_key_file_get_keys( keyfile, "general", &length, NULL );

    tr_bencInitDict( &dict, length );
    for( i = 0; i < length; ++i )
    {
        guint        j;
        const char * key = keys[i];
        gchar *      val = g_key_file_get_value( keyfile, "general", key,
                                                 NULL );

        for( j = 0; j < G_N_ELEMENTS( renamed ); ++j )
            if( !strcmp( renamed[j].oldkey, key ) )
                key = renamed[j].newkey;

        if( !strcmp( val, "true" ) || !strcmp( val, "false" ) )
            tr_bencDictAddInt( &dict, key, !strcmp( val, "true" ) );
        else
        {
            char * end;
            long   l;
            errno = 0;
            l = strtol( val, &end, 10 );
            if( !errno && end && !*end )
                tr_bencDictAddInt( &dict, key, l );
            else
                tr_bencDictAddStr( &dict, key, val );
        }

        g_free( val );
    }

    g_key_file_free( keyfile );
    tr_bencToFile( &dict, TR_FMT_JSON, new_file );
    tr_bencFree( &dict );
}

static void
cf_check_older_configs( void )
{
    char * filename = getPrefsFilename( );

    if( !g_file_test( filename, G_FILE_TEST_IS_REGULAR ) )
    {
        char * key1 = getCompat121PrefsFilename( );
        char * key2 = getCompat090PrefsFilename( );

        if( g_file_test( key1, G_FILE_TEST_IS_REGULAR ) )
        {
            g_message( _( "Importing \"%s\"" ), key1 );
            translate_keyfile_to_json( key1, filename );
        }
        else if( g_file_test( key2, G_FILE_TEST_IS_REGULAR ) )
        {
            g_message( _( "Importing \"%s\"" ), key2 );
            translate_keyfile_to_json( key2, filename );
        }

        g_free( key2 );
        g_free( key1 );
    }

    g_free( filename );
}
