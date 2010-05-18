/*
 * This file Copyright (C) 2008-2010 Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <errno.h>
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit, atoi */
#include <string.h> /* strcmp */

#include <sys/types.h> /* umask*/
#include <sys/stat.h> /* umask*/

#include <fcntl.h> /* open */
#include <signal.h>
#ifdef HAVE_SYSLOG
#include <syslog.h>
#endif
#include <unistd.h> /* daemon */

#include <event.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "watch.h"

#define MY_NAME "transmission-daemon"

#define PREF_KEY_DIR_WATCH          "watch-dir"
#define PREF_KEY_DIR_WATCH_ENABLED  "watch-dir-enabled"

static tr_bool paused = FALSE;
static tr_bool closing = FALSE;
static tr_session * mySession = NULL;
static const char * pid_filename = NULL;

/***
****  Config File
***/

static const char *
getUsage( void )
{
    return "Transmission " LONG_VERSION_STRING
           "  http://www.transmissionbt.com/\n"
           "A fast and easy BitTorrent client\n"
           "\n"
           MY_NAME " is a headless Transmission session\n"
           "that can be controlled via transmission-remote\n"
           "or the web interface.\n"
           "\n"
           "Usage: " MY_NAME " [options]";
}

static const struct tr_option options[] =
{
    { 'a', "allowed", "Allowed IP addresses.  (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", 1, "<list>" },
    { 'b', "blocklist", "Enable peer blocklists", "b", 0, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", 0, NULL },
    { 'c', "watch-dir", "Directory to watch for new .torrent files", "c", 1, "<directory>" },
    { 'C', "no-watch-dir", "Disable the watch-dir", "C", 0, NULL },
    { 941, "incomplete-dir", "Where to store new torrents until they're complete", NULL, 1, "<directory>" },
    { 942, "no-incomplete-dir", "Don't store incomplete torrents in a different location", NULL, 0, NULL },
    { 'd', "dump-settings", "Dump the settings and exit", "d", 0, NULL },
    { 'e', "logfile", "Dump the log messages to this filename", "e", 1, "<filename>" },
    { 'f', "foreground", "Run in the foreground instead of daemonizing", "f", 0, NULL },
    { 'g', "config-dir", "Where to look for configuration files", "g", 1, "<path>" },
    { 'p', "port", "RPC port (Default: " TR_DEFAULT_RPC_PORT_STR ")", "p", 1, "<port>" },
    { 't', "auth", "Require authentication", "t", 0, NULL },
    { 'T', "no-auth", "Don't require authentication", "T", 0, NULL },
    { 'u', "username", "Set username for authentication", "u", 1, "<username>" },
    { 'v', "password", "Set password for authentication", "v", 1, "<password>" },
    { 'V', "version", "Show version number and exit", "V", 0, NULL },
    { 'w', "download-dir", "Where to save downloaded data", "w", 1, "<path>" },
    { 800, "paused", "Pause all torrents on startup", NULL, 0, NULL },
    { 'o', "dht", "Enable distributed hash tables (DHT)", "o", 0, NULL },
    { 'O', "no-dht", "Disable distributed hash tables (DHT)", "O", 0, NULL },
    { 'y', "lpd", "Enable local peer discovery (LPD)", "y", 0, NULL },
    { 'Y', "no-lpd", "Disable local peer discovery (LPD)", "Y", 0, NULL },
    { 'P', "peerport", "Port for incoming peers (Default: " TR_DEFAULT_PEER_PORT_STR ")", "P", 1, "<port>" },
    { 'm', "portmap", "Enable portmapping via NAT-PMP or UPnP", "m", 0, NULL },
    { 'M', "no-portmap", "Disable portmapping", "M", 0, NULL },
    { 'L', "peerlimit-global", "Maximum overall number of peers (Default: " TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ")", "L", 1, "<limit>" },
    { 'l', "peerlimit-torrent", "Maximum number of peers per torrent (Default: " TR_DEFAULT_PEER_LIMIT_TORRENT_STR ")", "l", 1, "<limit>" },
    { 910, "encryption-required",  "Encrypt all peer connections", "er", 0, NULL },
    { 911, "encryption-preferred", "Prefer encrypted peer connections", "ep", 0, NULL },
    { 912, "encryption-tolerated", "Prefer unencrypted peer connections", "et", 0, NULL },
    { 'i', "bind-address-ipv4", "Where to listen for peer connections", "i", 1, "<ipv4 address>" },
    { 'I', "bind-address-ipv6", "Where to listen for peer connections", "I", 1, "<ipv6 address>" },
    { 'r', "rpc-bind-address", "Where to listen for RPC connections", "r", 1, "<ipv4 address>" },
    { 953, "global-seedratio", "All torrents, unless overridden by a per-torrent setting, should seed until a specific ratio", "gsr", 1, "ratio" },
    { 954, "no-global-seedratio", "All torrents, unless overridden by a per-torrent setting, should seed regardless of ratio", "GSR", 0, NULL },
    { 'x', "pid-file", "Enable PID file", "x", 1, "<pid-file>" },
    { 0, NULL, NULL, NULL, 0, NULL }
};

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage( ), options );
    exit( 0 );
}

static void
gotsig( int sig )
{
    switch( sig )
    {
        case SIGHUP:
        {
            tr_benc settings;
            const char * configDir = tr_sessionGetConfigDir( mySession );
            tr_inf( "Reloading settings from \"%s\"", configDir );
            tr_bencInitDict( &settings, 0 );
            tr_bencDictAddBool( &settings, TR_PREFS_KEY_RPC_ENABLED, TRUE );
            tr_sessionLoadSettings( &settings, configDir, MY_NAME );
            tr_sessionSet( mySession, &settings );
            tr_bencFree( &settings );
            tr_sessionReloadBlocklists( mySession );
            break;
        }

        default:
            closing = TRUE;
            break;
    }
}

#if defined(WIN32)
 #define USE_NO_DAEMON
#elif !defined(HAVE_DAEMON) || defined(__UCLIBC__)
 #define USE_TR_DAEMON
#else
 #define USE_OS_DAEMON
#endif

static int
tr_daemon( int nochdir, int noclose )
{
#if defined(USE_OS_DAEMON)
    return daemon( nochdir, noclose );
#elif defined(USE_TR_DAEMON)
    pid_t pid = fork( );
    if( pid < 0 )
        return -1;
    else if( pid > 0 )
        _exit( 0 );
    else {
        pid = setsid( );
        if( pid < 0 )
            return -1;

        pid = fork( );
        if( pid < 0 )
            return -1;
        else if( pid > 0 )
            _exit( 0 );
        else {

            if( !nochdir )
                if( chdir( "/" ) < 0 )
                    return -1;

            umask( (mode_t)0 );

            if( !noclose ) {
                /* send stdin, stdout, and stderr to /dev/null */
                int i;
                int fd = open( "/dev/null", O_RDWR, 0 );
                if( fd < 0 )
                    fprintf( stderr, "unable to open /dev/null: %s\n", tr_strerror(errno) );
                for( i=0; i<3; ++i ) {
                    if( close( i ) )
                        return -1;
                    dup2( fd, i );
                }
                close( fd );
            }

            return 0;
        }
    }
#else /* USE_NO_DAEMON */
    return 0;
#endif
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

static void
onFileAdded( tr_session * session, const char * dir, const char * file )
{
    char * filename = tr_buildPath( dir, file, NULL );
    tr_ctor * ctor = tr_ctorNew( session );
    int err = tr_ctorSetMetainfoFromFile( ctor, filename );

    if( !err )
    {
        tr_torrentNew( ctor, &err );

        if( err == TR_PARSE_ERR )
            tr_err( "Error parsing .torrent file \"%s\"", file );
        else
        {
            tr_bool trash = FALSE;
            int test = tr_ctorGetDeleteSource( ctor, &trash );

            tr_inf( "Parsing .torrent file successful \"%s\"", file );

            if( !test && trash )
            {
                tr_inf( "Deleting input .torrent file \"%s\"", file );
                if( remove( filename ) )
                    tr_err( "Error deleting .torrent file: %s", tr_strerror( errno ) );
            }
        }
    }

    tr_ctorFree( ctor );
    tr_free( filename );
}

static void
printMessage( FILE * logfile, int level, const char * name, const char * message, const char * file, int line )
{
    if( logfile != NULL )
    {
        char timestr[64];
        tr_getLogTimeStr( timestr, sizeof( timestr ) );
        if( name )
            fprintf( logfile, "[%s] %s %s (%s:%d)\n", timestr, name, message, file, line );
        else
            fprintf( logfile, "[%s] %s (%s:%d)\n", timestr, message, file, line );
    }
#ifdef HAVE_SYSLOG
    else /* daemon... write to syslog */
    {
        int priority;

        /* figure out the syslog priority */
        switch( level ) {
            case TR_MSG_ERR: priority = LOG_ERR; break;
            case TR_MSG_DBG: priority = LOG_DEBUG; break;
            default: priority = LOG_INFO; break;
        }

        if( name )
            syslog( priority, "%s %s (%s:%d)", name, message, file, line );
        else
            syslog( priority, "%s (%s:%d)", message, file, line );
    }
#endif
}

static void
pumpLogMessages( FILE * logfile )
{
    const tr_msg_list * l;
    tr_msg_list * list = tr_getQueuedMessages( );

    for( l=list; l!=NULL; l=l->next )
        printMessage( logfile, l->level, l->name, l->message, l->file, l->line );

    tr_freeMessageList( list );
}

int
main( int argc, char ** argv )
{
    int c;
    const char * optarg;
    tr_benc settings;
    tr_bool boolVal;
    tr_bool loaded;
    tr_bool foreground = FALSE;
    tr_bool dumpSettings = FALSE;
    const char * configDir = NULL;
    dtr_watchdir * watchdir = NULL;
    FILE * logfile = NULL;
    tr_bool pidfile_created = FALSE;

    signal( SIGINT, gotsig );
    signal( SIGTERM, gotsig );
#ifndef WIN32
    signal( SIGHUP, gotsig );
#endif

    /* load settings from defaults + config file */
    tr_bencInitDict( &settings, 0 );
    tr_bencDictAddBool( &settings, TR_PREFS_KEY_RPC_ENABLED, TRUE );
    configDir = getConfigDir( argc, (const char**)argv );
    loaded = tr_sessionLoadSettings( &settings, configDir, MY_NAME );

    /* overwrite settings from the comamndline */
    tr_optind = 1;
    while(( c = tr_getopt( getUsage(), argc, (const char**)argv, options, &optarg ))) {
        switch( c ) {
            case 'a': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_WHITELIST, optarg );
                      tr_bencDictAddBool( &settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, TRUE );
                      break;
            case 'b': tr_bencDictAddBool( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, TRUE );
                      break;
            case 'B': tr_bencDictAddBool( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, FALSE );
                      break;
            case 'c': tr_bencDictAddStr( &settings, PREF_KEY_DIR_WATCH, optarg );
                      tr_bencDictAddBool( &settings, PREF_KEY_DIR_WATCH_ENABLED, TRUE );
                      break;
            case 'C': tr_bencDictAddBool( &settings, PREF_KEY_DIR_WATCH_ENABLED, FALSE );
                      break;
            case 941: tr_bencDictAddStr( &settings, TR_PREFS_KEY_INCOMPLETE_DIR, optarg );
                      tr_bencDictAddBool( &settings, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, TRUE );
                      break;
            case 942: tr_bencDictAddBool( &settings, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, FALSE );
                      break;
            case 'd': dumpSettings = TRUE;
                      break;
            case 'e': logfile = fopen( optarg, "a+" );
                      if( logfile == NULL )
                          fprintf( stderr, "Couldn't open \"%s\": %s\n", optarg, tr_strerror( errno ) );
                      break;
            case 'f': foreground = TRUE;
                      break;
            case 'g': /* handled above */
                      break;
            case 'V': /* version */
                      fprintf(stderr, "Transmission %s\n", LONG_VERSION_STRING);
                      exit( 0 );
            case 'o': tr_bencDictAddBool( &settings, TR_PREFS_KEY_DHT_ENABLED, TRUE );
                      break;
            case 'O': tr_bencDictAddBool( &settings, TR_PREFS_KEY_DHT_ENABLED, FALSE );
                      break;
            case 'p': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_PORT, atoi( optarg ) );
                      break;
            case 't': tr_bencDictAddBool( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, TRUE );
                      break;
            case 'T': tr_bencDictAddBool( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, FALSE );
                      break;
            case 'u': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_USERNAME, optarg );
                      break;
            case 'v': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_PASSWORD, optarg );
                      break;
            case 'w': tr_bencDictAddStr( &settings, TR_PREFS_KEY_DOWNLOAD_DIR, optarg );
                      break;
            case 'P': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_PORT, atoi( optarg ) );
                      break;
            case 'm': tr_bencDictAddBool( &settings, TR_PREFS_KEY_PORT_FORWARDING, TRUE );
                      break;
            case 'M': tr_bencDictAddBool( &settings, TR_PREFS_KEY_PORT_FORWARDING, FALSE );
                      break;
            case 'L': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, atoi( optarg ) );
                      break;
            case 'l': tr_bencDictAddInt( &settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, atoi( optarg ) );
                      break;
            case 800: paused = TRUE;
                      break;
            case 910: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_REQUIRED );
                      break;
            case 911: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_ENCRYPTION_PREFERRED );
                      break;
            case 912: tr_bencDictAddInt( &settings, TR_PREFS_KEY_ENCRYPTION, TR_CLEAR_PREFERRED );
                      break;
            case 'i': tr_bencDictAddStr( &settings, TR_PREFS_KEY_BIND_ADDRESS_IPV4, optarg );
                      break;
            case 'I': tr_bencDictAddStr( &settings, TR_PREFS_KEY_BIND_ADDRESS_IPV6, optarg );
                      break;
            case 'r': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_BIND_ADDRESS, optarg );
                      break;
            case 953: tr_bencDictAddReal( &settings, TR_PREFS_KEY_RATIO, atof(optarg) );
                      tr_bencDictAddBool( &settings, TR_PREFS_KEY_RATIO_ENABLED, TRUE );
                      break;
            case 954: tr_bencDictAddBool( &settings, TR_PREFS_KEY_RATIO_ENABLED, FALSE );
                      break;
            case 'x': pid_filename = optarg;
                      break;
            case 'y': tr_bencDictAddBool( &settings, TR_PREFS_KEY_LPD_ENABLED, TRUE );
                      break;
            case 'Y': tr_bencDictAddBool( &settings, TR_PREFS_KEY_LPD_ENABLED, FALSE );
                      break;
            default:  showUsage( );
                      break;
        }
    }

    if( foreground && !logfile )
        logfile = stderr;

    if( !loaded )
    {
        printMessage( logfile, TR_MSG_ERR, MY_NAME, "Error loading config file -- exiting.", __FILE__, __LINE__ );
        return -1;
    }

    if( dumpSettings )
    {
        char * str = tr_bencToStr( &settings, TR_FMT_JSON, NULL );
        fprintf( stderr, "%s", str );
        tr_free( str );
        return 0;
    }

    if( !foreground && tr_daemon( TRUE, FALSE ) < 0 )
    {
        char buf[256];
        tr_snprintf( buf, sizeof( buf ), "Failed to daemonize: %s", tr_strerror( errno ) );
        printMessage( logfile, TR_MSG_ERR, MY_NAME, buf, __FILE__, __LINE__ );
        exit( 1 );
    }

    /* start the session */
    mySession = tr_sessionInit( "daemon", configDir, TRUE, &settings );
    tr_ninf( NULL, "Using settings from \"%s\"", configDir );
    tr_sessionSaveSettings( mySession, configDir, &settings );

    if( pid_filename != NULL )
    {
        FILE * fp = fopen( pid_filename, "w+" );
        if( fp != NULL )
        {
            fprintf( fp, "%d", (int)getpid() );
            fclose( fp );
            tr_inf( "Saved pidfile \"%s\"", pid_filename );
            pidfile_created = TRUE;
        }
        else
            tr_err( "Unable to save pidfile \"%s\": %s", pid_filename, strerror( errno ) );
    }

    if( tr_bencDictFindBool( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, &boolVal ) && boolVal )
        tr_ninf( MY_NAME, "requiring authentication" );

    /* maybe add a watchdir */
    {
        const char * dir;

        if( tr_bencDictFindBool( &settings, PREF_KEY_DIR_WATCH_ENABLED, &boolVal )
            && boolVal
            && tr_bencDictFindStr( &settings, PREF_KEY_DIR_WATCH, &dir )
            && dir
            && *dir )
        {
            tr_inf( "Watching \"%s\" for new .torrent files", dir );
            watchdir = dtr_watchdir_new( mySession, dir, onFileAdded );
        }
    }

    /* load the torrents */
    {
        tr_torrent ** torrents;
        tr_ctor * ctor = tr_ctorNew( mySession );
        if( paused )
            tr_ctorSetPaused( ctor, TR_FORCE, TRUE );
        torrents = tr_sessionLoadTorrents( mySession, ctor, NULL );
        tr_free( torrents );
        tr_ctorFree( ctor );
    }

#ifdef HAVE_SYSLOG
    if( !foreground )
        openlog( MY_NAME, LOG_CONS|LOG_PID, LOG_DAEMON );
#endif

    while( !closing ) {
        tr_wait_msec( 1000 ); /* sleep one second */
        dtr_watchdir_update( watchdir );
        pumpLogMessages( logfile );
    }

    /* shutdown */
#if HAVE_SYSLOG
    if( !foreground )
    {
        syslog( LOG_INFO, "%s", "Closing session" );
        closelog( );
    }
#endif

    printf( "Closing transmission session..." );
    tr_sessionSaveSettings( mySession, configDir, &settings );
    dtr_watchdir_free( watchdir );
    tr_sessionClose( mySession );
    printf( " done.\n" );

    /* cleanup */
    if( pidfile_created )
        remove( pid_filename );
    tr_bencFree( &settings );
    return 0;
}
