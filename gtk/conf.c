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
#include "util.h"

static char * gl_confdir = NULL;
static char * gl_lockpath = NULL;

/* errstr may be NULL, this might be called before GTK is initialized */
gboolean
cf_init( const char *dir,
         char **     errstr )
{
    if( errstr != NULL )
        *errstr = NULL;

    gl_confdir = g_strdup( dir );

    if( mkdir_p( gl_confdir, 0755 ) )
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
lockfile( const char *         filename,
          tr_lockfile_state_t *tr_state,
          char **              errstr )
{
    const tr_lockfile_state_t state = tr_lockfile( filename );
    const gboolean            success = state == TR_LOCKFILE_SUCCESS;

    if( errstr ) switch( state )
        {
            case TR_LOCKFILE_EOPEN:
                *errstr =
                    g_strdup_printf( _( "Couldn't open \"%1$s\": %2$s" ),
                                    filename, g_strerror( errno ) );
                break;

            case TR_LOCKFILE_ELOCK:
                *errstr = g_strdup_printf( _( "%s is already running." ),
                                          g_get_application_name( ) );
                break;

            case TR_LOCKFILE_SUCCESS:
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
cf_lock( tr_lockfile_state_t *tr_state,
         char **              errstr )
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

static char*
getPrefsFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename( gl_confdir, "settings.json", NULL );
}

static tr_benc*
getPrefs( void )
{
    static tr_benc  dict;
    static gboolean loaded = FALSE;

    if( !loaded )
    {
        char * filename = getPrefsFilename( );
        if( tr_bencLoadJSONFile( filename, &dict ) )
            tr_bencInitDict( &dict, 100 );
        g_free( filename );
        loaded = TRUE;
    }

    return &dict;
}

/***
****
***/

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
    tr_benc * d = getPrefs( );

    tr_bencDictRemove( d, key );
    tr_bencDictAddInt( d, key, value );
}

void
pref_int_set_default( const char * key,
                      int64_t      value )
{
    if( !tr_bencDictFind( getPrefs( ), key ) )
        pref_int_set( key, value );
}

/***
****
***/

gboolean
pref_flag_get( const char * key )
{
    int64_t i;

    tr_bencDictFindInt( getPrefs( ), key, &i );
    return i != 0;
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
    pref_int_set( key, value != 0 );
}

void
pref_flag_set_default( const char * key,
                       gboolean     value )
{
    pref_int_set_default( key, value != 0 );
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
pref_string_set( const char * key,
                 const char * value )
{
    tr_benc * d = getPrefs( );

    tr_bencDictRemove( d, key );
    tr_bencDictAddStr( d, key, value );
}

void
pref_string_set_default( const char * key,
                         const char * value )
{
    if( !tr_bencDictFind( getPrefs( ), key ) )
        pref_string_set( key, value );
}

/***
****
***/

void
pref_save( void )
{
    char * filename = getPrefsFilename( );
    char * path = g_path_get_dirname( filename );

    mkdir_p( path, 0755 );
    tr_bencSaveJSONFile( filename, getPrefs( ) );

    g_free( path );
    g_free( filename );
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
getCompat080PrefsFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename(
               g_get_home_dir( ), ".transmission", "gtk", "prefs", NULL );
}

static char*
getCompat090PrefsFilename( void )
{
    assert( gl_confdir != NULL );
    return g_build_filename(
               g_get_home_dir( ), ".transmission", "gtk", "prefs.ini", NULL );
}

static char*
getCompat121PrefsFilename( void )
{
    return g_build_filename(
               g_get_user_config_dir( ), "transmission", "gtk", "prefs.ini",
               NULL );
}

static void
translate_08_to_09( const char* oldfile,
                    const char* newfile )
{
    static struct pref_entry
    {
        const char*   oldkey;
        const char*   newkey;
    } pref_table[] = {
        { "add-behavior-ipc",       "add-behavior-ipc"               },
        { "add-behavior-standard",  "add-behavior-standard"          },
        { "download-directory",     "default-download-directory"     },
        { "download-limit",         "download-limit"                 },
        { "use-download-limit",     "download-limit-enabled"         },
        { "listening-port",         "listening-port"                 },
        { "use-nat-traversal",      "nat-traversal-enabled"          },
        { "use-peer-exchange",      "pex-enabled"                    },
        { "ask-quit",               "prompt-before-exit"             },
        { "ask-download-directory", "prompt-for-download-directory"  },
        { "use-tray-icon",          "system-tray-icon-enabled"       },
        { "upload-limit",           "upload-limit"                   },
        { "use-upload-limit",       "upload-limit-enabled"           }
    };

    GString * out = g_string_new( NULL );
    gchar *   contents = NULL;
    gsize     contents_len = 0;
    tr_benc   top;

    memset( &top, 0, sizeof( tr_benc ) );

    if( g_file_get_contents( oldfile, &contents, &contents_len, NULL )
      && !tr_bencLoad( contents, contents_len, &top, NULL )
      && top.type == TYPE_DICT )
    {
        unsigned int i;
        g_string_append( out, "\n[general]\n" );
        for( i = 0; i < G_N_ELEMENTS( pref_table ); ++i )
        {
            const tr_benc * val = tr_bencDictFind( &top,
                                                   pref_table[i].oldkey );
            if( val != NULL )
            {
                const char * valstr = val->val.s.s;
                if( !strcmp( valstr, "yes" ) ) valstr = "true";
                if( !strcmp( valstr, "no" ) ) valstr = "false";
                g_string_append_printf( out, "%s=%s\n",
                                        pref_table[i].newkey,
                                        valstr );
            }
        }
    }

    g_file_set_contents( newfile, out->str, out->len, NULL );
    g_string_free( out, TRUE );
    g_free( contents );
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
    tr_bencSaveJSONFile( new_file, &dict );
    tr_bencFree( &dict );
}

void
cf_check_older_configs( void )
{
    char * filename = getPrefsFilename( );

    if( !g_file_test( filename, G_FILE_TEST_IS_REGULAR ) )
    {
        char * key1 = getCompat121PrefsFilename( );
        char * key2 = getCompat090PrefsFilename( );
        char * benc = getCompat080PrefsFilename( );

        if( g_file_test( key1, G_FILE_TEST_IS_REGULAR ) )
        {
            translate_keyfile_to_json( key1, filename );
        }
        else if( g_file_test( key2, G_FILE_TEST_IS_REGULAR ) )
        {
            translate_keyfile_to_json( key2, filename );
        }
        else if( g_file_test( benc, G_FILE_TEST_IS_REGULAR ) )
        {
            char * tmpfile;
            int    fd =
                g_file_open_tmp( "transmission-prefs-XXXXXX", &tmpfile,
                                 NULL );
            if( fd != -1 ) close( fd );
            translate_08_to_09( benc, tmpfile );
            translate_keyfile_to_json( tmpfile, filename );
            unlink( tmpfile );
        }

        g_free( benc );
        g_free( key2 );
        g_free( key1 );
    }

    g_free( filename );
}

