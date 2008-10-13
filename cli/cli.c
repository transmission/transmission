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
#include <libtransmission/metainfo.h> /* tr_metainfoFree */
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h> /* tr_wait */
#include <libtransmission/web.h> /* tr_webRun */

#define LINEWIDTH 80
#define MY_NAME "transmission-cli"

static int          showInfo         = 0;
static int          showScrape       = 0;
static int          isPrivate        = 0;
static int          verboseLevel     = 0;
static int          encryptionMode   = TR_ENCRYPTION_PREFERRED;
static int          peerPort         = TR_DEFAULT_PORT;
static int          peerSocketTOS    = TR_DEFAULT_PEER_SOCKET_TOS;
static int          blocklistEnabled = TR_DEFAULT_BLOCKLIST_ENABLED;
static int          uploadLimit      = 20;
static int          downloadLimit    = -1;
static int          natTraversal     = TR_DEFAULT_PORT_FORWARDING_ENABLED;
static int          verify           = 0;
static sig_atomic_t gotsig           = 0;
static sig_atomic_t manualUpdate     = 0;

static const char * torrentPath  = NULL;
static const char * downloadDir  = NULL;
static const char * finishCall   = NULL;
static const char * announce     = NULL;
static const char * configdir    = NULL;
static const char * sourceFile   = NULL;
static const char * comment      = NULL;

static const struct tr_option options[] =
{
    { 'a', "announce",             "Set the new torrent's announce URL",
      "a",  1, "<url>"     },
    { 'b', "blocklist",            "Enable peer blocklists",
      "b",  0, NULL        },
    { 'B', "no-blocklist",         "Disable peer blocklists",
      "B",  0, NULL        },
    { 'c', "comment",              "Set the new torrent's comment",
      "c",  1, "<comment>" },
    { 'd', "downlimit",            "Set max download speed in KB/s",
      "d",  1, "<speed>"   },
    { 'D', "no-downlimit",         "Don't limit the download speed",
      "D",  0, NULL        },
    { 910, "encryption-required",  "Encrypt all peer connections",
      "er", 0, NULL        },
    { 911, "encryption-preferred", "Prefer encrypted peer connections",
      "ep", 0, NULL        },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections",
      "et", 0, NULL        },
    { 'f', "finish",               "Run a script when the torrent finishes",
      "f", 1, "<script>" },
    { 'g', "config-dir",           "Where to find configuration files",
      "g", 1, "<path>" },
    { 'i', "info",                 "Show torrent details and exit",
      "i",  0, NULL        },
    { 'm', "portmap",              "Enable portmapping via NAT-PMP or UPnP",
      "m",  0, NULL        },
    { 'M', "no-portmap",           "Disable portmapping",
      "M",  0, NULL        },
    { 'n', "new",                  "Create a new torrent",
      "n", 1, "<source>" },
    { 'p', "port",
      "Port for incoming peers (Default: " TR_DEFAULT_PORT_STR ")",
      "p", 1, "<port>" },
    { 'r', "private",              "Set the new torrent's 'private' flag",
      "r",  0, NULL        },
    { 's', "scrape",               "Scrape the torrent and exit",
      "s",  0, NULL        },
    { 't', "tos",
      "Peer socket TOS (0 to 255, default=" TR_DEFAULT_PEER_SOCKET_TOS_STR
      ")",
      "t", 1, "<tos>" },
    { 'u', "uplimit",              "Set max upload speed in KB/s",
      "u",  1, "<speed>"   },
    { 'U', "no-uplimit",           "Don't limit the upload speed",
      "U",  0, NULL        },
    { 'v', "verify",               "Verify the specified torrent",
      "v",  0, NULL        },
    { 'w', "download-dir",         "Where to save downloaded data",
      "w",  1, "<path>"    },
    {   0, NULL,                   NULL,
        NULL, 0, NULL        }
};

static const char *
getUsage( void )
{
    return "A fast and easy BitTorrent client\n"
           "\n"
           "Usage: " MY_NAME " [options] <torrent-filename>";
}

static int          parseCommandLine( int           argc,
                                      const char ** argv );

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
torrentStateChanged( tr_torrent   * torrent   UNUSED,
                     cp_status_t    status    UNUSED,
                     void         * user_data UNUSED )
{
    system( finishCall );
}

static int leftToScrape = 0;

static void
scrapeDoneFunc( struct tr_handle    * session UNUSED,
                long                          response_code,
                const void *                  response,
                size_t                        response_byte_count,
                void *                        host )
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
    if( st->status & TR_STATUS_CHECK_WAIT )
    {
        tr_snprintf( buf, buflen, "Waiting to verify local files" );
    }
    else if( st->status & TR_STATUS_CHECK )
    {
        tr_snprintf( buf, buflen,
                     "Verifying local files (%.2f%%, %.2f%% valid)",
                     100 * st->recheckProgress,
                     100.0 * st->percentDone );
    }
    else if( st->status & TR_STATUS_DOWNLOAD )
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
            st->rateDownload,
            st->peersGettingFromUs,
            st->rateUpload,
            ratioStr );
    }
    else if( st->status & TR_STATUS_SEED )
    {
        char ratioStr[80];
        tr_strlratio( ratioStr, st->ratio, sizeof( ratioStr ) );
        tr_snprintf(
            buf, buflen,
            "Seeding, uploading to %d of %d peer(s), %.0f KB/s [%s]",
            st->peersGettingFromUs, st->peersConnected,
            st->rateUpload, ratioStr );
    }
    else *buf = '\0';
}

int
main( int     argc,
      char ** argv )
{
    int          error;
    tr_handle *  h;
    tr_ctor *    ctor;
    tr_torrent * tor = NULL;
    char         cwd[MAX_PATH_LENGTH];

    printf( "Transmission %s - http://www.transmissionbt.com/\n",
            LONG_VERSION_STRING );

    /* user needs to pass in at least one argument */
    if( argc < 2 ) {
        tr_getopt_usage( MY_NAME, getUsage( ), options );
        return EXIT_FAILURE;
    }

    /* Get options */
    if( parseCommandLine( argc, (const char**)argv ) )
        return EXIT_FAILURE;

    /* Check the options for validity */
    if( !torrentPath )
    {
        fprintf( stderr, "No torrent specified!\n" );
        return EXIT_FAILURE;
    }
    if( peerPort < 1 || peerPort > 65535 )
    {
        fprintf( stderr, "Error: Port must between 1 and 65535; got %d\n",
                 peerPort );
        return EXIT_FAILURE;
    }
    if( peerSocketTOS < 0 || peerSocketTOS > 255 )
    {
        fprintf( stderr, "Error: value must between 0 and 255; got %d\n",
                 peerSocketTOS );
        return EXIT_FAILURE;
    }

    /* don't bind the port if we're just running the CLI
     * to get metainfo or to create a torrent */
    if( showInfo || showScrape || ( sourceFile != NULL ) )
        peerPort = -1;

    if( configdir == NULL )
        configdir = tr_getDefaultConfigDir( );

    /* if no download directory specified, use cwd instead */
    if( !downloadDir )
    {
        tr_getcwd( cwd, sizeof( cwd ) );
        downloadDir = cwd;
    }


    /* Initialize libtransmission */
    h = tr_sessionInitFull(
        configdir,
        "cli",                            /* tag */
        downloadDir,                       /* where to download torrents */
        TR_DEFAULT_PEX_ENABLED,
        natTraversal,                      /* nat enabled */
        peerPort,
        encryptionMode,
        TR_DEFAULT_LAZY_BITFIELD_ENABLED,
        uploadLimit >= 0,
        uploadLimit,
        downloadLimit >= 0,
        downloadLimit,
        TR_DEFAULT_GLOBAL_PEER_LIMIT,
        verboseLevel + 1,                  /* messageLevel */
        0,                                 /* is message queueing enabled? */
        blocklistEnabled,
        peerSocketTOS,
        TR_DEFAULT_RPC_ENABLED,
        TR_DEFAULT_RPC_PORT,
        TR_DEFAULT_RPC_WHITELIST_ENABLED,
        TR_DEFAULT_RPC_WHITELIST,
        FALSE, "fnord", "potzrebie",
        TR_DEFAULT_PROXY_ENABLED,
        TR_DEFAULT_PROXY,
        TR_DEFAULT_PROXY_PORT,
        TR_DEFAULT_PROXY_TYPE,
        TR_DEFAULT_PROXY_AUTH_ENABLED,
        TR_DEFAULT_PROXY_USERNAME,
        TR_DEFAULT_PROXY_PASSWORD );

    if( sourceFile && *sourceFile ) /* creating a torrent */
    {
        int                   err;
        tr_metainfo_builder * b = tr_metaInfoBuilderCreate( h, sourceFile );
        tr_tracker_info       ti;
        ti.tier = 0;
        ti.announce = (char*) announce;
        tr_makeMetaInfo( b, torrentPath, &ti, 1, comment, isPrivate );
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
    tr_ctorSetDownloadDir( ctor, TR_FORCE, downloadDir );

    if( showScrape )
    {
        tr_info info;

        if( !tr_torrentParse( h, ctor, &info ) )
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
                    tr_free( host );
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

        if( !tr_torrentParse( h, ctor, &info ) )
        {
            dumpInfo( stdout, &info );
            tr_metainfoFree( &info );
        }

        tr_ctorFree( ctor );
        goto cleanup;
    }

    tor = tr_torrentNew( h, ctor, &error );
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
    tr_torrentSetStatusCallback( tor, torrentStateChanged, NULL );
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
        if( st->status & TR_STATUS_STOPPED )
            break;

        getStatusStr( st, line, sizeof( line ) );
        printf( "\r%-*s", LINEWIDTH, line );
        if( st->error )
            fprintf( stderr, "\n%s\n", st->errorString );
    }

cleanup:
    printf( "\n" );
    tr_sessionClose( h );
    return EXIT_SUCCESS;
}

/***
****
****
****
***/

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage( ), options );
    exit( 0 );
}

static int
numarg( const char * arg )
{
    char *     end = NULL;
    const long num = strtol( arg, &end, 10 );

    if( *end )
    {
        fprintf( stderr, "Not a number: \"%s\"\n", arg );
        showUsage( );
    }
    return num;
}

static int
parseCommandLine( int           argc,
                  const char ** argv )
{
    int          c;
    const char * optarg;

    while( ( c = tr_getopt( getUsage( ), argc, argv, options, &optarg ) ) )
    {
        switch( c )
        {
            case 'a':
                announce = optarg; break;

            case 'b':
                blocklistEnabled = 1; break;

            case 'B':
                blocklistEnabled = 0; break;

            case 'c':
                comment = optarg; break;

            case 'd':
                downloadLimit = numarg( optarg ); break;

            case 'D':
                downloadLimit = -1; break;

            case 'f':
                finishCall = optarg; break;

            case 'g':
                configdir = optarg; break;

            case 'i':
                showInfo = 1; break;

            case 'm':
                natTraversal = 1; break;

            case 'M':
                natTraversal = 0; break;

            case 'n':
                sourceFile = optarg; break;

            case 'p':
                peerPort = numarg( optarg ); break;

            case 'r':
                isPrivate = 1; break;

            case 's':
                showScrape = 1; break;

            case 't':
                peerSocketTOS = numarg( optarg ); break;

            case 'u':
                uploadLimit = numarg( optarg ); break;

            case 'U':
                uploadLimit = -1; break;

            case 'v':
                verify = 1; break;

            case 'w':
                downloadDir = optarg; break;

            case 910:
                encryptionMode = TR_ENCRYPTION_REQUIRED; break;

            case 911:
                encryptionMode = TR_CLEAR_PREFERRED; break;

            case 912:
                encryptionMode = TR_ENCRYPTION_PREFERRED; break;

            case TR_OPT_UNK:
                torrentPath = optarg; break;

            default:
                return 1;
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

