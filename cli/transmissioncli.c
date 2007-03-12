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
#include <getopt.h>
#include <signal.h>
#include <transmission.h>
#ifdef SYS_BEOS
#include <kernel/OS.h>
#define usleep snooze
#endif

#define USAGE \
"Usage: %s [options] file.torrent [options]\n\n" \
"Options:\n" \
"  -d, --download <int> Maximum download rate (-1 = no limit, default = -1)\n"\
"  -f, --finish <shell script> Command you wish to run on completion\n" \
"  -h, --help           Print this help and exit\n" \
"  -i, --info           Print metainfo and exit\n" \
"  -n  --nat-traversal  Attempt NAT traversal using NAT-PMP or UPnP IGD\n" \
"  -p, --port <int>     Port we should listen on (default = %d)\n" \
"  -s, --scrape         Print counts of seeders/leechers and exit\n" \
"  -u, --upload <int>   Maximum upload rate (-1 = no limit, default = 20)\n" \
"  -v, --verbose <int>  Verbose level (0 to 2, default = 0)\n"

static int           showHelp      = 0;
static int           showInfo      = 0;
static int           showScrape    = 0;
static int           verboseLevel  = 0;
static int           bindPort      = TR_DEFAULT_PORT;
static int           uploadLimit   = 20;
static int           downloadLimit = -1;
static char          * torrentPath = NULL;
static int           natTraversal  = 0;
static sig_atomic_t  gotsig        = 0;
static tr_torrent_t  * tor;

static char          * finishCall   = NULL;

static int  parseCommandLine ( int argc, char ** argv );
static void sigHandler       ( int signal );

char * getStringRatio( float ratio )
{
    static char string[20];

    if( ratio == TR_RATIO_NA )
        return "n/a";
    if( ratio == TR_RATIO_INF )
        return "inf";
    snprintf( string, sizeof string, "%.3f", ratio );
    return string;
}

#define LINEWIDTH 80

int main( int argc, char ** argv )
{
    int i, error;
    tr_handle_t  * h;
    tr_stat_t    * s;
    tr_handle_status_t * hstat;

    printf( "Transmission %s (%d) - http://transmission.m0k.org/\n\n",
            VERSION_STRING, VERSION_REVISION );

    /* Get options */
    if( parseCommandLine( argc, argv ) )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return EXIT_FAILURE;
    }

    if( showHelp )
    {
        printf( USAGE, argv[0], TR_DEFAULT_PORT );
        return EXIT_SUCCESS;
    }

    if( verboseLevel < 0 )
    {
        verboseLevel = 0;
    }
    else if( verboseLevel > 9 )
    {
        verboseLevel = 9;
    }
    if( verboseLevel )
    {
        static char env[11];
        snprintf( env, sizeof env, "TR_DEBUG=%d", verboseLevel );
        putenv( env );
    }

    if( bindPort < 1 || bindPort > 65535 )
    {
        printf( "Invalid port '%d'\n", bindPort );
        return EXIT_FAILURE;
    }

    /* Initialize libtransmission */
    h = tr_init();

    /* Open and parse torrent file */
    if( !( tor = tr_torrentInit( h, torrentPath, 0, &error ) ) )
    {
        printf( "Failed opening torrent file `%s'\n", torrentPath );
        tr_close( h );
        return EXIT_FAILURE;
    }

    if( showInfo )
    {
        tr_info_t * info = tr_torrentInfo( tor );

        s = tr_torrentStat( tor );

        /* Print torrent info (quite à la btshowmetainfo) */
        printf( "hash:     " );
        for( i = 0; i < SHA_DIGEST_LENGTH; i++ )
        {
            printf( "%02x", info->hash[i] );
        }
        printf( "\n" );
        printf( "tracker:  %s:%d\n",
                s->tracker->address, s->tracker->port );
        printf( "announce: %s\n", s->tracker->announce );
        printf( "size:     %"PRIu64" (%"PRIu64" * %d + %"PRIu64")\n",
                info->totalSize, info->totalSize / info->pieceSize,
                info->pieceSize, info->totalSize % info->pieceSize );
        if( info->comment[0] )
        {
            printf( "comment:  %s\n", info->comment );
        }
        if( info->creator[0] )
        {
            printf( "creator:  %s\n", info->creator );
        }
        if( TR_FLAG_PRIVATE & info->flags )
        {
            printf( "private flag set\n" );
        }
        printf( "file(s):\n" );
        for( i = 0; i < info->fileCount; i++ )
        {
            printf( " %s (%"PRIu64")\n", info->files[i].name,
                    info->files[i].length );
        }

        goto cleanup;
    }

    if( showScrape )
    {
        int seeders, leechers, downloaded;

        if( tr_torrentScrape( tor, &seeders, &leechers, &downloaded ) )
        {
            printf( "Scrape failed.\n" );
        }
        else
        {
            printf( "%d seeder(s), %d leecher(s), %d download(s).\n",
                    seeders, leechers, downloaded );
        }

        goto cleanup;
    }

    signal( SIGINT, sigHandler );

    tr_setBindPort( h, bindPort );
    tr_setGlobalUploadLimit( h, uploadLimit );
    tr_setGlobalDownloadLimit( h, downloadLimit );

    tr_natTraversalEnable( h, natTraversal );
    
    tr_torrentSetFolder( tor, "." );
    tr_torrentStart( tor );

    for( ;; )
    {
        char string[LINEWIDTH];
        int  chars = 0;
        int result;

        sleep( 1 );

        if( gotsig )
        {
            gotsig = 0;
            tr_torrentStop( tor );
        }

        s = tr_torrentStat( tor );

        if( s->status & TR_STATUS_PAUSE )
        {
            break;
        }
        else if( s->status & TR_STATUS_CHECK )
        {
            chars = snprintf( string, sizeof string,
                "Checking files... %.2f %%", 100.0 * s->progress );
        }
        else if( s->status & TR_STATUS_DOWNLOAD )
        {
            chars = snprintf( string, sizeof string,
                "Progress: %.2f %%, %d peer%s, dl from %d (%.2f KB/s), "
                "ul to %d (%.2f KB/s) [%s]", 100.0 * s->progress,
                s->peersTotal, ( s->peersTotal == 1 ) ? "" : "s",
                s->peersUploading, s->rateDownload,
                s->peersDownloading, s->rateUpload,
                getStringRatio(s->ratio) );
        }
        else if( s->status & TR_STATUS_SEED )
        {
            chars = snprintf( string, sizeof string,
                "Seeding, uploading to %d of %d peer(s), %.2f KB/s [%s]",
                s->peersDownloading, s->peersTotal,
                s->rateUpload, getStringRatio(s->ratio) );
        }
        else if( s->status & TR_STATUS_STOPPING )
        {
            chars = snprintf( string, sizeof string, "Stopping..." );
        }
        if( ( signed )sizeof string > chars )
        {
            memset( &string[chars], ' ', sizeof string - 1 - chars );
        }
        string[sizeof string - 1] = '\0';
        fprintf( stderr, "\r%s", string );

        if( s->error )
        {
            fprintf( stderr, "\n%s\n", s->errorString );
        }
        else if( verboseLevel > 0 )
        {
            fprintf( stderr, "\n" );
        }
        
        if( tr_getFinished( tor ) )
        {
            result = system(finishCall);
        }
    }
    fprintf( stderr, "\n" );

    /* Try for 5 seconds to delete any port mappings for nat traversal */
    tr_natTraversalEnable( h, 0 );
    for( i = 0; i < 10; i++ )
    {
        hstat = tr_handleStatus( h );
        if( TR_NAT_TRAVERSAL_DISABLED == hstat->natTraversalStatus )
        {
            /* Port mappings were deleted */
            break;
        }
        usleep( 500000 );
    }
    
cleanup:
    tr_torrentClose( h, tor );
    tr_close( h );

    return EXIT_SUCCESS;
}

static int parseCommandLine( int argc, char ** argv )
{
    for( ;; )
    {
        static struct option long_options[] =
          { { "help",     no_argument,       NULL, 'h' },
            { "info",     no_argument,       NULL, 'i' },
            { "scrape",   no_argument,       NULL, 's' },
            { "verbose",  required_argument, NULL, 'v' },
            { "port",     required_argument, NULL, 'p' },
            { "upload",   required_argument, NULL, 'u' },
            { "download", required_argument, NULL, 'd' },
            { "finish",   required_argument, NULL, 'f' },
            { "nat-traversal", no_argument,  NULL, 'n' },
            { 0, 0, 0, 0} };

        int c, optind = 0;
        c = getopt_long( argc, argv, "hisv:p:u:d:f:n", long_options, &optind );
        if( c < 0 )
        {
            break;
        }
        switch( c )
        {
            case 'h':
                showHelp = 1;
                break;
            case 'i':
                showInfo = 1;
                break;
            case 's':
                showScrape = 1;
                break;
            case 'v':
                verboseLevel = atoi( optarg );
                break;
            case 'p':
                bindPort = atoi( optarg );
                break;
            case 'u':
                uploadLimit = atoi( optarg );
                break;
            case 'd':
                downloadLimit = atoi( optarg );
                break;
            case 'f':
                finishCall = optarg;
                break;
            case 'n':
                natTraversal = 1;
                break;
            default:
                return 1;
        }
    }

    if( optind > argc - 1  )
    {
        return !showHelp;
    }

    torrentPath = argv[optind];

    return 0;
}

static void sigHandler( int signal )
{
    switch( signal )
    {
        case SIGINT:
            gotsig = 1;
            break;

        default:
            break;
    }
}
