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
#include <getopt.h>
#include <signal.h>
#include <unistd.h> /* daemon, getcwd */

#include <libtransmission/transmission.h>
#include <libtransmission/bencode.h>
#include <libtransmission/rpc.h>
#include <libtransmission/utils.h>
#include <libtransmission/version.h>

#define MY_NAME "transmission-daemon"

static int closing = FALSE;
static tr_handle * mySession;
static char myConfigFilename[MAX_PATH_LENGTH];

#define KEY_BLOCKLIST        "blocklist-enabled"
#define KEY_DOWNLOAD_DIR     "download-dir"
#define KEY_ENCRYPTION       "encryption"
#define KEY_PEER_LIMIT       "max-peers-global"
#define KEY_PEER_PORT        "peer-port"
#define KEY_PORT_FORWARDING  "port-forwarding-enabled"
#define KEY_PEX_ENABLED      "pex-enabled"
#define KEY_AUTH_REQUIRED    "rpc-authentication-required"
#define KEY_USERNAME         "rpc-username"
#define KEY_PASSWORD         "rpc-password"
#define KEY_ACL              "rpc-access-control-list"
#define KEY_RPC_PORT         "rpc-port"
#define KEY_DSPEED           "download-limit"
#define KEY_DSPEED_ENABLED   "download-limit-enabled"
#define KEY_USPEED           "upload-limit"
#define KEY_USPEED_ENABLED   "upload-limit-enabled"

#define CONFIG_FILE          "settings.json"

/***
****  Config File
***/

static void
replaceInt( tr_benc * dict, const char * key, int64_t value )
{
    tr_bencDictRemove( dict, key );
    tr_bencDictAddInt( dict, key, value );
}
static void
replaceStr( tr_benc * dict, const char * key, const char* value )
{
    tr_bencDictRemove( dict, key );
    tr_bencDictAddStr( dict, key, value );
}
static void
saveState( tr_session * s )
{
    tr_benc d;
    if( tr_bencLoadJSONFile( myConfigFilename, &d ) )
        tr_bencInitDict( &d, 16 );
    
    replaceInt( &d, KEY_BLOCKLIST,       tr_blocklistIsEnabled( s ) );
    replaceStr( &d, KEY_DOWNLOAD_DIR,    tr_sessionGetDownloadDir( s ) );
    replaceInt( &d, KEY_PEER_LIMIT,      tr_sessionGetPeerLimit( s ) );
    replaceInt( &d, KEY_PEER_PORT,       tr_sessionGetPeerPort( s ) );
    replaceInt( &d, KEY_PORT_FORWARDING, tr_sessionIsPortForwardingEnabled( s ) );
    replaceInt( &d, KEY_PEX_ENABLED,     tr_sessionIsPexEnabled( s ) );
    replaceStr( &d, KEY_USERNAME,        tr_sessionGetRPCUsername( s ) );
    replaceStr( &d, KEY_PASSWORD,        tr_sessionGetRPCPassword( s ) );
    replaceStr( &d, KEY_ACL,             tr_sessionGetRPCACL( s ) );
    replaceInt( &d, KEY_RPC_PORT,        tr_sessionGetRPCPort( s ) );
    replaceInt( &d, KEY_AUTH_REQUIRED,   tr_sessionIsRPCPasswordEnabled( s ) );
    replaceInt( &d, KEY_DSPEED,          tr_sessionGetSpeedLimit( s, TR_DOWN ) );
    replaceInt( &d, KEY_DSPEED_ENABLED,  tr_sessionIsSpeedLimitEnabled( s, TR_DOWN ) );
    replaceInt( &d, KEY_USPEED,          tr_sessionGetSpeedLimit( s, TR_UP ) );
fprintf( stderr, "session says its speed upload speed limit is %d\n", tr_sessionGetSpeedLimit( s, TR_UP ) );
    replaceInt( &d, KEY_USPEED_ENABLED,  tr_sessionIsSpeedLimitEnabled( s, TR_UP ) );
    replaceInt( &d, KEY_ENCRYPTION,      tr_sessionGetEncryption( s ) );

    tr_bencSaveJSONFile( myConfigFilename, &d );
    tr_bencFree( &d );
    tr_ninf( MY_NAME, "saved \"%s\"", myConfigFilename );
}

static void
getConfigInt( tr_benc     * dict,
              const char  * key,
              int         * setme,
              int           defaultVal )
{
    if( *setme < 0 ) {
        int64_t i;
        if( tr_bencDictFindInt( dict, key, &i ) )
            *setme = i;
        else
            *setme = defaultVal;
    }
}

static void
getConfigStr( tr_benc      * dict,
              const char   * key,
              const char  ** setme,
              const char   * defaultVal )
{
    if( !*setme ) {
        const char * s;
        if( tr_bencDictFindStr( dict, key, &s ) )
            *setme = s;
        else
            *setme = defaultVal;
    }
}

/**
 * @param port      port number, or -1 if not set in the command line
 * @param auth      TRUE, FALSE, or -1 if not set on the command line
 * @param blocklist TRUE, FALSE, or -1 if not set on the command line
 */
static void
session_init( const char * configDir, const char * downloadDir,
              int rpcPort, const char * acl,
              int authRequired, const char * username, const char * password,
              int blocklistEnabled )
{
    char mycwd[MAX_PATH_LENGTH];
    tr_benc state, *dict = NULL;
    int peerPort=-1, peers=-1;
    int pexEnabled = -1;
    int fwdEnabled = -1;
    int upLimit=-1, upLimited=-1, downLimit=-1, downLimited=-1;
    int encryption = -1;
    tr_ctor * ctor;
    tr_torrent ** torrents;

    if( !tr_bencLoadJSONFile( myConfigFilename, &state ) )
        dict = &state;

    /***
    ****  Decide on which values to pass into tr_sessionInitFull().
    ****  The command-line arguments are given precedence and
    ****  the state file from the previous session is used as a fallback.
    ****  If neither of those can be found, the TR_DEFAULT fields are used .
    ***/

    getcwd( mycwd, sizeof( mycwd ) );
    getConfigStr( dict, KEY_DOWNLOAD_DIR,    &downloadDir,       mycwd );
    getConfigInt( dict, KEY_PEX_ENABLED,     &pexEnabled,        TR_DEFAULT_PEX_ENABLED );
    getConfigInt( dict, KEY_PORT_FORWARDING, &fwdEnabled,        TR_DEFAULT_PORT_FORWARDING_ENABLED );
    getConfigInt( dict, KEY_PEER_PORT,       &peerPort,          TR_DEFAULT_PORT );
    getConfigInt( dict, KEY_DSPEED,          &downLimit,         100 );
    getConfigInt( dict, KEY_DSPEED_ENABLED,  &downLimited,       FALSE );
    getConfigInt( dict, KEY_USPEED,          &upLimit,           100 );
    getConfigInt( dict, KEY_USPEED_ENABLED,  &upLimited,         FALSE );
    getConfigInt( dict, KEY_PEER_LIMIT,      &peers,             TR_DEFAULT_GLOBAL_PEER_LIMIT );
    getConfigInt( dict, KEY_BLOCKLIST,       &blocklistEnabled,  TR_DEFAULT_BLOCKLIST_ENABLED );
    getConfigInt( dict, KEY_RPC_PORT,        &rpcPort,           TR_DEFAULT_RPC_PORT );
    getConfigStr( dict, KEY_ACL,             &acl,               TR_DEFAULT_RPC_ACL );
    getConfigInt( dict, KEY_AUTH_REQUIRED,   &authRequired,      FALSE );
    getConfigStr( dict, KEY_USERNAME,        &username,          NULL );
    getConfigStr( dict, KEY_PASSWORD,        &password,          NULL );
    getConfigInt( dict, KEY_ENCRYPTION,      &encryption,        TR_ENCRYPTION_PREFERRED );

    /***
    ****
    ***/

    /* start the session */
    mySession = tr_sessionInitFull( configDir, "daemon", downloadDir,
                                    pexEnabled, fwdEnabled, peerPort,
                                    encryption,
                                    upLimited, upLimit,
                                    downLimited, downLimit,
                                    peers,
                                    TR_MSG_INF, 0,
                                    blocklistEnabled,
                                    TR_DEFAULT_PEER_SOCKET_TOS,
                                    TRUE, rpcPort, acl, authRequired, username, password,
                                    TR_DEFAULT_PROXY_ENABLED,
                                    TR_DEFAULT_PROXY,
                                    TR_DEFAULT_PROXY_TYPE,
                                    TR_DEFAULT_PROXY_AUTH_ENABLED,
                                    TR_DEFAULT_PROXY_USERNAME,
                                    TR_DEFAULT_PROXY_PASSWORD );


    if( authRequired )
        tr_ninf( MY_NAME, "requiring authentication" );

    /* load the torrents */
    ctor = tr_ctorNew( mySession );
    torrents = tr_sessionLoadTorrents( mySession, ctor, NULL );
    tr_free( torrents );
    tr_ctorFree( ctor );

    if( dict )
        tr_bencFree( &state );
}

static void
daemonUsage( void )
{
    puts( "Usage: " MY_NAME " [options]\n"
          "\n"
          "Transmission "LONG_VERSION_STRING" http://www.transmissionbt.com/\n"
          "A fast and easy BitTorrent client\n"
          "\n"
          "  -a  --acl <list>         Access Control List.  (Default: "TR_DEFAULT_RPC_ACL")\n"
          "  -b  --blocklist          Enable peer blocklists\n"
          "  -b0 --blocklist=0        Disable peer blocklists\n"
          "  -d  --download-dir <dir> Where store downloaded data\n"
          "  -f  --foreground         Run in the foreground and log to stderr\n"
          "  -g  --config-dir <dir>   Where to look for torrents and "CONFIG_FILE"\n"
          "  -h  --help               Display this message and exit\n"
          "  -p  --port=n             Port to listen to for requests  (Default: "TR_DEFAULT_RPC_PORT_STR")\n"
          "  -t  --auth               Require authentication\n"
          "  -t0 --auth=0             Don't require authentication\n"
          "  -u  --username <user>    Set username for authentication\n"
          "  -w  --password <pass>    Set password for authentication\n"
          "\n"
          MY_NAME" is a headless Transmission session\n"
          "that can be controlled via transmission-remote or Clutch.\n" );
    exit( 0 );
}

static void
readargs( int argc, char ** argv,
          int * nofork, char ** configDir, char ** downloadDir,
          int * rpcPort, char ** acl, int * authRequired, char ** username, char ** password,
          int * blocklistEnabled )
{
    int opt;
    char shortopts[] = "a:b::d:fg:hp:t::u:w:";
    struct option longopts[] = {
        { "acl",           required_argument,  NULL, 'a'  },
        { "blocklist",     optional_argument,  NULL, 'b'  },
        { "download-dir",  required_argument,  NULL, 'd'  },
        { "foreground",    no_argument,        NULL, 'f'  },
        { "config-dir",    required_argument,  NULL, 'g'  },
        { "help",          no_argument,        NULL, 'h'  },
        { "port",          required_argument,  NULL, 'p'  },
        { "auth",          optional_argument,  NULL, 't'  },
        { "username",      required_argument,  NULL, 'u'  },
        { "password",      required_argument,  NULL, 'w'  },
        { NULL,            0,                  NULL, '\0' }
    };
    while((( opt = getopt_long( argc, argv, shortopts, longopts, NULL ))) != -1 ) {
        switch( opt ) {
            case 'a': *acl = tr_strdup( optarg ); break;
            case 'b': *blocklistEnabled = optarg ? atoi(optarg)!=0 : TRUE; break;
            case 'd': *downloadDir = tr_strdup( optarg ); break;
            case 'f': *nofork = 1; break;
            case 'n': *authRequired = FALSE; break;
            case 'g': *configDir = tr_strdup( optarg ); break;
            case 't': *authRequired = optarg ? atoi(optarg)!=0 : TRUE; break;
            case 'u': *username = tr_strdup( optarg ); break; 
            case 'w': *password = tr_strdup( optarg ); break; 
            case 'p': *rpcPort = atoi( optarg ); break;
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
            tr_nerr( MY_NAME, "Error daemonizing (fork)! %d - %s", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( setsid() < 0 ) {
        tr_nerr( MY_NAME, "Error daemonizing (setsid)! %d - %s", errno, strerror(errno) );
        return -1;
    }

    switch( fork( ) ) {
        case 0:
            break;
        case -1:
            tr_nerr( MY_NAME, "Error daemonizing (fork2)! %d - %s", errno, strerror(errno) );
            return -1;
        default:
            _exit(0);
    }

    if( !nochdir && 0 > chdir( "/" ) ) {
        tr_nerr( MY_NAME, "Error daemonizing (chdir)! %d - %s", errno, strerror(errno) );
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
    int rpcPort = -1;
    int authRequired = -1;
    int blocklistEnabled = -1;
    char * configDir = NULL;
    char * downloadDir = NULL;
    char * acl = NULL;
    char * username = NULL;
    char * password = NULL;

    signal( SIGINT, gotsig );
    signal( SIGQUIT, gotsig );
    signal( SIGTERM, gotsig );
    signal( SIGPIPE, SIG_IGN );
    signal( SIGHUP, SIG_IGN );

    readargs( argc, argv, &nofork, &configDir, &downloadDir,
              &rpcPort, &acl, &authRequired, &username, &password,
              &blocklistEnabled );
    if( configDir == NULL )
        configDir = tr_strdup_printf( "%s-daemon", tr_getDefaultConfigDir() );
    tr_buildPath( myConfigFilename, sizeof( myConfigFilename ),
                  configDir, CONFIG_FILE, NULL );

    if( !nofork ) {
        if( 0 > daemon( 1, 0 ) ) {
            fprintf( stderr, "failed to daemonize: %s\n", strerror( errno ) );
            exit( 1 );
        }
    }

    session_init( configDir, downloadDir,
                  rpcPort, acl, authRequired, username, password,
                  blocklistEnabled );

    while( !closing )
        sleep( 1 );

    saveState( mySession );
    printf( "Closing transmission session..." );
    tr_sessionClose( mySession );
    printf( " done.\n" );

    tr_free( configDir );
    tr_free( downloadDir );
    tr_free( username );
    tr_free( password );
    tr_free( acl );
    return 0;
}
