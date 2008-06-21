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
            "  -t --auth <user>:<pass>   Username and password for authentication\n"
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
readargs( int argc, char ** argv )
{
    int opt;
    char optstr[] = "a:c:d:DeEf:ghlmMp:r:s:S:t:u:Uv:";
    
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
        { "auth",               required_argument, NULL, 't' },
        { "upload-limit",       required_argument, NULL, 'u' },
        { "upload-unlimited",   no_argument,       NULL, 'U' },
        { "verify",             required_argument, NULL, 'v' },
        { NULL, 0, NULL, 0 }
    };

    while((( opt = getopt_long( argc, argv, optstr, longopts, NULL ))) != -1 )
    {
        char * tmp;
        char buf[MAX_PATH_LENGTH];
        int addArg = TRUE;
        int64_t fields = 0;
        tr_benc top, *args;
        tr_bencInitDict( &top, 3 );
        args = tr_bencDictAddDict( &top, "arguments", 0 );

        switch( opt )
        {
            case 'g': debug = 1;
                      addArg = FALSE;
                      break;
            case 't': auth = tr_strdup( optarg );
                      addArg = FALSE;
                      break;
            case 'h': showUsage( );
                      addArg = FALSE;
                      break;
            case 'a': tr_bencDictAddStr( &top, "method", "torrent-add" );
                      tr_bencDictAddStr( args, "metainfo", ((tmp=getEncodedMetainfo(optarg))) );
                      tr_free( tmp );
                      break;
            case 'c': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddStr( args, "encryption", optarg );
                      break;
            case 'd': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-down", numarg( optarg ) );
                      tr_bencDictAddInt( args, "speed-limit-down-enabled", 1 );
                      break;
            case 'D': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-down-enabled", 0 );
                      break;
            case 'u': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-up", numarg( optarg ) );
                      tr_bencDictAddInt( args, "speed-limit-up-enabled", 1 );
                      break;
            case 'U': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "speed-limit-up-enabled", 0 );
                      break;
            case 'e': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "pex-allowed", 1 );
                      break;
            case 'E': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "pex-allowed", 0 );
                      break;
            case 'f': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddStr( args, "download-dir", absolutify(buf,sizeof(buf),optarg) );
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
            case 'p': tr_bencDictAddStr( &top, "method", "session-set" );
                      tr_bencDictAddInt( args, "port", numarg( optarg ) );
                      break;
            case 'r': tr_bencDictAddStr( &top, "method", "torrent-remove" );
                      if( strcmp( optarg, "all" ) )
                          tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), optarg, strlen(optarg) );
                      break;
            case 's': tr_bencDictAddStr( &top, "method", "torrent-start" );
                      if( strcmp( optarg, "all" ) )
                          tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), optarg, strlen(optarg) );
                      break;
            case 'S': tr_bencDictAddStr( &top, "method", "torrent-stop" );
                      if( strcmp( optarg, "all" ) )
                          tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), optarg, strlen(optarg) );
                      break;
            case 'v': tr_bencDictAddStr( &top, "method", "torrent-verify" );
                      if( strcmp( optarg, "all" ) )
                          tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), optarg, strlen(optarg) );
                      break;
            default:
                      showUsage( );
                      addArg = FALSE;
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
        tr_benc *args, *list;
        int64_t tag = -1;
        const char * str;
        tr_bencDictFindInt( &top, "tag", &tag );

        if( tr_bencDictFindStr( &top, "result", &str ) )
            printf( "%s:%d responded: \"%s\"\n", host, port, str );

        if( ( tag == TAG_LIST ) &&
            ( tr_bencDictFindDict( &top, "arguments", &args ) ) &&
            ( tr_bencDictFindList( args, "torrents", &list ) ) )
        {
            int i, n;
            printf( "%-3s  %-4s  %-8s  %-5s  %-5s  %-5s  %-11s  %s\n",
                    "ID", "Done", "ETA", "Up", "Down", "Ratio", "Status", "Name" );
            for( i=0, n=tr_bencListSize( list ); i<n; ++i )
            {
                int64_t id, eta, status, up, down, sizeWhenDone, leftUntilDone;
                const char *name, *ratiostr;
                tr_benc * d = tr_bencListChild( list, i );
                if(    tr_bencDictFindInt( d, "id", &id )
                    && tr_bencDictFindStr( d, "name", &name )
                    && tr_bencDictFindInt( d, "eta", &eta )
                    && tr_bencDictFindInt( d, "eta", &eta )
                    && tr_bencDictFindInt( d, "leftUntilDone", &leftUntilDone )
                    && tr_bencDictFindInt( d, "sizeWhenDone", &sizeWhenDone )
                    && tr_bencDictFindInt( d, "rateDownload", &down )
                    && tr_bencDictFindInt( d, "rateUpload", &up )
                    && tr_bencDictFindStr( d, "ratio", &ratiostr )
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

    readargs( argc, argv );
    if( reqCount )
        processRequests( host, port, (const char**)reqs, reqCount );
    else
        showUsage( );

    for( i=0; i<reqCount; ++i )
        tr_free( reqs[i] );

    tr_free( host );
    return 0;
}
