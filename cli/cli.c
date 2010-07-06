/******************************************************************************
 * $Id$
 *
 * Copyright (c) 2005-2006 Transmission authors and contributors
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h> /* tr_wait_msec */
#include <libtransmission/version.h>
#include <libtransmission/web.h> /* tr_webRun */

/***
****
***/

#define MEM_K 1024
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1000
#define DISK_B_STR "B"
#define DISK_K_STR "kB"
#define DISK_M_STR "MB"
#define DISK_G_STR "GB"
#define DISK_T_STR "TB"

#define SPEED_K 1000
#define SPEED_B_STR "B/s"
#define SPEED_K_STR "kB/s"
#define SPEED_M_STR "MB/s"
#define SPEED_G_STR "GB/s"
#define SPEED_T_STR "TB/s"

/***
****
***/

#define LINEWIDTH 80
#define MY_NAME "transmissioncli"

static tr_bool verify                = 0;
static sig_atomic_t gotsig           = 0;
static sig_atomic_t manualUpdate     = 0;

static const char * torrentPath  = NULL;

static const struct tr_option options[] =
{
    { 'b', "blocklist",            "Enable peer blocklists", "b",  0, NULL },
    { 'B', "no-blocklist",         "Disable peer blocklists", "B",  0, NULL },
    { 'd', "downlimit",            "Set max download speed in "SPEED_K_STR, "d",  1, "<speed>" },
    { 'D', "no-downlimit",         "Don't limit the download speed", "D",  0, NULL },
    { 910, "encryption-required",  "Encrypt all peer connections", "er", 0, NULL },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", 0, NULL },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", 0, NULL },
    { 'f', "finish",               "Run a script when the torrent finishes", "f", 1, "<script>" },
    { 'g', "config-dir",           "Where to find configuration files", "g", 1, "<path>" },
    { 'm', "portmap",              "Enable portmapping via NAT-PMP or UPnP", "m",  0, NULL },
    { 'M', "no-portmap",           "Disable portmapping", "M",  0, NULL },
    { 'p', "port", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", 1, "<port>" },
    { 't', "tos", "Peer socket TOS (0 to 255, default=" TR_DEFAULT_PEER_SOCKET_TOS_STR ")", "t", 1, "<tos>" },
    { 'u', "uplimit",              "Set max upload speed in "SPEED_K_STR, "u",  1, "<speed>"   },
    { 'U', "no-uplimit",           "Don't limit the upload speed", "U",  0, NULL        },
    { 'v', "verify",               "Verify the specified torrent", "v",  0, NULL        },
    { 'w', "download-dir",         "Where to save downloaded data", "w",  1, "<path>"    },
    { 0, NULL, NULL, NULL, 0, NULL }
};

static const char *
getUsage( void )
{
    return "A fast and easy BitTorrent client\n"
           "\n"
           "Usage: " MY_NAME " [options] <file|url|magnet>";
}

static int parseCommandLine( tr_benc*, int argc, const char ** argv );

static void         sigHandler( int signal );

static char*
tr_strlratio( char * buf,
              double ratio,
              size_t buflen )
{
    if( (int)ratio == TR_RATIO_NA )
        tr_strlcpy( buf, _( "None" ), buflen );
    else if( (int)ratio == TR_RATIO_INF )
        tr_strlcpy( buf, "Inf", buflen );
    else if( ratio < 10.0 )
        tr_snprintf( buf, buflen, "%.2f", ratio );
    else if( ratio < 100.0 )
        tr_snprintf( buf, buflen, "%.1f", ratio );
    else
        tr_snprintf( buf, buflen, "%.0f", ratio );
    return buf;
}

static tr_bool waitingOnWeb;

static void
onTorrentFileDownloaded( tr_session   * session UNUSED,
                         long           response_code UNUSED,
                         const void   * response,
                         size_t         response_byte_count,
                         void         * ctor )
{
    tr_ctorSetMetainfo( ctor, response, response_byte_count );
    waitingOnWeb = FALSE;
}

static void
getStatusStr( const tr_stat * st,
              char *          buf,
              size_t          buflen )
{
    if( st->activity & TR_STATUS_CHECK_WAIT )
    {
        tr_snprintf( buf, buflen, "Waiting to verify local files" );
    }
    else if( st->activity & TR_STATUS_CHECK )
    {
        tr_snprintf( buf, buflen,
                     "Verifying local files (%.2f%%, %.2f%% valid)",
                     tr_truncd( 100 * st->recheckProgress, 2 ),
                     tr_truncd( 100 * st->percentDone, 2 ) );
    }
    else if( st->activity & TR_STATUS_DOWNLOAD )
    {
        char upStr[80];
        char dnStr[80];
        char ratioStr[80];

        tr_formatter_speed_KBps( upStr, st->pieceUploadSpeed_KBps, sizeof( upStr ) );
        tr_formatter_speed_KBps( dnStr, st->pieceDownloadSpeed_KBps, sizeof( dnStr ) );
        tr_strlratio( ratioStr, st->ratio, sizeof( ratioStr ) );

        tr_snprintf( buf, buflen,
            "Progress: %.1f%%, "
            "dl from %d of %d peers (%s), "
            "ul to %d (%s) "
            "[%s]",
            tr_truncd( 100 * st->percentDone, 1 ),
            st->peersSendingToUs, st->peersConnected, upStr,
            st->peersGettingFromUs, dnStr,
            ratioStr );
    }
    else if( st->activity & TR_STATUS_SEED )
    {
        char upStr[80];
        char ratioStr[80];

        tr_formatter_speed_KBps( upStr, st->pieceUploadSpeed_KBps, sizeof( upStr ) );
        tr_strlratio( ratioStr, st->ratio, sizeof( ratioStr ) );

        tr_snprintf( buf, buflen,
                     "Seeding, uploading to %d of %d peer(s), %s [%s]",
                     st->peersGettingFromUs, st->peersConnected, upStr, ratioStr );
    }
    else *buf = '\0';
}

static const char*
getConfigDir( int argc, const char ** argv )
{
    int c;
    const char * configDir = NULL;
    const char * optarg;
    const int ind = tr_optind;

    while(( c = tr_getopt( getUsage( ), argc, argv, options, &optarg ))) {
        if( c == 'g' ) {
            configDir = optarg;
            break;
        }
    }

    tr_optind = ind;

    if( configDir == NULL )
        configDir = tr_getDefaultConfigDir( MY_NAME );

    return configDir;
}

int
main( int argc, char ** argv )
{
    int           error;
    tr_session  * h;
    tr_ctor     * ctor;
    tr_torrent  * tor = NULL;
    tr_benc       settings;
    const char  * configDir;
    uint8_t     * fileContents;
    size_t        fileLength;

    tr_formatter_mem_init( MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR );
    tr_formatter_size_init( DISK_K,DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR );
    tr_formatter_speed_init( SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR );

    printf( "Transmission %s - http://www.transmissionbt.com/\n",
            LONG_VERSION_STRING );

    /* user needs to pass in at least one argument */
    if( argc < 2 ) {
        tr_getopt_usage( MY_NAME, getUsage( ), options );
        return EXIT_FAILURE;
    }

    /* load the defaults from config file + libtransmission defaults */
    tr_bencInitDict( &settings, 0 );
    configDir = getConfigDir( argc, (const char**)argv );
    tr_sessionLoadSettings( &settings, configDir, MY_NAME );

    /* the command line overrides defaults */
    if( parseCommandLine( &settings, argc, (const char**)argv ) )
        return EXIT_FAILURE;

    /* Check the options for validity */
    if( !torrentPath ) {
        fprintf( stderr, "No torrent specified!\n" );
        return EXIT_FAILURE;
    }

    h = tr_sessionInit( "cli", configDir, FALSE, &settings );

    ctor = tr_ctorNew( h );

    fileContents = tr_loadFile( torrentPath, &fileLength );
    tr_ctorSetPaused( ctor, TR_FORCE, FALSE );
    if( fileContents != NULL ) {
        tr_ctorSetMetainfo( ctor, fileContents, fileLength );
    } else if( !memcmp( torrentPath, "magnet:?", 8 ) ) {
        tr_ctorSetMetainfoFromMagnetLink( ctor, torrentPath );
    } else if( !memcmp( torrentPath, "http", 4 ) ) {
        tr_webRun( h, torrentPath, NULL, onTorrentFileDownloaded, ctor );
        waitingOnWeb = TRUE;
        while( waitingOnWeb ) tr_wait_msec( 1000 );
    }
    tr_free( fileContents );

    tor = tr_torrentNew( ctor, &error );
    tr_ctorFree( ctor );
    if( !tor )
    {
        fprintf( stderr, "Failed opening torrent file `%s'\n", torrentPath );
        tr_sessionClose( h );
        return EXIT_FAILURE;
    }

    signal( SIGINT, sigHandler );
#ifndef WIN32
    signal( SIGHUP, sigHandler );
#endif
    tr_torrentStart( tor );

    if( verify )
    {
        verify = 0;
        tr_torrentVerify( tor );
    }

    for( ; ; )
    {
        char            line[LINEWIDTH];
        const tr_stat * st;
        const char * messageName[] = { NULL, "Tracker gave a warning:",
                                             "Tracker gave an error:",
                                             "Error:" };

        tr_wait_msec( 200 );

        if( gotsig )
        {
            gotsig = 0;
            printf( "\nStopping torrent...\n" );
            tr_torrentStop( tor );
        }

        if( manualUpdate )
        {
            manualUpdate = 0;
            if( !tr_torrentCanManualUpdate( tor ) )
                fprintf(
                    stderr,
                    "\nReceived SIGHUP, but can't send a manual update now\n" );
            else
            {
                fprintf( stderr,
                         "\nReceived SIGHUP: manual update scheduled\n" );
                tr_torrentManualUpdate( tor );
            }
        }

        st = tr_torrentStat( tor );
        if( st->activity & TR_STATUS_STOPPED )
            break;

        getStatusStr( st, line, sizeof( line ) );
        printf( "\r%-*s", LINEWIDTH, line );

        if( messageName[st->error] )
            fprintf( stderr, "\n%s: %s\n", messageName[st->error], st->errorString );
    }

    tr_sessionSaveSettings( h, configDir, &settings );

    printf( "\n" );
    tr_bencFree( &settings );
    tr_sessionClose( h );
    return EXIT_SUCCESS;
}

/***
****
****
****
***/

static int
parseCommandLine( tr_benc * d, int argc, const char ** argv )
{
    int          c;
    const char * optarg;

    while(( c = tr_getopt( getUsage( ), argc, argv, options, &optarg )))
    {
        switch( c )
        {
            case 'b': tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED, TRUE );
                      break;
            case 'B': tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED, FALSE );
                      break;
            case 'd': tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED_KBps, atoi( optarg ) );
                      tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED, TRUE );
                      break;
            case 'D': tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED, FALSE );
                      break;
            case 'f': tr_bencDictAddStr( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, optarg );
                      tr_bencDictAddBool( d, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, TRUE );
                      break;
            case 'g': /* handled above */
                      break;
            case 'm': tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING, TRUE );
                      break;
            case 'M': tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING, FALSE );
                      break;
            case 'p': tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT, atoi( optarg ) );
                      break;
            case 't': tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_SOCKET_TOS, atoi( optarg ) );
                      break;
            case 'u': tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED_KBps, atoi( optarg ) );
                      tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED, TRUE );
                      break;
            case 'U': tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED, FALSE );
                      break;
            case 'v': verify = 1;
                      break;
            case 'w': tr_bencDictAddStr( d, TR_PREFS_KEY_DOWNLOAD_DIR, optarg );
                      break;
            case 910: tr_bencDictAddInt( d, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_REQUIRED );
                      break;
            case 911: tr_bencDictAddInt( d, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_PREFERRED );
                      break;
            case 912: tr_bencDictAddInt( d, TR_PREFS_KEY_ENCRYPTION, TR_CLEAR_PREFERRED );
                      break;
            case TR_OPT_UNK:
                      torrentPath = optarg;
                      break;
            default: return 1;
        }
    }

    return 0;
}

static void
sigHandler( int signal )
{
    switch( signal )
    {
        case SIGINT:
            gotsig = 1; break;

#ifndef WIN32
        case SIGHUP:
            manualUpdate = 1; break;

#endif
        default:
            break;
    }
}

