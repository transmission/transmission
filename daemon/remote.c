/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp */

#include <unistd.h> /* getcwd */

#include <libevent/event.h>
#include <curl/curl.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpc.h>
#include <libtransmission/json.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-remote"
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT TR_DEFAULT_RPC_PORT

enum { TAG_LIST, TAG_DETAILS, TAG_FILES, TAG_PEERS };

static const char*
getUsage( void )
{
    return "Transmission "LONG_VERSION_STRING"  http://www.transmissionbt.com/\n"
           "A fast and easy BitTorrent client\n"
           "\n"
           "Usage: "MY_NAME" [host] [options]\n"
           "       "MY_NAME" [port] [options]\n"
           "       "MY_NAME" [host:port] [options]";
}

static tr_option opts[] =
{
    { 'a', "add",          "Add torrent files", "a", 0, NULL },
    { 'd', "downlimit",    "Set the maximum download speed in KB/s", "d", 1, "<number>" },
    { 'D', "no-downlimit", "Don't limit the download speed", "D", 0, NULL },
    { 'e', "encryption",   "Set encryption mode [required, preferred, tolerated]", "e", 1, "<mode>" },
    { 'f', "files",        "Get a file list for the current torrent(s)", "f", 0, NULL },
    { 'g', "debug",        "Print debugging information", "g", 0, NULL },
    { 'h', "help",         "Show this help page and exit", "h", 0, NULL },
    { 'i', "info",         "Show details of the current torrent(s)", "i", 0, NULL },
    { 'l', "list",         "List all torrents", "l", 0, NULL },
    { 'm', "portmap",      "Enable portmapping via NAT-PMP or UPnP", "m", 0, NULL },
    { 'M', "no-portmap",   "Disable portmapping", "M", 0, NULL },
    { 'n', "auth",         "Set username for authentication", "n", 1, "<user>:<pass>" },
    { 'p', "port",         "Port to listen for incoming peers", "p", 1, "<port>" },
    { 'r', "remove",       "Remove the current torrent(s)", "r", 0, NULL },
    { 's', "start",        "Start the current torrent(s)", "s", 0, NULL },
    { 'S', "stop",         "Stop the current torrent(s)", "S", 0, NULL },
    { 't', "torrent",      "Set the current torrent(s)", "t", 1, "<id|hash|all>" },
    { 'u', "uplimit",      "Set the maximum upload speed in KB/s", "u", 1, "<number>" },
    { 'U', "no-uplimit",   "Don't limit the upload speed", "U", 0, NULL },
    { 'v', "verify",       "Verify the current torrent(s)", "v", 0, NULL },
    { 'w', "download-dir", "Set the download folder for new torrents", "w", 1, "<path>" },
    { 'x', "pex",          "Enable peer exchange (PEX)", "x", 0, NULL },
    { 'X', "no-pex",       "Disable peer exchange (PEX)", "X", 0, NULL },
    { 0, NULL, NULL, NULL, 0, NULL }
};

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage(), opts );
    exit( 0 );
}

static int
numarg( const char * arg )
{
    char * end = NULL;
    const long num = strtol( arg, &end, 10 );
    if( *end ) {
        fprintf( stderr, "Not a number: \"%s\"\n", arg );
        showUsage( );
    }
    return num;
}

static char * reqs[256]; /* arbitrary max */
static int reqCount = 0;
static int debug = 0;
static char * auth = NULL;

static char*
absolutify( char * buf, size_t len, const char * path )
{
    if( *path == '/' )
        tr_strlcpy( buf, path, len );
    else {
        char cwd[MAX_PATH_LENGTH];
        getcwd( cwd, sizeof( cwd ) );
        tr_buildPath( buf, len, cwd, path, NULL );
    }
    return buf;
}

static char*
getEncodedMetainfo( const char * filename )
{
    size_t len = 0;
    uint8_t * buf = tr_loadFile( filename, &len );
    char * b64 = tr_base64_encode( buf, len, NULL );
    tr_free( buf );
    return b64;
}

static void
addIdArg( tr_benc * args, const char * id )
{
    if( !*id ) {
        fprintf( stderr, "No torrent specified!  Please use the -t option first.\n" );
        id = "-1"; /* no torrent will have this ID, so should be a no-op */
    } else if( strcmp( id, "all" ) ) {
        tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), id, strlen(id) );
    }
}

static void
readargs( int argc, const char ** argv )
{
    int c;
    int addingTorrents = 0;
    const char * optarg;
    char id[4096];

    *id = '\0';

    while(( c = tr_getopt( getUsage(), argc, argv, opts, &optarg )))
    {
        char buf[MAX_PATH_LENGTH];
        int addArg = TRUE;
        tr_benc top, *args;
        tr_bencInitDict( &top, 3 );
        int64_t fields = 0;
        args = tr_bencDictAddDict( &top, "arguments", 0 );

        switch( c )
        {
            case TR_OPT_UNK:
                      if( addingTorrents ) {
                          char * tmp;
                          tr_bencDictAddStr( &top, "method", "torrent-add" );
                          tr_bencDictAddStr( args, "metainfo", ((tmp=getEncodedMetainfo(optarg))) );
                          tr_free( tmp );
                      } else {
                          fprintf( stderr, "Unknown option: %s\n", optarg );
                          addArg = FALSE;
                      }
                      break;
            case 'a': addingTorrents = 1;
                      addArg = FALSE;
                      break;
            case 'd': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-down", numarg( optarg ) );
                      tr_bencDictAddInt( args, "speed-limit-down-enabled", 1 );
                      break;
            case 'D': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-down-enabled", 0 );
                      break;
            case 'e': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddStr( args, "encryption", optarg );
                      break;
            case 'f': tr_bencDictAddStr( &top, "method", "torrent-get" );
                      tr_bencDictAddInt( &top, "tag", TAG_FILES );
                      tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), optarg, strlen(optarg) );
                      fields = TR_RPC_TORRENT_FIELD_ID
                             | TR_RPC_TORRENT_FIELD_FILES
                             | TR_RPC_TORRENT_FIELD_PRIORITIES;
                      tr_bencDictAddInt( args, "fields", fields );
                      break;
            case 'g': debug = 1;
                      addArg = FALSE;
                      break;
            case 'i': tr_bencDictAddStr( &top, "method", "torrent-get" );
                      tr_bencDictAddInt( &top, "tag", TAG_DETAILS );
                      addIdArg( args, id );
                      fields = TR_RPC_TORRENT_FIELD_ACTIVITY
                          | TR_RPC_TORRENT_FIELD_ANNOUNCE
                          | TR_RPC_TORRENT_FIELD_ERROR
                          | TR_RPC_TORRENT_FIELD_HISTORY
                          | TR_RPC_TORRENT_FIELD_ID
                          | TR_RPC_TORRENT_FIELD_INFO
                          | TR_RPC_TORRENT_FIELD_SCRAPE
                          | TR_RPC_TORRENT_FIELD_SIZE
                          | TR_RPC_TORRENT_FIELD_TRACKER_STATS;
                      tr_bencDictAddInt( args, "fields", fields );
                      break;
            case 'l': tr_bencDictAddStr( &top, "method", "torrent-get" );
                      tr_bencDictAddInt( &top, "tag", TAG_LIST );
                      fields = TR_RPC_TORRENT_FIELD_ID
                             | TR_RPC_TORRENT_FIELD_ACTIVITY
                             | TR_RPC_TORRENT_FIELD_SIZE;
                      tr_bencDictAddInt( args, "fields", fields );
                      break;
            case 'm': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "port-forwarding-enabled", 1 );
                      break;
            case 'M': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "port-forwarding-enabled", 0 );
                      break;
            case 'n': auth = tr_strdup( optarg );
                      addArg = FALSE;
                      break;
            case 'p': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "port", numarg( optarg ) );
                      break;
            case 'r': tr_bencDictAddStr( &top, "method", "torrent-remove" );
                      addIdArg( args, id );
                      break;
            case 's': tr_bencDictAddStr( &top, "method", "torrent-start" );
                      addIdArg( args, id );
                      break;
            case 'S': tr_bencDictAddStr( &top, "method", "torrent-stop" );
                      addIdArg( args, id );
                      break;
            case 't': tr_strlcpy( id, optarg, sizeof( id ) );
                      addArg = FALSE;
                      break;
            case 'u': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-up", numarg( optarg ) );
                      tr_bencDictAddInt( args, "speed-limit-up-enabled", 1 );
                      break;
            case 'U': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-up-enabled", 0 );
                      break;
            case 'v': tr_bencDictAddStr( &top, "method", "torrent-verify" );
                      addIdArg( args, id );
                      break;
            case 'w': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddStr( args, "download-dir", absolutify(buf,sizeof(buf),optarg) );
                      break;
            case 'x': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "pex-allowed", 1 );
                      break;
            case 'X': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "pex-allowed", 0 );
                      break;
            default:  fprintf( stderr, "got opt [%d]\n", (int)c );
                      showUsage( );
                      break;
        }

        if( addArg )
            reqs[reqCount++] = tr_bencSaveAsJSON( &top, NULL );
        tr_bencFree( &top );
    }
}

/* [host:port] or [host] or [port] */
static void
getHostAndPort( int * argc, char ** argv, char ** host, int * port )
{
    if( *argv[1] != '-' )
    {
        int i;
        const char * s = argv[1];
        const char * delim = strchr( s, ':' );
        if( delim ) { /* user passed in both host and port */
            *host = tr_strndup( s, delim-s );
            *port = atoi( delim+1 );
        } else {
            char * end;
            const int i = strtol( s, &end, 10 );
            if( !*end ) /* user passed in a port */
                *port = i;
            else /* user passed in a host */
                *host = tr_strdup( s );
        }

        *argc -= 1;
        for( i=1; i<*argc; ++i )
            argv[i] = argv[i+1];
    }
}

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * buf )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( buf, ptr, byteCount );
    return byteCount;
}

static void
etaToString( char * buf, size_t buflen, int64_t eta )
{
         if( eta < 0 )           snprintf( buf, buflen, "Unknown" );
    else if( eta < 60 )          snprintf( buf, buflen, "%"PRId64"sec", eta );
    else if( eta < (60*60) )     snprintf( buf, buflen, "%"PRId64" min", eta/60 );
    else if( eta < (60*60*24) )  snprintf( buf, buflen, "%"PRId64" hrs", eta/(60*60) );
    else                         snprintf( buf, buflen, "%"PRId64" days", eta/(60*60*24) );
}

#define KILOBYTE_FACTOR 1024.0
#define MEGABYTE_FACTOR (1024.0 * 1024.0)
#define GIGABYTE_FACTOR (1024.0 * 1024.0 * 1024.0)

static char*
strlratio( char * buf, double numerator, double denominator, size_t buflen )
{
    if( denominator )
    {
        const double ratio = numerator / denominator;
        if( ratio < 10.0 )
            snprintf( buf, buflen, "%'.2f", ratio );
        else if( ratio < 100.0 )
            snprintf( buf, buflen, "%'.1f", ratio );
        else
            snprintf( buf, buflen, "%'.0f", ratio );
    }
    else if( numerator )
        tr_strlcpy( buf, "Infinity", buflen );
    else
        tr_strlcpy( buf, "None", buflen );
    return buf;
}

static char*
strlsize( char * buf, int64_t size, size_t buflen )
{
    if( !size )
        tr_strlcpy( buf, "None", buflen );
    else if( size < (int64_t)KILOBYTE_FACTOR )
        snprintf( buf, buflen, "%'"PRId64" bytes", (int64_t)size );
    else {
        double displayed_size;
        if (size < (int64_t)MEGABYTE_FACTOR) {
            displayed_size = (double) size / KILOBYTE_FACTOR;
            snprintf( buf, buflen, "%'.1f KB", displayed_size );
        } else if (size < (int64_t)GIGABYTE_FACTOR) {
            displayed_size = (double) size / MEGABYTE_FACTOR;
            snprintf( buf, buflen, "%'.1f MB", displayed_size );
        } else {
            displayed_size = (double) size / GIGABYTE_FACTOR;
            snprintf( buf, buflen, "%'.1f GB", displayed_size );
        }
    }
    return buf;
}

static const char*
torrentStatusToString( int i )
{
    switch( i )
    {
        case TR_STATUS_CHECK_WAIT: return "Will Verify";
        case TR_STATUS_CHECK:      return "Verifying";
        case TR_STATUS_DOWNLOAD:   return "Downloading";
        case TR_STATUS_SEED:       return "Seeding";
        case TR_STATUS_STOPPED:    return "Stopped";
        default:                   return "Error";
    }
}

static void
printDetails( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) &&
        ( tr_bencDictFindList( args, "torrents", &torrents ) ) )
    {
        int ti, tCount;
        for( ti=0, tCount=tr_bencListSize( torrents ); ti<tCount; ++ti )
        {
            tr_benc * t = tr_bencListChild( torrents, ti );
            const char * str;
            char buf[512];
            char buf2[512];
            int64_t i, j;

            printf( "NAME\n" );
            if( tr_bencDictFindInt( t, "id", &i ) )
                printf( "  Id: %"PRId64"\n", i );
            if( tr_bencDictFindStr( t, "name", &str ) )
                printf( "  Name: %s\n", str );
            if( tr_bencDictFindStr( t, "hashString", &str ) )
                printf( "  Hash: %s\n", str );
            printf( "\n" );

            printf( "TRANSFER\n" );
            if( tr_bencDictFindInt( t, "status", &i ) ) {
                switch( i ) {
                    case TR_STATUS_SEED:        str = "Seeding"; break;
                    case TR_STATUS_DOWNLOAD:    str = "Downloading"; break;
                    case TR_STATUS_STOPPED:     str = "Paused"; break;
                    case TR_STATUS_CHECK:       str = "Verifying local data"; break;
                    case TR_STATUS_CHECK_WAIT:  str = "Waiting to verify"; break;
                    default:                    str = "error"; break;
                }
                printf( "  State: %s\n", str );
            }
            if( tr_bencDictFindInt( t, "eta", &i ) ) {
                etaToString( buf, sizeof( buf ), i );
                printf( "  ETA: %s\n", buf );
            }
            if( tr_bencDictFindInt( t, "rateDownload", &i ) )
                printf( "  Download Speed: %.1f KB/s\n", i/1024.0 );
            if( tr_bencDictFindInt( t, "rateUpload", &i ) )
            {
                printf( "  Upload Speed: %.1f KB/s\n", i/1024.0 );
            }
            if( tr_bencDictFindInt( t, "haveUnchecked", &i ) &&
                tr_bencDictFindInt( t, "haveValid", &j ) )
            {
                strlsize( buf, i+j, sizeof( buf ) );
                strlsize( buf2, j, sizeof( buf2 ) );
                printf( "  Have: %s (%s verified)\n", buf, buf2 );
            }
            if( tr_bencDictFindInt( t, "sizeWhenDone", &i ) &&
                tr_bencDictFindInt( t, "leftUntilDone", &j ) )
            {
                strlratio( buf, (i-j), i, sizeof( buf ) );
                printf( "  Progress: %s%%\n", buf );
            }
            if( tr_bencDictFindInt( t, "sizeWhenDone", &i ) &&
                tr_bencDictFindInt( t, "totalSize", &j ) )
            {
                strlsize( buf, j, sizeof( buf ) );
                strlsize( buf2, i, sizeof( buf2 ) );
                printf( "  Total size: %s (%s wanted)\n", buf, buf2 );
            }
            if( tr_bencDictFindInt( t, "downloadedEver", &i ) &&
                tr_bencDictFindInt( t, "uploadedEver", &j ) ) {
                strlsize( buf, i, sizeof( buf ) );
                printf( "  Downloaded: %s\n", buf );
                strlsize( buf, j, sizeof( buf ) );
                printf( "  Uploaded: %s\n", buf );
                strlratio( buf, i, j, sizeof( buf ) );
                printf( "  Ratio: %s\n", buf );
            }
            if( tr_bencDictFindInt( t, "corruptEver", &i ) ) {
                strlsize( buf, i, sizeof( buf ) );
                printf( "  Corrupt DL: %s\n", buf );
            }
            if( tr_bencDictFindStr( t, "errorString", &str ) && str && *str )
                printf( "  Error: %s\n", str );
            printf( "\n" );
            
            printf( "HISTORY\n" );
            if( tr_bencDictFindInt( t, "addedDate", &i ) && i ) {
                const time_t tt = i;
                printf( "  Date added:      %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "doneDate", &i ) && i ) {
                const time_t tt = i;
                printf( "  Date finished:   %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "startDate", &i ) && i ) {
                const time_t tt = i;
                printf( "  Date started:    %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "activityDate", &i ) && i ) {
                const time_t tt = i;
                printf( "  Latest activity: %s", ctime( &tt ) );
            }
            printf( "\n" );
            
            printf( "TRACKER\n" );
            if( tr_bencDictFindInt( t, "lastAnnounceTime", &i ) && i ) {
                const time_t tt = i;
                printf( "  Latest announce: %s", ctime( &tt ) );
            }
            if( tr_bencDictFindStr( t, "announceURL", &str ) )
                printf( "  Announce URL: %s\n", str );
            if( tr_bencDictFindStr( t, "announceResponse", &str ) && str && *str )
                printf( "  Announce response: %s\n", str );
            if( tr_bencDictFindInt( t, "nextAnnounceTime", &i ) && i ) {
                const time_t tt = i;
                printf( "  Next announce:   %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "lastScrapeTime", &i ) && i ) {
                const time_t tt = i;
                printf( "  Latest scrape:   %s", ctime( &tt ) );
            }
            if( tr_bencDictFindStr( t, "scrapeResponse", &str ) )
                printf( "  Scrape response: %s\n", str );
            if( tr_bencDictFindInt( t, "nextScrapeTime", &i ) && i ) {
                const time_t tt = i;
                printf( "  Next scrape:     %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "seeders", &i ) &&
                tr_bencDictFindInt( t, "leechers", &j ) )
                printf( "  Tracker knows of %"PRId64" seeders and %"PRId64" leechers\n", i, j );
            if( tr_bencDictFindInt( t, "timesCompleted", &i ) )
                printf( "  Tracker has seen %"PRId64" clients complete this torrent\n", i );
            printf( "\n" );

            printf( "ORIGINS\n" );
            if( tr_bencDictFindInt( t, "dateCreated", &i ) && i ) {
                const time_t tt = i;
                printf( "  Date created: %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "isPrivate", &i ) )
                printf( "  Public torrent: %s\n", ( i ? "No" : "Yes" ) );
            if( tr_bencDictFindStr( t, "comment", &str ) && str && *str )
                printf( "  Comment: %s\n", str );
            if( tr_bencDictFindStr( t, "creator", &str ) && str && *str )
                printf( "  Creator: %s\n", str );
            if( tr_bencDictFindInt( t, "pieceCount", &i ) )
                printf( "  Piece Count: %"PRId64"\n", i );
            if( tr_bencDictFindInt( t, "pieceSize", &i ) )
                printf( "  Piece Size: %"PRId64"\n", i );
        }
    }
}

static void
printFileList( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) &&
        ( tr_bencDictFindList( args, "torrents", &torrents ) ) )
    {
        int i, in;
        for( i=0, in=tr_bencListSize( torrents ); i<in; ++i )
        {
            tr_benc * d = tr_bencListChild( torrents, i );
            tr_benc *files, *priorities, *wanteds;
            const char * name;
            if( tr_bencDictFindStr( d, "name", &name ) &&
                tr_bencDictFindList( d, "files", &files ) &&
                tr_bencDictFindList( d, "priorities", &priorities ) &&
                tr_bencDictFindList( d, "wanted", &wanteds ) )
            {
                int j=0, jn=tr_bencListSize(files);
                printf( "%s (%d files):\n", name, jn );
                printf("%3s  %8s %3s %9s  %s\n", "#", "Priority", "Get", "Size", "Name" );
                for( j=0, jn=tr_bencListSize( files ); j<jn; ++j )
                {
                    int64_t length;
                    int64_t priority;
                    int64_t wanted;
                    const char * filename;
                    tr_benc * file = tr_bencListChild( files, j );
                    if( tr_bencDictFindInt( file, "length", &length ) &&
                        tr_bencDictFindStr( file, "name", &filename ) &&
                        tr_bencGetInt( tr_bencListChild( priorities, j ), &priority ) &&
                        tr_bencGetInt( tr_bencListChild( wanteds, j ), &wanted ) )
                    {
                        char sizestr[64];
                        strlsize( sizestr, length, sizeof( sizestr ) );
                        const char * pristr;
                        switch( priority ) {
                            case TR_PRI_LOW:    pristr = "Low"; break;
                            case TR_PRI_HIGH:   pristr = "High"; break;
                            default:            pristr = "Normal"; break;
                        }
                        printf( "%3d: %-8s %-3s %9s  %s\n", (j+1), pristr, (wanted?"Yes":"No"), sizestr, filename );
                    }
                }
            }
        }
    }
}

static void
printTorrentList( tr_benc * top )
{
    tr_benc *args, *list;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) &&
        ( tr_bencDictFindList( args, "torrents", &list ) ) )
    {
        int i, n;
        printf( "%-3s  %-4s  %-8s  %-5s  %-5s  %-5s  %-11s  %s\n",
                "ID", "Done", "ETA", "Up", "Down", "Ratio", "Status", "Name" );
        for( i=0, n=tr_bencListSize( list ); i<n; ++i )
        {
            int64_t id, eta, status, up, down, sizeWhenDone, leftUntilDone;
            const char *name;
            tr_benc * d = tr_bencListChild( list, i );
            if(    tr_bencDictFindInt( d, "eta", &eta )
                && tr_bencDictFindInt( d, "id", &id )
                && tr_bencDictFindInt( d, "leftUntilDone", &leftUntilDone )
                && tr_bencDictFindStr( d, "name", &name )
                && tr_bencDictFindInt( d, "rateDownload", &down )
                && tr_bencDictFindInt( d, "rateUpload", &up )
                && tr_bencDictFindInt( d, "sizeWhenDone", &sizeWhenDone )
                && tr_bencDictFindInt( d, "status", &status ) )
            {
                char etaStr[16];
                if( leftUntilDone )
                    etaToString( etaStr, sizeof( etaStr ), eta );
                else
                    snprintf( etaStr, sizeof( etaStr ), "Done" );
                printf( "%3d  %3d%%  %-8s  %5.1f  %5.1f  %5.1f  %-11s  %s\n",
                        (int)id,
                        (int)(100.0*(sizeWhenDone-leftUntilDone)/sizeWhenDone),
                        etaStr,
                        up / 1024.0,
                        down / 1024.0,
                        (double)(sizeWhenDone-leftUntilDone)/sizeWhenDone,
                        torrentStatusToString( status ),
                        name );
            }
        }
    }
}

static void
processResponse( const char * host, int port,
                 const void * response, size_t len )
{
    tr_benc top;

    if( debug )
        fprintf( stderr, "got response: [%*.*s]\n",
                 (int)len, (int)len, (const char*) response );

    if( tr_jsonParse( response, len, &top, NULL ) )
       tr_nerr( MY_NAME, "Unable to parse response \"%*.*s\"", (int)len, (int)len, (char*)response );
    else
    {
        int64_t tag = -1;
        const char * str;
        tr_bencDictFindInt( &top, "tag", &tag );

        if( tr_bencDictFindStr( &top, "result", &str ) )
            printf( "%s:%d responded: \"%s\"\n", host, port, str );
        switch( tag ) {
            case TAG_FILES: printFileList( &top ); break;
            case TAG_DETAILS: printDetails( &top ); break;
            case TAG_LIST: printTorrentList( &top ); break;
            default: break;
        }

        tr_bencFree( &top );
    }
}

static void
processRequests( const char * host, int port,
                 const char ** reqs, int reqCount )
{
    int i;
    CURL * curl;
    struct evbuffer * buf = evbuffer_new( );
    char * url = tr_strdup_printf( "http://%s:%d/transmission/rpc", host, port );

    curl = curl_easy_init( );
    curl_easy_setopt( curl, CURLOPT_VERBOSE, debug );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, MY_NAME"/"LONG_VERSION_STRING );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeFunc );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, buf );
    curl_easy_setopt( curl, CURLOPT_POST, 1 );
    curl_easy_setopt( curl, CURLOPT_URL, url );
    if( auth ) {
        curl_easy_setopt( curl, CURLOPT_USERPWD, auth );
        curl_easy_setopt( curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY );
    }

    for( i=0; i<reqCount; ++i )
    {
        CURLcode res;
        curl_easy_setopt( curl, CURLOPT_POSTFIELDS, reqs[i] );
        if( debug )
            tr_ninf( MY_NAME, "posting [%s]\n", reqs[i] );
        if(( res = curl_easy_perform( curl )))
            tr_nerr( MY_NAME, "(%s:%d) %s", host, port, curl_easy_strerror( res ) );
        else
            processResponse( host, port, EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );

        evbuffer_drain( buf, EVBUFFER_LENGTH( buf ) );
    }

    /* cleanup */
    tr_free( url );
    evbuffer_free( buf );
    curl_easy_cleanup( curl );
}

int
main( int argc, char ** argv )
{
    int i;
    int port = DEFAULT_PORT;
    char * host = NULL;

    if( argc < 2 )
        showUsage( );

    getHostAndPort( &argc, argv, &host, &port );
    if( host == NULL )
        host = tr_strdup( DEFAULT_HOST );

    readargs( argc, (const char**)argv );
    if( reqCount )
        processRequests( host, port, (const char**)reqs, reqCount );
    else
        showUsage( );

    for( i=0; i<reqCount; ++i )
        tr_free( reqs[i] );

    tr_free( host );
    return 0;
}
