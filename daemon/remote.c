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

#include <getopt.h>
#include <unistd.h> /* getcwd */

#include <libevent/event.h>
#include <curl/curl.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpc.h>
#include <libtransmission/json.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-remote"
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT TR_DEFAULT_RPC_PORT

enum { TAG_LIST };

static void
showUsage( void )
{
    puts( "Transmission "LONG_VERSION_STRING"  http://www.transmissionbt.com/\n"
            "A fast and easy BitTorrent client\n"
            "\n"
            "Usage: "MY_NAME" [host] [options]\n"
            "       "MY_NAME" [port] [options]\n"
            "       "MY_NAME" [host:port] [options]\n"
            "\n"
            "Options:\n"
            "  -a --add <torrent>        Add a torrent\n"
            "  -c --encryption required  Require encryption for all peers\n"
            "  -c --encryption preferred Prefer peers to use encryption\n"
            "  -c --encryption tolerated Prefer peers to use plaintext\n"
            "  -d --download-limit <int> Max download rate in KiB/s\n"
            "  -D --download-unlimited   No download rate limit\n"
            "  -e --enable-pex           Enable peer exchange\n"
            "  -E --disable-pex          Disable peer exchange\n"
            "  -f --folder <path>        Folder to set for new torrents\n"
            "  -g --debug                Print debugging information\n"
            "  -h --help                 Display this message and exit\n"
            "  -l --list                 Long list of all torrent and status\n"
            "  -m --port-mapping         Automatic port mapping via NAT-PMP or UPnP\n"
            "  -M --no-port-mapping      Disable automatic port mapping\n"
            "  -p --port <int>           Port to listen for incoming peers\n"
            "  -r --remove <int>         Remove the torrent with the given ID\n"
            "  -r --remove all           Remove all torrents\n"
            "  -s --start <int>          Start the torrent with the given ID\n"
            "  -s --start all            Start all stopped torrents\n"
            "  -S --stop <int>           Stop the torrent with the given ID\n"
            "  -S --stop all             Stop all running torrents\n"
            "  -u --upload-limit <int>   Max upload rate in KiB/s\n"
            "  -U --upload-unlimited     No upload rate limit\n"
            "  -v --verify <id>          Verify the torrent's local data\n" );
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

static void
readargs( int argc, char ** argv )
{
    int opt;
    char optstr[] = "a:c:d:DeEf:ghlmMp:r:s:S:u:Uv:";
    
    const struct option longopts[] =
    {
        { "add",                required_argument, NULL, 'a' },
        { "encryption",         required_argument, NULL, 'c' },
        { "download-limit",     required_argument, NULL, 'd' },
        { "download-unlimited", no_argument,       NULL, 'D' },
        { "enable-pex",         no_argument,       NULL, 'e' },
        { "disable-pex",        no_argument,       NULL, 'E' },
        { "folder",             required_argument, NULL, 'f' },
        { "debug",              no_argument,       NULL, 'g' },
        { "help",               no_argument,       NULL, 'h' },
        { "list",               no_argument,       NULL, 'l' },
        { "port-mapping",       no_argument,       NULL, 'm' },
        { "no-port-mapping",    no_argument,       NULL, 'M' },
        { "port",               required_argument, NULL, 'p' },
        { "remove",             required_argument, NULL, 'r' },
        { "start",              required_argument, NULL, 's' },
        { "stop",               required_argument, NULL, 'S' },
        { "upload-limit",       required_argument, NULL, 'u' },
        { "upload-unlimited",   no_argument,       NULL, 'U' },
        { "verify",             required_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    while((( opt = getopt_long( argc, argv, optstr, longopts, NULL ))) != -1 )
    {
        char * req = NULL;
        char buf[MAX_PATH_LENGTH];

        switch( opt )
        {
            case 'g': debug = 1; break;
            case 'h': showUsage( ); break;
            case 'a': req = tr_strdup_printf( "method=torrent-add&filename=%s", optarg ); break;
            case 'c': req = tr_strdup_printf( "method=session-set&encryption=%s", optarg ); break;
            case 'd': req = tr_strdup_printf( "method=session-set&speed-limit-down=%d&speed-limit-down-enabled=1", numarg(optarg) ); break;
            case 'D': req = tr_strdup( "method=session-set&speed-limit-down-enabled=0" ); break;
            case 'u': req = tr_strdup_printf( "method=session-set&speed-limit-up=%d&speed-limit-up-enabled:1", numarg(optarg) ); break;
            case 'U': req = tr_strdup( "method=session-set&speed-limit-up-enabled=0" ); break;
            case 'e': req = tr_strdup( "method=session-set&pex-allowed=1" ); break;
            case 'E': req = tr_strdup( "method=session-set&pex-allowed=0" ); break;
            case 'f': req = tr_strdup_printf( "method=session-set&download-dir=%s", absolutify(buf,sizeof(buf),optarg)); break;
            case 'l': req = tr_strdup_printf( "method=torrent-list&tag=%d", TAG_LIST ); break;
            case 'm': req = tr_strdup( "method=session-set&port-forwarding-enabled=1" ); break;
            case 'M': req = tr_strdup( "method=session-set&port-forwarding-enabled=0" ); break;
            case 'p': req = tr_strdup_printf( "method=session-set&port=%d", numarg( optarg ) ); break;
            case 'r': req = strcmp( optarg, "all" )
                      ? tr_strdup_printf( "method=torrent-remove&ids=%s", optarg )
                      : tr_strdup       ( "method=torrent-remove" ); break;
            case 's': req = strcmp( optarg, "all" )
                      ? tr_strdup_printf( "method=torrent-start&ids=%s", optarg )
                      : tr_strdup       ( "method=torrent-start" ); break;
            case 'S': req = strcmp( optarg, "all" )
                      ? tr_strdup_printf( "method=torrent-stop&ids=%s", optarg )
                      : tr_strdup       ( "method=torrent-stop" ); break;
            case 'v': req = strcmp( optarg, "all" )
                      ? tr_strdup_printf( "method=torrent-verify&ids=%s", optarg )
                      : tr_strdup       ( "method=torrent-verify" ); break;
        }

        if( req )
            reqs[reqCount++] = req;
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
processResponse( const void * response, size_t len )
{
    tr_benc top;

    if( tr_jsonParse( response, len, &top, NULL ) )
       tr_nerr( MY_NAME, "Unable to parse response\n" );
    else
    {
        tr_benc *args, *list;
        int64_t tag = -1;
        const char * str;
        tr_bencDictFindInt( &top, "tag", &tag );

        if( tr_bencDictFindStr( &top, "result", &str ) )
            printf( "Server responded: \"%s\"\n", str );

        if( ( tag == TAG_LIST ) &&
            ( tr_bencDictFindDict( &top, "arguments", &args ) ) &&
            ( tr_bencDictFindList( args, "list", &list ) ) )
        {
            int i, n;
            for( i=0, n=tr_bencListSize( list ); i<n; ++i )
            {
                int64_t id, status;
                const char *name, *ratiostr, *upstr, *dnstr;
                tr_benc * d = tr_bencListChild( list, i );
                if(    tr_bencDictFindInt( d, "id", &id )
                    && tr_bencDictFindInt( d, "status", &status )
                    && tr_bencDictFindStr( d, "name", &name )
                    && tr_bencDictFindStr( d, "ratio", &ratiostr )
                    && tr_bencDictFindStr( d, "rateUpload", &upstr )
                    && tr_bencDictFindStr( d, "rateDownload", &dnstr ) )
                {
                    printf( "%4d.  Up: %5.1f  Down: %5.1f  Ratio: %4.1f  %-15s  %s\n",
                            (int)id,
                            strtod( upstr, NULL ),
                            strtod( dnstr, NULL ),
                            strtod( ratiostr, NULL ),
                            torrentStatusToString( status ),
                            name );
                }
            }
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

    curl = curl_easy_init( );
    curl_easy_setopt( curl, CURLOPT_VERBOSE, debug );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, MY_NAME"/"LONG_VERSION_STRING );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeFunc );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, buf );

    for( i=0; i<reqCount; ++i )
    {
        CURLcode res;
        char * url = tr_strdup_printf( "http://%s:%d/transmission?%s",
                                       host, port, reqs[i] );
        curl_easy_setopt( curl, CURLOPT_URL, url );
        if(( res = curl_easy_perform( curl )))
            tr_nerr( MY_NAME, "%s\n", curl_easy_strerror( res ) );
        else
            processResponse( EVBUFFER_DATA( buf ), EVBUFFER_LENGTH( buf ) );

        evbuffer_drain( buf, EVBUFFER_LENGTH( buf ) );
        tr_free( url );
    }

    /* cleanup */
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

    readargs( argc, argv );
    if( reqCount )
        processRequests( host, port, (const char**)reqs, reqCount );
    else
        showUsage( );

    for( i=0; i<reqCount; ++i )
        tr_free( reqs[i] );

    return 0;
}
