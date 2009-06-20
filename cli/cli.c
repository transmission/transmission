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
#include <libtransmission/makemeta.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h> /* tr_wait */
#include <libtransmission/version.h>
#include <libtransmission/web.h> /* tr_webRun */

#define LINEWIDTH 80
#define MY_NAME "transmission-cli"

static tr_bool showInfo         = 0;
static tr_bool showScrape       = 0;
static tr_bool isPrivate        = 0;
static tr_bool verify           = 0;
static sig_atomic_t gotsig           = 0;
static sig_atomic_t manualUpdate     = 0;

static const char * torrentPath  = NULL;
static const char * finishCall   = NULL;
static const char * sourceFile   = NULL;
static const char * comment      = NULL;

#define MAX_ANNOUNCE 128
static tr_tracker_info announce[MAX_ANNOUNCE];
static int announceCount = 0;

static const struct tr_option options[] =
{
    { 'a', "announce",             "Set the new torrent's announce URL", "a",  1, "<url>"     },
    { 'b', "blocklist",            "Enable peer blocklists", "b",  0, NULL        },
    { 'B', "no-blocklist",         "Disable peer blocklists", "B",  0, NULL        },
    { 'c', "comment",              "Set the new torrent's comment", "c",  1, "<comment>" },
    { 'd', "downlimit",            "Set max download speed in KB/s", "d",  1, "<speed>"   },
    { 'D', "no-downlimit",         "Don't limit the download speed", "D",  0, NULL        },
    { 910, "encryption-required",  "Encrypt all peer connections", "er", 0, NULL        },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", 0, NULL        },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", 0, NULL        },
    { 'f', "finish",               "Run a script when the torrent finishes", "f", 1, "<script>" },
    { 'g', "config-dir",           "Where to find configuration files", "g", 1, "<path>" },
    { 'i', "info",                 "Show torrent details and exit", "i",  0, NULL        },
    { 'm', "portmap",              "Enable portmapping via NAT-PMP or UPnP", "m",  0, NULL        },
    { 'M', "no-portmap",           "Disable portmapping", "M",  0, NULL        },
    { 'n', "new",                  "Create a new torrent", "n", 1, "<source>" },
    { 'p', "port", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", 1, "<port>" },
    { 'r', "private",              "Set the new torrent's 'private' flag", "r",  0, NULL        },
    { 's', "scrape",               "Scrape the torrent and exit", "s",  0, NULL        },
    { 't', "tos", "Peer socket TOS (0 to 255, default=" TR_DEFAULT_PEER_SOCKET_TOS_STR ")", "t", 1, "<tos>" },
    { 'u', "uplimit",              "Set max upload speed in KB/s", "u",  1, "<speed>"   },
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
           "Usage: " MY_NAME " [options] <torrent-filename>";
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

static int
is_rfc2396_alnum( char ch )
{
    return ( '0' <= ch && ch <= '9' )
           || ( 'A' <= ch && ch <= 'Z' )
           || ( 'a' <= ch && ch <= 'z' );
}

static void
escape( char *          out,
        const uint8_t * in,
        int             in_len )                     /* rfc2396 */
{
    const uint8_t *end = in + in_len;

    while( in != end )
        if( is_rfc2396_alnum( *in ) )
            *out++ = (char) *in++;
        else
            out += tr_snprintf( out, 4, "%%%02X", (unsigned int)*in++ );

    *out = '\0';
}

static void
torrentCompletenessChanged( tr_torrent       * torrent       UNUSED,
                            tr_completeness    completeness  UNUSED,
                            void             * user_data     UNUSED )
{
    system( finishCall );
}

static int leftToScrape = 0;

static void
scrapeDoneFunc( tr_session   * session UNUSED,
                long           response_code,
                const void   * response,
                size_t         response_byte_count,
                void         * host )
{
    tr_benc top, *files;

    if( !tr_bencLoad( response, response_byte_count, &top, NULL )
      && tr_bencDictFindDict( &top, "files", &files )
      && files->val.l.count >= 2 )
    {
        int64_t   complete = -1, incomplete = -1, downloaded = -1;
        tr_benc * hash = &files->val.l.vals[1];
        tr_bencDictFindInt( hash, "complete", &complete );
        tr_bencDictFindInt( hash, "incomplete", &incomplete );
        tr_bencDictFindInt( hash, "downloaded", &downloaded );
        printf( "%4d seeders, %4d leechers, %5d downloads at %s\n",
                (int)complete, (int)incomplete, (int)downloaded,
                (char*)host );
        tr_bencFree( &top );
    }
    else
        fprintf( stderr, "Unable to parse response (http code %lu) at %s",
                 response_code,
                 (char*)host );

    --leftToScrape;

    tr_free( host );
}

static void
dumpInfo( FILE *          out,
          const tr_info * inf )
{
    int             i;
    int             prevTier = -1;
    tr_file_index_t ff;

    fprintf( out, "hash:\t" );
    for( i = 0; i < SHA_DIGEST_LENGTH; ++i )
        fprintf( out, "%02x", inf->hash[i] );
    fprintf( out, "\n" );

    fprintf( out, "name:\t%s\n", inf->name );

    for( i = 0; i < inf->trackerCount; ++i )
    {
        if( prevTier != inf->trackers[i].tier )
        {
            prevTier = inf->trackers[i].tier;
            fprintf( out, "\ntracker tier #%d:\n", ( prevTier + 1 ) );
        }
        fprintf( out, "\tannounce:\t%s\n", inf->trackers[i].announce );
    }

    fprintf( out, "size:\t%" PRIu64 " (%" PRIu64 " * %d + %" PRIu64 ")\n",
             inf->totalSize, inf->totalSize / inf->pieceSize,
             inf->pieceSize, inf->totalSize % inf->pieceSize );

    if( inf->comment && *inf->comment )
        fprintf( out, "comment:\t%s\n", inf->comment );
    if( inf->creator && *inf->creator )
        fprintf( out, "creator:\t%s\n", inf->creator );
    if( inf->isPrivate )
        fprintf( out, "private flag set\n" );

    fprintf( out, "file(s):\n" );
    for( ff = 0; ff < inf->fileCount; ++ff )
        fprintf( out, "\t%s (%" PRIu64 ")\n", inf->files[ff].name,
                 inf->files[ff].length );
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
                     100 * st->recheckProgress,
                     100.0 * st->percentDone );
    }
    else if( st->activity & TR_STATUS_DOWNLOAD )
    {
        char ratioStr[80];
        tr_strlratio( ratioStr, st->ratio, sizeof( ratioStr ) );
        tr_snprintf(
            buf, buflen,
            "Progress: %.1f%%, dl from %d of %d peers (%.0f KB/s), "
            "ul to %d (%.0f KB/s) [%s]",
            st->percentDone * 100.0,
            st->peersSendingToUs,
            st->peersConnected,
            st->pieceDownloadSpeed,
            st->peersGettingFromUs,
            st->pieceUploadSpeed,
            ratioStr );
    }
    else if( st->activity & TR_STATUS_SEED )
    {
        char ratioStr[80];
        tr_strlratio( ratioStr, st->ratio, sizeof( ratioStr ) );
        tr_snprintf(
            buf, buflen,
            "Seeding, uploading to %d of %d peer(s), %.0f KB/s [%s]",
            st->peersGettingFromUs, st->peersConnected,
            st->pieceUploadSpeed, ratioStr );
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
main( int     argc,
      char ** argv )
{
    int           error;
    tr_session  * h;
    tr_ctor     * ctor;
    tr_torrent  * tor = NULL;
    tr_benc       settings;
    const char  * configDir;
    tr_bool       haveSource; 
    tr_bool       haveAnnounce; 

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

    /* don't bind the port if we're just running the CLI
       to get metainfo or to create a torrent */
    if( showInfo || showScrape || ( sourceFile != NULL ) )
        tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_PORT, -1 );

    h = tr_sessionInit( "cli", configDir, FALSE, &settings );

    haveSource = sourceFile && *sourceFile;
    haveAnnounce = announceCount > 0;

    if( haveSource && !haveAnnounce )
        fprintf( stderr, "Did you mean to create a torrent without a tracker's announce URL?\n" );

    if( haveSource ) /* creating a torrent */
    {
        int err;
        tr_metainfo_builder * b;
        fprintf( stderr, "creating torrent \"%s\"\n", torrentPath );

        b = tr_metaInfoBuilderCreate( sourceFile );
        tr_makeMetaInfo( b, torrentPath, announce, announceCount, comment, isPrivate );
        while( !b->isDone )
        {
            tr_wait( 1000 );
            printf( "." );
        }

        err = b->result;
        tr_metaInfoBuilderFree( b );
        return err;
    }

    ctor = tr_ctorNew( h );
    tr_ctorSetMetainfoFromFile( ctor, torrentPath );
    tr_ctorSetPaused( ctor, TR_FORCE, showScrape );

    if( showScrape )
    {
        tr_info info;

        if( !tr_torrentParse( ctor, &info ) )
        {
            int          i;
            const time_t start = time( NULL );
            for( i = 0; i < info.trackerCount; ++i )
            {
                if( info.trackers[i].scrape )
                {
                    const char * scrape = info.trackers[i].scrape;
                    char         escaped[SHA_DIGEST_LENGTH * 3 + 1];
                    char *       url, *host;
                    escape( escaped, info.hash, SHA_DIGEST_LENGTH );
                    url = tr_strdup_printf( "%s%cinfo_hash=%s",
                                            scrape,
                                            strchr( scrape,
                                                    '?' ) ? '&' : '?',
                                            escaped );
                    tr_httpParseURL( scrape, -1, &host, NULL, NULL );
                    ++leftToScrape;
                    tr_webRun( h, url, NULL, scrapeDoneFunc, host );
                    tr_free( url );
                }
            }

            fprintf( stderr, "scraping %d trackers:\n", leftToScrape );

            while( leftToScrape > 0 && ( ( time( NULL ) - start ) < 20 ) )
                tr_wait( 250 );
        }
        goto cleanup;
    }

    if( showInfo )
    {
        tr_info info;

        if( !tr_torrentParse( ctor, &info ) )
        {
            dumpInfo( stdout, &info );
            tr_metainfoFree( &info );
        }

        tr_ctorFree( ctor );
        goto cleanup;
    }

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
    tr_torrentSetCompletenessCallback( tor, torrentCompletenessChanged, NULL );
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

        tr_wait( 200 );

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
        if( st->error )
            fprintf( stderr, "\n%s\n", st->errorString );
    }

cleanup:

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
            case 'a': if( announceCount + 1 < MAX_ANNOUNCE ) {
                          announce[announceCount].tier = announceCount;
                          announce[announceCount].announce = (char*) optarg;
                          ++announceCount;
                      }
                      break;
            case 'b': tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED, TRUE );
                      break;
            case 'B': tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED, FALSE );
                      break;
            case 'c': comment = optarg;
                      break;
            case 'd': tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED, atoi( optarg ) );
                      tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED, TRUE );
                      break;
            case 'D': tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED, FALSE );
                      break;
            case 'f': finishCall = optarg;
                      break;
            case 'g': /* handled above */
                      break;
            case 'i': showInfo = 1;
                      break;
            case 'm': tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING, TRUE );
                      break;
            case 'M': tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING, FALSE );
                      break;
            case 'n': sourceFile = optarg; break;
            case 'p': tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT, atoi( optarg ) );
                      break;
            case 'r': isPrivate = 1;
                      break;
            case 's': showScrape = 1;
                      break;
            case 't': tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_SOCKET_TOS, atoi( optarg ) );
                      break;
            case 'u': tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED, atoi( optarg ) );
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

