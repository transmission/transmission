/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#include <assert.h>
#include <ctype.h> /* isspace */
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* strcmp */

#ifdef WIN32
 #include <direct.h> /* getcwd */
#else
 #include <unistd.h> /* getcwd */
#endif

#include <event2/buffer.h>

#define CURL_DISABLE_TYPECHECK /* otherwise -Wunreachable-code goes insane */
#include <curl/curl.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpcimpl.h>
#include <libtransmission/json.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-remote"
#define DEFAULT_HOST "localhost"
#define DEFAULT_PORT atoi(TR_DEFAULT_RPC_PORT_STR)
#define DEFAULT_URL TR_DEFAULT_RPC_URL_STR "rpc/"

#define ARGUMENTS "arguments"

#define MEM_K 1024
#define MEM_B_STR   "B"
#define MEM_K_STR "KiB"
#define MEM_M_STR "MiB"
#define MEM_G_STR "GiB"
#define MEM_T_STR "TiB"

#define DISK_K 1024
#define DISK_B_STR   "B"
#define DISK_K_STR "KiB"
#define DISK_M_STR "MiB"
#define DISK_G_STR "GiB"
#define DISK_T_STR "TiB"

#define SPEED_K 1024
#define SPEED_B_STR   "B/s"
#define SPEED_K_STR "KiB/s"
#define SPEED_M_STR "MiB/s"
#define SPEED_G_STR "GiB/s"
#define SPEED_T_STR "TiB/s"

/***
****
****  Display Utilities
****
***/

static void
etaToString( char *  buf, size_t  buflen, int64_t eta )
{
    if( eta < 0 )
        tr_snprintf( buf, buflen, "Unknown" );
    else if( eta < 60 )
        tr_snprintf( buf, buflen, "%" PRId64 "sec", eta );
    else if( eta < ( 60 * 60 ) )
        tr_snprintf( buf, buflen, "%" PRId64 " min", eta / 60 );
    else if( eta < ( 60 * 60 * 24 ) )
        tr_snprintf( buf, buflen, "%" PRId64 " hrs", eta / ( 60 * 60 ) );
    else
        tr_snprintf( buf, buflen, "%" PRId64 " days", eta / ( 60 * 60 * 24 ) );
}

static char*
tr_strltime( char * buf, int seconds, size_t buflen )
{
    int  days, hours, minutes;
    char d[128], h[128], m[128], s[128];

    if( seconds < 0 )
        seconds = 0;

    days = seconds / 86400;
    hours = ( seconds % 86400 ) / 3600;
    minutes = ( seconds % 3600 ) / 60;
    seconds = ( seconds % 3600 ) % 60;

    tr_snprintf( d, sizeof( d ), "%d %s", days, days==1?"day":"days" );
    tr_snprintf( h, sizeof( h ), "%d %s", hours, hours==1?"hour":"hours" );
    tr_snprintf( m, sizeof( m ), "%d %s", minutes, minutes==1?"minute":"minutes" );
    tr_snprintf( s, sizeof( s ), "%d %s", seconds, seconds==1?"seconds":"seconds" );

    if( days )
    {
        if( days >= 4 || !hours )
            tr_strlcpy( buf, d, buflen );
        else
            tr_snprintf( buf, buflen, "%s, %s", d, h );
    }
    else if( hours )
    {
        if( hours >= 4 || !minutes )
            tr_strlcpy( buf, h, buflen );
        else
            tr_snprintf( buf, buflen, "%s, %s", h, m );
    }
    else if( minutes )
    {
        if( minutes >= 4 || !seconds )
            tr_strlcpy( buf, m, buflen );
        else
            tr_snprintf( buf, buflen, "%s, %s", m, s );
    }
    else tr_strlcpy( buf, s, buflen );

    return buf;
}

static char*
strlpercent( char * buf, double x, size_t buflen )
{
    return tr_strpercent( buf, x, buflen );
}

static char*
strlratio2( char * buf, double ratio, size_t buflen )
{
    return tr_strratio( buf, buflen, ratio, "Inf" );
}

static char*
strlratio( char * buf, int64_t numerator, int64_t denominator, size_t buflen )
{
    double ratio;

    if( denominator != 0 )
        ratio = numerator / (double)denominator;
    else if( numerator != 0 )
        ratio = TR_RATIO_INF;
    else
        ratio = TR_RATIO_NA;

    return strlratio2( buf, ratio, buflen );
}

static char*
strlmem( char * buf, int64_t bytes, size_t buflen )
{
    if( !bytes )
        tr_strlcpy( buf, "None", buflen );
    else
        tr_formatter_mem_B( buf, bytes, buflen );

    return buf;
}

static char*
strlsize( char * buf, int64_t bytes, size_t buflen )
{
    if( bytes < 1 )
        tr_strlcpy( buf, "Unknown", buflen );
    else if( !bytes )
        tr_strlcpy( buf, "None", buflen );
    else
        tr_formatter_size_B( buf, bytes, buflen );

    return buf;
}

enum
{
    TAG_SESSION,
    TAG_STATS,
    TAG_DETAILS,
    TAG_FILES,
    TAG_LIST,
    TAG_PEERS,
    TAG_PIECES,
    TAG_PORTTEST,
    TAG_TORRENT_ADD,
    TAG_TRACKERS
};

static const char*
getUsage( void )
{
    return
        MY_NAME" "LONG_VERSION_STRING"\n"
        "A fast and easy BitTorrent client\n"
        "http://www.transmissionbt.com/\n"
        "\n"
        "Usage: " MY_NAME
        " [host] [options]\n"
        "       "
        MY_NAME " [port] [options]\n"
                "       "
        MY_NAME " [host:port] [options]\n"
                "       "
        MY_NAME " [http://host:port/transmission/] [options]\n"
                "\n"
                "See the man page for detailed explanations and many examples.";
}

/***
****
****  Command-Line Arguments
****
***/

static tr_option opts[] =
{
    { 'a', "add",                    "Add torrent files by filename or URL", "a",  0, NULL },
    { 970, "alt-speed",              "Use the alternate Limits", "as",  0, NULL },
    { 971, "no-alt-speed",           "Don't use the alternate Limits", "AS",  0, NULL },
    { 972, "alt-speed-downlimit",    "max alternate download speed (in "SPEED_K_STR")", "asd",  1, "<speed>" },
    { 973, "alt-speed-uplimit",      "max alternate upload speed (in "SPEED_K_STR")", "asu",  1, "<speed>" },
    { 974, "alt-speed-scheduler",    "Use the scheduled on/off times", "asc",  0, NULL },
    { 975, "no-alt-speed-scheduler", "Don't use the scheduled on/off times", "ASC",  0, NULL },
    { 976, "alt-speed-time-begin",   "Time to start using the alt speed limits (in hhmm)", NULL,  1, "<time>" },
    { 977, "alt-speed-time-end",     "Time to stop using the alt speed limits (in hhmm)", NULL,  1, "<time>" },
    { 978, "alt-speed-days",         "Numbers for any/all days of the week - eg. \"1-7\"", NULL,  1, "<days>" },
    { 963, "blocklist-update",       "Blocklist update", NULL, 0, NULL },
    { 'c', "incomplete-dir",         "Where to store new torrents until they're complete", "c", 1, "<dir>" },
    { 'C', "no-incomplete-dir",      "Don't store incomplete torrents in a different location", "C", 0, NULL },
    { 'b', "debug",                  "Print debugging information", "b",  0, NULL },
    { 'd', "downlimit",              "Set the max download speed in "SPEED_K_STR" for the current torrent(s) or globally", "d", 1, "<speed>" },
    { 'D', "no-downlimit",           "Disable max download speed for the current torrent(s) or globally", "D", 0, NULL },
    { 'e', "cache",                  "Set the maximum size of the session's memory cache (in " MEM_M_STR ")", "e", 1, "<size>" },
    { 910, "encryption-required",    "Encrypt all peer connections", "er", 0, NULL },
    { 911, "encryption-preferred",   "Prefer encrypted peer connections", "ep", 0, NULL },
    { 912, "encryption-tolerated",   "Prefer unencrypted peer connections", "et", 0, NULL },
    { 850, "exit",                   "Tell the transmission session to shut down", NULL, 0, NULL },
    { 940, "files",                  "List the current torrent(s)' files", "f",  0, NULL },
    { 'g', "get",                    "Mark files for download", "g",  1, "<files>" },
    { 'G', "no-get",                 "Mark files for not downloading", "G",  1, "<files>" },
    { 'i', "info",                   "Show the current torrent(s)' details", "i",  0, NULL },
    { 940, "info-files",             "List the current torrent(s)' files", "if",  0, NULL },
    { 941, "info-peers",             "List the current torrent(s)' peers", "ip",  0, NULL },
    { 942, "info-pieces",            "List the current torrent(s)' pieces", "ic",  0, NULL },
    { 943, "info-trackers",          "List the current torrent(s)' trackers", "it",  0, NULL },
    { 920, "session-info",           "Show the session's details", "si", 0, NULL },
    { 921, "session-stats",          "Show the session's statistics", "st", 0, NULL },
    { 'l', "list",                   "List all torrents", "l",  0, NULL },
    { 960, "move",                   "Move current torrent's data to a new folder", NULL, 1, "<path>" },
    { 961, "find",                   "Tell Transmission where to find a torrent's data", NULL, 1, "<path>" },
    { 'm', "portmap",                "Enable portmapping via NAT-PMP or UPnP", "m",  0, NULL },
    { 'M', "no-portmap",             "Disable portmapping", "M",  0, NULL },
    { 'n', "auth",                   "Set username and password", "n",  1, "<user:pw>" },
    { 810, "authenv",                "Set authentication info from the TR_AUTH environment variable (user:pw)", "ne", 0, NULL },
    { 'N', "netrc",                  "Set authentication info from a .netrc file", "N",  1, "<file>" },
    { 'o', "dht",                    "Enable distributed hash tables (DHT)", "o", 0, NULL },
    { 'O', "no-dht",                 "Disable distributed hash tables (DHT)", "O", 0, NULL },
    { 'p', "port",                   "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "p", 1, "<port>" },
    { 962, "port-test",              "Port testing", "pt", 0, NULL },
    { 'P', "random-port",            "Random port for incomping peers", "P", 0, NULL },
    { 900, "priority-high",          "Try to download these file(s) first", "ph", 1, "<files>" },
    { 901, "priority-normal",        "Try to download these file(s) normally", "pn", 1, "<files>" },
    { 902, "priority-low",           "Try to download these file(s) last", "pl", 1, "<files>" },
    { 700, "bandwidth-high",         "Give this torrent first chance at available bandwidth", "Bh", 0, NULL },
    { 701, "bandwidth-normal",       "Give this torrent bandwidth left over by high priority torrents", "Bn", 0, NULL },
    { 702, "bandwidth-low",          "Give this torrent bandwidth left over by high and normal priority torrents", "Bl", 0, NULL },
    { 600, "reannounce",             "Reannounce the current torrent(s)", NULL,  0, NULL },
    { 'r', "remove",                 "Remove the current torrent(s)", "r",  0, NULL },
    { 930, "peers",                  "Set the maximum number of peers for the current torrent(s) or globally", "pr", 1, "<max>" },
    { 'R', "remove-and-delete",      "Remove the current torrent(s) and delete local data", NULL, 0, NULL },
    { 800, "torrent-done-script",    "Specify a script to run when a torrent finishes", NULL, 1, "<file>" },
    { 801, "no-torrent-done-script", "Don't run a script when torrents finish", NULL, 0, NULL },
    { 950, "seedratio",              "Let the current torrent(s) seed until a specific ratio", "sr", 1, "ratio" },
    { 951, "seedratio-default",      "Let the current torrent(s) use the global seedratio settings", "srd", 0, NULL },
    { 952, "no-seedratio",           "Let the current torrent(s) seed regardless of ratio", "SR", 0, NULL },
    { 953, "global-seedratio",       "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio", "gsr", 1, "ratio" },
    { 954, "no-global-seedratio",    "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio", "GSR", 0, NULL },
    { 710, "tracker-add",            "Add a tracker to a torrent", "td", 1, "<tracker>" },
    { 712, "tracker-remove",         "Remove a tracker from a torrent", "tr", 1, "<trackerId>" },
    { 's', "start",                  "Start the current torrent(s)", "s",  0, NULL },
    { 'S', "stop",                   "Stop the current torrent(s)", "S",  0, NULL },
    { 't', "torrent",                "Set the current torrent(s)", "t",  1, "<torrent>" },
    { 990, "start-paused",           "Start added torrents paused", NULL, 0, NULL },
    { 991, "no-start-paused",        "Start added torrents unpaused", NULL, 0, NULL },
    { 992, "trash-torrent",          "Delete torrents after adding", NULL, 0, NULL },
    { 993, "no-trash-torrent",       "Do not delete torrents after adding", NULL, 0, NULL },
    { 984, "honor-session",          "Make the current torrent(s) honor the session limits", "hl",  0, NULL },
    { 985, "no-honor-session",       "Make the current torrent(s) not honor the session limits", "HL",  0, NULL },
    { 'u', "uplimit",                "Set the max upload speed in "SPEED_K_STR" for the current torrent(s) or globally", "u", 1, "<speed>" },
    { 'U', "no-uplimit",             "Disable max upload speed for the current torrent(s) or globally", "U", 0, NULL },
    { 830, "utp",                    "Enable uTP for peer connections", NULL, 0, NULL },
    { 831, "no-utp",                 "Disable uTP for peer connections", NULL, 0, NULL },
    { 'v', "verify",                 "Verify the current torrent(s)", "v",  0, NULL },
    { 'V', "version",                "Show version number and exit", "V", 0, NULL },
    { 'w', "download-dir",           "When adding a new torrent, set its download folder. Otherwise, set the default download folder", "w",  1, "<path>" },
    { 'x', "pex",                    "Enable peer exchange (PEX)", "x",  0, NULL },
    { 'X', "no-pex",                 "Disable peer exchange (PEX)", "X",  0, NULL },
    { 'y', "lpd",                    "Enable local peer discovery (LPD)", "y",  0, NULL },
    { 'Y', "no-lpd",                 "Disable local peer discovery (LPD)", "Y",  0, NULL },
    { 941, "peer-info",              "List the current torrent(s)' peers", "pi",  0, NULL },
    {   0, NULL,                     NULL, NULL, 0, NULL }
};

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage( ), opts );
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
        exit( EXIT_FAILURE );
    }
    return num;
}

enum
{
    MODE_TORRENT_START         = (1<<0),
    MODE_TORRENT_STOP          = (1<<1),
    MODE_TORRENT_VERIFY        = (1<<2),
    MODE_TORRENT_REANNOUNCE    = (1<<3),
    MODE_TORRENT_SET           = (1<<4),
    MODE_TORRENT_GET           = (1<<5),
    MODE_TORRENT_ADD           = (1<<6),
    MODE_TORRENT_REMOVE        = (1<<7),
    MODE_TORRENT_SET_LOCATION  = (1<<8),
    MODE_SESSION_SET           = (1<<9),
    MODE_SESSION_GET           = (1<<10),
    MODE_SESSION_STATS         = (1<<11),
    MODE_SESSION_CLOSE         = (1<<12),
    MODE_BLOCKLIST_UPDATE      = (1<<13),
    MODE_PORT_TEST             = (1<<14)
};

static int
getOptMode( int val )
{
    switch( val )
    {
        case TR_OPT_ERR:
        case TR_OPT_UNK:
        case 'a': /* add torrent */
        case 'b': /* debug */
        case 'n': /* auth */
        case 810: /* authenv */
        case 'N': /* netrc */
        case 't': /* set current torrent */
        case 'V': /* show version number */
            return 0;

        case 'c': /* incomplete-dir */
        case 'C': /* no-incomplete-dir */
        case 'e': /* cache */
        case 'm': /* portmap */
        case 'M': /* "no-portmap */
        case 'o': /* dht */
        case 'O': /* no-dht */
        case 'p': /* incoming peer port */
        case 'P': /* random incoming peer port */
        case 'x': /* pex */
        case 'X': /* no-pex */
        case 'y': /* lpd */
        case 'Y': /* no-lpd */
        case 800: /* torrent-done-script */
        case 801: /* no-torrent-done-script */
        case 830: /* utp */
        case 831: /* no-utp */
        case 970: /* alt-speed */
        case 971: /* no-alt-speed */
        case 972: /* alt-speed-downlimit */
        case 973: /* alt-speed-uplimit */
        case 974: /* alt-speed-scheduler */
        case 975: /* no-alt-speed-scheduler */
        case 976: /* alt-speed-time-begin */
        case 977: /* alt-speed-time-end */
        case 978: /* alt-speed-days */
        case 910: /* encryption-required */
        case 911: /* encryption-preferred */
        case 912: /* encryption-tolerated */
        case 953: /* global-seedratio */
        case 954: /* no-global-seedratio */
        case 990: /* start-paused */
        case 991: /* no-start-paused */
        case 992: /* trash-torrent */
        case 993: /* no-trash-torrent */
            return MODE_SESSION_SET;

        case 712: /* tracker-remove */
        case 950: /* seedratio */
        case 951: /* seedratio-default */
        case 952: /* no-seedratio */
        case 984: /* honor-session */
        case 985: /* no-honor-session */
            return MODE_TORRENT_SET;

        case 920: /* session-info */
            return MODE_SESSION_GET;

        case 'g': /* get */
        case 'G': /* no-get */
        case 700: /* torrent priority-high */
        case 701: /* torrent priority-normal */
        case 702: /* torrent priority-low */
        case 710: /* tracker-add */
        case 900: /* file priority-high */
        case 901: /* file priority-normal */
        case 902: /* file priority-low */
            return MODE_TORRENT_SET | MODE_TORRENT_ADD;

        case 961: /* find */
            return MODE_TORRENT_SET_LOCATION | MODE_TORRENT_ADD;

        case 'i': /* info */
        case 'l': /* list all torrents */
        case 940: /* info-files */
        case 941: /* info-peer */
        case 942: /* info-pieces */
        case 943: /* info-tracker */
            return MODE_TORRENT_GET;

        case 'd': /* download speed limit */
        case 'D': /* no download speed limit */
        case 'u': /* upload speed limit */
        case 'U': /* no upload speed limit */
        case 930: /* peers */
            return MODE_SESSION_SET | MODE_TORRENT_SET;

        case 's': /* start */
            return MODE_TORRENT_START | MODE_TORRENT_ADD;

        case 'S': /* stop */
            return MODE_TORRENT_STOP | MODE_TORRENT_ADD;

        case 'w': /* download-dir */
            return MODE_SESSION_SET | MODE_TORRENT_ADD;

        case 850: /* session-close */
            return MODE_SESSION_CLOSE;

        case 963: /* blocklist-update */
            return MODE_BLOCKLIST_UPDATE;

        case 921: /* session-stats */
            return MODE_SESSION_STATS;

        case 'v': /* verify */
            return MODE_TORRENT_VERIFY;

        case 600: /* reannounce */
            return MODE_TORRENT_REANNOUNCE;

        case 962: /* port-test */
            return MODE_PORT_TEST;

        case 'r': /* remove */
        case 'R': /* remove and delete */
            return MODE_TORRENT_REMOVE;

        case 960: /* move */
            return MODE_TORRENT_SET_LOCATION;

        default:
            fprintf( stderr, "unrecognized argument %d\n", val );
            assert( "unrecognized argument" && 0 );
            return 0;
    }
}

static bool debug = 0;
static char * auth = NULL;
static char * netrc = NULL;
static char * sessionId = NULL;

static char*
tr_getcwd( void )
{
    char * result;
    char buf[2048];
#ifdef WIN32
    result = _getcwd( buf, sizeof( buf ) );
#else
    result = getcwd( buf, sizeof( buf ) );
#endif
    if( result == NULL )
    {
        fprintf( stderr, "getcwd error: \"%s\"", tr_strerror( errno ) );
        *buf = '\0';
    }
    return tr_strdup( buf );
}

static char*
absolutify( const char * path )
{
    char * buf;

    if( *path == '/' )
        buf = tr_strdup( path );
    else {
        char * cwd = tr_getcwd( );
        buf = tr_buildPath( cwd, path, NULL );
        tr_free( cwd );
    }

    return buf;
}

static char*
getEncodedMetainfo( const char * filename )
{
    size_t    len = 0;
    char *    b64 = NULL;
    uint8_t * buf = tr_loadFile( filename, &len );

    if( buf )
    {
        b64 = tr_base64_encode( buf, len, NULL );
        tr_free( buf );
    }
    return b64;
}

static void
addIdArg( tr_benc * args, const char * id )
{
    if( !*id )
    {
        fprintf(
            stderr,
            "No torrent specified!  Please use the -t option first.\n" );
        id = "-1"; /* no torrent will have this ID, so should be a no-op */
    }
    if( strcmp( id, "all" ) )
    {
        const char * pch;
        bool isList = strchr(id,',') || strchr(id,'-');
        bool isNum = true;
        for( pch=id; isNum && *pch; ++pch )
            if( !isdigit( *pch ) )
                isNum = false;
        if( isNum || isList )
            tr_rpc_parse_list_str( tr_bencDictAdd( args, "ids" ), id, strlen( id ) );
        else
            tr_bencDictAddStr( args, "ids", id ); /* it's a torrent sha hash */
    }
}

static void
addTime( tr_benc * args, const char * key, const char * arg )
{
    int time;
    bool success = false;

    if( arg && ( strlen( arg ) == 4 ) )
    {
        const char hh[3] = { arg[0], arg[1], '\0' };
        const char mm[3] = { arg[2], arg[3], '\0' };
        const int hour = atoi( hh );
        const int min = atoi( mm );

        if( 0<=hour && hour<24 && 0<=min && min<60 )
        {
            time = min + ( hour * 60 );
            success = true;
        }
    }

    if( success )
        tr_bencDictAddInt( args, key, time );
    else
        fprintf( stderr, "Please specify the time of day in 'hhmm' format.\n" );
}

static void
addDays( tr_benc * args, const char * key, const char * arg )
{
    int days = 0;

    if( arg )
    {
        int i;
        int valueCount;
        int * values = tr_parseNumberRange( arg, -1, &valueCount );
        for( i=0; i<valueCount; ++i )
        {
            if ( values[i] < 0 || values[i] > 7 ) continue;
            if ( values[i] == 7 ) values[i] = 0;

            days |= 1 << values[i];
        }
        tr_free( values );
    }

    if ( days )
        tr_bencDictAddInt( args, key, days );
    else
        fprintf( stderr, "Please specify the days of the week in '1-3,4,7' format.\n" );
}

static void
addFiles( tr_benc *    args,
          const char * key,
          const char * arg )
{
    tr_benc * files = tr_bencDictAddList( args, key, 100 );

    if( !*arg )
    {
        fprintf( stderr, "No files specified!\n" );
        arg = "-1"; /* no file will have this index, so should be a no-op */
    }
    if( strcmp( arg, "all" ) )
    {
        int i;
        int valueCount;
        int * values = tr_parseNumberRange( arg, -1, &valueCount );
        for( i=0; i<valueCount; ++i )
            tr_bencListAddInt( files, values[i] );
        tr_free( values );
    }
}

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

static const char * files_keys[] = {
    "files",
    "name",
    "priorities",
    "wanted"
};

static const char * details_keys[] = {
    "activityDate",
    "addedDate",
    "bandwidthPriority",
    "comment",
    "corruptEver",
    "creator",
    "dateCreated",
    "desiredAvailable",
    "doneDate",
    "downloadDir",
    "downloadedEver",
    "downloadLimit",
    "downloadLimited",
    "error",
    "errorString",
    "eta",
    "hashString",
    "haveUnchecked",
    "haveValid",
    "honorsSessionLimits",
    "id",
    "isFinished",
    "isPrivate",
    "leftUntilDone",
    "name",
    "peersConnected",
    "peersGettingFromUs",
    "peersSendingToUs",
    "peer-limit",
    "pieceCount",
    "pieceSize",
    "rateDownload",
    "rateUpload",
    "recheckProgress",
    "secondsDownloading",
    "secondsSeeding",
    "seedRatioMode",
    "seedRatioLimit",
    "sizeWhenDone",
    "startDate",
    "status",
    "totalSize",
    "uploadedEver",
    "uploadLimit",
    "uploadLimited",
    "webseeds",
    "webseedsSendingToUs"
};

static const char * list_keys[] = {
    "error",
    "errorString",
    "eta",
    "id",
    "isFinished",
    "leftUntilDone",
    "name",
    "peersGettingFromUs",
    "peersSendingToUs",
    "rateDownload",
    "rateUpload",
    "sizeWhenDone",
    "status",
    "uploadRatio"
};

static size_t
writeFunc( void * ptr, size_t size, size_t nmemb, void * buf )
{
    const size_t byteCount = size * nmemb;
    evbuffer_add( buf, ptr, byteCount );
    return byteCount;
}

/* look for a session id in the header in case the server gives back a 409 */
static size_t
parseResponseHeader( void *ptr, size_t size, size_t nmemb, void * stream UNUSED )
{
    const char * line = ptr;
    const size_t line_len = size * nmemb;
    const char * key = TR_RPC_SESSION_ID_HEADER ": ";
    const size_t key_len = strlen( key );

    if( ( line_len >= key_len ) && !memcmp( line, key, key_len ) )
    {
        const char * begin = line + key_len;
        const char * end = begin;
        while( !isspace( *end ) )
            ++end;
        tr_free( sessionId );
        sessionId = tr_strndup( begin, end-begin );
    }

    return line_len;
}

static long
getTimeoutSecs( const char * req )
{
  if( strstr( req, "\"method\":\"blocklist-update\"" ) != NULL )
    return 300L;

  return 60L; /* default value */
}

static char*
getStatusString( tr_benc * t, char * buf, size_t buflen )
{
    int64_t status;
    bool boolVal;

    if( !tr_bencDictFindInt( t, "status", &status ) )
    {
        *buf = '\0';
    }
    else switch( status )
    {
        case TR_STATUS_STOPPED:
            if( tr_bencDictFindBool( t, "isFinished", &boolVal ) && boolVal )
                tr_strlcpy( buf, "Finished", buflen );
            else
                tr_strlcpy( buf, "Stopped", buflen );
            break;

        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK: {
            const char * str = status == TR_STATUS_CHECK_WAIT
                             ? "Will Verify"
                             : "Verifying";
            double percent;
            if( tr_bencDictFindReal( t, "recheckProgress", &percent ) )
                tr_snprintf( buf, buflen, "%s (%.0f%%)", str, floor(percent*100.0) );
            else
                tr_strlcpy( buf, str, buflen );

            break;
        }

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED: {
            int64_t fromUs = 0;
            int64_t toUs = 0;
            tr_bencDictFindInt( t, "peersGettingFromUs", &fromUs );
            tr_bencDictFindInt( t, "peersSendingToUs", &toUs );
            if( fromUs && toUs )
                tr_strlcpy( buf, "Up & Down", buflen );
            else if( toUs )
                tr_strlcpy( buf, "Downloading", buflen );
            else if( fromUs ) {
                int64_t leftUntilDone = 0;
                tr_bencDictFindInt( t, "leftUntilDone", &leftUntilDone );
                if( leftUntilDone > 0 )
                    tr_strlcpy( buf, "Uploading", buflen );
                else
                    tr_strlcpy( buf, "Seeding", buflen );
            } else {
                tr_strlcpy( buf, "Idle", buflen );
            }
            break;
        }
    }

    return buf;
}

static const char *bandwidthPriorityNames[] =
    { "Low", "Normal", "High", "Invalid" };

static void
printDetails( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) )
      && ( tr_bencDictFindList( args, "torrents", &torrents ) ) )
    {
        int ti, tCount;
        for( ti = 0, tCount = tr_bencListSize( torrents ); ti < tCount;
             ++ti )
        {
            tr_benc *    t = tr_bencListChild( torrents, ti );
            tr_benc *    l;
            const char * str;
            char         buf[512];
            char         buf2[512];
            int64_t      i, j, k;
            bool      boolVal;
            double       d;

            printf( "NAME\n" );
            if( tr_bencDictFindInt( t, "id", &i ) )
                printf( "  Id: %" PRId64 "\n", i );
            if( tr_bencDictFindStr( t, "name", &str ) )
                printf( "  Name: %s\n", str );
            if( tr_bencDictFindStr( t, "hashString", &str ) )
                printf( "  Hash: %s\n", str );
            printf( "\n" );

            printf( "TRANSFER\n" );
            getStatusString( t, buf, sizeof( buf ) );
            printf( "  State: %s\n", buf );

            if( tr_bencDictFindStr( t, "downloadDir", &str ) )
                printf( "  Location: %s\n", str );

            if( tr_bencDictFindInt( t, "sizeWhenDone", &i )
              && tr_bencDictFindInt( t, "leftUntilDone", &j ) )
            {
                strlpercent( buf, 100.0 * ( i - j ) / i, sizeof( buf ) );
                printf( "  Percent Done: %s%%\n", buf );
            }

            if( tr_bencDictFindInt( t, "eta", &i ) )
                printf( "  ETA: %s\n", tr_strltime( buf, i, sizeof( buf ) ) );
            if( tr_bencDictFindInt( t, "rateDownload", &i ) )
                printf( "  Download Speed: %s\n", tr_formatter_speed_KBps( buf, i/(double)tr_speed_K, sizeof( buf ) ) );
            if( tr_bencDictFindInt( t, "rateUpload", &i ) )
                printf( "  Upload Speed: %s\n", tr_formatter_speed_KBps( buf, i/(double)tr_speed_K, sizeof( buf ) ) );
            if( tr_bencDictFindInt( t, "haveUnchecked", &i )
              && tr_bencDictFindInt( t, "haveValid", &j ) )
            {
                strlsize( buf, i + j, sizeof( buf ) );
                strlsize( buf2, j, sizeof( buf2 ) );
                printf( "  Have: %s (%s verified)\n", buf, buf2 );
            }

            if( tr_bencDictFindInt( t, "sizeWhenDone", &i ) )
            {
                if( i < 1 )
                    printf( "  Availability: None\n" );
                if( tr_bencDictFindInt( t, "desiredAvailable", &j)
                    && tr_bencDictFindInt( t, "leftUntilDone", &k) )
                {
                    j += i - k;
                    strlpercent( buf, 100.0 * j / i, sizeof( buf ) );
                    printf( "  Availability: %s%%\n", buf );
                }
                if( tr_bencDictFindInt( t, "totalSize", &j ) )
                {
                    strlsize( buf2, i, sizeof( buf2 ) );
                    strlsize( buf, j, sizeof( buf ) );
                    printf( "  Total size: %s (%s wanted)\n", buf, buf2 );
                }
            }
            if( tr_bencDictFindInt( t, "downloadedEver", &i )
              && tr_bencDictFindInt( t, "uploadedEver", &j ) )
            {
                strlsize( buf, i, sizeof( buf ) );
                printf( "  Downloaded: %s\n", buf );
                strlsize( buf, j, sizeof( buf ) );
                printf( "  Uploaded: %s\n", buf );
                strlratio( buf, j, i, sizeof( buf ) );
                printf( "  Ratio: %s\n", buf );
            }
            if( tr_bencDictFindInt( t, "corruptEver", &i ) )
            {
                strlsize( buf, i, sizeof( buf ) );
                printf( "  Corrupt DL: %s\n", buf );
            }
            if( tr_bencDictFindStr( t, "errorString", &str ) && str && *str &&
                tr_bencDictFindInt( t, "error", &i ) && i )
            {
                switch( i ) {
                    case TR_STAT_TRACKER_WARNING: printf( "  Tracker gave a warning: %s\n", str ); break;
                    case TR_STAT_TRACKER_ERROR:   printf( "  Tracker gave an error: %s\n", str ); break;
                    case TR_STAT_LOCAL_ERROR:     printf( "  Error: %s\n", str ); break;
                    default: break; /* no error */
                }
            }
            if( tr_bencDictFindInt( t, "peersConnected", &i )
              && tr_bencDictFindInt( t, "peersGettingFromUs", &j )
              && tr_bencDictFindInt( t, "peersSendingToUs", &k ) )
            {
                printf(
                    "  Peers: "
                    "connected to %" PRId64 ", "
                                            "uploading to %" PRId64
                    ", "
                    "downloading from %"
                    PRId64 "\n",
                    i, j, k );
            }

            if( tr_bencDictFindList( t, "webseeds", &l )
              && tr_bencDictFindInt( t, "webseedsSendingToUs", &i ) )
            {
                const int64_t n = tr_bencListSize( l );
                if( n > 0 )
                    printf(
                        "  Web Seeds: downloading from %" PRId64 " of %"
                        PRId64
                        " web seeds\n", i, n );
            }
            printf( "\n" );

            printf( "HISTORY\n" );
            if( tr_bencDictFindInt( t, "addedDate", &i ) && i )
            {
                const time_t tt = i;
                printf( "  Date added:       %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "doneDate", &i ) && i )
            {
                const time_t tt = i;
                printf( "  Date finished:    %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "startDate", &i ) && i )
            {
                const time_t tt = i;
                printf( "  Date started:     %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "activityDate", &i ) && i )
            {
                const time_t tt = i;
                printf( "  Latest activity:  %s", ctime( &tt ) );
            }
            if( tr_bencDictFindInt( t, "secondsDownloading", &i ) && ( i > 0 ) )
                printf( "  Downloading Time: %s\n", tr_strltime( buf, i, sizeof( buf ) ) );
            if( tr_bencDictFindInt( t, "secondsSeeding", &i ) && ( i > 0 ) )
                printf( "  Seeding Time:     %s\n", tr_strltime( buf, i, sizeof( buf ) ) );
            printf( "\n" );

            printf( "ORIGINS\n" );
            if( tr_bencDictFindInt( t, "dateCreated", &i ) && i )
            {
                const time_t tt = i;
                printf( "  Date created: %s", ctime( &tt ) );
            }
            if( tr_bencDictFindBool( t, "isPrivate", &boolVal ) )
                printf( "  Public torrent: %s\n", ( boolVal ? "No" : "Yes" ) );
            if( tr_bencDictFindStr( t, "comment", &str ) && str && *str )
                printf( "  Comment: %s\n", str );
            if( tr_bencDictFindStr( t, "creator", &str ) && str && *str )
                printf( "  Creator: %s\n", str );
            if( tr_bencDictFindInt( t, "pieceCount", &i ) )
                printf( "  Piece Count: %" PRId64 "\n", i );
            if( tr_bencDictFindInt( t, "pieceSize", &i ) )
                printf( "  Piece Size: %s\n", strlmem( buf, i, sizeof( buf ) ) );
            printf( "\n" );

            printf( "LIMITS & BANDWIDTH\n" );
            if( tr_bencDictFindBool( t, "downloadLimited", &boolVal )
                && tr_bencDictFindInt( t, "downloadLimit", &i ) )
            {
                printf( "  Download Limit: " );
                if( boolVal )
                    printf( "%s\n", tr_formatter_speed_KBps( buf, i, sizeof( buf ) ) );
                else
                    printf( "Unlimited\n" );
            }
            if( tr_bencDictFindBool( t, "uploadLimited", &boolVal )
                && tr_bencDictFindInt( t, "uploadLimit", &i ) )
            {
                printf( "  Upload Limit: " );
                if( boolVal )
                    printf( "%s\n", tr_formatter_speed_KBps( buf, i, sizeof( buf ) ) );
                else
                    printf( "Unlimited\n" );
            }
            if( tr_bencDictFindInt( t, "seedRatioMode", &i))
            {
                switch( i ) {
                    case TR_RATIOLIMIT_GLOBAL:
                        printf( "  Ratio Limit: Default\n" );
                        break;
                    case TR_RATIOLIMIT_SINGLE:
                        if( tr_bencDictFindReal( t, "seedRatioLimit", &d))
                            printf( "  Ratio Limit: %.2f\n", d );
                        break;
                    case TR_RATIOLIMIT_UNLIMITED:
                        printf( "  Ratio Limit: Unlimited\n" );
                        break;
                    default: break;
                }
            }
            if( tr_bencDictFindBool( t, "honorsSessionLimits", &boolVal ) )
                printf( "  Honors Session Limits: %s\n", ( boolVal ? "Yes" : "No" ) );
            if( tr_bencDictFindInt ( t, "peer-limit", &i ) )
                printf( "  Peer limit: %" PRId64 "\n", i );
            if (tr_bencDictFindInt (t, "bandwidthPriority", &i))
                printf ("  Bandwidth Priority: %s\n",
                        bandwidthPriorityNames[(i + 1) & 3]);

            printf( "\n" );
        }
    }
}

static void
printFileList( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) )
      && ( tr_bencDictFindList( args, "torrents", &torrents ) ) )
    {
        int i, in;
        for( i = 0, in = tr_bencListSize( torrents ); i < in; ++i )
        {
            tr_benc *    d = tr_bencListChild( torrents, i );
            tr_benc *    files, *priorities, *wanteds;
            const char * name;
            if( tr_bencDictFindStr( d, "name", &name )
              && tr_bencDictFindList( d, "files", &files )
              && tr_bencDictFindList( d, "priorities", &priorities )
              && tr_bencDictFindList( d, "wanted", &wanteds ) )
            {
                int j = 0, jn = tr_bencListSize( files );
                printf( "%s (%d files):\n", name, jn );
                printf( "%3s  %4s %8s %3s %9s  %s\n", "#", "Done",
                        "Priority", "Get", "Size",
                        "Name" );
                for( j = 0, jn = tr_bencListSize( files ); j < jn; ++j )
                {
                    int64_t      have;
                    int64_t      length;
                    int64_t      priority;
                    int64_t      wanted;
                    const char * filename;
                    tr_benc *    file = tr_bencListChild( files, j );
                    if( tr_bencDictFindInt( file, "length", &length )
                      && tr_bencDictFindStr( file, "name", &filename )
                      && tr_bencDictFindInt( file, "bytesCompleted", &have )
                      && tr_bencGetInt( tr_bencListChild( priorities,
                                                          j ), &priority )
                      && tr_bencGetInt( tr_bencListChild( wanteds,
                                                          j ), &wanted ) )
                    {
                        char         sizestr[64];
                        double       percent = (double)have / length;
                        const char * pristr;
                        strlsize( sizestr, length, sizeof( sizestr ) );
                        switch( priority )
                        {
                            case TR_PRI_LOW:
                                pristr = "Low"; break;

                            case TR_PRI_HIGH:
                                pristr = "High"; break;

                            default:
                                pristr = "Normal"; break;
                        }
                        printf( "%3d: %3.0f%% %-8s %-3s %9s  %s\n",
                                j,
                                floor( 100.0 * percent ),
                                pristr,
                                ( wanted ? "Yes" : "No" ),
                                sizestr,
                                filename );
                    }
                }
            }
        }
    }
}

static void
printPeersImpl( tr_benc * peers )
{
    int i, n;
    printf( "%-20s  %-12s  %-5s %-6s  %-6s  %s\n",
            "Address", "Flags", "Done", "Down", "Up", "Client" );
    for( i = 0, n = tr_bencListSize( peers ); i < n; ++i )
    {
        double progress;
        const char * address, * client, * flagstr;
        int64_t rateToClient, rateToPeer;
        tr_benc * d = tr_bencListChild( peers, i );

        if( tr_bencDictFindStr( d, "address", &address )
          && tr_bencDictFindStr( d, "clientName", &client )
          && tr_bencDictFindReal( d, "progress", &progress )
          && tr_bencDictFindStr( d, "flagStr", &flagstr )
          && tr_bencDictFindInt( d, "rateToClient", &rateToClient )
          && tr_bencDictFindInt( d, "rateToPeer", &rateToPeer ) )
        {
            printf( "%-20s  %-12s  %-5.1f %6.1f  %6.1f  %s\n",
                    address, flagstr, (progress*100.0),
                    rateToClient / (double)tr_speed_K,
                    rateToPeer / (double)tr_speed_K,
                    client );
        }
    }
}

static void
printPeers( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( tr_bencDictFindDict( top, "arguments", &args )
      && tr_bencDictFindList( args, "torrents", &torrents ) )
    {
        int i, n;
        for( i=0, n=tr_bencListSize( torrents ); i<n; ++i )
        {
            tr_benc * peers;
            tr_benc * torrent = tr_bencListChild( torrents, i );
            if( tr_bencDictFindList( torrent, "peers", &peers ) ) {
                printPeersImpl( peers );
                if( i+1<n )
                    printf( "\n" );
            }
        }
    }
}

static void
printPiecesImpl( const uint8_t * raw, size_t rawlen, int64_t j )
{
    int i, k, len;
    char * str = tr_base64_decode( raw, rawlen, &len );
    printf( "  " );
    for( i=k=0; k<len; ++k ) {
        int e;
        for( e=0; i<j && e<8; ++e, ++i )
            printf( "%c", str[k] & (1<<(7-e)) ? '1' : '0' );
        printf( " " );
        if( !(i%64) )
            printf( "\n  " );
    }
    printf( "\n" );
    tr_free( str );
}

static void
printPieces( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( tr_bencDictFindDict( top, "arguments", &args )
      && tr_bencDictFindList( args, "torrents", &torrents ) )
    {
        int i, n;
        for( i=0, n=tr_bencListSize( torrents ); i<n; ++i )
        {
            int64_t j;
            const uint8_t * raw;
            size_t       rawlen;
            tr_benc * torrent = tr_bencListChild( torrents, i );
            if( tr_bencDictFindRaw( torrent, "pieces", &raw, &rawlen ) &&
                tr_bencDictFindInt( torrent, "pieceCount", &j ) ) {
                printPiecesImpl( raw, rawlen, j );
                if( i+1<n )
                    printf( "\n" );
            }
        }
    }
}

static void
printPortTest( tr_benc * top )
{
    tr_benc *args;
    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) )
    {
        bool      boolVal;

        if( tr_bencDictFindBool( args, "port-is-open", &boolVal ) )
            printf( "Port is open: %s\n", ( boolVal ? "Yes" : "No" ) );
    }
}

static void
printTorrentList( tr_benc * top )
{
    tr_benc *args, *list;

    if( ( tr_bencDictFindDict( top, "arguments", &args ) )
      && ( tr_bencDictFindList( args, "torrents", &list ) ) )
    {
        int i, n;
        int64_t total_size=0;
        double total_up=0, total_down=0;
        char haveStr[32];

        printf( "%-4s   %-4s  %9s  %-8s  %6s  %6s  %-5s  %-11s  %s\n",
                "ID", "Done", "Have", "ETA", "Up", "Down", "Ratio", "Status",
                "Name" );

        for( i = 0, n = tr_bencListSize( list ); i < n; ++i )
        {
            int64_t      id, eta, status, up, down;
            int64_t      sizeWhenDone, leftUntilDone;
            double       ratio;
            const char * name;
            tr_benc *   d = tr_bencListChild( list, i );
            if( tr_bencDictFindInt( d, "eta", &eta )
              && tr_bencDictFindInt( d, "id", &id )
              && tr_bencDictFindInt( d, "leftUntilDone", &leftUntilDone )
              && tr_bencDictFindStr( d, "name", &name )
              && tr_bencDictFindInt( d, "rateDownload", &down )
              && tr_bencDictFindInt( d, "rateUpload", &up )
              && tr_bencDictFindInt( d, "sizeWhenDone", &sizeWhenDone )
              && tr_bencDictFindInt( d, "status", &status )
              && tr_bencDictFindReal( d, "uploadRatio", &ratio ) )
            {
                char etaStr[16];
                char statusStr[64];
                char ratioStr[32];
                char doneStr[8];
                int64_t error;
                char errorMark;

                if( sizeWhenDone )
                    tr_snprintf( doneStr, sizeof( doneStr ), "%d%%", (int)( 100.0 * ( sizeWhenDone - leftUntilDone ) / sizeWhenDone ) );
                else
                    tr_strlcpy( doneStr, "n/a", sizeof( doneStr ) );

                strlsize( haveStr, sizeWhenDone - leftUntilDone, sizeof( haveStr ) );

                if( leftUntilDone || eta != -1 )
                    etaToString( etaStr, sizeof( etaStr ), eta );
                else
                    tr_snprintf( etaStr, sizeof( etaStr ), "Done" );
                if( tr_bencDictFindInt( d, "error", &error ) && error )
                    errorMark = '*';
                else
                    errorMark = ' ';
                printf(
                    "%4d%c  %4s  %9s  %-8s  %6.1f  %6.1f  %5s  %-11s  %s\n",
                    (int)id, errorMark,
                    doneStr,
                    haveStr,
                    etaStr,
                    up/(double)tr_speed_K,
                    down/(double)tr_speed_K,
                    strlratio2( ratioStr, ratio, sizeof( ratioStr ) ),
                    getStatusString( d, statusStr, sizeof( statusStr ) ),
                    name );

                total_up += up;
                total_down += down;
                total_size += sizeWhenDone - leftUntilDone;
            }
        }

        printf( "Sum:         %9s            %6.1f  %6.1f\n",
                strlsize( haveStr, total_size, sizeof( haveStr ) ),
                total_up/(double)tr_speed_K,
                total_down/(double)tr_speed_K );
    }
}

static void
printTrackersImpl( tr_benc * trackerStats )
{
    int i;
    char         buf[512];
    tr_benc * t;

    for( i=0; (( t = tr_bencListChild( trackerStats, i ))); ++i )
    {
        int64_t downloadCount;
        bool hasAnnounced;
        bool hasScraped;
        const char * host;
        int64_t id;
        bool isBackup;
        int64_t lastAnnouncePeerCount;
        const char * lastAnnounceResult;
        int64_t lastAnnounceStartTime;
        bool lastAnnounceSucceeded;
        int64_t lastAnnounceTime;
        bool lastAnnounceTimedOut;
        const char * lastScrapeResult;
        bool lastScrapeSucceeded;
        int64_t lastScrapeStartTime;
        int64_t lastScrapeTime;
        bool lastScrapeTimedOut;
        int64_t leecherCount;
        int64_t nextAnnounceTime;
        int64_t nextScrapeTime;
        int64_t seederCount;
        int64_t tier;
        int64_t announceState;
        int64_t scrapeState;

        if( tr_bencDictFindInt ( t, "downloadCount", &downloadCount ) &&
            tr_bencDictFindBool( t, "hasAnnounced", &hasAnnounced ) &&
            tr_bencDictFindBool( t, "hasScraped", &hasScraped ) &&
            tr_bencDictFindStr ( t, "host", &host ) &&
            tr_bencDictFindInt ( t, "id", &id ) &&
            tr_bencDictFindBool( t, "isBackup", &isBackup ) &&
            tr_bencDictFindInt ( t, "announceState", &announceState ) &&
            tr_bencDictFindInt ( t, "scrapeState", &scrapeState ) &&
            tr_bencDictFindInt ( t, "lastAnnouncePeerCount", &lastAnnouncePeerCount ) &&
            tr_bencDictFindStr ( t, "lastAnnounceResult", &lastAnnounceResult ) &&
            tr_bencDictFindInt ( t, "lastAnnounceStartTime", &lastAnnounceStartTime ) &&
            tr_bencDictFindBool( t, "lastAnnounceSucceeded", &lastAnnounceSucceeded ) &&
            tr_bencDictFindInt ( t, "lastAnnounceTime", &lastAnnounceTime ) &&
            tr_bencDictFindBool( t, "lastAnnounceTimedOut", &lastAnnounceTimedOut ) &&
            tr_bencDictFindStr ( t, "lastScrapeResult", &lastScrapeResult ) &&
            tr_bencDictFindInt ( t, "lastScrapeStartTime", &lastScrapeStartTime ) &&
            tr_bencDictFindBool( t, "lastScrapeSucceeded", &lastScrapeSucceeded ) &&
            tr_bencDictFindInt ( t, "lastScrapeTime", &lastScrapeTime ) &&
            tr_bencDictFindBool( t, "lastScrapeTimedOut", &lastScrapeTimedOut ) &&
            tr_bencDictFindInt ( t, "leecherCount", &leecherCount ) &&
            tr_bencDictFindInt ( t, "nextAnnounceTime", &nextAnnounceTime ) &&
            tr_bencDictFindInt ( t, "nextScrapeTime", &nextScrapeTime ) &&
            tr_bencDictFindInt ( t, "seederCount", &seederCount ) &&
            tr_bencDictFindInt ( t, "tier", &tier ) )
        {
            const time_t now = time( NULL );

            printf( "\n" );
            printf( "  Tracker %d: %s\n", (int)(id), host );
            if( isBackup )
                printf( "  Backup on tier %d\n", (int)tier );
            else
                printf( "  Active in tier %d\n", (int)tier );

            if( !isBackup )
            {
                if( hasAnnounced && announceState != TR_TRACKER_INACTIVE )
                {
                    tr_strltime( buf, now - lastAnnounceTime, sizeof( buf ) );
                    if( lastAnnounceSucceeded )
                        printf( "  Got a list of %d peers %s ago\n",
                                (int)lastAnnouncePeerCount, buf );
                    else if( lastAnnounceTimedOut )
                        printf( "  Peer list request timed out; will retry\n" );
                    else
                        printf( "  Got an error \"%s\" %s ago\n",
                                lastAnnounceResult, buf );
                }

                switch( announceState )
                {
                    case TR_TRACKER_INACTIVE:
                        printf( "  No updates scheduled\n" );
                        break;
                    case TR_TRACKER_WAITING:
                        tr_strltime( buf, nextAnnounceTime - now, sizeof( buf ) );
                        printf( "  Asking for more peers in %s\n", buf );
                        break;
                    case TR_TRACKER_QUEUED:
                        printf( "  Queued to ask for more peers\n" );
                        break;
                    case TR_TRACKER_ACTIVE:
                        tr_strltime( buf, now - lastAnnounceStartTime, sizeof( buf ) );
                        printf( "  Asking for more peers now... %s\n", buf );
                        break;
                }

                if( hasScraped )
                {
                    tr_strltime( buf, now - lastScrapeTime, sizeof( buf ) );
                    if( lastScrapeSucceeded )
                        printf( "  Tracker had %d seeders and %d leechers %s ago\n",
                                (int)seederCount, (int)leecherCount, buf );
                    else if( lastScrapeTimedOut )
                        printf( "  Tracker scrape timed out; will retry\n" );
                    else
                        printf( "  Got a scrape error \"%s\" %s ago\n",
                                lastScrapeResult, buf );
                }

                switch( scrapeState )
                {
                    case TR_TRACKER_INACTIVE:
                        break;
                    case TR_TRACKER_WAITING:
                        tr_strltime( buf, nextScrapeTime - now, sizeof( buf ) );
                        printf( "  Asking for peer counts in %s\n", buf );
                        break;
                    case TR_TRACKER_QUEUED:
                        printf( "  Queued to ask for peer counts\n" );
                        break;
                    case TR_TRACKER_ACTIVE:
                        tr_strltime( buf, now - lastScrapeStartTime, sizeof( buf ) );
                        printf( "  Asking for peer counts now... %s\n", buf );
                        break;
                }
            }
        }
    }
}

static void
printTrackers( tr_benc * top )
{
    tr_benc *args, *torrents;

    if( tr_bencDictFindDict( top, "arguments", &args )
      && tr_bencDictFindList( args, "torrents", &torrents ) )
    {
        int i, n;
        for( i=0, n=tr_bencListSize( torrents ); i<n; ++i )
        {
            tr_benc * trackerStats;
            tr_benc * torrent = tr_bencListChild( torrents, i );
            if( tr_bencDictFindList( torrent, "trackerStats", &trackerStats ) ) {
                printTrackersImpl( trackerStats );
                if( i+1<n )
                    printf( "\n" );
            }
        }
    }
}

static void
printSession( tr_benc * top )
{
    tr_benc *args;
    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) )
    {
        int64_t i;
        char buf[64];
        bool boolVal;
        const char * str;

        printf( "VERSION\n" );
        if( tr_bencDictFindStr( args,  "version", &str ) )
            printf( "  Daemon version: %s\n", str );
        if( tr_bencDictFindInt( args, "rpc-version", &i ) )
            printf( "  RPC version: %" PRId64 "\n", i );
        if( tr_bencDictFindInt( args, "rpc-version-minimum", &i ) )
            printf( "  RPC minimum version: %" PRId64 "\n", i );
        printf( "\n" );

        printf( "CONFIG\n" );
        if( tr_bencDictFindStr( args, "config-dir", &str ) )
            printf( "  Configuration directory: %s\n", str );
        if( tr_bencDictFindStr( args,  TR_PREFS_KEY_DOWNLOAD_DIR, &str ) )
            printf( "  Download directory: %s\n", str );
        if( tr_bencDictFindInt( args,  "download-dir-free-space", &i ) )
            printf( "  Download directory free space: %s\n",  strlsize( buf, i, sizeof buf ) );
        if( tr_bencDictFindInt( args, TR_PREFS_KEY_PEER_PORT, &i ) )
            printf( "  Listenport: %" PRId64 "\n", i );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_PORT_FORWARDING, &boolVal ) )
            printf( "  Portforwarding enabled: %s\n", ( boolVal ? "Yes" : "No" ) );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_DHT_ENABLED, &boolVal ) )
            printf( "  Distributed hash table enabled: %s\n", ( boolVal ? "Yes" : "No" ) );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_LPD_ENABLED, &boolVal ) )
            printf( "  Local peer discovery enabled: %s\n", ( boolVal ? "Yes" : "No" ) );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_PEX_ENABLED, &boolVal ) )
            printf( "  Peer exchange allowed: %s\n", ( boolVal ? "Yes" : "No" ) );
        if( tr_bencDictFindStr( args,  TR_PREFS_KEY_ENCRYPTION, &str ) )
            printf( "  Encryption: %s\n", str );
        if( tr_bencDictFindInt( args, TR_PREFS_KEY_MAX_CACHE_SIZE_MB, &i ) )
            printf( "  Maximum memory cache size: %s\n", tr_formatter_mem_MB( buf, i, sizeof( buf ) ) );
        printf( "\n" );

        {
            bool altEnabled, altTimeEnabled, upEnabled, downEnabled, seedRatioLimited;
            int64_t altDown, altUp, altBegin, altEnd, altDay, upLimit, downLimit, peerLimit;
            double seedRatioLimit;

            if( tr_bencDictFindInt ( args, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, &altDown ) &&
                tr_bencDictFindBool( args, TR_PREFS_KEY_ALT_SPEED_ENABLED, &altEnabled ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, &altBegin ) &&
                tr_bencDictFindBool( args, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, &altTimeEnabled ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_ALT_SPEED_TIME_END, &altEnd ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, &altDay ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_ALT_SPEED_UP_KBps, &altUp ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &peerLimit ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_DSPEED_KBps, &downLimit ) &&
                tr_bencDictFindBool( args, TR_PREFS_KEY_DSPEED_ENABLED, &downEnabled ) &&
                tr_bencDictFindInt ( args, TR_PREFS_KEY_USPEED_KBps, &upLimit ) &&
                tr_bencDictFindBool( args, TR_PREFS_KEY_USPEED_ENABLED, &upEnabled ) &&
                tr_bencDictFindReal( args, "seedRatioLimit", &seedRatioLimit ) &&
                tr_bencDictFindBool( args, "seedRatioLimited", &seedRatioLimited) )
            {
                char buf[128];
                char buf2[128];
                char buf3[128];

                printf( "LIMITS\n" );
                printf( "  Peer limit: %" PRId64 "\n", peerLimit );

                if( seedRatioLimited )
                    tr_snprintf( buf, sizeof( buf ), "%.2f", seedRatioLimit );
                else
                    tr_strlcpy( buf, "Unlimited", sizeof( buf ) );
                printf( "  Default seed ratio limit: %s\n", buf );

                if( altEnabled )
                    tr_formatter_speed_KBps( buf, altUp, sizeof( buf ) );
                else if( upEnabled )
                    tr_formatter_speed_KBps( buf, upLimit, sizeof( buf ) );
                else
                    tr_strlcpy( buf, "Unlimited", sizeof( buf ) );
                printf( "  Upload speed limit: %s  (%s limit: %s; %s turtle limit: %s)\n",
                        buf,
                        upEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps( buf2, upLimit, sizeof( buf2 ) ),
                        altEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps( buf3, altUp, sizeof( buf3 ) ) );

                if( altEnabled )
                    tr_formatter_speed_KBps( buf, altDown, sizeof( buf ) );
                else if( downEnabled )
                    tr_formatter_speed_KBps( buf, downLimit, sizeof( buf ) );
                else
                    tr_strlcpy( buf, "Unlimited", sizeof( buf ) );
                printf( "  Download speed limit: %s  (%s limit: %s; %s turtle limit: %s)\n",
                        buf,
                        downEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps( buf2, downLimit, sizeof( buf2 ) ),
                        altEnabled ? "Enabled" : "Disabled",
                        tr_formatter_speed_KBps( buf2, altDown, sizeof( buf2 ) ) );

                if( altTimeEnabled ) {
                    printf( "  Turtle schedule: %02d:%02d - %02d:%02d  ",
                            (int)(altBegin/60), (int)(altBegin%60),
                            (int)(altEnd/60), (int)(altEnd%60) );
                    if( altDay & TR_SCHED_SUN )   printf( "Sun " );
                    if( altDay & TR_SCHED_MON )   printf( "Mon " );
                    if( altDay & TR_SCHED_TUES )  printf( "Tue " );
                    if( altDay & TR_SCHED_WED )   printf( "Wed " );
                    if( altDay & TR_SCHED_THURS ) printf( "Thu " );
                    if( altDay & TR_SCHED_FRI )   printf( "Fri " );
                    if( altDay & TR_SCHED_SAT )   printf( "Sat " );
                    printf( "\n" );
                }
            }
        }
        printf( "\n" );

        printf( "MISC\n" );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_START, &boolVal ) )
            printf( "  Autostart added torrents: %s\n", ( boolVal ? "Yes" : "No" ) );
        if( tr_bencDictFindBool( args, TR_PREFS_KEY_TRASH_ORIGINAL, &boolVal ) )
            printf( "  Delete automatically added torrents: %s\n", ( boolVal ? "Yes" : "No" ) );
    }
}

static void
printSessionStats( tr_benc * top )
{
    tr_benc *args, *d;
    if( ( tr_bencDictFindDict( top, "arguments", &args ) ) )
    {
        char buf[512];
        int64_t up, down, secs, sessions;

        if( tr_bencDictFindDict( args, "current-stats", &d )
            && tr_bencDictFindInt( d, "uploadedBytes", &up )
            && tr_bencDictFindInt( d, "downloadedBytes", &down )
            && tr_bencDictFindInt( d, "secondsActive", &secs ) )
        {
            printf( "\nCURRENT SESSION\n" );
            printf( "  Uploaded:   %s\n", strlsize( buf, up, sizeof( buf ) ) );
            printf( "  Downloaded: %s\n", strlsize( buf, down, sizeof( buf ) ) );
            printf( "  Ratio:      %s\n", strlratio( buf, up, down, sizeof( buf ) ) );
            printf( "  Duration:   %s\n", tr_strltime( buf, secs, sizeof( buf ) ) );
        }

        if( tr_bencDictFindDict( args, "cumulative-stats", &d )
            && tr_bencDictFindInt( d, "sessionCount", &sessions )
            && tr_bencDictFindInt( d, "uploadedBytes", &up )
            && tr_bencDictFindInt( d, "downloadedBytes", &down )
            && tr_bencDictFindInt( d, "secondsActive", &secs ) )
        {
            printf( "\nTOTAL\n" );
            printf( "  Started %lu times\n", (unsigned long)sessions );
            printf( "  Uploaded:   %s\n", strlsize( buf, up, sizeof( buf ) ) );
            printf( "  Downloaded: %s\n", strlsize( buf, down, sizeof( buf ) ) );
            printf( "  Ratio:      %s\n", strlratio( buf, up, down, sizeof( buf ) ) );
            printf( "  Duration:   %s\n", tr_strltime( buf, secs, sizeof( buf ) ) );
        }
    }
}

static char id[4096];

static int
processResponse( const char * rpcurl, const void * response, size_t len )
{
    tr_benc top;
    int status = EXIT_SUCCESS;

    if( debug )
        fprintf( stderr, "got response (len %d):\n--------\n%*.*s\n--------\n",
                 (int)len, (int)len, (int)len, (const char*) response );

    if( tr_jsonParse( NULL, response, len, &top, NULL ) )
    {
        tr_nerr( MY_NAME, "Unable to parse response \"%*.*s\"", (int)len,
                 (int)len, (char*)response );
        status |= EXIT_FAILURE;
    }
    else
    {
        int64_t      tag = -1;
        const char * str;
        tr_bencDictFindInt( &top, "tag", &tag );

        switch( tag )
        {
            case TAG_SESSION:
                printSession( &top ); break;

            case TAG_STATS:
                printSessionStats( &top ); break;

            case TAG_DETAILS:
                printDetails( &top ); break;

            case TAG_FILES:
                printFileList( &top ); break;

            case TAG_LIST:
                printTorrentList( &top ); break;

            case TAG_PEERS:
                printPeers( &top ); break;

            case TAG_PIECES:
                printPieces( &top ); break;

            case TAG_PORTTEST:
                printPortTest( &top ); break;

            case TAG_TRACKERS:
                printTrackers( &top ); break;

            case TAG_TORRENT_ADD: {
                int64_t i;
                tr_benc * b = &top;
                if( tr_bencDictFindDict( &top, ARGUMENTS, &b )
                        && tr_bencDictFindDict( b, "torrent-added", &b )
                        && tr_bencDictFindInt( b, "id", &i ) )
                    tr_snprintf( id, sizeof(id), "%"PRId64, i );
                /* fall-through to default: to give success or failure msg */
            }
            default:
                if( !tr_bencDictFindStr( &top, "result", &str ) )
                    status |= EXIT_FAILURE;
                else {
                    printf( "%s responded: \"%s\"\n", rpcurl, str );
                    if( strcmp( str, "success") )
                        status |= EXIT_FAILURE;
                }
        }

        tr_bencFree( &top );
    }

    return status;
}

static CURL*
tr_curl_easy_init( struct evbuffer * writebuf )
{
    CURL * curl = curl_easy_init( );
    curl_easy_setopt( curl, CURLOPT_USERAGENT, MY_NAME "/" LONG_VERSION_STRING );
    curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, writeFunc );
    curl_easy_setopt( curl, CURLOPT_WRITEDATA, writebuf );
    curl_easy_setopt( curl, CURLOPT_HEADERFUNCTION, parseResponseHeader );
    curl_easy_setopt( curl, CURLOPT_POST, 1 );
    curl_easy_setopt( curl, CURLOPT_NETRC, CURL_NETRC_OPTIONAL );
    curl_easy_setopt( curl, CURLOPT_HTTPAUTH, CURLAUTH_ANY );
    curl_easy_setopt( curl, CURLOPT_VERBOSE, debug );
    curl_easy_setopt( curl, CURLOPT_ENCODING, "" ); /* "" tells curl to fill in the blanks with what it was compiled to support */
    if( netrc )
        curl_easy_setopt( curl, CURLOPT_NETRC_FILE, netrc );
    if( auth )
        curl_easy_setopt( curl, CURLOPT_USERPWD, auth );
    if( sessionId ) {
        char * h = tr_strdup_printf( "%s: %s", TR_RPC_SESSION_ID_HEADER, sessionId );
        struct curl_slist * custom_headers = curl_slist_append( NULL, h );
        curl_easy_setopt( curl, CURLOPT_HTTPHEADER, custom_headers );
        /* fixme: leaks */
    }
    return curl;
}

static int
flush( const char * rpcurl, tr_benc ** benc )
{
    CURLcode res;
    CURL * curl;
    int status = EXIT_SUCCESS;
    struct evbuffer * buf = evbuffer_new( );
    char * json = tr_bencToStr( *benc, TR_FMT_JSON_LEAN, NULL );

    curl = tr_curl_easy_init( buf );
    curl_easy_setopt( curl, CURLOPT_URL, rpcurl );
    curl_easy_setopt( curl, CURLOPT_POSTFIELDS, json );
    curl_easy_setopt( curl, CURLOPT_TIMEOUT, getTimeoutSecs( json ) );

    if( debug )
        fprintf( stderr, "posting:\n--------\n%s\n--------\n", json );

    if(( res = curl_easy_perform( curl )))
    {
        tr_nerr( MY_NAME, "(%s) %s", rpcurl, curl_easy_strerror( res ) );
        status |= EXIT_FAILURE;
    }
    else
    {
        long response;
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &response );
        switch( response ) {
            case 200:
                status |= processResponse( rpcurl, (const char*) evbuffer_pullup( buf, -1 ), evbuffer_get_length( buf ) );
                break;
            case 409:
                /* Session id failed. Our curl header func has already
                 * pulled the new session id from this response's headers,
                 * build a new CURL* and try again */
                curl_easy_cleanup( curl );
                curl = NULL;
                flush( rpcurl, benc );
                benc = NULL;
                break;
            default:
                fprintf( stderr, "Unexpected response: %s\n", evbuffer_pullup( buf, -1 ) );
                status |= EXIT_FAILURE;
                break;
        }
    }

    /* cleanup */
    tr_free( json );
    evbuffer_free( buf );
    if( curl != 0 )
        curl_easy_cleanup( curl );
    if( benc != NULL ) {
        tr_bencFree( *benc );
        *benc = 0;
    }
    return status;
}

static tr_benc*
ensure_sset( tr_benc ** sset )
{
    tr_benc * args;

    if( *sset )
        args = tr_bencDictFind( *sset, ARGUMENTS );
    else {
        *sset = tr_new0( tr_benc, 1 );
        tr_bencInitDict( *sset, 3 );
        tr_bencDictAddStr( *sset, "method", "session-set" );
        args = tr_bencDictAddDict( *sset, ARGUMENTS, 0 );
    }

    return args;
}

static tr_benc*
ensure_tset( tr_benc ** tset )
{
    tr_benc * args;

    if( *tset )
        args = tr_bencDictFind( *tset, ARGUMENTS );
    else {
        *tset = tr_new0( tr_benc, 1 );
        tr_bencInitDict( *tset, 3 );
        tr_bencDictAddStr( *tset, "method", "torrent-set" );
        args = tr_bencDictAddDict( *tset, ARGUMENTS, 1 );
    }

    return args;
}

static int
processArgs( const char * rpcurl, int argc, const char ** argv )
{
    int c;
    int status = EXIT_SUCCESS;
    const char * optarg;
    tr_benc *sset = 0;
    tr_benc *tset = 0;
    tr_benc *tadd = 0;

    *id = '\0';

    while(( c = tr_getopt( getUsage( ), argc, argv, opts, &optarg )))
    {
        const int stepMode = getOptMode( c );

        if( !stepMode ) /* meta commands */
        {
            switch( c )
            {
                case 'a': /* add torrent */
                    if( sset != 0 ) status |= flush( rpcurl, &sset );
                    if( tadd != 0 ) status |= flush( rpcurl, &tadd );
                    if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }
                    tadd = tr_new0( tr_benc, 1 );
                    tr_bencInitDict( tadd, 3 );
                    tr_bencDictAddStr( tadd, "method", "torrent-add" );
                    tr_bencDictAddInt( tadd, "tag", TAG_TORRENT_ADD );
                    tr_bencDictAddDict( tadd, ARGUMENTS, 0 );
                    break;

                case 'b': /* debug */
                    debug = true;
                    break;

                case 'n': /* auth */
                    auth = tr_strdup( optarg );
                    break;

                case 810: /* authenv */
                    {
                        char *authenv = getenv("TR_AUTH");
                        if( !authenv ) {
                            fprintf( stderr, "The TR_AUTH environment variable is not set\n" );
                            exit( 0 );
                        }
                        auth = tr_strdup( authenv );
                    }
                    break;

                case 'N': /* netrc */
                    netrc = tr_strdup( optarg );
                    break;

                case 't': /* set current torrent */
                    if( tadd != 0 ) status |= flush( rpcurl, &tadd );
                    if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }
                    tr_strlcpy( id, optarg, sizeof( id ) );
                    break;

                case 'V': /* show version number */
                    fprintf( stderr, "%s %s\n", MY_NAME, LONG_VERSION_STRING );
                    exit( 0 );
                    break;

                case TR_OPT_ERR:
                    fprintf( stderr, "invalid option\n" );
                    showUsage( );
                    status |= EXIT_FAILURE;
                    break;

                case TR_OPT_UNK:
                    if( tadd ) {
                        tr_benc * args = tr_bencDictFind( tadd, ARGUMENTS );
                        char * tmp = getEncodedMetainfo( optarg );
                        if( tmp )
                            tr_bencDictAddStr( args, "metainfo", tmp );
                        else
                            tr_bencDictAddStr( args, "filename", optarg );
                        tr_free( tmp );
                    } else {
                        fprintf( stderr, "Unknown option: %s\n", optarg );
                        status |= EXIT_FAILURE;
                    }
                    break;
            }
        }
        else if( stepMode == MODE_TORRENT_GET )
        {
            size_t i, n;
            tr_benc * top = tr_new0( tr_benc, 1 );
            tr_benc * args;
            tr_benc * fields;
            tr_bencInitDict( top, 3 );
            tr_bencDictAddStr( top, "method", "torrent-get" );
            args = tr_bencDictAddDict( top, ARGUMENTS, 0 );
            fields = tr_bencDictAddList( args, "fields", 0 );

            if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }

            switch( c )
            {
                case 'i': tr_bencDictAddInt( top, "tag", TAG_DETAILS );
                          n = TR_N_ELEMENTS( details_keys );
                          for( i=0; i<n; ++i ) tr_bencListAddStr( fields, details_keys[i] );
                          addIdArg( args, id );
                          break;
                case 'l': tr_bencDictAddInt( top, "tag", TAG_LIST );
                          n = TR_N_ELEMENTS( list_keys );
                          for( i=0; i<n; ++i ) tr_bencListAddStr( fields, list_keys[i] );
                          break;
                case 940: tr_bencDictAddInt( top, "tag", TAG_FILES );
                          n = TR_N_ELEMENTS( files_keys );
                          for( i=0; i<n; ++i ) tr_bencListAddStr( fields, files_keys[i] );
                          addIdArg( args, id );
                          break;
                case 941: tr_bencDictAddInt( top, "tag", TAG_PEERS );
                          tr_bencListAddStr( fields, "peers" );
                          addIdArg( args, id );
                          break;
                case 942: tr_bencDictAddInt( top, "tag", TAG_PIECES );
                          tr_bencListAddStr( fields, "pieces" );
                          tr_bencListAddStr( fields, "pieceCount" );
                          addIdArg( args, id );
                          break;
                case 943: tr_bencDictAddInt( top, "tag", TAG_TRACKERS );
                          tr_bencListAddStr( fields, "trackerStats" );
                          addIdArg( args, id );
                          break;
                default:  assert( "unhandled value" && 0 );
            }

            status |= flush( rpcurl, &top );
        }
        else if( stepMode == MODE_SESSION_SET )
        {
            tr_benc * args = ensure_sset( &sset );

            switch( c )
            {
                case 800: tr_bencDictAddStr( args, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_FILENAME, optarg );
                          tr_bencDictAddBool( args, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, true );
                          break;
                case 801: tr_bencDictAddBool( args, TR_PREFS_KEY_SCRIPT_TORRENT_DONE_ENABLED, false );
                          break;
                case 970: tr_bencDictAddBool( args, TR_PREFS_KEY_ALT_SPEED_ENABLED, true );
                          break;
                case 971: tr_bencDictAddBool( args, TR_PREFS_KEY_ALT_SPEED_ENABLED, false );
                          break;
                case 972: tr_bencDictAddInt( args, TR_PREFS_KEY_ALT_SPEED_DOWN_KBps, numarg( optarg ) );
                          break;
                case 973: tr_bencDictAddInt( args, TR_PREFS_KEY_ALT_SPEED_UP_KBps, numarg( optarg ) );
                          break;
                case 974: tr_bencDictAddBool( args, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, true );
                          break;
                case 975: tr_bencDictAddBool( args, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, false );
                          break;
                case 976: addTime( args, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, optarg );
                          break;
                case 977: addTime( args, TR_PREFS_KEY_ALT_SPEED_TIME_END, optarg );
                          break;
                case 978: addDays( args, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, optarg );
                          break;
                case 'c': tr_bencDictAddStr( args, TR_PREFS_KEY_INCOMPLETE_DIR, optarg );
                          tr_bencDictAddBool( args, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, true );
                          break;
                case 'C': tr_bencDictAddBool( args, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, false );
                          break;
                case 'e': tr_bencDictAddInt( args, TR_PREFS_KEY_MAX_CACHE_SIZE_MB, atoi(optarg) );
                          break;
                case 910: tr_bencDictAddStr( args, TR_PREFS_KEY_ENCRYPTION, "required" );
                          break;
                case 911: tr_bencDictAddStr( args, TR_PREFS_KEY_ENCRYPTION, "preferred" );
                          break;
                case 912: tr_bencDictAddStr( args, TR_PREFS_KEY_ENCRYPTION, "tolerated" );
                          break;
                case 'm': tr_bencDictAddBool( args, TR_PREFS_KEY_PORT_FORWARDING, true );
                          break;
                case 'M': tr_bencDictAddBool( args, TR_PREFS_KEY_PORT_FORWARDING, false );
                          break;
                case 'o': tr_bencDictAddBool( args, TR_PREFS_KEY_DHT_ENABLED, true );
                          break;
                case 'O': tr_bencDictAddBool( args, TR_PREFS_KEY_DHT_ENABLED, false );
                          break;
                case 830: tr_bencDictAddBool( args, TR_PREFS_KEY_UTP_ENABLED, true );
                          break;
                case 831: tr_bencDictAddBool( args, TR_PREFS_KEY_UTP_ENABLED, false );
                          break;
                case 'p': tr_bencDictAddInt( args, TR_PREFS_KEY_PEER_PORT, numarg( optarg ) );
                          break;
                case 'P': tr_bencDictAddBool( args, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, true);
                          break;
                case 'x': tr_bencDictAddBool( args, TR_PREFS_KEY_PEX_ENABLED, true );
                          break;
                case 'X': tr_bencDictAddBool( args, TR_PREFS_KEY_PEX_ENABLED, false );
                          break;
                case 'y': tr_bencDictAddBool( args, TR_PREFS_KEY_LPD_ENABLED, true );
                          break;
                case 'Y': tr_bencDictAddBool( args, TR_PREFS_KEY_LPD_ENABLED, false );
                          break;
                case 953: tr_bencDictAddReal( args, "seedRatioLimit", atof(optarg) );
                          tr_bencDictAddBool( args, "seedRatioLimited", true );
                          break;
                case 954: tr_bencDictAddBool( args, "seedRatioLimited", false );
                          break;
                case 990: tr_bencDictAddBool( args, TR_PREFS_KEY_START, false );
                          break;
                case 991: tr_bencDictAddBool( args, TR_PREFS_KEY_START, true );
                          break;
                case 992: tr_bencDictAddBool( args, TR_PREFS_KEY_TRASH_ORIGINAL, true );
                          break;
                case 993: tr_bencDictAddBool( args, TR_PREFS_KEY_TRASH_ORIGINAL, false );
                          break;
                default:  assert( "unhandled value" && 0 );
                          break;
            }
        }
        else if( stepMode == ( MODE_SESSION_SET | MODE_TORRENT_SET ) )
        {
            tr_benc * targs = 0;
            tr_benc * sargs = 0;

            if( *id )
                targs = ensure_tset( &tset );
            else
                sargs = ensure_sset( &sset );

            switch( c )
            {
                case 'd': if( targs ) {
                              tr_bencDictAddInt( targs, "downloadLimit", numarg( optarg ) );
                              tr_bencDictAddBool( targs, "downloadLimited", true );
                          } else {
                              tr_bencDictAddInt( sargs, TR_PREFS_KEY_DSPEED_KBps, numarg( optarg ) );
                              tr_bencDictAddBool( sargs, TR_PREFS_KEY_DSPEED_ENABLED, true );
                          }
                          break;
                case 'D': if( targs )
                              tr_bencDictAddBool( targs, "downloadLimited", false );
                          else
                              tr_bencDictAddBool( sargs, TR_PREFS_KEY_DSPEED_ENABLED, false );
                          break;
                case 'u': if( targs ) {
                              tr_bencDictAddInt( targs, "uploadLimit", numarg( optarg ) );
                              tr_bencDictAddBool( targs, "uploadLimited", true );
                          } else {
                              tr_bencDictAddInt( sargs, TR_PREFS_KEY_USPEED_KBps, numarg( optarg ) );
                              tr_bencDictAddBool( sargs, TR_PREFS_KEY_USPEED_ENABLED, true );
                          }
                          break;
                case 'U': if( targs )
                              tr_bencDictAddBool( targs, "uploadLimited", false );
                          else
                              tr_bencDictAddBool( sargs, TR_PREFS_KEY_USPEED_ENABLED, false );
                          break;
                case 930: if( targs )
                              tr_bencDictAddInt( targs, "peer-limit", atoi(optarg) );
                          else
                              tr_bencDictAddInt( sargs, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, atoi(optarg) );
                          break;
                default:  assert( "unhandled value" && 0 );
                          break;
            }
        }
        else if( stepMode == MODE_TORRENT_SET )
        {
            tr_benc * args = ensure_tset( &tset );

            switch( c )
            {
                case 712: tr_bencListAddInt( tr_bencDictAddList( args, "trackerRemove", 1 ), atoi( optarg ) );
                          break;
                case 950: tr_bencDictAddReal( args, "seedRatioLimit", atof(optarg) );
                          tr_bencDictAddInt( args, "seedRatioMode", TR_RATIOLIMIT_SINGLE );
                          break;
                case 951: tr_bencDictAddInt( args, "seedRatioMode", TR_RATIOLIMIT_GLOBAL );
                          break;
                case 952: tr_bencDictAddInt( args, "seedRatioMode", TR_RATIOLIMIT_UNLIMITED );
                          break;
                case 984: tr_bencDictAddBool( args, "honorsSessionLimits", true );
                          break;
                case 985: tr_bencDictAddBool( args, "honorsSessionLimits", false );
                          break;
                default:  assert( "unhandled value" && 0 );
                          break;
            }
        }
        else if( stepMode == ( MODE_TORRENT_SET | MODE_TORRENT_ADD ) )
        {
            tr_benc * args;

            if( tadd )
                args = tr_bencDictFind( tadd, ARGUMENTS );
            else
                args = ensure_tset( &tset );

            switch( c )
            {
                case 'g': addFiles( args, "files-wanted", optarg );
                          break;
                case 'G': addFiles( args, "files-unwanted", optarg );
                          break;
                case 900: addFiles( args, "priority-high", optarg );
                          break;
                case 901: addFiles( args, "priority-normal", optarg );
                          break;
                case 902: addFiles( args, "priority-low", optarg );
                          break;
                case 700: tr_bencDictAddInt( args, "bandwidthPriority",  1 );
                          break;
                case 701: tr_bencDictAddInt( args, "bandwidthPriority",  0 );
                          break;
                case 702: tr_bencDictAddInt( args, "bandwidthPriority", -1 );
                          break;
                case 710: tr_bencListAddStr( tr_bencDictAddList( args, "trackerAdd", 1 ), optarg );
                          break;
                default:  assert( "unhandled value" && 0 );
                          break;
            }
        }
        else if( c == 961 ) /* set location */
        {
            if( tadd )
            {
                tr_benc * args = tr_bencDictFind( tadd, ARGUMENTS );
                tr_bencDictAddStr( args, "download-dir", optarg );
            }
            else
            {
                tr_benc * args;
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "torrent-set-location" );
                args = tr_bencDictAddDict( top, ARGUMENTS, 3 );
                tr_bencDictAddStr( args, "location", optarg );
                tr_bencDictAddBool( args, "move", false );
                addIdArg( args, id );
                status |= flush( rpcurl, &top );
                break;
            }
        }
        else switch( c )
        {
            case 920: /* session-info */
            {
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "session-get" );
                tr_bencDictAddInt( top, "tag", TAG_SESSION );
                status |= flush( rpcurl, &top );
                break;
            }
            case 's': /* start */
            {
                if( tadd )
                    tr_bencDictAddBool( tr_bencDictFind( tadd, "arguments" ), "paused", false );
                else {
                    tr_benc * top = tr_new0( tr_benc, 1 );
                    tr_bencInitDict( top, 2 );
                    tr_bencDictAddStr( top, "method", "torrent-start" );
                    addIdArg( tr_bencDictAddDict( top, ARGUMENTS, 1 ), id );
                    status |= flush( rpcurl, &top );
                }
                break;
            }
            case 'S': /* stop */
            {
                if( tadd )
                    tr_bencDictAddBool( tr_bencDictFind( tadd, "arguments" ), "paused", true );
                else {
                    tr_benc * top = tr_new0( tr_benc, 1 );
                    tr_bencInitDict( top, 2 );
                    tr_bencDictAddStr( top, "method", "torrent-stop" );
                    addIdArg( tr_bencDictAddDict( top, ARGUMENTS, 1 ), id );
                    status |= flush( rpcurl, &top );
                }
                break;
            }
            case 'w':
            {
                char * path = absolutify( optarg );
                if( tadd )
                    tr_bencDictAddStr( tr_bencDictFind( tadd, "arguments" ), "download-dir", path );
                else {
                    tr_benc * args = ensure_sset( &sset );
                    tr_bencDictAddStr( args, "download-dir", path );
                }
                tr_free( path );
                break;
            }
            case 850:
            {
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 1 );
                tr_bencDictAddStr( top, "method", "session-close" );
                status |= flush( rpcurl, &top );
                break;
            }
            case 963:
            {
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 1 );
                tr_bencDictAddStr( top, "method", "blocklist-update" );
                status |= flush( rpcurl, &top );
                break;
            }
            case 921:
            {
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "session-stats" );
                tr_bencDictAddInt( top, "tag", TAG_STATS );
                status |= flush( rpcurl, &top );
                break;
            }
            case 962:
            {
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "port-test" );
                tr_bencDictAddInt( top, "tag", TAG_PORTTEST );
                status |= flush( rpcurl, &top );
                break;
            }
            case 600:
            {
                tr_benc * top;
                if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }
                top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "torrent-reannounce" );
                addIdArg( tr_bencDictAddDict( top, ARGUMENTS, 1 ), id );
                status |= flush( rpcurl, &top );
                break;
            }
            case 'v':
            {
                tr_benc * top;
                if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }
                top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "torrent-verify" );
                addIdArg( tr_bencDictAddDict( top, ARGUMENTS, 1 ), id );
                status |= flush( rpcurl, &top );
                break;
            }
            case 'r':
            case 'R':
            {
                tr_benc * args;
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "torrent-remove" );
                args = tr_bencDictAddDict( top, ARGUMENTS, 2 );
                tr_bencDictAddBool( args, "delete-local-data", c=='R' );
                addIdArg( args, id );
                status |= flush( rpcurl, &top );
                break;
            }
            case 960:
            {
                tr_benc * args;
                tr_benc * top = tr_new0( tr_benc, 1 );
                tr_bencInitDict( top, 2 );
                tr_bencDictAddStr( top, "method", "torrent-set-location" );
                args = tr_bencDictAddDict( top, ARGUMENTS, 3 );
                tr_bencDictAddStr( args, "location", optarg );
                tr_bencDictAddBool( args, "move", true );
                addIdArg( args, id );
                status |= flush( rpcurl, &top );
                break;
            }
            default:
            {
                fprintf( stderr, "got opt [%d]\n", c );
                showUsage( );
                break;
            }
        }
    }

    if( tadd != 0 ) status |= flush( rpcurl, &tadd );
    if( tset != 0 ) { addIdArg( tr_bencDictFind( tset, ARGUMENTS ), id ); status |= flush( rpcurl, &tset ); }
    if( sset != 0 ) status |= flush( rpcurl, &sset );
    return status;
}

/* [host:port] or [host] or [port] or [http://host:port/transmission/] */
static void
getHostAndPortAndRpcUrl( int * argc, char ** argv,
                         char ** host, int * port, char ** rpcurl )
{
    if( *argv[1] != '-' )
    {
        int          i;
        const char * s = argv[1];
        const char * delim = strchr( s, ':' );
        if( !strncmp(s, "http://", 7 ) )   /* user passed in full rpc url */
        {
            *rpcurl = tr_strdup_printf( "%s/rpc/", s );
        }
        else if( delim )   /* user passed in both host and port */
        {
            *host = tr_strndup( s, delim - s );
            *port = atoi( delim + 1 );
        }
        else
        {
            char *    end;
            const int i = strtol( s, &end, 10 );
            if( !*end ) /* user passed in a port */
                *port = i;
            else /* user passed in a host */
                *host = tr_strdup( s );
        }

        *argc -= 1;
        for( i = 1; i < *argc; ++i )
            argv[i] = argv[i + 1];
    }
}

int
main( int argc, char ** argv )
{
    int port = DEFAULT_PORT;
    char * host = NULL;
    char * rpcurl = NULL;
    int exit_status = EXIT_SUCCESS;

    if( argc < 2 ) {
        showUsage( );
        return EXIT_FAILURE;
    }

    tr_formatter_mem_init( MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR );
    tr_formatter_size_init( DISK_K,DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR );
    tr_formatter_speed_init( SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR );

    getHostAndPortAndRpcUrl( &argc, argv, &host, &port, &rpcurl );
    if( host == NULL )
        host = tr_strdup( DEFAULT_HOST );
    if( rpcurl == NULL )
        rpcurl = tr_strdup_printf( "http://%s:%d%s", host, port, DEFAULT_URL );

    exit_status = processArgs( rpcurl, argc, (const char**)argv );

    tr_free( host );
    tr_free( rpcurl );
    return exit_status;
}
