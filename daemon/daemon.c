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

#include <event2/buffer.h>

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#include "watch.h"

#define MY_NAME "transmission-daemon"

#define PREF_KEY_DIR_WATCH          "watch-dir"
#define PREF_KEY_DIR_WATCH_ENABLED  "watch-dir-enabled"
#define PREF_KEY_PIDFILE            "pidfile"

#define MEM_K 1024
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

static tr_bool paused = FALSE;
static tr_bool closing = FALSE;
static tr_session * mySession = NULL;

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

    { 'a', "allowed", "Allowed IP addresses. (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", 1, "<list>" },
    { 'b', "blocklist", "Enable peer blocklists", "b", 0, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", 0, NULL },
    { 'c', "watch-dir", "Where to watch for new .torrent files", "c", 1, "<directory>" },
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
    { 810, "log-error", "Show error messages", NULL, 0, NULL },
    { 811, "log-info", "Show error and info messages", NULL, 0, NULL },
    { 812, "log-debug", "Show error, info, and debug messages", NULL, 0, NULL },
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
    { 'i', "bind-address-ipv4", "Where to listen for peer connections", "i", 1, "<ipv4 addr>" },
    { 'I', "bind-address-ipv6", "Where to listen for peer connections", "I", 1, "<ipv6 addr>" },
    { 'r', "rpc-bind-address", "Where to listen for RPC connections", "r", 1, "<ipv4 addr>" },
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

    /* this is loosely based off of glibc's daemon() implementation
     * http://sourceware.org/git/?p=glibc.git;a=blob_plain;f=misc/daemon.c */

    switch( fork( ) ) {
        case -1: return -1;
        case 0: break;
        default: _exit(0);
    }

    if( setsid( ) == -1 )
        return -1;

    if( !nochdir )
        chdir( "/" );

    if( !noclose ) {
        int fd = open( "/dev/null", O_RDWR, 0 );
        dup2( fd, STDIN_FILENO );
        dup2( fd, STDOUT_FILENO );
        dup2( fd, STDERR_FILENO );
        close( fd );
    }

    return 0;

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
            else
            {
                char * new_filename = tr_strdup_printf( "%s.added", filename );
                rename( filename, new_filename );
                tr_free( new_filename );
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

    if( logfile != NULL )
        fflush( logfile );

    tr_freeMessageList( list );
}

static tr_rpc_callback_status
on_rpc_callback( tr_session            * session UNUSED,
                 tr_rpc_callback_type    type,
                 struct tr_torrent     * tor UNUSED,
                 void                  * user_data UNUSED )
{
    if( type == TR_RPC_SESSION_CLOSE )
        closing = TRUE;
    return TR_RPC_OK;
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
    const char * pid_filename;
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
                      fprintf(stderr, "%s %s\n", MY_NAME, LONG_VERSION_STRING);
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
            case 'x': tr_bencDictAddStr( &settings, PREF_KEY_PIDFILE, optarg );
                      break;
            case 'y': tr_bencDictAddBool( &settings, TR_PREFS_KEY_LPD_ENABLED, TRUE );
                      break;
            case 'Y': tr_bencDictAddBool( &settings, TR_PREFS_KEY_LPD_ENABLED, FALSE );
                      break;
            case 810: tr_bencDictAddInt( &settings,  TR_PREFS_KEY_MSGLEVEL, TR_MSG_ERR );
                      break;
            case 811: tr_bencDictAddInt( &settings,  TR_PREFS_KEY_MSGLEVEL, TR_MSG_INF );
                      break;
            case 812: tr_bencDictAddInt( &settings,  TR_PREFS_KEY_MSGLEVEL, TR_MSG_DBG );
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
    tr_formatter_mem_init( MEM_K, MEM_K_STR, MEM_M_STR, MEM_G_STR, MEM_T_STR );
    tr_formatter_size_init( DISK_K, DISK_K_STR, DISK_M_STR, DISK_G_STR, DISK_T_STR );
    tr_formatter_speed_init( SPEED_K, SPEED_K_STR, SPEED_M_STR, SPEED_G_STR, SPEED_T_STR );
    mySession = tr_sessionInit( "daemon", configDir, TRUE, &settings );
    tr_sessionSetRPCCallback( mySession, on_rpc_callback, NULL );
    tr_ninf( NULL, "Using settings from \"%s\"", configDir );
    tr_sessionSaveSettings( mySession, configDir, &settings );

    pid_filename = NULL;
    tr_bencDictFindStr( &settings, PREF_KEY_PIDFILE, &pid_filename );
    if( pid_filename && *pid_filename )
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
