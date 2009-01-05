/*
 * This file Copyright (C) 2008 Charles Kerr <charles@transmissionbt.com>
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
#include <stdlib.h>
#include <string.h> /* memcpy */

#include <signal.h>
#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include "transmission.h"
#include "session.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "fdlimit.h"
#include "list.h"
#include "metainfo.h" /* tr_metainfoFree */
#include "net.h"
#include "peer-mgr.h"
#include "platform.h" /* tr_lock */
#include "port-forwarding.h"
#include "rpc-server.h"
#include "stats.h"
#include "torrent.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"
#include "crypto.h"

#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, NULL, __VA_ARGS__ ); \
    } while( 0 )

static tr_port
getRandomPort( tr_session * s )
{
    return tr_cryptoWeakRandInt( s->randomPortHigh - s->randomPortLow + 1) + s->randomPortLow;
}

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
uint8_t*
tr_peerIdNew( void )
{
    int          i;
    int          val;
    int          total = 0;
    uint8_t *    buf = tr_new( uint8_t, 21 );
    const char * pool = "0123456789abcdefghijklmnopqrstuvwxyz";
    const int    base = 36;

    memcpy( buf, PEERID_PREFIX, 8 );

    for( i = 8; i < 19; ++i )
    {
        val = tr_cryptoRandInt( base );
        total += val;
        buf[i] = pool[val];
    }

    val = total % base ? base - ( total % base ) : 0;
    buf[19] = pool[val];
    buf[20] = '\0';

    return buf;
}

const uint8_t*
tr_getPeerId( void )
{
    static uint8_t * id = NULL;

    if( id == NULL )
        id = tr_peerIdNew( );
    return id;
}

/***
****
***/

tr_encryption_mode
tr_sessionGetEncryption( tr_session * session )
{
    assert( session );

    return session->encryptionMode;
}

void
tr_sessionSetEncryption( tr_session *       session,
                         tr_encryption_mode mode )
{
    assert( session );
    assert( mode == TR_ENCRYPTION_PREFERRED
          || mode == TR_ENCRYPTION_REQUIRED
          || mode == TR_CLEAR_PREFERRED );

    session->encryptionMode = mode;
}

/***
****
***/

static int
tr_stringEndsWith( const char * str,
                   const char * end )
{
    const size_t slen = strlen( str );
    const size_t elen = strlen( end );

    return slen >= elen && !memcmp( &str[slen - elen], end, elen );
}

static void
loadBlocklists( tr_session * session )
{
    int         binCount = 0;
    int         newCount = 0;
    struct stat sb;
    char      * dirname;
    DIR *       odir = NULL;
    tr_list *   list = NULL;
    const tr_bool   isEnabled = session->isBlocklistEnabled;

    /* walk through the directory and find blocklists */
    dirname = tr_buildPath( session->configDir, "blocklists", NULL );
    if( !stat( dirname,
               &sb ) && S_ISDIR( sb.st_mode )
      && ( ( odir = opendir( dirname ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d; d = readdir( odir ) )
        {
            char * filename;

            if( !d->d_name || d->d_name[0] == '.' ) /* skip dotfiles, ., and ..
                                                      */
                continue;

            filename = tr_buildPath( dirname, d->d_name, NULL );

            if( tr_stringEndsWith( filename, ".bin" ) )
            {
                /* if we don't already have this blocklist, add it */
                if( !tr_list_find( list, filename,
                                   (TrListCompareFunc)strcmp ) )
                {
                    tr_list_append( &list,
                                   _tr_blocklistNew( filename, isEnabled ) );
                    ++binCount;
                }
            }
            else
            {
                /* strip out the file suffix, if there is one, and add ".bin"
                  instead */
                tr_blocklist * b;
                const char *   dot = strrchr( d->d_name, '.' );
                const int      len = dot ? dot - d->d_name
                                         : (int)strlen( d->d_name );
                char         * tmp = tr_strdup_printf(
                                        "%s" TR_PATH_DELIMITER_STR "%*.*s.bin",
                                        dirname, len, len, d->d_name );
                b = _tr_blocklistNew( tmp, isEnabled );
                _tr_blocklistSetContent( b, filename );
                tr_list_append( &list, b );
                ++newCount;
                tr_free( tmp );
            }

            tr_free( filename );
        }

        closedir( odir );
    }

    session->blocklists = list;

    if( binCount )
        tr_dbg( "Found %d blocklists in \"%s\"", binCount, dirname );
    if( newCount )
        tr_dbg( "Found %d new blocklists in \"%s\"", newCount, dirname );

    tr_free( dirname );
}

/***
****
***/

#ifdef TR_EMBEDDED
 #define TR_DEFAULT_ENCRYPTION              TR_CLEAR_PREFERRED
#else
 #define TR_DEFAULT_ENCRYPTION              TR_ENCRYPTION_PREFERRED
#endif

void
tr_sessionGetDefaultSettings( tr_benc * d )
{
    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 30 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,        FALSE );
    tr_bencDictAddStr( d, TR_PREFS_KEY_DOWNLOAD_DIR,             tr_getDefaultDownloadDir( ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_DSPEED,                   100 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_DSPEED_ENABLED,           0 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ENCRYPTION,               TR_DEFAULT_ENCRYPTION );
    tr_bencDictAddInt( d, TR_PREFS_KEY_LAZY_BITFIELD,            TRUE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_MSGLEVEL,                 TR_MSG_INF );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,        atoi( TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,       atoi( TR_DEFAULT_PEER_LIMIT_TORRENT_STR ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT,                atoi( TR_DEFAULT_PEER_PORT_STR ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ENABLED, FALSE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,     1024 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,    65535 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_SOCKET_TOS,          atoi( TR_DEFAULT_PEER_SOCKET_TOS_STR ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEX_ENABLED,              TRUE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PORT_FORWARDING,          TRUE );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY,                    "" );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_AUTH_ENABLED,       FALSE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_ENABLED,            FALSE );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY_PASSWORD,           "" );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_PORT,               80 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_TYPE,               TR_PROXY_HTTP );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY_USERNAME,           "" );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,        FALSE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_ENABLED,              TRUE );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_PASSWORD,             "" );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_USERNAME,             "" );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_WHITELIST,            TR_DEFAULT_RPC_WHITELIST );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,    TRUE );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_PORT,                 atoi( TR_DEFAULT_RPC_PORT_STR ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED,                   100 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED_ENABLED,           0 );
}

void
tr_sessionGetSettings( tr_session * s, struct tr_benc * d )
{
    int i, n=0;
    char * freeme[16];

    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 30 );
    tr_bencDictAddInt( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,        tr_blocklistIsEnabled( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_DOWNLOAD_DIR,             s->downloadDir );
    tr_bencDictAddInt( d, TR_PREFS_KEY_DSPEED,                   tr_sessionGetSpeedLimit( s, TR_DOWN ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_DSPEED_ENABLED,           tr_sessionIsSpeedLimitEnabled( s, TR_DOWN ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ENCRYPTION,               s->encryptionMode );
    tr_bencDictAddInt( d, TR_PREFS_KEY_LAZY_BITFIELD,            s->useLazyBitfield );
    tr_bencDictAddInt( d, TR_PREFS_KEY_MSGLEVEL,                 tr_getMessageLevel( ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,        tr_sessionGetPeerLimit( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,       s->peerLimitPerTorrent );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT,                tr_sessionGetPeerPort( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ENABLED, s->isPortRandom );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,     s->randomPortLow );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,    s->randomPortHigh );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_SOCKET_TOS,          s->peerSocketTOS );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEX_ENABLED,              s->isPexEnabled );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PORT_FORWARDING,          tr_sessionIsPortForwardingEnabled( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY,                    s->proxy );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_AUTH_ENABLED,       s->isProxyAuthEnabled );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_ENABLED,            s->isProxyEnabled );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY_PASSWORD,           s->proxyPassword );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_PORT,               s->proxyPort );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PROXY_TYPE,               s->proxyType );
    tr_bencDictAddStr( d, TR_PREFS_KEY_PROXY_USERNAME,           s->proxyUsername );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,        tr_sessionIsRPCPasswordEnabled( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_ENABLED,              tr_sessionIsRPCEnabled( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_PASSWORD,             freeme[n++] = tr_sessionGetRPCPassword( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_PORT,                 tr_sessionGetRPCPort( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_USERNAME,             freeme[n++] = tr_sessionGetRPCUsername( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_RPC_WHITELIST,            freeme[n++] = tr_sessionGetRPCWhitelist( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,    tr_sessionGetRPCWhitelistEnabled( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED,                   tr_sessionGetSpeedLimit( s, TR_UP ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_USPEED_ENABLED,           tr_sessionIsSpeedLimitEnabled( s, TR_UP ) );

    for( i=0; i<n; ++i )
        tr_free( freeme[i] );
}

void
tr_sessionLoadSettings( tr_benc * d, const char * configDir, const char * appName )
{
    char * filename;
    tr_benc fileSettings;

    assert( tr_bencIsDict( d ) );

    /* get the defaults */
    tr_sessionGetDefaultSettings( d );

    /* if caller didn't specify a config dir, use the default */
    if( !configDir || !*configDir )
        configDir = tr_getDefaultConfigDir( appName );

    /* file settings override the defaults */
    filename = tr_buildPath( configDir, "settings.json", NULL );
    if( !tr_bencLoadJSONFile( filename, &fileSettings ) ) {
        tr_bencMergeDicts( d, &fileSettings );
        tr_bencFree( &fileSettings );
    }

    /* cleanup */
    tr_free( filename );
}

void
tr_sessionSaveSettings( tr_session * session, const char * configDir, tr_benc * settings )
{
    tr_benc fileSettings;
    char * filename;

    assert( tr_bencIsDict( settings ) );
 
    filename = tr_buildPath( configDir, "settings.json", NULL );

    tr_sessionGetSettings( session, settings );

    if( tr_bencLoadJSONFile( filename, &fileSettings ) ) {
        tr_bencSaveJSONFile( filename, settings );
    } else {
        tr_bencMergeDicts( &fileSettings, settings );
        tr_bencSaveJSONFile( filename, &fileSettings );
        tr_bencFree( &fileSettings );
    }

    tr_inf( "Saved \"%s\"", filename );
    tr_free( filename );
}

static void metainfoLookupRescan( tr_session * );

tr_session *
tr_sessionInit( const char  * tag,
                const char  * configDir,
                tr_bool       messageQueuingEnabled,
                tr_benc     * clientSettings )
{
    int64_t i;
    int64_t j;
    tr_bool found;
    const char * str;
    tr_benc settings;
    tr_session * session;
    char * filename;
    int64_t rpc_enabled, whitelist_enabled, rpc_auth_enabled, rpc_port;
    const char * whitelist = NULL, *rpc_passwd = NULL, *rpc_username = NULL;

    assert( tr_bencIsDict( clientSettings ) );

    session = tr_new0( tr_session, 1 );
    session->bandwidth = tr_bandwidthNew( session, NULL );
    session->lock = tr_lockNew( );
    session->tag = tr_strdup( tag );
    dbgmsg( "tr_sessionInit: the session's top-level bandwidth object is %p", session->bandwidth );

    tr_bencInitDict( &settings, 0 );
    tr_sessionGetDefaultSettings( &settings );
    tr_bencMergeDicts( &settings, clientSettings );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, &i ); 
    assert( found ); 
    session->peerLimitPerTorrent = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_MSGLEVEL, &i ); 
    assert( found ); 
    tr_setMessageLevel( i ); 
    tr_setMessageQueuing( messageQueuingEnabled ); 
 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEX_ENABLED, &i ); 
    assert( found ); 
    session->isPexEnabled = i != 0; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_ENCRYPTION, &i ); 
    assert( found ); 
    session->encryptionMode = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_SOCKET_TOS, &i ); 
    assert( found ); 
    session->peerSocketTOS = i; 
 
    found = tr_bencDictFindStr( &settings, TR_PREFS_KEY_DOWNLOAD_DIR, &str ); 
    assert( found ); 
    session->downloadDir = tr_strdup( str ); 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PROXY_ENABLED, &i ); 
    assert( found ); 
    session->isProxyEnabled = i != 0; 
 
    found = tr_bencDictFindStr( &settings, TR_PREFS_KEY_PROXY, &str ); 
    assert( found ); 
    session->proxy = tr_strdup( str ); 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PROXY_PORT, &i ); 
    assert( found ); 
    session->proxyPort = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PROXY_TYPE, &i ); 
    assert( found ); 
    session->proxyType = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PROXY_AUTH_ENABLED, &i ); 
    assert( found ); 
    session->isProxyAuthEnabled = i != 0; 
 
    found = tr_bencDictFindStr( &settings, TR_PREFS_KEY_PROXY_USERNAME, &str ); 
    assert( found ); 
    session->proxyUsername = tr_strdup( str ); 
 
    found = tr_bencDictFindStr( &settings, TR_PREFS_KEY_PROXY_PASSWORD, &str ); 
    assert( found ); 
    session->proxyPassword = tr_strdup( str ); 
 
    session->so_sndbuf = 1500 * 3; /* 3x MTU for most ethernet/wireless */ 
    session->so_rcvbuf = 8192; 
 
    tr_setConfigDir( session, configDir ); 

    tr_netInit( ); /* must go before tr_eventInit */

    tr_eventInit( session );
    while( !session->events )
        tr_wait( 50 );

    session->peerMgr = tr_peerMgrNew( session );

    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_LAZY_BITFIELD, &i ); 
    assert( found ); 
    session->useLazyBitfield = i != 0; 

    /* Initialize rate and file descripts controls */

    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &i ); 
    assert( found ); 
    tr_fdInit( i ); 

    /** 
    *** random port 
    **/ 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_PORT_RANDOM_ENABLED, &i ); 
    assert( found ); 
    session->isPortRandom = i != 0; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW, &i ); 
    assert( found ); 
    session->randomPortLow = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH, &i ); 
    assert( found ); 
    session->randomPortHigh = i; 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_PORT_FORWARDING, &i ) 
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_PEER_PORT, &j ); 
    assert( found ); 
    session->peerPort = session->isPortRandom ? getRandomPort( session ) : j; 
    session->shared = tr_sharedInit( session, i, session->peerPort ); 
    session->isPortSet = session->isPortRandom || j>0; 

    /** 
    **/ 
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_USPEED, &i )
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_USPEED_ENABLED, &j );
    assert( found ); 
    tr_sessionSetSpeedLimit( session, TR_UP, i );
    tr_sessionSetSpeedLimitEnabled( session, TR_UP, j );
 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_DSPEED, &i )
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_DSPEED_ENABLED, &j );
    assert( found ); 
    tr_sessionSetSpeedLimit( session, TR_DOWN, i );
    tr_sessionSetSpeedLimitEnabled( session, TR_DOWN, j );
 
    /* first %s is the application name
       second %s is the version number */
    tr_inf( _( "%s %s started" ), TR_NAME, LONG_VERSION_STRING );

    /* initialize the blocklist */
    filename = tr_buildPath( session->configDir, "blocklists", NULL );
    tr_mkdirp( filename, 0777 );
    tr_free( filename );
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, &i ); 
    assert( found ); 
    session->isBlocklistEnabled = i; 
    loadBlocklists( session ); 

    tr_statsInit( session );

    session->web = tr_webInit( session ); 
    found = tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_ENABLED, &rpc_enabled ) 
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_PORT, &rpc_port ) 
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_WHITELIST_ENABLED, &whitelist_enabled ) 
         && tr_bencDictFindInt( &settings, TR_PREFS_KEY_RPC_AUTH_REQUIRED, &rpc_auth_enabled ) 
         && tr_bencDictFindStr( &settings, TR_PREFS_KEY_RPC_WHITELIST, &whitelist ) 
         && tr_bencDictFindStr( &settings, TR_PREFS_KEY_RPC_USERNAME, &rpc_username ) 
         && tr_bencDictFindStr( &settings, TR_PREFS_KEY_RPC_PASSWORD, &rpc_passwd ); 
    assert( found ); 
    session->rpcServer = tr_rpcInit( session, rpc_enabled, rpc_port, whitelist_enabled, whitelist, 
                                     rpc_auth_enabled, rpc_username, rpc_passwd ); 

    metainfoLookupRescan( session );

    tr_bencFree( &settings );
    return session;
}

/***
****
***/

void
tr_sessionSetDownloadDir( tr_session * session, const char * dir )
{
    if( session->downloadDir != dir )
    {
        tr_free( session->downloadDir );
        session->downloadDir = tr_strdup( dir );
    }
}

const char *
tr_sessionGetDownloadDir( const tr_session * session )
{
    return session->downloadDir;
}

/***
****
***/

void
tr_globalLock( tr_session * session )
{
    tr_lockLock( session->lock );
}

void
tr_globalUnlock( tr_session * session )
{
    tr_lockUnlock( session->lock );
}

tr_bool
tr_globalIsLocked( const tr_session * session )
{
    return session && tr_lockHave( session->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 *
 **********************************************************************/

struct bind_port_data
{
    tr_session * session;
    tr_port      port;
};

static void
tr_setBindPortImpl( void * vdata )
{
    struct bind_port_data * data = vdata;
    tr_session * session = data->session;
    const tr_port port = data->port;

    session->isPortSet = 1;
    tr_sharedSetPort( session->shared, port );

    tr_free( data );
}

static void
setPortImpl( tr_session * session, tr_port port )
{
    struct bind_port_data * data = tr_new( struct bind_port_data, 1 );
    data->session = session;
    data->port = port;
    tr_runInEventThread( session, tr_setBindPortImpl, data );
}

void
tr_sessionSetPeerPort( tr_session * session,
                       tr_port      port )
{
    session->isPortRandom = FALSE;
    session->peerPort = port;
    setPortImpl( session, session->peerPort );
}

tr_port
tr_sessionSetPeerPortRandom( tr_session * session )
{
    session->isPortRandom = TRUE;
    session->peerPort = getRandomPort( session );
    setPortImpl( session, session->peerPort );
    return session->peerPort;
}

tr_port
tr_sessionGetPeerPort( const tr_session * session )
{
    assert( session );

    return session->peerPort;
}

tr_port_forwarding
tr_sessionGetPortForwarding( const tr_session * session )
{
    return tr_sharedTraversalStatus( session->shared );
}

/***
****
***/

static void
updateBandwidth( tr_session * session, tr_direction dir )
{
    const tr_bool zeroCase = session->speedLimit[dir] < 1 && session->isSpeedLimited[dir];

    tr_bandwidthSetLimited( session->bandwidth, dir, session->isSpeedLimited[dir] && !zeroCase );

    tr_bandwidthSetDesiredSpeed( session->bandwidth, dir, session->speedLimit[dir] );
}

void
tr_sessionSetSpeedLimitEnabled( tr_session      * session,
                                tr_direction      dir,
                                tr_bool           isLimited )
{
    assert( session );
    assert( tr_isDirection( dir ) );

    session->isSpeedLimited[dir] = isLimited;
    updateBandwidth( session, dir );
}

void
tr_sessionSetSpeedLimit( tr_session    * session,
                         tr_direction    dir,
                         int             desiredSpeed )
{
    assert( session );
    assert( tr_isDirection( dir ) );

    session->speedLimit[dir] = desiredSpeed;
    updateBandwidth( session, dir );
}

tr_bool
tr_sessionIsSpeedLimitEnabled( const tr_session  * session,
                               tr_direction        dir )
{
    assert( session );
    assert( tr_isDirection( dir ) );

    return session->isSpeedLimited[dir];
}

int
tr_sessionGetSpeedLimit( const tr_session  * session,
                         tr_direction        dir )
{
    assert( session );
    assert( tr_isDirection( dir ) );

    return session->speedLimit[dir];
}

/***
****
***/

void
tr_sessionSetPeerLimit( tr_session * session UNUSED,
                        uint16_t     maxGlobalPeers )
{
    tr_fdSetPeerLimit( maxGlobalPeers );
}

uint16_t
tr_sessionGetPeerLimit( const tr_session * session UNUSED )
{
    return tr_fdGetPeerLimit( );
}

void
tr_sessionSetPeerLimitPerTorrent( tr_session  * session, uint16_t n )
{
    session->peerLimitPerTorrent = n;
}

uint16_t
tr_sessionGetPeerLimitPerTorrent( const tr_session * session )
{
    return session->peerLimitPerTorrent;
}

/***
****
***/

double
tr_sessionGetPieceSpeed( const tr_session * session, tr_direction dir )
{
    return session ? tr_bandwidthGetPieceSpeed( session->bandwidth, 0, dir ) : 0.0;
}

double
tr_sessionGetRawSpeed( const tr_session * session, tr_direction dir )
{
    return session ? tr_bandwidthGetPieceSpeed( session->bandwidth, 0, dir ) : 0.0;
}

int
tr_sessionCountTorrents( const tr_session * session )
{
    return session->torrentCount;
}

static int
compareTorrentByCur( const void * va, const void * vb )
{
    const tr_torrent * a = *(const tr_torrent**)va;
    const tr_torrent * b = *(const tr_torrent**)vb;
    const uint64_t     aCur = a->downloadedCur + a->uploadedCur;
    const uint64_t     bCur = b->downloadedCur + b->uploadedCur;

    if( aCur != bCur )
        return aCur > bCur ? -1 : 1; /* close the biggest torrents first */

    return 0;
}

static void
tr_closeAllConnections( void * vsession )
{
    tr_session *  session = vsession;
    tr_torrent *  tor;
    int           i, n;
    tr_torrent ** torrents;

    tr_statsClose( session );
    tr_sharedShuttingDown( session->shared );
    tr_rpcClose( &session->rpcServer );

    /* close the torrents.  get the most active ones first so that
     * if we can't get them all closed in a reasonable amount of time,
     * at least we get the most important ones first. */
    tor = NULL;
    n = session->torrentCount;
    torrents = tr_new( tr_torrent *, session->torrentCount );
    for( i = 0; i < n; ++i )
        torrents[i] = tor = tr_torrentNext( session, tor );
    qsort( torrents, n, sizeof( tr_torrent* ), compareTorrentByCur );
    for( i = 0; i < n; ++i )
        tr_torrentFree( torrents[i] );
    tr_free( torrents );

    tr_peerMgrFree( session->peerMgr );

    tr_trackerSessionClose( session );
    tr_list_free( &session->blocklists,
                  (TrListForeachFunc)_tr_blocklistFree );
    tr_webClose( &session->web );

    session->isClosed = TRUE;
}

static int
deadlineReached( const uint64_t deadline )
{
    return tr_date( ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 30

void
tr_sessionClose( tr_session * session )
{
    int            i;
    const int      maxwait_msec = SHUTDOWN_MAX_SECONDS * 1000;
    const uint64_t deadline = tr_date( ) + maxwait_msec;

    dbgmsg( "shutting down transmission session %p", session );

    /* close the session */
    tr_runInEventThread( session, tr_closeAllConnections, session );
    while( !session->isClosed && !deadlineReached( deadline ) )
    {
        dbgmsg(
            "waiting for the shutdown commands to run in the main thread" );
        tr_wait( 100 );
    }

    /* "shared" and "tracker" have live sockets,
     * so we need to keep the transmission thread alive
     * for a bit while they tell the router & tracker
     * that we're closing now */
    while( ( session->shared
           || session->tracker ) && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting on port unmap (%p) or tracker (%p)",
                session->shared, session->tracker );
        tr_wait( 100 );
    }

    tr_fdClose( );

    /* close the libtransmission thread */
    tr_eventClose( session );
    while( session->events && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting for the libevent thread to shutdown cleanly" );
        tr_wait( 100 );
    }

    /* free the session memory */
    tr_bandwidthFree( session->bandwidth );
    tr_lockFree( session->lock );
    for( i = 0; i < session->metainfoLookupCount; ++i )
        tr_free( session->metainfoLookup[i].filename );
    tr_free( session->metainfoLookup );
    tr_free( session->tag );
    tr_free( session->configDir );
    tr_free( session->resumeDir );
    tr_free( session->torrentDir );
    tr_free( session->downloadDir );
    tr_free( session->proxy );
    tr_free( session->proxyUsername );
    tr_free( session->proxyPassword );
    tr_free( session );
}

tr_torrent **
tr_sessionLoadTorrents( tr_session * session,
                        tr_ctor    * ctor,
                        int        * setmeCount )
{
    int           i, n = 0;
    struct stat   sb;
    DIR *         odir = NULL;
    const char *  dirname = tr_getTorrentDir( session );
    tr_torrent ** torrents;
    tr_list *     l = NULL, *list = NULL;

    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */

    if( !stat( dirname, &sb )
      && S_ISDIR( sb.st_mode )
      && ( ( odir = opendir ( dirname ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
        {
            if( d->d_name && d->d_name[0] != '.' ) /* skip dotfiles, ., and ..
                                                     */
            {
                tr_torrent * tor;
                char * path = tr_buildPath( dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, path );
                if(( tor = tr_torrentNew( session, ctor, NULL )))
                {
                    tr_list_append( &list, tor );
                    ++n;
                }
                tr_free( path );
            }
        }
        closedir( odir );
    }

    torrents = tr_new( tr_torrent *, n );
    for( i = 0, l = list; l != NULL; l = l->next )
        torrents[i++] = (tr_torrent*) l->data;
    assert( i == n );

    tr_list_free( &list, NULL );

    if( n )
        tr_inf( _( "Loaded %d torrents" ), n );

    if( setmeCount )
        *setmeCount = n;
    return torrents;
}

/***
****
***/

void
tr_sessionSetPexEnabled( tr_session * session,
                         tr_bool      enabled )
{
    session->isPexEnabled = enabled != 0;
}

tr_bool
tr_sessionIsPexEnabled( const tr_session * session )
{
    return session->isPexEnabled;
}

/***
****
***/

void
tr_sessionSetLazyBitfieldEnabled( tr_session * session,
                                  tr_bool      enabled )
{
    session->useLazyBitfield = enabled != 0;
}

tr_bool
tr_sessionIsLazyBitfieldEnabled( const tr_session * session )
{
    return session->useLazyBitfield;
}

/***
****
***/

void
tr_sessionSetPortForwardingEnabled( tr_session  * session,
                                    tr_bool       enabled )
{
    tr_globalLock( session );
    tr_sharedTraversalEnable( session->shared, enabled );
    tr_globalUnlock( session );
}

tr_bool
tr_sessionIsPortForwardingEnabled( const tr_session * session )
{
    return tr_sharedTraversalIsEnabled( session->shared );
}

/***
****
***/

int
tr_blocklistGetRuleCount( const tr_session * session )
{
    int       n = 0;
    tr_list * l;

    for( l = session->blocklists; l; l = l->next )
        n += _tr_blocklistGetRuleCount( l->data );
    return n;
}

tr_bool
tr_blocklistIsEnabled( const tr_session * session )
{
    return session->isBlocklistEnabled;
}

void
tr_blocklistSetEnabled( tr_session * session,
                        tr_bool      isEnabled )
{
    tr_list * l;

    session->isBlocklistEnabled = isEnabled != 0;

    for( l=session->blocklists; l!=NULL; l=l->next )
        _tr_blocklistSetEnabled( l->data, isEnabled );
}

tr_bool
tr_blocklistExists( const tr_session * session )
{
    return session->blocklists != NULL;
}

int
tr_blocklistSetContent( tr_session * session,
                        const char * contentFilename )
{
    tr_list *      l;
    tr_blocklist * b;
    const char *   defaultName = "level1.bin";

    for( b = NULL, l = session->blocklists; !b && l; l = l->next )
        if( tr_stringEndsWith( _tr_blocklistGetFilename( l->data ),
                               defaultName ) )
            b = l->data;

    if( !b )
    {
        char * path = tr_buildPath( session->configDir, "blocklists", defaultName, NULL );
        b = _tr_blocklistNew( path, session->isBlocklistEnabled );
        tr_list_append( &session->blocklists, b );
        tr_free( path );
    }

    return _tr_blocklistSetContent( b, contentFilename );
}

tr_bool
tr_sessionIsAddressBlocked( const tr_session * session,
                            const tr_address * addr )
{
    tr_list * l;

    for( l = session->blocklists; l; l = l->next )
        if( _tr_blocklistHasAddress( l->data, addr ) )
            return TRUE;
    return FALSE;
}

/***
****
***/

static int
compareLookupEntries( const void * va,
                      const void * vb )
{
    const struct tr_metainfo_lookup * a = va;
    const struct tr_metainfo_lookup * b = vb;

    return strcmp( a->hashString, b->hashString );
}

static void
metainfoLookupResort( tr_session * session )
{
    qsort( session->metainfoLookup,
           session->metainfoLookupCount,
           sizeof( struct tr_metainfo_lookup ),
           compareLookupEntries );
}

static int
compareHashStringToLookupEntry( const void * va,
                                const void * vb )
{
    const char *                      a = va;
    const struct tr_metainfo_lookup * b = vb;

    return strcmp( a, b->hashString );
}

const char*
tr_sessionFindTorrentFile( const tr_session * session,
                           const char       * hashStr )
{
    struct tr_metainfo_lookup * l = bsearch( hashStr,
                                             session->metainfoLookup,
                                             session->metainfoLookupCount,
                                             sizeof( struct tr_metainfo_lookup ),
                                             compareHashStringToLookupEntry );

    return l ? l->filename : NULL;
}

static void
metainfoLookupRescan( tr_session * session )
{
    int          i;
    int          n;
    struct stat  sb;
    const char * dirname = tr_getTorrentDir( session );
    DIR *        odir = NULL;
    tr_ctor *    ctor = NULL;
    tr_list *    list = NULL;

    /* walk through the directory and find the mappings */
    ctor = tr_ctorNew( session );
    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && ( ( odir = opendir( dirname ) ) ) )
    {
        struct dirent *d;
        for( d = readdir( odir ); d != NULL; d = readdir( odir ) )
        {
            if( d->d_name && d->d_name[0] != '.' ) /* skip dotfiles, ., and ..
                                                     */
            {
                tr_info inf;
                char * path = tr_buildPath( dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, path );
                if( !tr_torrentParse( session, ctor, &inf ) )
                {
                    tr_list_append( &list, tr_strdup( inf.hashString ) );
                    tr_list_append( &list, tr_strdup( path ) );
                    tr_metainfoFree( &inf );
                }
                tr_free( path );
            }
        }
        closedir( odir );
    }
    tr_ctorFree( ctor );

    n = tr_list_size( list ) / 2;
    session->metainfoLookup = tr_new0( struct tr_metainfo_lookup, n );
    session->metainfoLookupCount = n;
    for( i = 0; i < n; ++i )
    {
        char * hashString = tr_list_pop_front( &list );
        char * filename = tr_list_pop_front( &list );

        memcpy( session->metainfoLookup[i].hashString, hashString,
                2 * SHA_DIGEST_LENGTH + 1 );
        tr_free( hashString );
        session->metainfoLookup[i].filename = filename;
    }

    metainfoLookupResort( session );
    tr_dbg( "Found %d torrents in \"%s\"", n, dirname );
}

void
tr_sessionSetTorrentFile( tr_session * session,
                          const char * hashString,
                          const char * filename )
{
    struct tr_metainfo_lookup * l = bsearch( hashString,
                                             session->metainfoLookup,
                                             session->metainfoLookupCount,
                                             sizeof( struct tr_metainfo_lookup ),
                                             compareHashStringToLookupEntry );

    if( l )
    {
        if( l->filename != filename )
        {
            tr_free( l->filename );
            l->filename = tr_strdup( filename );
        }
    }
    else
    {
        const int n = session->metainfoLookupCount++;
        struct tr_metainfo_lookup * node;
        session->metainfoLookup = tr_renew( struct tr_metainfo_lookup,
                                            session->metainfoLookup,
                                            session->metainfoLookupCount );
        node = session->metainfoLookup + n;
        memcpy( node->hashString, hashString, 2 * SHA_DIGEST_LENGTH + 1 );
        node->filename = tr_strdup( filename );
        metainfoLookupResort( session );
    }
}

tr_torrent*
tr_torrentNext( tr_session * session,
                tr_torrent * tor )
{
    return tor ? tor->next : session->torrentList;
}

/***
****
***/

void
tr_sessionSetRPCEnabled( tr_session * session,
                         tr_bool      isEnabled )
{
    tr_rpcSetEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionIsRPCEnabled( const tr_session * session )
{
    return tr_rpcIsEnabled( session->rpcServer );
}

void
tr_sessionSetRPCPort( tr_session * session,
                      tr_port      port )
{
    tr_rpcSetPort( session->rpcServer, port );
}

tr_port 
tr_sessionGetRPCPort( const tr_session * session )
{
    return tr_rpcGetPort( session->rpcServer );
}

void
tr_sessionSetRPCCallback( tr_session * session,
                          tr_rpc_func  func,
                          void *       user_data )
{
    session->rpc_func = func;
    session->rpc_func_user_data = user_data;
}

void
tr_sessionSetRPCWhitelist( tr_session * session,
                           const char * whitelist )
{
    tr_rpcSetWhitelist( session->rpcServer, whitelist );
}

char*
tr_sessionGetRPCWhitelist( const tr_session * session )
{
    return tr_rpcGetWhitelist( session->rpcServer );
}

void
tr_sessionSetRPCWhitelistEnabled( tr_session * session,
                                  tr_bool      isEnabled )
{
    tr_rpcSetWhitelistEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionGetRPCWhitelistEnabled( const tr_session * session )
{
    return tr_rpcGetWhitelistEnabled( session->rpcServer );
}


void
tr_sessionSetRPCPassword( tr_session * session,
                          const char * password )
{
    tr_rpcSetPassword( session->rpcServer, password );
}

char*
tr_sessionGetRPCPassword( const tr_session * session )
{
    return tr_rpcGetPassword( session->rpcServer );
}

void
tr_sessionSetRPCUsername( tr_session * session,
                          const char * username )
{
    tr_rpcSetUsername( session->rpcServer, username );
}

char*
tr_sessionGetRPCUsername( const tr_session * session )
{
    return tr_rpcGetUsername( session->rpcServer );
}

void
tr_sessionSetRPCPasswordEnabled( tr_session * session,
                                 tr_bool      isEnabled )
{
    tr_rpcSetPasswordEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionIsRPCPasswordEnabled( const tr_session * session )
{
    return tr_rpcIsPasswordEnabled( session->rpcServer );
}

/***
****
***/

tr_bool
tr_sessionIsProxyEnabled( const tr_session * session )
{
    return session->isProxyEnabled;
}

void
tr_sessionSetProxyEnabled( tr_session * session,
                           tr_bool      isEnabled )
{
    session->isProxyEnabled = isEnabled != 0;
}

tr_proxy_type
tr_sessionGetProxyType( const tr_session * session )
{
    return session->proxyType;
}

void
tr_sessionSetProxyType( tr_session *  session,
                        tr_proxy_type type )
{
    session->proxyType = type;
}

const char*
tr_sessionGetProxy( const tr_session * session )
{
    return session->proxy;
}

tr_port
tr_sessionGetProxyPort( const tr_session * session )
{
    return session->proxyPort;
}

void
tr_sessionSetProxy( tr_session * session,
                    const char * proxy )
{
    if( proxy != session->proxy )
    {
        tr_free( session->proxy );
        session->proxy = tr_strdup( proxy );
    }
}

void
tr_sessionSetProxyPort( tr_session * session,
                        tr_port      port )
{
    session->proxyPort = port;
}

tr_bool
tr_sessionIsProxyAuthEnabled( const tr_session * session )
{
    return session->isProxyAuthEnabled;
}

void
tr_sessionSetProxyAuthEnabled( tr_session * session,
                               tr_bool      isEnabled )
{
    session->isProxyAuthEnabled = isEnabled != 0;
}

const char*
tr_sessionGetProxyUsername( const tr_session * session )
{
    return session->proxyUsername;
}

void
tr_sessionSetProxyUsername( tr_session * session,
                            const char * username )
{
    if( username != session->proxyUsername )
    {
        tr_free( session->proxyUsername );
        session->proxyUsername = tr_strdup( username );
    }
}

const char*
tr_sessionGetProxyPassword( const tr_session * session )
{
    return session->proxyPassword;
}

void
tr_sessionSetProxyPassword( tr_session * session,
                            const char * password )
{
    if( password != session->proxyPassword )
    {
        tr_free( session->proxyPassword );
        session->proxyPassword = tr_strdup( password );
    }
}

