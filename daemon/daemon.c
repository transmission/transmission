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

#include <assert.h>
#include <errno.h>
#include <stdio.h> /* printf */
#include <stdlib.h> /* exit, atoi */
#include <string.h> /* strcmp */

#include <fcntl.h> /* open */
#include <signal.h>
#include <unistd.h> /* daemon */

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/tr-getopt.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-daemon"

static int           closing = FALSE;
static tr_session  * mySession = NULL;

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
                   "that can be controlled via transmission-remote or Clutch.\n"
                   "\n"
                   "Usage: " MY_NAME " [options]";
}

static const struct tr_option options[] =
{
    { 'a', "allowed", "Allowed IP addresses.  (Default: " TR_DEFAULT_RPC_WHITELIST ")", "a", 1, "<list>" },
    { 'b', "blocklist", "Enable peer blocklists", "b", 0, NULL },
    { 'B', "no-blocklist", "Disable peer blocklists", "B", 0, NULL },
    { 'f', "foreground", "Run in the foreground instead of daemonizing", "f", 0, NULL },
    { 'g', "config-dir", "Where to look for configuration files", "g", 1, "<path>" },
    { 'p', "port", "RPC port (Default: " TR_DEFAULT_RPC_PORT_STR ")", "p", 1, "<port>" },
    { 't', "auth", "Require authentication", "t", 0, NULL },
    { 'T', "no-auth", "Don't require authentication", "T", 0, NULL },
    { 'u', "username", "Set username for authentication", "u", 1, "<username>" },
    { 'v', "password", "Set password for authentication", "v", 1, "<password>" },
    { 'w', "download-dir", "Where to save downloaded data", "w", 1, "<path>" },
    { 0, NULL, NULL, NULL, 0, NULL }
};

static void
showUsage( void )
{
    tr_getopt_usage( MY_NAME, getUsage( ), options );
    exit( 0 );
}

static void
gotsig( int sig UNUSED )
{
    closing = TRUE;
}

#if !defined( WIN32 )
#if !defined( HAVE_DAEMON )
static int
daemon( int nochdir,
        int noclose )
{
    switch( fork( ) )
    {
        case 0:
            break;

        case - 1:
            tr_nerr( MY_NAME, "Error daemonizing (fork)! %d - %s", errno,
                    strerror(
                        errno ) );
            return -1;

        default:
            _exit( 0 );
    }

    if( setsid( ) < 0 )
    {
        tr_nerr( MY_NAME, "Error daemonizing (setsid)! %d - %s", errno,
                strerror(
                    errno ) );
        return -1;
    }

    switch( fork( ) )
    {
        case 0:
            break;

        case - 1:
            tr_nerr( MY_NAME, "Error daemonizing (fork2)! %d - %s", errno,
                    strerror(
                        errno ) );
            return -1;

        default:
            _exit( 0 );
    }

    if( !nochdir && 0 > chdir( "/" ) )
    {
        tr_nerr( MY_NAME, "Error daemonizing (chdir)! %d - %s", errno,
                strerror(
                    errno ) );
        return -1;
    }

    if( !noclose )
    {
        int fd;
        if( ( ( fd = open( "/dev/null", O_RDONLY ) ) ) != 0 )
        {
            dup2( fd,  0 );
            close( fd );
        }
        if( ( ( fd = open( "/dev/null", O_WRONLY ) ) ) != 1 )
        {
            dup2( fd, 1 );
            close( fd );
        }
        if( ( ( fd = open( "/dev/null", O_WRONLY ) ) ) != 2 )
        {
            dup2( fd, 2 );
            close( fd );
        }
    }

    return 0;
}
#endif
#endif

static const char*
getConfigDir( int argc, const char ** argv )
{
    int c;
    const char * configDir = NULL;
    const char * optarg;
    const int ind = tr_optind;

    while(( c = tr_getopt( getUsage( ), argc, argv, options, &optarg )))
        if( c == 'g' )
            configDir = optarg;

    tr_optind = ind;

    if( configDir == NULL )
        configDir = tr_getDefaultConfigDir( MY_NAME );

    return configDir;
}


int
main( int     argc,
      char ** argv )
{
    int c;
    const char * optarg;
    tr_benc settings;
    tr_bool foreground = FALSE;
    const char * configDir = NULL;

    signal( SIGINT, gotsig );
    signal( SIGTERM, gotsig );
#ifndef WIN32 
    signal( SIGQUIT, gotsig );
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );
#endif

    /* load settings from defaults + config file */
    tr_bencInitDict( &settings, 0 );
    configDir = getConfigDir( argc, (const char**)argv );
    tr_sessionLoadSettings( &settings, configDir, MY_NAME );

    /* overwrite settings from the comamndline */
    tr_optind = 1;
    while(( c = tr_getopt( getUsage(), argc, (const char**)argv, options, &optarg ))) {
        switch( c ) {
            case 'a': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_WHITELIST, optarg );
                      tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, 1 );
                      break;
            case 'b': tr_bencDictAddInt( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, 1 );
                      break;
            case 'B': tr_bencDictAddInt( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, 0 );
                      break;
            case 'f': foreground = TRUE;
                      break;
            case 'g': /* handled above */
                      break;
            case 'p': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_PORT, atoi( optarg ) );
                      break;
            case 't': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, 0 );
                      break;
            case 'T': tr_bencDictAddInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, 1 );
                      break;
            case 'u': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_USERNAME, optarg );
                      break;
            case 'v': tr_bencDictAddStr( &settings, TR_PREFS_KEY_RPC_PASSWORD, optarg );
                      break;
            case 'w': tr_bencDictAddStr( &settings, TR_PREFS_KEY_DOWNLOAD_DIR, optarg );
                      break;
            default:  showUsage( );
                      break;
        }
    }

#ifndef WIN32
    if( !foreground )
    {
        if( 0 > daemon( 1, 0 ) )
        {
            fprintf( stderr, "failed to daemonize: %s\n", strerror( errno ) );
            exit( 1 );
        }
    }
#endif

    /* start the session */
    mySession = tr_sessionInit( "daemon", configDir, FALSE, &settings );

    if( tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, NULL ) )
        tr_ninf( MY_NAME, "requiring authentication" );

    /* load the torrents */
    {
        tr_ctor * ctor = tr_ctorNew( mySession );
        tr_torrent ** torrents = tr_sessionLoadTorrents( mySession, ctor, NULL );
        tr_free( torrents );
        tr_ctorFree( ctor );
    }

    while( !closing )
        tr_wait( 1000 ); /* sleep one second */

    /* shutdown */
    printf( "Closing transmission session..." );
    tr_sessionSaveSettings( mySession, configDir, &settings );
    tr_sessionClose( mySession );
    printf( " done.\n" );

    /* cleanup */
    tr_bencFree( &settings );
    return 0;
}
