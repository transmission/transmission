/*
 * This file Copyright (C) 2008-2009 Mnemosyne LLC
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
#include <sys/types.h> /* stat(), umask() */
#include <sys/stat.h> /* stat(), umask() */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include <event.h>

#include "transmission.h"
#include "announcer.h"
#include "bandwidth.h"
#include "bencode.h"
#include "blocklist.h"
#include "crypto.h"
#include "fdlimit.h"
#include "list.h"
#include "metainfo.h" /* tr_metainfoFree */
#include "net.h"
#include "peer-io.h"
#include "peer-mgr.h"
#include "platform.h" /* tr_lock */
#include "port-forwarding.h"
#include "rpc-server.h"
#include "session.h"
#include "stats.h"
#include "torrent.h"
#include "tr-dht.h"
#include "trevent.h"
#include "utils.h"
#include "verify.h"
#include "version.h"
#include "web.h"

enum
{
    SAVE_INTERVAL_SECS = 120
};


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

struct tr_bindinfo
{
    int socket;
    tr_address addr;
    struct event ev;
};


static void
close_bindinfo( struct tr_bindinfo * b )
{
    if( ( b != NULL ) && ( b->socket >=0 ) )
    {
        event_del( &b->ev );
        tr_netCloseSocket( b->socket );
    }
}

static void
close_incoming_peer_port( tr_session * session )
{
    close_bindinfo( session->public_ipv4 );
    close_bindinfo( session->public_ipv6 );
}

static void
free_incoming_peer_port( tr_session * session )
{
    close_bindinfo( session->public_ipv4 );
    tr_free( session->public_ipv4 );
    session->public_ipv4 = NULL;

    close_bindinfo( session->public_ipv6 );
    tr_free( session->public_ipv6 );
    session->public_ipv6 = NULL;
}

static void
accept_incoming_peer( int fd, short what UNUSED, void * vsession )
{
    int clientSocket;
    tr_port clientPort;
    tr_address clientAddr;
    tr_session * session = vsession;

    clientSocket = tr_netAccept( session, fd, &clientAddr, &clientPort );
    if( clientSocket > 0 ) {
        tr_deepLog( __FILE__, __LINE__, NULL, "new incoming connection %d (%s)",
                   clientSocket, tr_peerIoAddrStr( &clientAddr, clientPort ) );
        tr_peerMgrAddIncoming( session->peerMgr, &clientAddr, clientPort, clientSocket );
    }
}

static void
open_incoming_peer_port( tr_session * session )
{
    struct tr_bindinfo * b;

    /* bind an ipv4 port to listen for incoming peers... */
    b = session->public_ipv4;
    b->socket = tr_netBindTCP( &b->addr, session->peerPort, FALSE );
    if( b->socket >= 0 ) {
        event_set( &b->ev, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session );
        event_add( &b->ev, NULL );
    }

    /* and do the exact same thing for ipv6, if it's supported... */
    if( tr_net_hasIPv6( session->peerPort ) ) {
        b = session->public_ipv6;
        b->socket = tr_netBindTCP( &b->addr, session->peerPort, FALSE );
        if( b->socket >= 0 ) {
            event_set( &b->ev, b->socket, EV_READ | EV_PERSIST, accept_incoming_peer, session );
            event_add( &b->ev, NULL );
        }
    }
}

const tr_address*
tr_sessionGetPublicAddress( const tr_session * session, int tr_af_type )
{
    switch( tr_af_type )
    {
        case TR_AF_INET: return &session->public_ipv4->addr;
        case TR_AF_INET6: return &session->public_ipv6->addr; break;
        default: return NULL;
    }
}

/***
****
***/

static tr_bool
isAltTime( const tr_session * s )
{
    int minutes, day;
    tr_bool withinTime;
    struct tm tm;
    const time_t now = time( NULL );
    const int begin = s->altSpeedTimeBegin;
    const int end = s->altSpeedTimeEnd;
    const tr_bool toNextDay = begin > end;

    tr_localtime_r( &now, &tm );
    minutes = tm.tm_hour*60 + tm.tm_min;
    day = tm.tm_wday;

    if( !toNextDay )
        withinTime = ( begin <= minutes ) && ( minutes < end );
    else /* goes past midnight */
        withinTime = ( begin <= minutes ) || ( minutes < end );

    if( !withinTime )
        return FALSE;

    if( toNextDay && (minutes < end) )
        day = (day - 1) % 7;

    return ((1<<day) & s->altSpeedTimeDay) != 0;
}

/***
****
***/

#ifdef TR_EMBEDDED
 #define TR_DEFAULT_ENCRYPTION   TR_CLEAR_PREFERRED
#else
 #define TR_DEFAULT_ENCRYPTION   TR_ENCRYPTION_PREFERRED
#endif

void
tr_sessionGetDefaultSettings( const char * configDir, tr_benc * d )
{
    char * incompleteDir = tr_buildPath( configDir, "Incomplete", NULL );

    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 35 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,        FALSE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DHT_ENABLED,              TRUE );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_DOWNLOAD_DIR,             tr_getDefaultDownloadDir( ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED,                   100 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED,           FALSE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ENCRYPTION,               TR_DEFAULT_ENCRYPTION );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_INCOMPLETE_DIR,           incompleteDir );
    tr_bencDictAddBool( d, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED,   FALSE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_LAZY_BITFIELD,            TRUE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MSGLEVEL,                 TR_MSG_INF );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_OPEN_FILE_LIMIT,          atoi( TR_DEFAULT_OPEN_FILE_LIMIT_STR ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,        atoi( TR_DEFAULT_PEER_LIMIT_GLOBAL_STR ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,       atoi( TR_DEFAULT_PEER_LIMIT_TORRENT_STR ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT,                atoi( TR_DEFAULT_PEER_PORT_STR ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, FALSE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,     49152 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,    65535 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_SOCKET_TOS,          atoi( TR_DEFAULT_PEER_SOCKET_TOS_STR ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEX_ENABLED,              TRUE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING,          TRUE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PREALLOCATION,            TR_PREALLOCATE_SPARSE );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY,                    "" );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PROXY_AUTH_ENABLED,       FALSE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PROXY_ENABLED,            FALSE );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY_PASSWORD,           "" );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PROXY_PORT,               80 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PROXY_TYPE,               TR_PROXY_HTTP );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY_USERNAME,           "" );
    tr_bencDictAddReal( d, TR_PREFS_KEY_RATIO,                    2.0 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RATIO_ENABLED,            FALSE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RENAME_PARTIAL_FILES,     TRUE );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,        FALSE );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_BIND_ADDRESS,         "0.0.0.0" );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_ENABLED,              TRUE );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_PASSWORD,             "" );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_USERNAME,             "" );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_WHITELIST,            TR_DEFAULT_RPC_WHITELIST );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,    TRUE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_RPC_PORT,                 atoi( TR_DEFAULT_RPC_PORT_STR ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_ENABLED,        FALSE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_UP,             50 ); /* half the regular */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_DOWN,           50 ); /* half the regular */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN,     540 ); /* 9am */
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED,   FALSE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_END,       1020 ); /* 5pm */
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_DAY,       TR_SCHED_ALL );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_USPEED,                   100 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED,           FALSE );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UMASK,                    022 );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT, 14 );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV4,        TR_DEFAULT_BIND_ADDRESS_IPV4 );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV6,        TR_DEFAULT_BIND_ADDRESS_IPV6 );

    tr_free( incompleteDir );
}

void
tr_sessionGetSettings( tr_session * s, struct tr_benc * d )
{
    assert( tr_bencIsDict( d ) );

    tr_bencDictReserve( d, 30 );
    tr_bencDictAddBool( d, TR_PREFS_KEY_BLOCKLIST_ENABLED,        tr_blocklistIsEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DHT_ENABLED,              s->isDHTEnabled );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_DOWNLOAD_DIR,             s->downloadDir );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_DSPEED,                   tr_sessionGetSpeedLimit( s, TR_DOWN ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_DSPEED_ENABLED,           tr_sessionIsSpeedLimited( s, TR_DOWN ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ENCRYPTION,               s->encryptionMode );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_INCOMPLETE_DIR,           tr_sessionGetIncompleteDir( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED,   tr_sessionIsIncompleteDirEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_LAZY_BITFIELD,            s->useLazyBitfield );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_MSGLEVEL,                 tr_getMessageLevel( ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_OPEN_FILE_LIMIT,          tr_fdGetFileLimit( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL,        tr_sessionGetPeerLimit( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT,       s->peerLimitPerTorrent );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT,                tr_sessionGetPeerPort( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, s->isPortRandom );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW,     s->randomPortLow );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH,    s->randomPortHigh );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PEER_SOCKET_TOS,          s->peerSocketTOS );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PEX_ENABLED,              s->isPexEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PORT_FORWARDING,          tr_sessionIsPortForwardingEnabled( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PREALLOCATION,            s->preallocationMode );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY,                    s->proxy );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PROXY_AUTH_ENABLED,       s->isProxyAuthEnabled );
    tr_bencDictAddBool( d, TR_PREFS_KEY_PROXY_ENABLED,            s->isProxyEnabled );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY_PASSWORD,           s->proxyPassword );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PROXY_PORT,               s->proxyPort );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_PROXY_TYPE,               s->proxyType );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_PROXY_USERNAME,           s->proxyUsername );
    tr_bencDictAddReal( d, TR_PREFS_KEY_RATIO,                    s->desiredRatio );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RATIO_ENABLED,            s->isRatioLimited );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RENAME_PARTIAL_FILES,     tr_sessionIsIncompleteFileNamingEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_AUTH_REQUIRED,        tr_sessionIsRPCPasswordEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_BIND_ADDRESS,         tr_sessionGetRPCBindAddress( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_ENABLED,              tr_sessionIsRPCEnabled( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_PASSWORD,             tr_sessionGetRPCPassword( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_RPC_PORT,                 tr_sessionGetRPCPort( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_USERNAME,             tr_sessionGetRPCUsername( s ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_RPC_WHITELIST,            tr_sessionGetRPCWhitelist( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_RPC_WHITELIST_ENABLED,    tr_sessionGetRPCWhitelistEnabled( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_ENABLED,        tr_sessionUsesAltSpeed( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_UP,             tr_sessionGetAltSpeed( s, TR_UP ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_DOWN,           tr_sessionGetAltSpeed( s, TR_DOWN ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN,     tr_sessionGetAltSpeedBegin( s ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED,   tr_sessionUsesAltSpeedTime( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_END,       tr_sessionGetAltSpeedEnd( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_ALT_SPEED_TIME_DAY,       tr_sessionGetAltSpeedDay( s ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_USPEED,                   tr_sessionGetSpeedLimit( s, TR_UP ) );
    tr_bencDictAddBool( d, TR_PREFS_KEY_USPEED_ENABLED,           tr_sessionIsSpeedLimited( s, TR_UP ) );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UMASK,                    s->umask );
    tr_bencDictAddInt ( d, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT, s->uploadSlotsPerTorrent );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV4,        tr_ntop_non_ts( &s->public_ipv4->addr ) );
    tr_bencDictAddStr ( d, TR_PREFS_KEY_BIND_ADDRESS_IPV6,        tr_ntop_non_ts( &s->public_ipv6->addr ) );
}

void
tr_sessionLoadSettings( tr_benc * d, const char * configDir, const char * appName )
{
    char * filename;
    tr_benc fileSettings;
    tr_benc sessionDefaults;
    tr_benc tmp;

    assert( tr_bencIsDict( d ) );

    /* initializing the defaults: caller may have passed in some app-level defaults.
     * preserve those and use the session defaults to fill in any missing gaps. */
    tr_bencInitDict( &sessionDefaults, 0 );
    tr_sessionGetDefaultSettings( configDir, &sessionDefaults );
    tr_bencMergeDicts( &sessionDefaults, d );
    tmp = *d; *d = sessionDefaults; sessionDefaults = tmp;

    /* if caller didn't specify a config dir, use the default */
    if( !configDir || !*configDir )
        configDir = tr_getDefaultConfigDir( appName );

    /* file settings override the defaults */
    filename = tr_buildPath( configDir, "settings.json", NULL );
    if( !tr_bencLoadFile( &fileSettings, TR_FMT_JSON, filename ) ) {
        tr_bencMergeDicts( d, &fileSettings );
        tr_bencFree( &fileSettings );
    }

    /* cleanup */
    tr_bencFree( &sessionDefaults );
    tr_free( filename );
}

void
tr_sessionSaveSettings( tr_session    * session,
                        const char    * configDir,
                        const tr_benc * clientSettings )
{
    tr_benc settings;
    char * filename = tr_buildPath( configDir, "settings.json", NULL );

    assert( tr_bencIsDict( clientSettings ) );

    tr_bencInitDict( &settings, 0 );

    /* the existing file settings are the fallback values */
    {
        tr_benc fileSettings;
        if( !tr_bencLoadFile( &fileSettings, TR_FMT_JSON, filename ) )
        {
            tr_bencMergeDicts( &settings, &fileSettings );
            tr_bencFree( &fileSettings );
        }
    }

    /* the client's settings override the file settings */
    tr_bencMergeDicts( &settings, clientSettings );

    /* the session's true values override the file & client settings */
    {
        tr_benc sessionSettings;
        tr_bencInitDict( &sessionSettings, 0 );
        tr_sessionGetSettings( session, &sessionSettings );
        tr_bencMergeDicts( &settings, &sessionSettings );
        tr_bencFree( &sessionSettings );
    }

    /* save the result */
    tr_bencToFile( &settings, TR_FMT_JSON, filename );
    tr_inf( "Saved \"%s\"", filename );

    /* cleanup */
    tr_free( filename );
    tr_bencFree( &settings );
}

/***
****
***/

/**
 * Periodically save the .resume files of any torrents whose
 * status has recently changed.  This prevents loss of metadata
 * in the case of a crash, unclean shutdown, clumsy user, etc.
 */
static void
onSaveTimer( int foo UNUSED, short bar UNUSED, void * vsession )
{
    tr_torrent * tor = NULL;
    tr_session * session = vsession;

    while(( tor = tr_torrentNext( session, tor )))
        tr_torrentSave( tor );

    tr_statsSaveDirty( session );

    tr_timerAdd( session->saveTimer, SAVE_INTERVAL_SECS, 0 );
}

/***
****
***/

static void tr_sessionInitImpl( void * );
static void onAltTimer( int, short, void* );
static void setAltTimer( tr_session * session );

struct init_data
{
    tr_session  * session;
    const char  * configDir;
    tr_bool       messageQueuingEnabled;
    tr_benc     * clientSettings;
};

tr_session *
tr_sessionInit( const char  * tag,
                const char  * configDir,
                tr_bool       messageQueuingEnabled,
                tr_benc     * clientSettings )
{
    int64_t i;
    tr_session * session;
    struct init_data data;

    tr_msgInit( );

    assert( tr_bencIsDict( clientSettings ) );

    /* initialize the bare skeleton of the session object */
    session = tr_new0( tr_session, 1 );
    session->bandwidth = tr_bandwidthNew( session, NULL );
    session->lock = tr_lockNew( );
    session->tag = tr_strdup( tag );
    session->magicNumber = SESSION_MAGIC_NUMBER;
    tr_bencInitList( &session->removedTorrents, 0 );

    /* nice to start logging at the very beginning */
    if( tr_bencDictFindInt( clientSettings, TR_PREFS_KEY_MSGLEVEL, &i ) )
        tr_setMessageLevel( i );

    /* start the libtransmission thread */
    tr_netInit( ); /* must go before tr_eventInit */
    tr_eventInit( session );
    assert( session->events != NULL );

    /* run the rest in the libtransmission thread */
    ++session->waiting;
    data.session = session;
    data.configDir = configDir;
    data.messageQueuingEnabled = messageQueuingEnabled;
    data.clientSettings = clientSettings;
    tr_runInEventThread( session, tr_sessionInitImpl, &data );
    while( session->waiting > 0 )
        tr_wait_msec( 100 );

    return session;
}

static void useAltSpeed( tr_session * session, tr_bool enabled, tr_bool byUser );
static void useAltSpeedTime( tr_session * session, tr_bool enabled, tr_bool byUser );

static void
onNowTimer( int foo UNUSED, short bar UNUSED, void * vsession )
{
    int usec;
    const int min = 100;
    const int max = 999999;
    struct timeval tv;
    tr_session * session = vsession;

    assert( tr_isSession( session ) );
    assert( session->nowTimer != NULL );

    /* schedule the next timer for right after the next second begins */
    gettimeofday( &tv, NULL );
    usec = 1000000 - tv.tv_usec;
    if( usec > max ) usec = max;
    if( usec < min ) usec = min;
    tr_timerAdd( session->nowTimer, 0, usec );
    tr_timeUpdate( tv.tv_sec );
    /* fprintf( stderr, "time %zu sec, %zu microsec\n", (size_t)tr_time(), (size_t)tv.tv_usec );  */
}

static void loadBlocklists( tr_session * session );

static void
tr_sessionInitImpl( void * vdata )
{
    tr_benc settings;
    struct init_data * data = vdata;
    tr_benc * clientSettings = data->clientSettings;
    tr_session * session = data->session;

    assert( tr_amInEventThread( session ) );
    assert( tr_bencIsDict( clientSettings ) );

    dbgmsg( "tr_sessionInit: the session's top-level bandwidth object is %p",
            session->bandwidth );

    tr_bencInitDict( &settings, 0 );
    tr_sessionGetDefaultSettings( data->configDir, &settings );
    tr_bencMergeDicts( &settings, clientSettings );

    session->nowTimer = tr_new0( struct event, 1 );
    evtimer_set( session->nowTimer, onNowTimer, session );
    onNowTimer( 0, 0, session );

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    tr_setMessageQueuing( data->messageQueuingEnabled );

    tr_setConfigDir( session, data->configDir );

    session->peerMgr = tr_peerMgrNew( session );

    session->shared = tr_sharedInit( session );

    /**
    ***  Blocklist
    **/

    {
        char * filename = tr_buildPath( session->configDir, "blocklists", NULL );
        tr_mkdirp( filename, 0777 );
        tr_free( filename );
        loadBlocklists( session );
    }

    assert( tr_isSession( session ) );

    session->altTimer = tr_new0( struct event, 1 );
    evtimer_set( session->altTimer, onAltTimer, session );
    setAltTimer( session );

    session->saveTimer = tr_new0( struct event, 1 );
    evtimer_set( session->saveTimer, onSaveTimer, session );
    tr_timerAdd( session->saveTimer, SAVE_INTERVAL_SECS, 0 );

    tr_announcerInit( session );

    /* first %s is the application name
       second %s is the version number */
    tr_inf( _( "%s %s started" ), TR_NAME, LONG_VERSION_STRING );

    tr_statsInit( session );

    session->web = tr_webInit( session );
    --session->waiting;

    tr_sessionSet( session, &settings );

    if( session->isDHTEnabled )
    {
        tr_dhtInit( session, &session->public_ipv4->addr );
    }

    /* cleanup */
    tr_bencFree( &settings );
}

static void
sessionSetImpl( void * vdata )
{
    int64_t i;
    double  d;
    tr_bool boolVal;
    const char * str;
    struct tr_bindinfo b;
    struct init_data * data = vdata;
    tr_session * session = data->session;
    tr_benc * settings = data->clientSettings;

    assert( tr_isSession( session ) );
    assert( tr_bencIsDict( settings ) );
    assert( tr_amInEventThread( session ) );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_MSGLEVEL, &i ) )
        tr_setMessageLevel( i );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_UMASK, &i ) ) {
        session->umask = (mode_t)i;
        umask( session->umask );
    }

    /* misc features */
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_LAZY_BITFIELD, &boolVal ) )
        tr_sessionSetLazyBitfieldEnabled( session, boolVal );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_LIMIT_TORRENT, &i ) )
        tr_sessionSetPeerLimitPerTorrent( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PEX_ENABLED, &boolVal ) )
        tr_sessionSetPexEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_DHT_ENABLED, &boolVal ) )
        tr_sessionSetDHTEnabled( session, boolVal );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ENCRYPTION, &i ) )
        tr_sessionSetEncryption( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_SOCKET_TOS, &i ) )
        session->peerSocketTOS = i;
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_BLOCKLIST_ENABLED, &boolVal ) )
        tr_blocklistSetEnabled( session, boolVal );

    /* files and directories */
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PREALLOCATION, &i ) )
        session->preallocationMode = i;
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_DOWNLOAD_DIR, &str ) )
        tr_sessionSetDownloadDir( session, str );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_INCOMPLETE_DIR, &str ) )
        tr_sessionSetIncompleteDir( session, str );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_INCOMPLETE_DIR_ENABLED, &boolVal ) )
        tr_sessionSetIncompleteDirEnabled( session, boolVal );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_RENAME_PARTIAL_FILES, &boolVal ) )
        tr_sessionSetIncompleteFileNamingEnabled( session, boolVal );

    /* proxies */
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PROXY_ENABLED, &boolVal ) )
        tr_sessionSetProxyEnabled( session, boolVal );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_PROXY, &str ) )
        tr_sessionSetProxy( session, str );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PROXY_PORT, &i ) )
        tr_sessionSetProxyPort( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PROXY_TYPE, &i ) )
        tr_sessionSetProxyType( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PROXY_AUTH_ENABLED, &boolVal ) )
        tr_sessionSetProxyAuthEnabled( session, boolVal );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_PROXY_USERNAME, &str ) )
        tr_sessionSetProxyUsername( session, str );
    if( tr_bencDictFindStr( settings, TR_PREFS_KEY_PROXY_PASSWORD, &str ) )
        tr_sessionSetProxyPassword( session, str );

    /* rpc server */
    if( session->rpcServer != NULL ) /* close the old one */
        tr_rpcClose( &session->rpcServer );
    session->rpcServer = tr_rpcInit( session, settings );

    /* public addresses */

    free_incoming_peer_port( session );

    str = TR_PREFS_KEY_BIND_ADDRESS_IPV4;
    tr_bencDictFindStr( settings, TR_PREFS_KEY_BIND_ADDRESS_IPV4, &str );
    if( !tr_pton( str, &b.addr ) || ( b.addr.type != TR_AF_INET ) )
        b.addr = tr_inaddr_any;
    b.socket = -1;
    session->public_ipv4 = tr_memdup( &b, sizeof( struct tr_bindinfo ) );
    tr_webSetInterface( session->web, &session->public_ipv4->addr );

    str = TR_PREFS_KEY_BIND_ADDRESS_IPV6;
    tr_bencDictFindStr( settings, TR_PREFS_KEY_BIND_ADDRESS_IPV6, &str );
    if( !tr_pton( str, &b.addr ) || ( b.addr.type != TR_AF_INET6 ) )
        b.addr = tr_in6addr_any;
    b.socket = -1;
    session->public_ipv6 = tr_memdup( &b, sizeof( struct tr_bindinfo ) );

    /* incoming peer port */
    if( tr_bencDictFindInt ( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_LOW, &i ) )
        session->randomPortLow = i;
    if( tr_bencDictFindInt ( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_HIGH, &i ) )
        session->randomPortHigh = i;
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PEER_PORT_RANDOM_ON_START, &boolVal ) )
        tr_sessionSetPeerPortRandomOnStart( session, boolVal );
    if( !tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_PORT, &i ) )
        i = session->peerPort;
    tr_sessionSetPeerPort( session, boolVal ? getRandomPort( session ) : i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_PORT_FORWARDING, &boolVal ) )
        tr_sessionSetPortForwardingEnabled( session, boolVal );

    /* file and peer socket limits */
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &i ) )
        tr_fdSetPeerLimit( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_OPEN_FILE_LIMIT, &i ) )
        tr_fdSetFileLimit( session, i );

    /**
    **/

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_UPLOAD_SLOTS_PER_TORRENT, &i ) )
        session->uploadSlotsPerTorrent = i;

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_USPEED, &i ) )
        tr_sessionSetSpeedLimit( session, TR_UP, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_USPEED_ENABLED, &boolVal ) )
        tr_sessionLimitSpeed( session, TR_UP, boolVal );

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_DSPEED, &i ) )
        tr_sessionSetSpeedLimit( session, TR_DOWN, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_DSPEED_ENABLED, &boolVal ) )
        tr_sessionLimitSpeed( session, TR_DOWN, boolVal );

    if( tr_bencDictFindReal( settings, TR_PREFS_KEY_RATIO, &d ) )
        tr_sessionSetRatioLimit( session, d );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_RATIO_ENABLED, &boolVal ) )
        tr_sessionSetRatioLimited( session, boolVal );

    /**
    ***  Alternate speed limits
    **/

    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_UP, &i ) )
        tr_sessionSetAltSpeed( session, TR_UP, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_DOWN, &i ) )
        tr_sessionSetAltSpeed( session, TR_DOWN, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_BEGIN, &i ) )
        tr_sessionSetAltSpeedBegin( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_END, &i ) )
        tr_sessionSetAltSpeedEnd( session, i );
    if( tr_bencDictFindInt( settings, TR_PREFS_KEY_ALT_SPEED_TIME_DAY, &i ) )
        tr_sessionSetAltSpeedDay( session, i );
    if( tr_bencDictFindBool( settings, TR_PREFS_KEY_ALT_SPEED_TIME_ENABLED, &boolVal ) )
        useAltSpeedTime( session, boolVal, FALSE );
    if( boolVal )
        useAltSpeed( session, isAltTime( session ), FALSE );
    else if( tr_bencDictFindBool( settings, TR_PREFS_KEY_ALT_SPEED_ENABLED, &boolVal ) )
        useAltSpeed( session, boolVal, FALSE );

    --session->waiting;
}

void
tr_sessionSet( tr_session * session, struct tr_benc  * settings )
{
    struct init_data data;
    data.session = session;
    data.clientSettings = settings;

    /* run the rest in the libtransmission thread */
    ++session->waiting;
    tr_runInEventThread( session, sessionSetImpl, &data );
    while( session->waiting > 0 )
        tr_wait_msec( 100 );
}

/***
****
***/

void
tr_sessionSetDownloadDir( tr_session * session, const char * dir )
{
    assert( tr_isSession( session ) );

    if( session->downloadDir != dir )
    {
        tr_free( session->downloadDir );
        session->downloadDir = tr_strdup( dir );
    }
}

const char *
tr_sessionGetDownloadDir( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->downloadDir;
}

/***
****
***/

void
tr_sessionSetIncompleteFileNamingEnabled( tr_session * session, tr_bool b )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( b ) );

    session->isIncompleteFileNamingEnabled = b;
}

tr_bool
tr_sessionIsIncompleteFileNamingEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isIncompleteFileNamingEnabled;
}

/***
****
***/


void
tr_sessionSetIncompleteDir( tr_session * session, const char * dir )
{
    assert( tr_isSession( session ) );

    if( session->incompleteDir != dir )
    {
        tr_free( session->incompleteDir );

        session->incompleteDir = tr_strdup( dir );
    }
}

const char*
tr_sessionGetIncompleteDir( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->incompleteDir;
}

void
tr_sessionSetIncompleteDirEnabled( tr_session * session, tr_bool b )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( b ) );

    session->isIncompleteDirEnabled = b;
}

tr_bool
tr_sessionIsIncompleteDirEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isIncompleteDirEnabled;
}

/***
****
***/

void
tr_globalLock( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_lockLock( session->lock );
}

void
tr_globalUnlock( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_lockUnlock( session->lock );
}

tr_bool
tr_globalIsLocked( const tr_session * session )
{
    return tr_isSession( session ) && tr_lockHave( session->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 *
 **********************************************************************/

static void
setPeerPort( void * session )
{
    tr_torrent * tor = NULL;

    assert( tr_isSession( session ) );

    close_incoming_peer_port( session );
    open_incoming_peer_port( session );
    tr_sharedPortChanged( session );

    while(( tor = tr_torrentNext( session, tor )))
        tr_torrentChangeMyPort( tor );
}

void
tr_sessionSetPeerPort( tr_session * session, tr_port port )
{
    assert( tr_isSession( session ) );

    if( session->peerPort != port )
    {
        session->peerPort = port;

        tr_runInEventThread( session, setPeerPort, session );
    }
}

tr_port
tr_sessionGetPeerPort( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->peerPort;
}

tr_port
tr_sessionSetPeerPortRandom( tr_session * session )
{
    assert( tr_isSession( session ) );

    tr_sessionSetPeerPort( session, getRandomPort( session ) );
    return session->peerPort;
}

void
tr_sessionSetPeerPortRandomOnStart( tr_session * session,
                                    tr_bool random )
{
    assert( tr_isSession( session ) );

    session->isPortRandom = random;
}

tr_bool
tr_sessionGetPeerPortRandomOnStart( tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isPortRandom;
}

tr_port_forwarding
tr_sessionGetPortForwarding( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_sharedTraversalStatus( session->shared );
}

/***
****
***/

static void
updateSeedRatio( tr_session * session )
{
    tr_torrent * tor = NULL;

    while(( tor = tr_torrentNext( session, tor )))
        tor->needsSeedRatioCheck = TRUE;
}

void
tr_sessionSetRatioLimited( tr_session * session, tr_bool isLimited )
{
    assert( tr_isSession( session ) );

    session->isRatioLimited = isLimited;
    updateSeedRatio( session );
}

void
tr_sessionSetRatioLimit( tr_session * session, double desiredRatio )
{
    assert( tr_isSession( session ) );

    session->desiredRatio = desiredRatio;
    updateSeedRatio( session );
}

tr_bool
tr_sessionIsRatioLimited( const tr_session  * session )
{
    assert( tr_isSession( session ) );

    return session->isRatioLimited;
}

double
tr_sessionGetRatioLimit( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->desiredRatio;
}

/***
****  SPEED LIMITS
***/

tr_bool
tr_sessionGetActiveSpeedLimit( const tr_session * session, tr_direction dir, int * setme )
{
    int isLimited = TRUE;

    if( !tr_isSession( session ) )
        return FALSE;

    if( tr_sessionUsesAltSpeed( session ) )
        *setme = tr_sessionGetAltSpeed( session, dir );
    else if( tr_sessionIsSpeedLimited( session, dir ) )
        *setme = tr_sessionGetSpeedLimit( session, dir );
    else
        isLimited = FALSE;

    return isLimited;
}

static void
updateBandwidth( tr_session * session, tr_direction dir )
{
    int limit = 0;
    const tr_bool isLimited = tr_sessionGetActiveSpeedLimit( session, dir, &limit );
    const tr_bool zeroCase = isLimited && !limit;

    tr_bandwidthSetLimited( session->bandwidth, dir, isLimited && !zeroCase );

    tr_bandwidthSetDesiredSpeed( session->bandwidth, dir, limit );
}

static void
altSpeedToggled( void * vsession )
{
    tr_session * session = vsession;

    assert( tr_isSession( session ) );

    updateBandwidth( session, TR_UP );
    updateBandwidth( session, TR_DOWN );

    if( session->altCallback != NULL )
        (*session->altCallback)( session, session->altSpeedEnabled, session->altSpeedChangedByUser, session->altCallbackUserData );
}

/* tell the alt speed limit timer to fire again at the top of the minute */
static void
setAltTimer( tr_session * session )
{
    const time_t now = time( NULL );
    struct tm tm;

    assert( tr_isSession( session ) );
    assert( session->altTimer != NULL );

    tr_localtime_r( &now, &tm );
    tr_timerAdd( session->altTimer, 60-tm.tm_sec, 0 );
}

/* this is called once a minute to:
 * (1) update session->isAltTime
 * (2) alter the speed limits when the alt limits go on and off */
static void
onAltTimer( int foo UNUSED, short bar UNUSED, void * vsession )
{
    tr_session * session = vsession;

    assert( tr_isSession( session ) );

    if( session->altSpeedTimeEnabled )
    {
        const time_t now = time( NULL );
        struct tm tm;
        int currentMinute, day;
        tr_bool isBeginTime, isEndTime, isDay;
        tr_localtime_r( &now, &tm );
        currentMinute = tm.tm_hour*60 + tm.tm_min;
        day = tm.tm_wday;

        isBeginTime = currentMinute == session->altSpeedTimeBegin;
        isEndTime = currentMinute == session->altSpeedTimeEnd;
        if( isBeginTime || isEndTime )
        {
            /* if looking at the end date, look at the next day if end time is before begin time */
            if( isEndTime && !isBeginTime && session->altSpeedTimeEnd < session->altSpeedTimeBegin )
                day = (day - 1) % 7;

            isDay = ((1<<day) & session->altSpeedTimeDay) != 0;

            if( isDay )
                useAltSpeed( session, isBeginTime, FALSE );
        }
    }

    setAltTimer( session );
}

/***
****  Primary session speed limits
***/

void
tr_sessionSetSpeedLimit( tr_session * s, tr_direction d, int KB_s )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );
    assert( KB_s >= 0 );

    s->speedLimit[d] = KB_s;

    updateBandwidth( s, d );
}

int
tr_sessionGetSpeedLimit( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->speedLimit[d];
}

void
tr_sessionLimitSpeed( tr_session * s, tr_direction d, tr_bool b )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );
    assert( tr_isBool( b ) );

    s->speedLimitEnabled[d] = b;

    updateBandwidth( s, d );
}

tr_bool
tr_sessionIsSpeedLimited( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->speedLimitEnabled[d];
}

/***
****  Alternative speed limits that are used during scheduled times
***/

void
tr_sessionSetAltSpeed( tr_session * s, tr_direction d, int KB_s )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );
    assert( KB_s >= 0 );

    s->altSpeed[d] = KB_s;

    updateBandwidth( s, d );
}

int
tr_sessionGetAltSpeed( const tr_session * s, tr_direction d )
{
    assert( tr_isSession( s ) );
    assert( tr_isDirection( d ) );

    return s->altSpeed[d];
}

void
useAltSpeedTime( tr_session * session, tr_bool enabled, tr_bool byUser )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( enabled ) );
    assert( tr_isBool( byUser ) );

    if( session->altSpeedTimeEnabled != enabled )
    {
        const tr_bool isAlt = isAltTime( session );

        session->altSpeedTimeEnabled = enabled;

        if( enabled && session->altSpeedEnabled != isAlt )
            useAltSpeed( session, isAlt, byUser );
    }
}
void
tr_sessionUseAltSpeedTime( tr_session * s, tr_bool b )
{
    useAltSpeedTime( s, b, TRUE );
}

tr_bool
tr_sessionUsesAltSpeedTime( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->altSpeedTimeEnabled;
}

void
tr_sessionSetAltSpeedBegin( tr_session * s, int minutes )
{
    assert( tr_isSession( s ) );
    assert( 0<=minutes && minutes<(60*24) );

    if( s->altSpeedTimeBegin != minutes )
    {
        s->altSpeedTimeBegin = minutes;

        if( tr_sessionUsesAltSpeedTime( s ) )
            useAltSpeed( s, isAltTime( s ), TRUE );
    }
}

int
tr_sessionGetAltSpeedBegin( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->altSpeedTimeBegin;
}

void
tr_sessionSetAltSpeedEnd( tr_session * s, int minutes )
{
    assert( tr_isSession( s ) );
    assert( 0<=minutes && minutes<(60*24) );

    if( s->altSpeedTimeEnd != minutes )
    {
        s->altSpeedTimeEnd = minutes;

        if( tr_sessionUsesAltSpeedTime( s ) )
            useAltSpeed( s, isAltTime( s ), TRUE );
    }
}

int
tr_sessionGetAltSpeedEnd( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->altSpeedTimeEnd;
}

void
tr_sessionSetAltSpeedDay( tr_session * s, tr_sched_day day )
{
    assert( tr_isSession( s ) );

    if( s->altSpeedTimeDay != day )
    {
        s->altSpeedTimeDay = day;

        if( tr_sessionUsesAltSpeedTime( s ) )
            useAltSpeed( s, isAltTime( s ), TRUE );
    }
}

tr_sched_day
tr_sessionGetAltSpeedDay( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->altSpeedTimeDay;
}

void
useAltSpeed( tr_session * s, tr_bool enabled, tr_bool byUser )
{
    assert( tr_isSession( s ) );
    assert( tr_isBool( enabled ) );
    assert( tr_isBool( byUser ) );

    if( s->altSpeedEnabled != enabled)
    {
        s->altSpeedEnabled = enabled;
        s->altSpeedChangedByUser = byUser;

        tr_runInEventThread( s, altSpeedToggled, s );
    }
}
void
tr_sessionUseAltSpeed( tr_session * session, tr_bool enabled )
{
    useAltSpeed( session, enabled, TRUE );
}

tr_bool
tr_sessionUsesAltSpeed( const tr_session * s )
{
    assert( tr_isSession( s ) );

    return s->altSpeedEnabled;
}

void
tr_sessionSetAltSpeedFunc( tr_session       * session,
                           tr_altSpeedFunc    func,
                           void             * userData )
{
    assert( tr_isSession( session ) );

    session->altCallback = func;
    session->altCallbackUserData = userData;
}

void
tr_sessionClearAltSpeedFunc( tr_session * session )
{
    tr_sessionSetAltSpeedFunc( session, NULL, NULL );
}

/***
****
***/

void
tr_sessionSetPeerLimit( tr_session * session, uint16_t maxGlobalPeers )
{
    assert( tr_isSession( session ) );

    tr_fdSetPeerLimit( session, maxGlobalPeers );
}

uint16_t
tr_sessionGetPeerLimit( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_fdGetPeerLimit( session );
}

void
tr_sessionSetPeerLimitPerTorrent( tr_session  * session, uint16_t n )
{
    assert( tr_isSession( session ) );

    session->peerLimitPerTorrent = n;
}

uint16_t
tr_sessionGetPeerLimitPerTorrent( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->peerLimitPerTorrent;
}

/***
****
***/

double
tr_sessionGetPieceSpeed( const tr_session * session, tr_direction dir )
{
    return tr_isSession( session ) ? tr_bandwidthGetPieceSpeed( session->bandwidth, 0, dir ) : 0.0;
}

double
tr_sessionGetRawSpeed( const tr_session * session, tr_direction dir )
{
    return tr_isSession( session ) ? tr_bandwidthGetPieceSpeed( session->bandwidth, 0, dir ) : 0.0;
}

int
tr_sessionCountTorrents( const tr_session * session )
{
    return tr_isSession( session ) ? session->torrentCount : 0;
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

static void closeBlocklists( tr_session * );

static void
sessionCloseImpl( void * vsession )
{
    tr_session *  session = vsession;
    tr_torrent *  tor;
    int           i, n;
    tr_torrent ** torrents;

    assert( tr_isSession( session ) );

    free_incoming_peer_port( session );

    if( session->isDHTEnabled )
        tr_dhtUninit( session );

    evtimer_del( session->saveTimer );
    tr_free( session->saveTimer );
    session->saveTimer = NULL;

    evtimer_del( session->nowTimer );
    tr_free( session->nowTimer );
    session->nowTimer = NULL;

    evtimer_del( session->altTimer );
    tr_free( session->altTimer );
    session->altTimer = NULL;

    tr_verifyClose( session );
    tr_sharedClose( session );
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

    tr_announcerClose( session );
    tr_statsClose( session );
    tr_peerMgrFree( session->peerMgr );

    closeBlocklists( session );
    tr_webClose( &session->web );

    session->isClosed = TRUE;
}

static int
deadlineReached( const uint64_t deadline )
{
    return tr_date( ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 20

void
tr_sessionClose( tr_session * session )
{
    const int      maxwait_msec = SHUTDOWN_MAX_SECONDS * 1000;
    const uint64_t deadline = tr_date( ) + maxwait_msec;

    assert( tr_isSession( session ) );

    dbgmsg( "shutting down transmission session %p", session );

    /* close the session */
    tr_runInEventThread( session, sessionCloseImpl, session );
    while( !session->isClosed && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting for the libtransmission thread to finish" );
        tr_wait_msec( 100 );
    }

    /* "shared" and "tracker" have live sockets,
     * so we need to keep the transmission thread alive
     * for a bit while they tell the router & tracker
     * that we're closing now */
    while( ( session->shared
           || session->announcer ) && !deadlineReached( deadline ) )
    {
        dbgmsg( "waiting on port unmap (%p) or announcer (%p)",
                session->shared, session->announcer );
        tr_wait_msec( 100 );
    }

    tr_fdClose( session );

    /* close the libtransmission thread */
    tr_eventClose( session );
    while( session->events != NULL )
    {
        static tr_bool forced = FALSE;
        dbgmsg( "waiting for libtransmission thread to finish" );
        tr_wait_msec( 100 );
        if( deadlineReached( deadline ) && !forced )
        {
            event_loopbreak( );
            forced = TRUE;
        }
    }

    /* free the session memory */
    tr_bencFree( &session->removedTorrents );
    tr_bandwidthFree( session->bandwidth );
    tr_lockFree( session->lock );
    if( session->metainfoLookup ) {
        tr_bencFree( session->metainfoLookup );
        tr_free( session->metainfoLookup );
    }
    tr_free( session->tag );
    tr_free( session->configDir );
    tr_free( session->resumeDir );
    tr_free( session->torrentDir );
    tr_free( session->downloadDir );
    tr_free( session->incompleteDir );
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

    assert( tr_isSession( session ) );

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
                if(( tor = tr_torrentNew( ctor, NULL )))
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
    assert( tr_isSession( session ) );

    session->isPexEnabled = enabled != 0;
}

tr_bool
tr_sessionIsPexEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isPexEnabled;
}

tr_bool
tr_sessionAllowsDHT( const tr_session * session UNUSED )
{
    return tr_sessionIsDHTEnabled( session );
}

tr_bool
tr_sessionIsDHTEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isDHTEnabled;
}

static void
toggleDHTImpl(  void * data )
{
    tr_session * session = data;
    assert( tr_isSession( session ) );

    if( session->isDHTEnabled )
        tr_dhtUninit( session );

    session->isDHTEnabled = !session->isDHTEnabled;

    if( session->isDHTEnabled )
        tr_dhtInit( session, &session->public_ipv4->addr );
}

void
tr_sessionSetDHTEnabled( tr_session * session, tr_bool enabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( enabled ) );

    if( ( enabled != 0 ) != ( session->isDHTEnabled != 0 ) )
        tr_runInEventThread( session, toggleDHTImpl, session );
}

/***
****
***/

void
tr_sessionSetLazyBitfieldEnabled( tr_session * session,
                                  tr_bool      enabled )
{
    assert( tr_isSession( session ) );

    session->useLazyBitfield = enabled != 0;
}

tr_bool
tr_sessionIsLazyBitfieldEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->useLazyBitfield;
}

/***
****
***/

void
tr_sessionSetPortForwardingEnabled( tr_session  * session,
                                    tr_bool       enabled )
{
    assert( tr_isSession( session ) );

    tr_globalLock( session );
    tr_sharedTraversalEnable( session->shared, enabled );
    tr_globalUnlock( session );
}

tr_bool
tr_sessionIsPortForwardingEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_sharedTraversalIsEnabled( session->shared );
}

/***
****
***/

static int
tr_stringEndsWith( const char * str, const char * end )
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

static void
closeBlocklists( tr_session * session )
{
    tr_list_free( &session->blocklists,
                  (TrListForeachFunc)_tr_blocklistFree );
}

void
tr_sessionReloadBlocklists( tr_session * session )
{
    closeBlocklists( session );
    loadBlocklists( session );
}

int
tr_blocklistGetRuleCount( const tr_session * session )
{
    int       n = 0;
    tr_list * l;

    assert( tr_isSession( session ) );

    for( l = session->blocklists; l; l = l->next )
        n += _tr_blocklistGetRuleCount( l->data );
    return n;
}

tr_bool
tr_blocklistIsEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isBlocklistEnabled;
}

void
tr_blocklistSetEnabled( tr_session * session,
                        tr_bool      isEnabled )
{
    tr_list * l;

    assert( tr_isSession( session ) );

    session->isBlocklistEnabled = isEnabled != 0;

    for( l=session->blocklists; l!=NULL; l=l->next )
        _tr_blocklistSetEnabled( l->data, isEnabled );
}

tr_bool
tr_blocklistExists( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->blocklists != NULL;
}

int
tr_blocklistSetContent( tr_session * session,
                        const char * contentFilename )
{
    tr_list *      l;
    tr_blocklist * b;
    const char *   defaultName = "level1.bin";

    assert( tr_isSession( session ) );

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

    assert( tr_isSession( session ) );

    for( l = session->blocklists; l; l = l->next )
        if( _tr_blocklistHasAddress( l->data, addr ) )
            return TRUE;
    return FALSE;
}

/***
****
***/

static void
metainfoLookupInit( tr_session * session )
{
    struct stat  sb;
    const char * dirname = tr_getTorrentDir( session );
    DIR *        odir = NULL;
    tr_ctor *    ctor = NULL;
    tr_benc * lookup;
    int n = 0;

    assert( tr_isSession( session ) );

    /* walk through the directory and find the mappings */
    lookup = tr_new0( tr_benc, 1 );
    tr_bencInitDict( lookup, 0 );
    ctor = tr_ctorNew( session );
    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && ( ( odir = opendir( dirname ) ) ) )
    {
        struct dirent *d;
        while(( d = readdir( odir )))
        {
            if( d->d_name && d->d_name[0] != '.' )
            {
                tr_info inf;
                char * path = tr_buildPath( dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, path );
                if( !tr_torrentParse( ctor, &inf ) )
                {
                    ++n;
                    tr_bencDictAddStr( lookup, inf.hashString, path );
                }
                tr_free( path );
            }
        }
        closedir( odir );
    }
    tr_ctorFree( ctor );

    session->metainfoLookup = lookup;
    tr_dbg( "Found %d torrents in \"%s\"", n, dirname );
}

const char*
tr_sessionFindTorrentFile( const tr_session * session,
                           const char       * hashString )
{
    const char * filename = NULL;
    if( !session->metainfoLookup )
        metainfoLookupInit( (tr_session*)session );
    tr_bencDictFindStr( session->metainfoLookup, hashString, &filename );
    return filename;
}

void
tr_sessionSetTorrentFile( tr_session * session,
                          const char * hashString,
                          const char * filename )
{
    /* since we walk session->configDir/torrents/ to build the lookup table,
     * and tr_sessionSetTorrentFile() is just to tell us there's a new file
     * in that same directory, we don't need to do anything here if the
     * lookup table hasn't been built yet */
    if( session->metainfoLookup )
        tr_bencDictAddStr( session->metainfoLookup, hashString, filename );
}

tr_torrent*
tr_torrentNext( tr_session * session,
                tr_torrent * tor )
{
    tr_torrent * ret;

    assert( !session || tr_isSession( session ) );

    if( !session )
        ret = NULL;
    else if( !tor )
        ret = session->torrentList;
    else
        ret = tor->next;

    return ret;
}

/***
****
***/

void
tr_sessionSetRPCEnabled( tr_session * session,
                         tr_bool      isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionIsRPCEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcIsEnabled( session->rpcServer );
}

void
tr_sessionSetRPCPort( tr_session * session,
                      tr_port      port )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPort( session->rpcServer, port );
}

tr_port
tr_sessionGetRPCPort( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetPort( session->rpcServer );
}

void
tr_sessionSetRPCCallback( tr_session * session,
                          tr_rpc_func  func,
                          void *       user_data )
{
    assert( tr_isSession( session ) );

    session->rpc_func = func;
    session->rpc_func_user_data = user_data;
}

void
tr_sessionSetRPCWhitelist( tr_session * session,
                           const char * whitelist )
{
    assert( tr_isSession( session ) );

    tr_rpcSetWhitelist( session->rpcServer, whitelist );
}

const char*
tr_sessionGetRPCWhitelist( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetWhitelist( session->rpcServer );
}

void
tr_sessionSetRPCWhitelistEnabled( tr_session * session,
                                  tr_bool      isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetWhitelistEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionGetRPCWhitelistEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetWhitelistEnabled( session->rpcServer );
}


void
tr_sessionSetRPCPassword( tr_session * session,
                          const char * password )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPassword( session->rpcServer, password );
}

const char*
tr_sessionGetRPCPassword( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetPassword( session->rpcServer );
}

void
tr_sessionSetRPCUsername( tr_session * session,
                          const char * username )
{
    assert( tr_isSession( session ) );

    tr_rpcSetUsername( session->rpcServer, username );
}

const char*
tr_sessionGetRPCUsername( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetUsername( session->rpcServer );
}

void
tr_sessionSetRPCPasswordEnabled( tr_session * session,
                                 tr_bool      isEnabled )
{
    assert( tr_isSession( session ) );

    tr_rpcSetPasswordEnabled( session->rpcServer, isEnabled );
}

tr_bool
tr_sessionIsRPCPasswordEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcIsPasswordEnabled( session->rpcServer );
}

const char *
tr_sessionGetRPCBindAddress( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return tr_rpcGetBindAddress( session->rpcServer );
}

/***
****
***/

tr_bool
tr_sessionIsProxyEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isProxyEnabled;
}

void
tr_sessionSetProxyEnabled( tr_session * session,
                           tr_bool      isEnabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( isEnabled ) );

    session->isProxyEnabled = isEnabled != 0;
}

tr_proxy_type
tr_sessionGetProxyType( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->proxyType;
}

void
tr_sessionSetProxyType( tr_session *  session,
                        tr_proxy_type type )
{
    assert( tr_isSession( session ) );

    session->proxyType = type;
}

const char*
tr_sessionGetProxy( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->proxy;
}

tr_port
tr_sessionGetProxyPort( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->proxyPort;
}

void
tr_sessionSetProxy( tr_session * session,
                    const char * proxy )
{
    assert( tr_isSession( session ) );

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
    assert( tr_isSession( session ) );

    session->proxyPort = port;
}

tr_bool
tr_sessionIsProxyAuthEnabled( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->isProxyAuthEnabled;
}

void
tr_sessionSetProxyAuthEnabled( tr_session * session,
                               tr_bool      isEnabled )
{
    assert( tr_isSession( session ) );
    assert( tr_isBool( isEnabled ) );

    session->isProxyAuthEnabled = isEnabled != 0;
}

const char*
tr_sessionGetProxyUsername( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->proxyUsername;
}

void
tr_sessionSetProxyUsername( tr_session * session,
                            const char * username )
{
    assert( tr_isSession( session ) );

    if( username != session->proxyUsername )
    {
        tr_free( session->proxyUsername );
        session->proxyUsername = tr_strdup( username );
    }
}

const char*
tr_sessionGetProxyPassword( const tr_session * session )
{
    assert( tr_isSession( session ) );

    return session->proxyPassword;
}

void
tr_sessionSetProxyPassword( tr_session * session,
                            const char * password )
{
    assert( tr_isSession( session ) );

    if( password != session->proxyPassword )
    {
        tr_free( session->proxyPassword );
        session->proxyPassword = tr_strdup( password );
    }
}

int
tr_sessionGetActiveTorrentCount( tr_session * session )
{
    int ret = 0;
    tr_torrent * tor = NULL;

    assert( tr_isSession( session ) );

    while(( tor = tr_torrentNext( session, tor )))
        if( tr_torrentGetActivity( tor ) != TR_STATUS_STOPPED )
            ++ret;

    return ret;
}
