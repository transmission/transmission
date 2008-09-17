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
#include <stdlib.h>
#include <string.h> /* memcpy */

#include <signal.h>
#include <sys/types.h> /* stat */
#include <sys/stat.h> /* stat */
#include <unistd.h> /* stat */
#include <dirent.h> /* opendir */

#include "transmission.h"
#include "blocklist.h"
#include "fdlimit.h"
#include "list.h"
#include "metainfo.h" /* tr_metainfoFree */
#include "net.h"
#include "peer-mgr.h"
#include "platform.h" /* tr_lock */
#include "port-forwarding.h"
#include "ratecontrol.h"
#include "rpc-server.h"
#include "stats.h"
#include "torrent.h"
#include "tracker.h"
#include "trevent.h"
#include "utils.h"
#include "web.h"
#include "crypto.h"

/* Generate a peer id : "-TRxyzb-" + 12 random alphanumeric
   characters, where x is the major version number, y is the
   minor version number, z is the maintenance number, and b
   designates beta (Azureus-style) */
uint8_t*
tr_peerIdNew( void )
{
    int i;
    int val;
    int total = 0;
    uint8_t * buf = tr_new( uint8_t, 21 );
    const char * pool = "0123456789abcdefghijklmnopqrstuvwxyz";
    const int base = 36;

    memcpy( buf, PEERID_PREFIX, 8 );

    for( i=8; i<19; ++i ) {
        val = tr_cryptoRandInt( base );
        total += val;
        buf[i] = pool[val];
    }

    val = total % base ? base - (total % base) : 0;
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
tr_sessionSetEncryption( tr_session * session, tr_encryption_mode mode )
{
    assert( session );
    assert( mode==TR_ENCRYPTION_PREFERRED
         || mode==TR_ENCRYPTION_REQUIRED
         || mode==TR_CLEAR_PREFERRED );

    session->encryptionMode = mode;
}

/***
****
***/

static int
tr_stringEndsWith( const char * str, const char * end )
{
    const size_t slen = strlen( str );
    const size_t elen = strlen( end );
    return slen>=elen && !memcmp( &str[slen-elen], end, elen );
}

static void
loadBlocklists( tr_session * session )
{
    int binCount = 0;
    int newCount = 0;
    struct stat sb;
    char dirname[MAX_PATH_LENGTH];
    DIR * odir = NULL;
    tr_list * list = NULL;
    const int isEnabled = session->isBlocklistEnabled;

    /* walk through the directory and find blocklists */
    tr_buildPath( dirname, sizeof( dirname ), session->configDir, "blocklists", NULL );
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && (( odir = opendir( dirname ))))
    {
        struct dirent *d;
        for( d=readdir( odir ); d; d=readdir( odir ) )
        {
            char filename[MAX_PATH_LENGTH];

            if( !d->d_name || d->d_name[0]=='.' ) /* skip dotfiles, ., and .. */
                continue;

            tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );

            if( tr_stringEndsWith( filename, ".bin" ) )
            {
                /* if we don't already have this blocklist, add it */
                if( !tr_list_find( list, filename, (TrListCompareFunc)strcmp ) )
                {
                    tr_list_append( &list, _tr_blocklistNew( filename, isEnabled ) );
                    ++binCount;
                }
            }
            else
            {
                /* strip out the file suffix, if there is one, and add ".bin" instead */
                tr_blocklist * b;
                const char * dot = strrchr( d->d_name, '.' );
                const int len = dot ? dot - d->d_name : (int)strlen( d->d_name );
                char tmp[MAX_PATH_LENGTH];
                tr_snprintf( tmp, sizeof( tmp ),
                             "%s%c%*.*s.bin", dirname, TR_PATH_DELIMITER, len, len, d->d_name );
                b = _tr_blocklistNew( tmp, isEnabled );
                _tr_blocklistSetContent( b, filename );
                tr_list_append( &list, b );
                ++newCount;
            }
        }

        closedir( odir );
    }

    session->blocklists = list;

    if( binCount )
        tr_dbg( "Found %d blocklists in \"%s\"", binCount, dirname );
    if( newCount )
        tr_dbg( "Found %d new blocklists in \"%s\"", newCount, dirname );
}

/***
****
***/

static void metainfoLookupRescan( tr_handle * h );

tr_handle *
tr_sessionInitFull( const char        * configDir,
                    const char        * tag,
                    const char        * downloadDir,
                    int                 isPexEnabled,
                    int                 isPortForwardingEnabled,
                    int                 publicPort,
                    tr_encryption_mode  encryptionMode,
                    int                 useLazyBitfield,
                    int                 useUploadLimit,
                    int                 uploadLimit,
                    int                 useDownloadLimit,
                    int                 downloadLimit,
                    int                 globalPeerLimit,
                    int                 messageLevel,
                    int                 isMessageQueueingEnabled,
                    int                 isBlocklistEnabled,
                    int                 peerSocketTOS,
                    int                 rpcIsEnabled,
                    int                 rpcPort,
                    const char        * rpcACL,
                    int                 rpcAuthIsEnabled,
                    const char        * rpcUsername,
                    const char        * rpcPassword,
                    int                 proxyIsEnabled,
                    const char        * proxy,
                    int                 proxyPort,
                    tr_proxy_type       proxyType,
                    int                 proxyAuthIsEnabled,
                    const char        * proxyUsername,
                    const char        * proxyPassword )
{
    tr_handle * h;
    char filename[MAX_PATH_LENGTH];

#ifndef WIN32
    /* Don't exit when writing on a broken socket */
    signal( SIGPIPE, SIG_IGN );
#endif

    tr_msgInit( );
    tr_setMessageLevel( messageLevel );
    tr_setMessageQueuing( isMessageQueueingEnabled );

    h = tr_new0( tr_handle, 1 );
    h->lock = tr_lockNew( );
    h->isPexEnabled = isPexEnabled ? 1 : 0;
    h->encryptionMode = encryptionMode;
    h->peerSocketTOS = peerSocketTOS;
    h->downloadDir = tr_strdup( downloadDir );
    h->isProxyEnabled = proxyIsEnabled ? 1 : 0;
    h->proxy = tr_strdup( proxy );
    h->proxyPort = proxyPort;
    h->proxyType = proxyType;
    h->isProxyAuthEnabled = proxyAuthIsEnabled != 0;
    h->proxyUsername = tr_strdup( proxyUsername );
    h->proxyPassword = tr_strdup( proxyPassword );

    if( configDir == NULL )
        configDir = tr_getDefaultConfigDir( );
    tr_setConfigDir( h, configDir );

    tr_netInit(); /* must go before tr_eventInit */

    tr_eventInit( h );
    while( !h->events )
        tr_wait( 50 );

    h->tag = tr_strdup( tag );
    h->peerMgr = tr_peerMgrNew( h );

    h->useLazyBitfield = useLazyBitfield != 0;

    /* Initialize rate and file descripts controls */

    h->uploadLimit = uploadLimit;
    h->useUploadLimit = useUploadLimit;
    h->downloadLimit = downloadLimit;
    h->useDownloadLimit = useDownloadLimit;

    tr_fdInit( globalPeerLimit );
    h->shared = tr_sharedInit( h, isPortForwardingEnabled, publicPort );
    h->isPortSet = publicPort >= 0;

    /* first %s is the application name
       second %s is the version number */
    tr_inf( _( "%s %s started" ), TR_NAME, LONG_VERSION_STRING );

    /* initialize the blocklist */
    tr_buildPath( filename, sizeof( filename ), h->configDir, "blocklists", NULL );
    tr_mkdirp( filename, 0777 );
    h->isBlocklistEnabled = isBlocklistEnabled;
    loadBlocklists( h );

    tr_statsInit( h );

    h->web = tr_webInit( h );
    h->rpcServer = tr_rpcInit( h, rpcIsEnabled, rpcPort, rpcACL,
                                  rpcAuthIsEnabled, rpcUsername, rpcPassword );

    metainfoLookupRescan( h );

    return h;
}

tr_handle *
tr_sessionInit( const char * configDir,
                const char * downloadDir,
                const char * tag )
{
    return tr_sessionInitFull( configDir,
                               downloadDir,
                               tag,
                               TR_DEFAULT_PEX_ENABLED,
                               TR_DEFAULT_PORT_FORWARDING_ENABLED,
                               -1, /* public port */
                               TR_ENCRYPTION_PREFERRED, /* encryption mode */
                               TR_DEFAULT_LAZY_BITFIELD_ENABLED,
                               FALSE, /* use upload speed limit? */ 
                               -1, /* upload speed limit */
                               FALSE, /* use download speed limit? */
                               -1, /* download speed limit */
                               TR_DEFAULT_GLOBAL_PEER_LIMIT,
                               TR_MSG_INF, /* message level */
                               FALSE, /* is message queueing enabled? */
                               FALSE, /* is the blocklist enabled? */
                               TR_DEFAULT_PEER_SOCKET_TOS,
                               TR_DEFAULT_RPC_ENABLED,
                               TR_DEFAULT_RPC_PORT,
                               TR_DEFAULT_RPC_ACL,
                               FALSE,
                               "fnord",
                               "potzrebie",
                               TR_DEFAULT_PROXY_ENABLED,
                               TR_DEFAULT_PROXY,
                               TR_DEFAULT_PROXY_PORT,
                               TR_DEFAULT_PROXY_TYPE,
                               TR_DEFAULT_PROXY_AUTH_ENABLED,
                               TR_DEFAULT_PROXY_USERNAME,
                               TR_DEFAULT_PROXY_PASSWORD );

}

/***
****
***/

void
tr_sessionSetDownloadDir( tr_handle * handle, const char * dir )
{
    if( handle->downloadDir != dir )
    {
        tr_free( handle->downloadDir );
        handle->downloadDir = tr_strdup( dir );
    }
}

const char *
tr_sessionGetDownloadDir( const tr_handle * handle )
{
    return handle->downloadDir;
}

/***
****
***/

void
tr_globalLock( struct tr_handle * handle )
{
    tr_lockLock( handle->lock );
}

void
tr_globalUnlock( struct tr_handle * handle )
{
    tr_lockUnlock( handle->lock );
}

int
tr_globalIsLocked( const struct tr_handle * handle )
{
    return handle && tr_lockHave( handle->lock );
}

/***********************************************************************
 * tr_setBindPort
 ***********************************************************************
 * 
 **********************************************************************/

struct bind_port_data
{
    tr_handle * handle;
    int port;
};

static void
tr_setBindPortImpl( void * vdata )
{
    struct bind_port_data * data = vdata;
    tr_handle * handle = data->handle;
    const int port = data->port;

    handle->isPortSet = 1;
    tr_sharedSetPort( handle->shared, port );

    tr_free( data );
}

void
tr_sessionSetPeerPort( tr_handle * handle, int port )
{
    struct bind_port_data * data = tr_new( struct bind_port_data, 1 );
    data->handle = handle;
    data->port = port;
    tr_runInEventThread( handle, tr_setBindPortImpl, data );
}

int
tr_sessionGetPeerPort( const tr_handle * h )
{
    assert( h );
    return tr_sharedGetPeerPort( h->shared );
}

tr_port_forwarding
tr_sessionGetPortForwarding( const tr_handle * h )
{
    return tr_sharedTraversalStatus( h->shared );
}

/***
****
***/

void
tr_sessionSetSpeedLimitEnabled( tr_handle      * h,
                                tr_direction     direction,
                                int              use_flag )
{
    assert( h );
    assert( direction==TR_UP || direction==TR_DOWN );

    if( direction == TR_UP )
        h->useUploadLimit = use_flag ? 1 : 0;
    else
        h->useDownloadLimit = use_flag ? 1 : 0;
}

int
tr_sessionIsSpeedLimitEnabled( const tr_handle * h, tr_direction direction )
{
       return direction==TR_UP ? h->useUploadLimit : h->useDownloadLimit;
}

void
tr_sessionSetSpeedLimit( tr_handle  * h,
                         tr_direction direction,
                         int          KiB_sec )
{
    if( direction == TR_DOWN )
        h->downloadLimit = KiB_sec;
    else
        h->uploadLimit = KiB_sec;
}

int
tr_sessionGetSpeedLimit( const tr_handle * h, tr_direction direction )
{
    return direction==TR_UP ? h->uploadLimit : h->downloadLimit;
}

/***
****
***/

void
tr_sessionSetPeerLimit( tr_handle * handle UNUSED,
                        uint16_t    maxGlobalPeers )
{
    tr_fdSetPeerLimit( maxGlobalPeers );
}

uint16_t
tr_sessionGetPeerLimit( const tr_handle * handle UNUSED )
{
    return tr_fdGetPeerLimit( );
}

/***
****
***/

void
tr_sessionGetSpeed( const tr_handle  * session,
                    float            * toClient,
                    float            * toPeer )
{
    if( session && toClient )
        *toClient = tr_peerMgrGetRate( session->peerMgr, TR_DOWN );

    if( session && toPeer )
        *toPeer = tr_peerMgrGetRate( session->peerMgr, TR_UP );
}

int
tr_sessionCountTorrents( const tr_handle * h )
{
    return h->torrentCount;
}

static int
compareTorrentByCur( const void * va, const void * vb )
{
    const tr_torrent * a = *(const tr_torrent**)va;
    const tr_torrent * b = *(const tr_torrent**)vb;
    const uint64_t aCur = a->downloadedCur + a->uploadedCur;
    const uint64_t bCur = b->downloadedCur + b->uploadedCur;

    if( aCur != bCur )
        return aCur > bCur ? -1 : 1; /* close the biggest torrents first */

    return 0;
}

static void
tr_closeAllConnections( void * vsession )
{
    tr_handle * session = vsession;
    tr_torrent * tor;
    int i, n;
    tr_torrent ** torrents;

    tr_statsClose( session );
    tr_sharedShuttingDown( session->shared );
    tr_rpcClose( &session->rpcServer );

    /* close the torrents.  get the most active ones first so that
     * if we can't get them all closed in a reasonable amount of time,
     * at least we get the most important ones first. */
    tor = NULL;
    n = session->torrentCount;
    torrents = tr_new( tr_torrent*, session->torrentCount );
    for( i=0; i<n; ++i )
        torrents[i] = tor = tr_torrentNext( session, tor );
    qsort( torrents, n, sizeof(tr_torrent*), compareTorrentByCur );
    for( i=0; i<n; ++i )
        tr_torrentFree( torrents[i] );
    tr_free( torrents );

    tr_peerMgrFree( session->peerMgr );

    tr_trackerSessionClose( session );
    tr_list_free( &session->blocklists, (TrListForeachFunc)_tr_blocklistFree );
    tr_webClose( &session->web );
    
    session->isClosed = TRUE;
}

static int
deadlineReached( const uint64_t deadline )
{
    return tr_date( ) >= deadline;
}

#define SHUTDOWN_MAX_SECONDS 30

#define dbgmsg(fmt...) tr_deepLog( __FILE__, __LINE__, NULL, ##fmt )

void
tr_sessionClose( tr_handle * session )
{
    int i;
    const int maxwait_msec = SHUTDOWN_MAX_SECONDS * 1000;
    const uint64_t deadline = tr_date( ) + maxwait_msec;

    dbgmsg( "shutting down transmission session %p", session );

    /* close the session */
    tr_runInEventThread( session, tr_closeAllConnections, session );
    while( !session->isClosed && !deadlineReached( deadline ) ) {
        dbgmsg( "waiting for the shutdown commands to run in the main thread" );
        tr_wait( 100 );
    }

    /* "shared" and "tracker" have live sockets,
     * so we need to keep the transmission thread alive
     * for a bit while they tell the router & tracker
     * that we're closing now */
    while( ( session->shared || session->tracker ) && !deadlineReached( deadline ) ) {
        dbgmsg( "waiting on port unmap (%p) or tracker (%p)",
                session->shared, session->tracker );
        tr_wait( 100 );
    }
    tr_fdClose( );

    /* close the libtransmission thread */
    tr_eventClose( session );
    while( session->events && !deadlineReached( deadline ) ) {
        dbgmsg( "waiting for the libevent thread to shutdown cleanly" );
        tr_wait( 100 );
    }

    /* free the session memory */
    tr_lockFree( session->lock );
    for( i=0; i<session->metainfoLookupCount; ++i )
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
tr_sessionLoadTorrents ( tr_handle   * h,
                         tr_ctor     * ctor,
                         int         * setmeCount )
{
    int i, n = 0;
    struct stat sb;
    DIR * odir = NULL;
    const char * dirname = tr_getTorrentDir( h );
    tr_torrent ** torrents;
    tr_list *l=NULL, *list=NULL;

    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */

    if( !stat( dirname, &sb )
        && S_ISDIR( sb.st_mode )
        && (( odir = opendir ( dirname ) )) )
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_torrent * tor;
                char filename[MAX_PATH_LENGTH];
                tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );

                tr_ctorSetMetainfoFromFile( ctor, filename );
                tor = tr_torrentNew( h, ctor, NULL );
                if( tor ) {
                    tr_list_append( &list, tor );
                    ++n;
                }
            }
        }
        closedir( odir );
    }

    torrents = tr_new( tr_torrent*, n );
    for( i=0, l=list; l!=NULL; l=l->next )
        torrents[i++] = (tr_torrent*) l->data;
    assert( i==n );

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
tr_sessionSetPexEnabled( tr_handle * handle, int enabled )
{
    handle->isPexEnabled = enabled ? 1 : 0;
}

int
tr_sessionIsPexEnabled( const tr_handle * handle )
{
    return handle->isPexEnabled;
}

/***
****
***/

void
tr_sessionSetLazyBitfieldEnabled( tr_handle * handle, int enabled )
{
    handle->useLazyBitfield = enabled ? 1 : 0;
}

int
tr_sessionIsLazyBitfieldEnabled( const tr_handle * handle )
{
    return handle->useLazyBitfield;
}

/***
****
***/

void
tr_sessionSetPortForwardingEnabled( tr_handle * h, int enable )
{
    tr_globalLock( h );
    tr_sharedTraversalEnable( h->shared, enable );
    tr_globalUnlock( h );
}

int
tr_sessionIsPortForwardingEnabled( const tr_handle * h )
{
    return tr_sharedTraversalIsEnabled( h->shared );
}

/***
****
***/

int
tr_blocklistGetRuleCount( const tr_session * session )
{
    int n = 0;
    tr_list * l;
    for( l=session->blocklists; l; l=l->next )
        n += _tr_blocklistGetRuleCount( l->data );
    return n;
}

int
tr_blocklistIsEnabled( const tr_session * session )
{
    return session->isBlocklistEnabled;
}

void
tr_blocklistSetEnabled( tr_session * session, int isEnabled )
{
    tr_list * l;
    session->isBlocklistEnabled = isEnabled ? 1 : 0;
    for( l=session->blocklists; l; l=l->next )
        _tr_blocklistSetEnabled( l->data, isEnabled );
}

int
tr_blocklistExists( const tr_session * session )
{
    return session->blocklists != NULL;
}

int
tr_blocklistSetContent( tr_session  * session, const char * contentFilename )
{
    tr_list * l;
    tr_blocklist * b;
    const char * defaultName = "level1.bin";

    for( b=NULL, l=session->blocklists; !b && l; l=l->next )
        if( tr_stringEndsWith( _tr_blocklistGetFilename( l->data ), defaultName ) )
            b = l->data;

    if( !b ) {
        char filename[MAX_PATH_LENGTH];
        tr_buildPath( filename, sizeof( filename ), session->configDir, "blocklists", defaultName, NULL );
        b = _tr_blocklistNew( filename, session->isBlocklistEnabled );
        tr_list_append( &session->blocklists, b );
    }

    return _tr_blocklistSetContent( b, contentFilename );
}

int
tr_sessionIsAddressBlocked( const tr_session      * session,
                            const struct in_addr  * addr )
{
    tr_list * l;
    for( l=session->blocklists; l; l=l->next )
        if( _tr_blocklistHasAddress( l->data, addr ) )
            return TRUE;
    return FALSE;
}

/***
****
***/

static int
compareLookupEntries( const void * va, const void * vb )
{
    const struct tr_metainfo_lookup * a = va;
    const struct tr_metainfo_lookup * b = vb;
    return strcmp( a->hashString, b->hashString );
}

static void
metainfoLookupResort( tr_handle * h )
{
    qsort( h->metainfoLookup, 
           h->metainfoLookupCount,
           sizeof( struct tr_metainfo_lookup ),
           compareLookupEntries );
}

static int
compareHashStringToLookupEntry( const void * va, const void * vb )
{
    const char * a = va;
    const struct tr_metainfo_lookup * b = vb;
    return strcmp( a, b->hashString );
}

const char*
tr_sessionFindTorrentFile( const tr_handle  * h,
                           const char       * hashStr )
{
    struct tr_metainfo_lookup * l = bsearch( hashStr,
                                             h->metainfoLookup,
                                             h->metainfoLookupCount,
                                             sizeof( struct tr_metainfo_lookup ),
                                             compareHashStringToLookupEntry );
    return l ? l->filename : NULL;
}

static void
metainfoLookupRescan( tr_handle * h )
{
    int i;
    int n;
    struct stat sb;
    const char * dirname = tr_getTorrentDir( h );
    DIR * odir = NULL;
    tr_ctor * ctor = NULL;
    tr_list * list = NULL;

    /* walk through the directory and find the mappings */
    ctor = tr_ctorNew( h );
    tr_ctorSetSave( ctor, FALSE ); /* since we already have them */
    if( !stat( dirname, &sb ) && S_ISDIR( sb.st_mode ) && (( odir = opendir( dirname ))))
    {
        struct dirent *d;
        for (d = readdir( odir ); d!=NULL; d=readdir( odir ) )
        {
            if( d->d_name && d->d_name[0]!='.' ) /* skip dotfiles, ., and .. */
            {
                tr_info inf;
                char filename[MAX_PATH_LENGTH];
                tr_buildPath( filename, sizeof(filename), dirname, d->d_name, NULL );
                tr_ctorSetMetainfoFromFile( ctor, filename );
                if( !tr_torrentParse( h, ctor, &inf ) )
                {
                    tr_list_append( &list, tr_strdup( inf.hashString ) );
                    tr_list_append( &list, tr_strdup( filename ) );
                    tr_metainfoFree( &inf );
                }
            }
        }
        closedir( odir );
    }
    tr_ctorFree( ctor );

    n = tr_list_size( list ) / 2;
    h->metainfoLookup = tr_new0( struct tr_metainfo_lookup, n );
    h->metainfoLookupCount = n;
    for( i=0; i<n; ++i )
    {
        char * hashString = tr_list_pop_front( &list );
        char * filename = tr_list_pop_front( &list );

        memcpy( h->metainfoLookup[i].hashString, hashString, 2*SHA_DIGEST_LENGTH+1 );
        tr_free( hashString );
        h->metainfoLookup[i].filename = filename;
    }

    metainfoLookupResort( h );
    tr_dbg( "Found %d torrents in \"%s\"", n, dirname );
}

void
tr_sessionSetTorrentFile( tr_handle    * h,
                          const char   * hashString,
                          const char   * filename )
{
    struct tr_metainfo_lookup * l = bsearch( hashString,
                                             h->metainfoLookup,
                                             h->metainfoLookupCount,
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
        const int n = h->metainfoLookupCount++;
        struct tr_metainfo_lookup * node;
        h->metainfoLookup = tr_renew( struct tr_metainfo_lookup,
                                      h->metainfoLookup,
                                      h->metainfoLookupCount );
        node = h->metainfoLookup + n;
        memcpy( node->hashString, hashString, 2*SHA_DIGEST_LENGTH+1 );
        node->filename = tr_strdup( filename );
        metainfoLookupResort( h );
    }
}

tr_torrent*
tr_torrentNext( tr_handle * session, tr_torrent * tor )
{
    return tor ? tor->next : session->torrentList;
}

/***
****
***/

void
tr_sessionSetRPCEnabled( tr_session * session, int isEnabled )
{
    tr_rpcSetEnabled( session->rpcServer, isEnabled );
}
int
tr_sessionIsRPCEnabled( const tr_session * session )
{
    return tr_rpcIsEnabled( session->rpcServer );
}
void
tr_sessionSetRPCPort( tr_session * session, int port )
{
    tr_rpcSetPort( session->rpcServer, port );
}
int
tr_sessionGetRPCPort( const tr_session * session )
{
    return tr_rpcGetPort( session->rpcServer );
}
void
tr_sessionSetRPCCallback( tr_session    * session,
                          tr_rpc_func    func,
                          void         * user_data )
{
    session->rpc_func = func;
    session->rpc_func_user_data = user_data;
}
int
tr_sessionTestRPCACL( const tr_session  * session,
                      const char       * acl,
                      char            ** allocme_errmsg )
{
    return tr_rpcTestACL( session->rpcServer, acl, allocme_errmsg );
}
int
tr_sessionSetRPCACL( tr_session    * session,
                     const char   * acl,
                     char        ** allocme_errmsg )
{
    return tr_rpcSetACL( session->rpcServer, acl, allocme_errmsg );
}
char*
tr_sessionGetRPCACL( const tr_session * session )
{
    return tr_rpcGetACL( session->rpcServer );
}
void
tr_sessionSetRPCPassword( tr_session * session, const char * password )
{
    tr_rpcSetPassword( session->rpcServer, password );
}
char*
tr_sessionGetRPCPassword( const tr_session * session )
{
    return tr_rpcGetPassword( session->rpcServer );
}
void
tr_sessionSetRPCUsername( tr_session * session, const char * username )
{
    tr_rpcSetUsername( session->rpcServer, username );
}
char*
tr_sessionGetRPCUsername( const tr_session * session )
{
    return tr_rpcGetUsername( session->rpcServer );
}
void
tr_sessionSetRPCPasswordEnabled( tr_session * session, int isEnabled )
{
    tr_rpcSetPasswordEnabled( session->rpcServer, isEnabled );
}
int
tr_sessionIsRPCPasswordEnabled( const tr_session * session )
{
    return tr_rpcIsPasswordEnabled( session->rpcServer );
}

/***
****
***/

int
tr_sessionIsProxyEnabled( const tr_session * session )
{
    return session->isProxyEnabled;
}
void
tr_sessionSetProxyEnabled( tr_session * session, int isEnabled )
{
    session->isProxyEnabled = isEnabled ? 1 : 0;
}
tr_proxy_type
tr_sessionGetProxyType( const tr_session * session )
{
    return session->proxyType;
}
void
tr_sessionSetProxyType( tr_session * session, tr_proxy_type type )
{
    session->proxyType = type;
}
const char*
tr_sessionGetProxy( const tr_session * session )
{
    return session->proxy;
}
int
tr_sessionGetProxyPort( const tr_session * session )
{
    return session->proxyPort;
}
void
tr_sessionSetProxy( tr_session * session, const char * proxy )
{
    if( proxy != session->proxy )
    {
        tr_free( session->proxy );
        session->proxy = tr_strdup( proxy );
    }
}
void
tr_sessionSetProxyPort( tr_session * session, int port )
{
    session->proxyPort = port;
}
int
tr_sessionIsProxyAuthEnabled( const tr_session * session )
{
    return session->isProxyAuthEnabled;
}
void
tr_sessionSetProxyAuthEnabled( tr_session * session, int isEnabled )
{
    session->isProxyAuthEnabled = isEnabled ? 1 : 0;
}
const char*
tr_sessionGetProxyUsername( const tr_session * session )
{
    return session->proxyUsername;
}
void
tr_sessionSetProxyUsername( tr_session * session, const char * username )
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
tr_sessionSetProxyPassword( tr_session * session, const char * password )
{
    if( password != session->proxyPassword )
    {
        tr_free( session->proxyPassword );
        session->proxyPassword = tr_strdup( password );
    }
}
