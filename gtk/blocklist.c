#include <stdarg.h>
#include <stdlib.h> /* getenv() */
#include <unistd.h> /* write() */
#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gi18n.h>

#include <curl/curl.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "blocklist.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

#define BLOCKLIST_DATE "blocklist-date"

/***
****
***/

struct idle_data
{
    TrCore *    core;
    gboolean    isDone;
    char *      str;
};
static gboolean
emitProgressIdle( gpointer gdata )
{
    struct idle_data * data = gdata;

    tr_core_blocksig( data->core, data->isDone, data->str );

    g_free( data->str );
    g_free( data );
    return FALSE;
}

static void
emitProgress( TrCore *     core,
              gboolean     isDone,
              const char * fmt,
              ... )
{
    struct idle_data * data = tr_new0( struct idle_data, 1 );
    va_list            args;

    data->core = core;
    data->isDone = isDone;
    va_start( args, fmt );
    g_vasprintf( &data->str, fmt, args );
    va_end( args );

    tr_inf( "%s", data->str );
    g_idle_add( emitProgressIdle, data );
}

/***
****
***/

static size_t
writeFunc( void * ptr,
           size_t size,
           size_t nmemb,
           void * fd )
{
    const size_t byteCount = size * nmemb;

    return write( *(int*)fd, ptr, byteCount );
}

static gpointer
blocklistThreadFunc( gpointer gcore )
{
    TrCore *     core = TR_CORE( gcore );
    const char * url =
        "http://download.m0k.org/transmission/files/level1.gz";
    gboolean     ok = TRUE;
    char *       filename = NULL;
    char *       filename2 = NULL;
    int          fd;
    int          rules;

    emitProgress( core, FALSE, _( "Retrieving blocklist..." ) );

    if( ok )
    {
        GError * err = NULL;
        fd = g_file_open_tmp( "transmission-blockfile-XXXXXX", &filename,
                              &err );
        if( err )
        {
            emitProgress( core, TRUE, _(
                              "Unable to get blocklist: %s" ), err->message );
            g_clear_error( &err );
            ok = FALSE;
        }
    }

    if( ok )
    {
        long verbose = getenv( "TR_CURL_VERBOSE" ) == NULL ? 0L : 1L;

        CURL * curl = curl_easy_init( );
        curl_easy_setopt( curl, CURLOPT_URL, url );
        curl_easy_setopt( curl, CURLOPT_ENCODING, "deflate" );
        curl_easy_setopt( curl, CURLOPT_USERAGENT, "Transmission/"
                                                   LONG_VERSION_STRING );
        curl_easy_setopt( curl, CURLOPT_VERBOSE, verbose );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeFunc );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, &fd );
        curl_easy_setopt( curl, CURLOPT_NOPROGRESS, 1 );
        ok = !curl_easy_perform( curl );
        curl_easy_cleanup( curl );
        close( fd );
    }

    if( !ok )
    {
        emitProgress( core, TRUE, _( "Unable to get blocklist." ) );
    }

    if( ok )
    {
        char * cmd;
        emitProgress( core, FALSE, _( "Uncompressing blocklist..." ) );
        filename2 = g_strdup_printf( "%s.txt", filename );
        cmd = g_strdup_printf( "zcat %s > %s ", filename, filename2 );
        tr_dbg( "%s", cmd );
        (void) system( cmd );
        g_free( cmd );
    }

    if( ok )
    {
        emitProgress( core, FALSE, _( "Parsing blocklist..." ) );
        rules = tr_blocklistSetContent( tr_core_session( core ), filename2 );
    }

    if( ok )
    {
        emitProgress( core, TRUE, _(
                          "Blocklist updated with %'d entries" ), rules );
        pref_int_set( BLOCKLIST_DATE, time( NULL ) );
    }

    g_free( filename2 );
    g_free( filename );
    return NULL;
}

/***
****
***/

void
gtr_blocklist_update( TrCore * core )
{
    g_thread_create( blocklistThreadFunc, core, TRUE, NULL );
}

void
gtr_blocklist_maybe_autoupdate( TrCore * core )
{
    if( pref_flag_get( PREF_KEY_BLOCKLIST_UPDATES_ENABLED )
      && ( time( NULL ) - pref_int_get( BLOCKLIST_DATE ) >
          ( 60 * 60 * 24 * 7 ) ) )
        gtr_blocklist_update( core );
}

