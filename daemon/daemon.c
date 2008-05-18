/*
 * This file Copyright (C) 2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h> /* printf */ 
#include <stdlib.h> /* exit, atoi */
#include <string.h> /* strcmp */

#include <fcntl.h> /* open */
#include <getopt.h>
#include <signal.h>
#include <unistd.h> /* daemon, getcwd */

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpc.h>
#include <libtransmission/utils.h> /* tr_strdup */
#include <libtransmission/version.h>

#define MY_NAME "transmission-daemon"

static int closing = FALSE;
static tr_handle * gl_session;
static char gl_configfile[MAX_PATH_LENGTH];

static void
saveState( tr_handle * h )
{
    tr_benc d;
    const char * str;

    tr_bencInitDict( &d, 12 );
    tr_bencDictAddStr( &d, "download-dir", tr_sessionGetDownloadDir( h ) );
    tr_bencDictAddInt( &d, "peer-limit", tr_sessionGetPeerLimit( h ) );
    tr_bencDictAddInt( &d, "pex-allowed", tr_sessionIsPexEnabled( h ) );
    tr_bencDictAddInt( &d, "port", tr_sessionGetPublicPort( h ) );
    tr_bencDictAddInt( &d, "port-forwarding-enabled",
                           tr_sessionIsPortForwardingEnabled( h ) );
    tr_bencDictAddStr( &d, "rpc-acl", tr_sessionGetRPCACL( h ) );
    tr_bencDictAddInt( &d, "rpc-port", tr_sessionGetRPCPort( h ) );
    tr_bencDictAddInt( &d, "speed-limit-up",
                           tr_sessionGetSpeedLimit( h, TR_UP ) );
    tr_bencDictAddInt( &d, "speed-limit-up-enabled",
                           tr_sessionIsSpeedLimitEnabled( h, TR_UP ) );
    tr_bencDictAddInt( &d, "speed-limit-down",
                           tr_sessionGetSpeedLimit( h, TR_DOWN ) );
    tr_bencDictAddInt( &d, "speed-limit-down-enabled",
                           tr_sessionIsSpeedLimitEnabled( h, TR_DOWN ) );
    switch( tr_sessionGetEncryption( h ) ) {
        case TR_PLAINTEXT_PREFERRED: str = "tolerated"; break;
        case TR_ENCRYPTION_REQUIRED: str = "required"; break;
        default: str = "preferred"; break;
    }
    tr_bencDictAddStr( &d, "encryption", str );

    tr_ninf( MY_NAME, "saving \"%s\"\n", gl_configfile );
    tr_bencSaveFile( gl_configfile, &d );

    tr_bencFree( &d );

}

static void
session_init( const char * configDir, int rpc_port, const char * rpc_acl )
{
    tr_benc state;
    int have_state;
    int64_t peer_port = TR_DEFAULT_PORT;
    int64_t peers = TR_DEFAULT_GLOBAL_PEER_LIMIT;
    int64_t pex_enabled = TR_DEFAULT_PEX_ENABLED;
    int64_t fwd_enabled = TR_DEFAULT_PORT_FORWARDING_ENABLED;
    int64_t up_limit = 100;
    int64_t up_limited = FALSE;
    int64_t down_limit = 100;
    int64_t down_limited = FALSE;
    int encryption = TR_ENCRYPTION_PREFERRED;
    char downloadDir[MAX_PATH_LENGTH] = { '\0' };
    const char * rpc_acl_fallback = TR_DEFAULT_RPC_ACL;
    int64_t rpc_port_fallback = TR_DEFAULT_RPC_PORT;
    tr_ctor * ctor;
    tr_torrent ** torrents;

    assert( !gl_session );

    if(( have_state = !tr_bencLoadFile( gl_configfile, &state )))
    {
        const char * str;
        tr_ninf( MY_NAME, "loading settings from \"%s\"", gl_configfile );

        if( tr_bencDictFindStr( &state, "download-dir", &str ) )
            tr_strlcpy( downloadDir, str, sizeof( downloadDir ) );
        tr_bencDictFindInt( &state, "port", &peer_port );
        tr_bencDictFindInt( &state, "port-forwarding-enabled", &fwd_enabled );
        tr_bencDictFindInt( &state, "peer-limit", &peers );
        tr_bencDictFindInt( &state, "pex-allowed", &pex_enabled );
        tr_bencDictFindStr( &state, "rpc-acl", &rpc_acl_fallback );
        tr_bencDictFindInt( &state, "rpc-port", &rpc_port_fallback );
        tr_bencDictFindInt( &state, "speed-limit-down", &down_limit );
        tr_bencDictFindInt( &state, "speed-limit-down-enabled", &down_limited );
        tr_bencDictFindInt( &state, "speed-limit-up", &up_limit );
        tr_bencDictFindInt( &state, "speed-limit-up-enabled", &up_limited );
        if( tr_bencDictFindStr( &state, "encryption", &str ) ) {
            if( !strcmp( str, "required" ) )
                encryption = TR_ENCRYPTION_REQUIRED;
            else if( !strcmp( str, "tolerated" ) )
                encryption = TR_PLAINTEXT_PREFERRED;
        }
    }

    /* fallbacks */
    if( !*downloadDir )
        getcwd( downloadDir, sizeof( downloadDir ) );
    if( rpc_port < 1 )
        rpc_port = rpc_port_fallback;
    if( !rpc_acl || !*rpc_acl )
        rpc_acl = rpc_acl_fallback;

    /* start the session */
    gl_session = tr_sessionInitFull( configDir, "daemon", downloadDir,
                                     pex_enabled, fwd_enabled, peer_port,
                                     encryption,
                                     up_limit, up_limited,
                                     down_limit, down_limited,
                                     peers,
                                     TR_MSG_INF, 0,
                                     FALSE, /* is the blocklist enabled? */
                                     TR_DEFAULT_PEER_SOCKET_TOS,
                                     TRUE, rpc_port, rpc_acl );

    /* load the torrents */
    ctor = tr_ctorNew( gl_session );
    torrents = tr_sessionLoadTorrents( gl_session, ctor, NULL );
    tr_free( torrents );
    tr_ctorFree( ctor );

    if( have_state )
        tr_bencFree( &state );
}

static void
daemonUsage( void )
{
    puts( "usage: " MY_NAME " [-dfh] [-p file] [-s file]\n"
          "\n"
          "Transmission " LONG_VERSION_STRING " http://www.transmissionbt.com/\n"
          "A fast and easy BitTorrent client\n"
          "\n"
          "  -a --acl <list>         Access Control List.  (Default: "TR_DEFAULT_RPC_ACL")\n"
          "  -f --foreground         Run in the foreground and log to stderr\n"
          "  -g --config-dir <dir>   Where to look for torrents and daemon-config.benc\n"
          "  -h --help               Display this message and exit\n"
          "  -p --port n             Port to listen to for requests (Default: "TR_DEFAULT_RPC_PORT_STR")\n"
          "\n"
          MY_NAME" is a headless Transmission session\n"
          "that can be controlled via transmission-remote or Clutch.\n" );
    exit( 0 );
}

static void
readargs( int argc, char ** argv,
          int * nofork, int * port, char ** acl,
          char ** configDir )
{
    int opt;
    char optstr[] = "a:fg:hp:";
    struct option longopts[] = {
        { "acl",         required_argument,  NULL, 'a'  },
        { "foreground",  no_argument,        NULL, 'f'  },
        { "config-dir",  required_argument,  NULL, 'g'  },
        { "help",        no_argument,        NULL, 'h'  },
        { "port",        required_argument,  NULL, 'p'  },
        { NULL,          0,                  NULL, '\0' }
    };
    while((( opt = getopt_long( argc, argv, optstr, longopts, NULL ))) != -1 ) {
        switch( opt ) {
            case 'a': *acl = tr_strdup( optarg ); break;
            case 'f': *nofork = 1; break;
            case 'g': *configDir = tr_strdup( optarg ); break;
            case 'p': *port = atoi( optarg ); break;
            default: daemonUsage( ); break;
        }
    }
}

static void
gotsig( int sig UNUSED )
{
    closing = TRUE;
}

#if !defined(HAVE_DAEMON)
static int
daemon( int nochdir, int noclose )
{
    switch( fork( ) ) {
        case 0:
            break;
        case -1:
            tr_nerr( MY_NAME, "Error daemonizing (fork)! %d - %s\n", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( setsid() < 0 ) {
        tr_nerr( MY_NAME, "Error daemonizing (setsid)! %d - %s\n", errno, strerror(errno) );
        return -1;
    }

    switch( fork( ) ) {
        case 0:
            break;
        case -1:
            tr_nerr( MY_NAME, "Error daemonizing (fork2)! %d - %s\n", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( !nochdir && 0 > chdir( "/" ) ) {
        tr_nerr( MY_NAME, "Error daemonizing (chdir)! %d - %s\n", errno, strerror(errno) );
        return -1;
    }

    if( !noclose ) {
        int fd;
        if((( fd = open("/dev/null", O_RDONLY))) != 0 ) {
            dup2( fd,  0 );
            close( fd );
        }
        if((( fd = open("/dev/null", O_WRONLY))) != 1 ) {
            dup2( fd, 1 );
            close( fd );
        }
        if((( fd = open("/dev/null", O_WRONLY))) != 2 ) {
            dup2( fd, 2 );
            close( fd );
        }
    }

    return 0;
}
#endif

int
main( int argc, char ** argv )
{
    int nofork = 0;
    int port = TR_DEFAULT_RPC_PORT;
    char * configDir = NULL;
    char * acl = NULL;

    signal( SIGINT, gotsig );
    signal( SIGQUIT, gotsig );
    signal( SIGTERM, gotsig );
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );

    readargs( argc, argv, &nofork, &port, &acl, &configDir );
    if( configDir == NULL )
        configDir = tr_strdup_printf( "%s-daemon", tr_getDefaultConfigDir() );
    tr_buildPath( gl_configfile, sizeof( gl_configfile ),
                  configDir, "daemon-config.benc", NULL );

    if( !nofork ) {
        if( 0 > daemon( 1, 0 ) ) {
            fprintf( stderr, "failed to daemonize: %s\n", strerror( errno ) );
            exit( 1 );
        }
    }

    session_init( configDir, port, acl );

    while( !closing )
        sleep( 1 );

    saveState( gl_session );
    printf( "Closing transmission session..." );
    tr_sessionClose( gl_session );
    printf( " done.\n" );

    tr_free( configDir );
    return 0;
}
