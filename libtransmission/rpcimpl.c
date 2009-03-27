/*
 * This file Copyright (C) 2008-2009 Charles Kerr <charles@transmissionbt.com>
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
#include <ctype.h> /* isdigit */
#include <stdlib.h> /* strtol */
#include <string.h> /* strcmp */

#include <event.h> /* evbuffer */

#include "transmission.h"
#include "bencode.h"
#include "rpcimpl.h"
#include "json.h"
#include "session.h"
#include "stats.h"
#include "torrent.h"
#include "completion.h"
#include "utils.h"
#include "web.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

#if 0
#define dbgmsg(fmt, ...) \
    do { \
        fprintf( stderr, "%s:%d"#fmt, __FILE__, __LINE__, __VA_ARGS__ ); \
        fprintf( stderr, "\n" ); \
    } while( 0 )
#else
#define dbgmsg( ... ) \
    do { \
        if( tr_deepLoggingIsActive( ) ) \
            tr_deepLog( __FILE__, __LINE__, "RPC", __VA_ARGS__ ); \
    } while( 0 )
#endif


/***
****
***/

static tr_rpc_callback_status
notify( tr_session * session,
        int          type,
        tr_torrent * tor )
{
    tr_rpc_callback_status status = 0;

    if( session->rpc_func )
        status = session->rpc_func( session, type, tor,
                                    session->rpc_func_user_data );

    return status;
}

/***
****
***/

/* For functions that can't be immediately executed, like torrentAdd,
 * this is the callback data used to pass a response to the caller
 * when the task is complete */
struct tr_rpc_idle_data
{
    tr_session            * session;
    tr_benc               * response;
    tr_benc               * args_out;
    tr_rpc_response_func    callback;
    void                  * callback_user_data;
};

static void
tr_idle_function_done( struct tr_rpc_idle_data * data, const char * result )
{
    struct evbuffer * buf = tr_getBuffer( );

    if( result == NULL )
        result = "success";
    tr_bencDictAddStr( data->response, "result", result );

    tr_bencSaveAsJSON( data->response, buf );
    (*data->callback)( data->session, (const char*)EVBUFFER_DATA(buf),
                       EVBUFFER_LENGTH(buf), data->callback_user_data );

    tr_releaseBuffer( buf );
    tr_bencFree( data->response );
    tr_free( data->response );
    tr_free( data );
}

/***
****
***/

static tr_torrent **
getTorrents( tr_session * session,
             tr_benc    * args,
             int        * setmeCount )
{
    int           torrentCount = 0;
    int64_t       id;
    tr_torrent ** torrents = NULL;
    tr_benc *     ids;
    const char * str;

    if( tr_bencDictFindList( args, "ids", &ids ) )
    {
        int       i;
        const int n = tr_bencListSize( ids );

        torrents = tr_new0( tr_torrent *, n );

        for( i = 0; i < n; ++i )
        {
            tr_torrent * tor = NULL;
            tr_benc *    node = tr_bencListChild( ids, i );
            const char * str;
            if( tr_bencGetInt( node, &id ) )
                tor = tr_torrentFindFromId( session, id );
            else if( tr_bencGetStr( node, &str ) )
                tor = tr_torrentFindFromHashString( session, str );
            if( tor )
                torrents[torrentCount++] = tor;
        }
    }
    else if( tr_bencDictFindInt( args, "ids", &id )
           || tr_bencDictFindInt( args, "id", &id ) )
    {
        tr_torrent * tor;
        torrents = tr_new0( tr_torrent *, 1 );
        if( ( tor = tr_torrentFindFromId( session, id ) ) )
            torrents[torrentCount++] = tor;
    }
    else if( tr_bencDictFindStr( args, "ids", &str ) )
    {
        if( !strcmp( str, "recently-active" ) )
        {
            tr_torrent * tor = NULL;
            const time_t now = time( NULL );
            const time_t window = 60;
            const int n = tr_sessionCountTorrents( session );
            torrents = tr_new0( tr_torrent *, n );
            while( ( tor = tr_torrentNext( session, tor ) ) )
                if( tor->anyDate >= now - window )
                    torrents[torrentCount++] = tor;
        }
    }
    else /* all of them */
    {
        tr_torrent * tor = NULL;
        const int    n = tr_sessionCountTorrents( session );
        torrents = tr_new0( tr_torrent *, n );
        while( ( tor = tr_torrentNext( session, tor ) ) )
            torrents[torrentCount++] = tor;
    }

    *setmeCount = torrentCount;
    return torrents;
}

static const char*
torrentStart( tr_session               * session,
              tr_benc                  * args_in,
              tr_benc                  * args_out UNUSED,
              struct tr_rpc_idle_data  * idle_data )
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i = 0; i < torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentStart( tor );
        notify( session, TR_RPC_TORRENT_STARTED, tor );
    }
    tr_free( torrents );
    return NULL;
}

static const char*
torrentStop( tr_session               * session,
             tr_benc                  * args_in,
             tr_benc                  * args_out UNUSED,
             struct tr_rpc_idle_data  * idle_data )
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i = 0; i < torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentStop( tor );
        notify( session, TR_RPC_TORRENT_STOPPED, tor );
    }
    tr_free( torrents );
    return NULL;
}

static const char*
torrentRemove( tr_session               * session,
               tr_benc                  * args_in,
               tr_benc                  * args_out UNUSED,
               struct tr_rpc_idle_data  * idle_data )
{
    int i;
    int torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        const tr_rpc_callback_status status = notify( session, TR_RPC_TORRENT_REMOVING, tor );
        int64_t deleteFlag;
        if( tr_bencDictFindInt( args_in, "delete-local-data", &deleteFlag ) && deleteFlag )
            tr_torrentDeleteLocalData( tor, NULL );
        if( !( status & TR_RPC_NOREMOVE ) )
            tr_torrentRemove( tor );
    }

    tr_free( torrents );
    return NULL;
}

static const char*
torrentReannounce( tr_session               * session,
                   tr_benc                  * args_in,
                   tr_benc                  * args_out UNUSED,
                   struct tr_rpc_idle_data  * idle_data )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        if( tr_torrentCanManualUpdate( tor ) )
        {
            tr_torrentManualUpdate( tor );
            notify( session, TR_RPC_TORRENT_CHANGED, tor );
        }
    }

    tr_free( torrents );
    return NULL;
}

static const char*
torrentVerify( tr_session               * session,
               tr_benc                  * args_in,
               tr_benc                  * args_out UNUSED,
               struct tr_rpc_idle_data  * idle_data )
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i = 0; i < torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentVerify( tor );
        notify( session, TR_RPC_TORRENT_CHANGED, tor );
    }

    tr_free( torrents );
    return NULL;
}

/***
****
***/

static void
addFiles( const tr_torrent * tor,
          tr_benc *          list )
{
    tr_file_index_t i;
    tr_file_index_t n;
    const tr_info * info = tr_torrentInfo( tor );
    tr_file_stat *  files = tr_torrentFiles( tor, &n );

    for( i = 0; i < info->fileCount; ++i )
    {
        const tr_file * file = &info->files[i];
        tr_benc *       d = tr_bencListAddDict( list, 3 );
        tr_bencDictAddInt( d, "bytesCompleted", files[i].bytesCompleted );
        tr_bencDictAddInt( d, "length", file->length );
        tr_bencDictAddStr( d, "name", file->name );
    }

    tr_torrentFilesFree( files, n );
}

static void
addWebseeds( const tr_info * info,
             tr_benc *       webseeds )
{
    int i;

    for( i = 0; i < info->webseedCount; ++i )
        tr_bencListAddStr( webseeds, info->webseeds[i] );
}

static void
addTrackers( const tr_info * info,
             tr_benc *       trackers )
{
    int i;

    for( i = 0; i < info->trackerCount; ++i )
    {
        const tr_tracker_info * t = &info->trackers[i];
        tr_benc *               d = tr_bencListAddDict( trackers, 3 );
        tr_bencDictAddStr( d, "announce", t->announce );
        tr_bencDictAddStr( d, "scrape", t->scrape );
        tr_bencDictAddInt( d, "tier", t->tier );
    }
}

static void
addPeers( const tr_torrent * tor,
          tr_benc *          list )
{
    int            i;
    int            peerCount;
    tr_peer_stat * peers = tr_torrentPeers( tor, &peerCount );

    tr_bencInitList( list, peerCount );

    for( i = 0; i < peerCount; ++i )
    {
        tr_benc *            d = tr_bencListAddDict( list, 14 );
        const tr_peer_stat * peer = peers + i;
        tr_bencDictAddStr( d, "address", peer->addr );
        tr_bencDictAddStr( d, "clientName", peer->client );
        tr_bencDictAddInt( d, "clientIsChoked", peer->clientIsChoked );
        tr_bencDictAddInt( d, "clientIsInterested",
                           peer->clientIsInterested );
        tr_bencDictAddStr( d, "flagStr", peer->flagStr );
        tr_bencDictAddInt( d, "isDownloadingFrom", peer->isDownloadingFrom );
        tr_bencDictAddInt( d, "isEncrypted", peer->isEncrypted );
        tr_bencDictAddInt( d, "isIncoming", peer->isIncoming );
        tr_bencDictAddInt( d, "isUploadingTo", peer->isUploadingTo );
        tr_bencDictAddInt( d, "peerIsChoked", peer->peerIsChoked );
        tr_bencDictAddInt( d, "peerIsInterested", peer->peerIsInterested );
        tr_bencDictAddInt( d, "port", peer->port );
        tr_bencDictAddDouble( d, "progress", peer->progress );
        tr_bencDictAddInt( d, "rateToClient",
                          (int)( peer->rateToClient * 1024.0 ) );
        tr_bencDictAddInt( d, "rateToPeer",
                          (int)( peer->rateToPeer * 1024.0 ) );
    }

    tr_torrentPeersFree( peers, peerCount );
}

static void
addField( const tr_torrent * tor,
          tr_benc *          d,
          const char *       key )
{
    const tr_info * inf = tr_torrentInfo( tor );
    const tr_stat * st = tr_torrentStat( (tr_torrent*)tor );

    if( !strcmp( key, "activityDate" ) )
        tr_bencDictAddInt( d, key, st->activityDate );
    else if( !strcmp( key, "addedDate" ) )
        tr_bencDictAddInt( d, key, st->addedDate );
    else if( !strcmp( key, "announceResponse" ) )
        tr_bencDictAddStr( d, key, st->announceResponse );
    else if( !strcmp( key, "announceURL" ) )
        tr_bencDictAddStr( d, key, st->announceURL );
    else if( !strcmp( key, "comment" ) )
        tr_bencDictAddStr( d, key, inf->comment ? inf->comment : "" );
    else if( !strcmp( key, "corruptEver" ) )
        tr_bencDictAddInt( d, key, st->corruptEver );
    else if( !strcmp( key, "creator" ) )
        tr_bencDictAddStr( d, key, inf->creator ? inf->creator : "" );
    else if( !strcmp( key, "dateCreated" ) )
        tr_bencDictAddInt( d, key, inf->dateCreated );
    else if( !strcmp( key, "desiredAvailable" ) )
        tr_bencDictAddInt( d, key, st->desiredAvailable );
    else if( !strcmp( key, "doneDate" ) )
        tr_bencDictAddInt( d, key, st->doneDate );
    else if( !strcmp( key, "downloadDir" ) )
        tr_bencDictAddStr( d, key, tr_torrentGetDownloadDir( tor ) );
    else if( !strcmp( key, "downloadedEver" ) )
        tr_bencDictAddInt( d, key, st->downloadedEver );
    else if( !strcmp( key, "downloaders" ) )
        tr_bencDictAddInt( d, key, st->downloaders );
    else if( !strcmp( key, "downloadLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
    else if( !strcmp( key, "downloadHonorsLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentIsUsingSpeedLimit( tor, TR_DOWN ) );
    else if( !strcmp( key, "downloadHonorsGlobalLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentIsUsingGlobalSpeedLimit( tor, TR_DOWN ) );
    else if( !strcmp( key, "error" ) )
        tr_bencDictAddInt( d, key, st->error );
    else if( !strcmp( key, "errorString" ) )
        tr_bencDictAddStr( d, key, st->errorString );
    else if( !strcmp( key, "eta" ) )
        tr_bencDictAddInt( d, key, st->eta );
    else if( !strcmp( key, "files" ) )
        addFiles( tor, tr_bencDictAddList( d, key, inf->fileCount ) );
    else if( !strcmp( key, "hashString" ) )
        tr_bencDictAddStr( d, key, tor->info.hashString );
    else if( !strcmp( key, "haveUnchecked" ) )
        tr_bencDictAddInt( d, key, st->haveUnchecked );
    else if( !strcmp( key, "haveValid" ) )
        tr_bencDictAddInt( d, key, st->haveValid );
    else if( !strcmp( key, "id" ) )
        tr_bencDictAddInt( d, key, st->id );
    else if( !strcmp( key, "isPrivate" ) )
        tr_bencDictAddInt( d, key, tr_torrentIsPrivate( tor ) );
    else if( !strcmp( key, "lastAnnounceTime" ) )
        tr_bencDictAddInt( d, key, st->lastAnnounceTime );
    else if( !strcmp( key, "lastScrapeTime" ) )
        tr_bencDictAddInt( d, key, st->lastScrapeTime );
    else if( !strcmp( key, "leechers" ) )
        tr_bencDictAddInt( d, key, st->leechers );
    else if( !strcmp( key, "leftUntilDone" ) )
        tr_bencDictAddInt( d, key, st->leftUntilDone );
    else if( !strcmp( key, "manualAnnounceTime" ) )
        tr_bencDictAddInt( d, key, st->manualAnnounceTime );
    else if( !strcmp( key, "maxConnectedPeers" ) )
        tr_bencDictAddInt( d, key,  tr_torrentGetPeerLimit( tor ) );
    else if( !strcmp( key, "name" ) )
        tr_bencDictAddStr( d, key, inf->name );
    else if( !strcmp( key, "nextAnnounceTime" ) )
        tr_bencDictAddInt( d, key, st->nextAnnounceTime );
    else if( !strcmp( key, "nextScrapeTime" ) )
        tr_bencDictAddInt( d, key, st->nextScrapeTime );
    else if( !strcmp( key, "peers" ) )
        addPeers( tor, tr_bencDictAdd( d, key ) );
    else if( !strcmp( key, "peersConnected" ) )
        tr_bencDictAddInt( d, key, st->peersConnected );
    else if( !strcmp( key, "peersFrom" ) )
    {
        tr_benc *   tmp = tr_bencDictAddDict( d, key, 4 );
        const int * f = st->peersFrom;
        tr_bencDictAddInt( tmp, "fromCache",    f[TR_PEER_FROM_CACHE] );
        tr_bencDictAddInt( tmp, "fromIncoming", f[TR_PEER_FROM_INCOMING] );
        tr_bencDictAddInt( tmp, "fromPex",      f[TR_PEER_FROM_PEX] );
        tr_bencDictAddInt( tmp, "fromTracker",  f[TR_PEER_FROM_TRACKER] );
    }
    else if( !strcmp( key, "peersGettingFromUs" ) )
        tr_bencDictAddInt( d, key, st->peersGettingFromUs );
    else if( !strcmp( key, "peersKnown" ) )
        tr_bencDictAddInt( d, key, st->peersKnown );
    else if( !strcmp( key, "peersSendingToUs" ) )
        tr_bencDictAddInt( d, key, st->peersSendingToUs );
    else if( !strcmp( key, "pieces" ) ) {
        const tr_bitfield * pieces = tr_cpPieceBitfield( &tor->completion );
        char * str = tr_base64_encode( pieces->bits, pieces->byteCount, NULL );
        tr_bencDictAddStr( d, key, str );
        tr_free( str );
    }
    else if( !strcmp( key, "pieceCount" ) )
        tr_bencDictAddInt( d, key, inf->pieceCount );
    else if( !strcmp( key, "pieceSize" ) )
        tr_bencDictAddInt( d, key, inf->pieceSize );
    else if( !strcmp( key, "priorities" ) )
    {
        tr_file_index_t i;
        tr_benc *       p = tr_bencDictAddList( d, key, inf->fileCount );
        for( i = 0; i < inf->fileCount; ++i )
            tr_bencListAddInt( p, inf->files[i].priority );
    }
    else if( !strcmp( key, "rateDownload" ) )
        tr_bencDictAddInt( d, key, (int)( st->pieceDownloadSpeed * 1024 ) );
    else if( !strcmp( key, "rateUpload" ) )
        tr_bencDictAddInt( d, key, (int)( st->pieceUploadSpeed * 1024 ) );
    else if( !strcmp( key, "ratio" ) )
        tr_bencDictAddDouble( d, key, st->ratio );
    else if( !strcmp( key, "recheckProgress" ) )
        tr_bencDictAddDouble( d, key, st->recheckProgress );
    else if( !strcmp( key, "scrapeResponse" ) )
        tr_bencDictAddStr( d, key, st->scrapeResponse );
    else if( !strcmp( key, "scrapeURL" ) )
        tr_bencDictAddStr( d, key, st->scrapeURL );
    else if( !strcmp( key, "seeders" ) )
        tr_bencDictAddInt( d, key, st->seeders );
    else if( !strcmp( key, "sizeWhenDone" ) )
        tr_bencDictAddInt( d, key, st->sizeWhenDone );
    else if( !strcmp( key, "startDate" ) )
        tr_bencDictAddInt( d, key, st->startDate );
    else if( !strcmp( key, "status" ) )
        tr_bencDictAddInt( d, key, st->activity );
    else if( !strcmp( key, "swarmSpeed" ) )
        tr_bencDictAddInt( d, key, (int)( st->swarmSpeed * 1024 ) );
    else if( !strcmp( key, "timesCompleted" ) )
        tr_bencDictAddInt( d, key, st->timesCompleted );
    else if( !strcmp( key, "trackers" ) )
        addTrackers( inf, tr_bencDictAddList( d, key, inf->trackerCount ) );
    else if( !strcmp( key, "totalSize" ) )
        tr_bencDictAddInt( d, key, inf->totalSize );
    else if( !strcmp( key, "uploadedEver" ) )
        tr_bencDictAddInt( d, key, st->uploadedEver );
    else if( !strcmp( key, "uploadRatio" ) )
        tr_bencDictAddDouble( d, key, tr_getRatio( st->uploadedEver, st->downloadedEver ) );
    else if( !strcmp( key, "uploadLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentGetSpeedLimit( tor, TR_UP ) );
    else if( !strcmp( key, "uploadHonorsLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentIsUsingSpeedLimit( tor, TR_UP ) );
    else if( !strcmp( key, "uploadHonorsGlobalLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentIsUsingGlobalSpeedLimit( tor, TR_UP ) );
    else if( !strcmp( key, "wanted" ) )
    {
        tr_file_index_t i;
        tr_benc *       w = tr_bencDictAddList( d, key, inf->fileCount );
        for( i = 0; i < inf->fileCount; ++i )
            tr_bencListAddInt( w, inf->files[i].dnd ? 0 : 1 );
    }
    else if( !strcmp( key, "webseeds" ) )
        addWebseeds( inf, tr_bencDictAddList( d, key, inf->trackerCount ) );
    else if( !strcmp( key, "webseedsSendingToUs" ) )
        tr_bencDictAddInt( d, key, st->webseedsSendingToUs );
}

static void
addInfo( const tr_torrent * tor,
         tr_benc *          d,
         tr_benc *          fields )
{
    int          i;
    const int    n = tr_bencListSize( fields );
    const char * str;

    tr_bencInitDict( d, n );

    for( i = 0; i < n; ++i )
        if( tr_bencGetStr( tr_bencListChild( fields, i ), &str ) )
            addField( tor, d, str );
}

static const char*
torrentGet( tr_session               * session,
            tr_benc                  * args_in,
            tr_benc                  * args_out,
            struct tr_rpc_idle_data  * idle_data )
{
    int           i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );
    tr_benc *     list = tr_bencDictAddList( args_out, "torrents", torrentCount );
    tr_benc *     fields;
    const char *  msg = NULL;

    assert( idle_data == NULL );

    if( !tr_bencDictFindList( args_in, "fields", &fields ) )
        msg = "no fields specified";
    else for( i = 0; i < torrentCount; ++i )
            addInfo( torrents[i], tr_bencListAdd( list ), fields );

    tr_free( torrents );
    return msg;
}

/***
****
***/

static const char*
setFilePriorities( tr_torrent * tor,
                   int          priority,
                   tr_benc *    list )
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_bencListSize( list );
    const char * errmsg = NULL;
    tr_file_index_t * files = tr_new0( tr_file_index_t, tor->info.fileCount );

    if( n )
    {
        for( i = 0; i < n; ++i ) {
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) ) {
                if( 0 <= tmp && tmp < tor->info.fileCount ) {
                    files[fileCount++] = tmp;
                } else {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        tr_file_index_t t;
        for( t = 0; t < tor->info.fileCount; ++t )
            files[fileCount++] = t;
    }

    if( fileCount )
        tr_torrentSetFilePriorities( tor, files, fileCount, priority );

    tr_free( files );
    return errmsg;
}

static const char*
setFileDLs( tr_torrent * tor,
            int          do_download,
            tr_benc *    list )
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_bencListSize( list );
    const char * errmsg = NULL;
    tr_file_index_t * files = tr_new0( tr_file_index_t, tor->info.fileCount );

    if( n ) /* if argument list, process them */
    {
        for( i = 0; i < n; ++i ) {
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) ) {
                if( 0 <= tmp && tmp < tor->info.fileCount ) {
                    files[fileCount++] = tmp;
                } else {
                    errmsg = "file index out of range";
                }
            }
        }
    }
    else /* if empty set, apply to all */
    {
        tr_file_index_t t;
        for( t = 0; t < tor->info.fileCount; ++t )
            files[fileCount++] = t;
    }

    if( fileCount )
        tr_torrentSetFileDLs( tor, files, fileCount, do_download );

    tr_free( files );
    return errmsg;
}

static const char*
torrentSet( tr_session               * session,
            tr_benc                  * args_in,
            tr_benc                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data )
{
    const char * errmsg = NULL;
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( session, args_in, &torrentCount );

    assert( idle_data == NULL );

    for( i = 0; i < torrentCount; ++i )
    {
        int64_t      tmp;
        double       d;
        tr_benc *    files;
        tr_torrent * tor = torrents[i];

        if( tr_bencDictFindList( args_in, "files-unwanted", &files ) )
            setFileDLs( tor, FALSE, files );
        if( tr_bencDictFindList( args_in, "files-wanted", &files ) )
            setFileDLs( tor, TRUE, files );
        if( tr_bencDictFindInt( args_in, "peer-limit", &tmp ) )
            tr_torrentSetPeerLimit( tor, tmp );
        if( !errmsg &&  tr_bencDictFindList( args_in, "priority-high", &files ) )
            errmsg = setFilePriorities( tor, TR_PRI_HIGH, files );
        if( !errmsg && tr_bencDictFindList( args_in, "priority-low", &files ) )
            errmsg = setFilePriorities( tor, TR_PRI_LOW, files );
        if( !errmsg && tr_bencDictFindList( args_in, "priority-normal", &files ) )
            errmsg = setFilePriorities( tor, TR_PRI_NORMAL, files );
        if( tr_bencDictFindInt( args_in, "downloadLimit", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_DOWN, tmp );
        if( tr_bencDictFindInt( args_in, "downloadHonorsLimit", &tmp ) )
            tr_torrentUseSpeedLimit( tor, TR_DOWN, tmp!=0 );
        if( tr_bencDictFindInt( args_in, "downloadHonorsGlobalLimit", &tmp ) )
            tr_torrentUseGlobalSpeedLimit( tor, TR_DOWN, tmp!=0 );
        if( tr_bencDictFindInt( args_in, "uploadLimit", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_UP, tmp );
        if( tr_bencDictFindInt( args_in, "uploadHonorsLimit", &tmp ) )
            tr_torrentUseSpeedLimit( tor, TR_UP, tmp!=0 );
        if( tr_bencDictFindInt( args_in, "uploadHonorsGlobalLimit", &tmp ) )
            tr_torrentUseGlobalSpeedLimit( tor, TR_UP, tmp!=0 );
        if( tr_bencDictFindDouble( args_in, "ratio-limit", &d ) )
            tr_torrentSetRatioLimit( tor, d );
        if( tr_bencDictFindInt( args_in, "ratio-limit-mode", &tmp ) )
            tr_torrentSetRatioMode( tor, tmp );
        notify( session, TR_RPC_TORRENT_CHANGED, tor );
    }

    tr_free( torrents );
    return errmsg;
}

/***
****
***/

static void
addTorrentImpl( struct tr_rpc_idle_data * data, tr_ctor * ctor )
{
    int err = 0;
    const char * result = NULL;
    tr_torrent * tor = tr_torrentNew( data->session, ctor, &err );

    tr_ctorFree( ctor );

    if( tor )
    {
        tr_benc fields;
        tr_bencInitList( &fields, 3 );
        tr_bencListAddStr( &fields, "id" );
        tr_bencListAddStr( &fields, "name" );
        tr_bencListAddStr( &fields, "hashString" );
        addInfo( tor, tr_bencDictAdd( data->args_out, "torrent-added" ), &fields );
        notify( data->session, TR_RPC_TORRENT_ADDED, tor );
        tr_bencFree( &fields );
    }
    else if( err == TR_EDUPLICATE )
    {
        result = "duplicate torrent";
    }
    else if( err == TR_EINVALID )
    {
        result = "invalid or corrupt torrent file";
    }

    tr_idle_function_done( data, result );
}


struct add_torrent_idle_data
{
    struct tr_rpc_idle_data * data;
    tr_ctor * ctor;
};

static void
gotMetadataFromURL( tr_session       * session UNUSED,
                    long               response_code,
                    const void       * response,
                    size_t             response_byte_count,
                    void             * user_data )
{
    struct add_torrent_idle_data * data = user_data;

    dbgmsg( "torrentAdd: HTTP response code was %ld (%s); response length was %zu bytes",
            response_code, tr_webGetResponseStr( response_code ), response_byte_count );

    if( response_code == 200 )
    {
        tr_ctorSetMetainfo( data->ctor, response, response_byte_count );
        addTorrentImpl( data->data, data->ctor );
    }
    else
    {
        char result[1024];
        tr_snprintf( result, sizeof( result ), "http error %ld: %s",
                     response_code, tr_webGetResponseStr( response_code ) );
        tr_idle_function_done( data->data, result );
    }

    tr_free( data );
}

static tr_bool
isCurlURL( const char * filename )
{
    if( filename == NULL )
        return FALSE;

    return ( strstr( filename, "ftp://" ) != NULL )
        || ( strstr( filename, "http://" ) != NULL )
        || ( strstr( filename, "https://" ) != NULL );
}

static const char*
torrentAdd( tr_session               * session,
            tr_benc                  * args_in,
            tr_benc                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data )
{
    const char * filename = NULL;
    const char * metainfo_base64 = NULL;

    assert( idle_data != NULL );

    tr_bencDictFindStr( args_in, "filename", &filename );
    tr_bencDictFindStr( args_in, "metainfo", &metainfo_base64 );
    if( !filename && !metainfo_base64 )
        return "no filename or metainfo specified";
    else
    {
        int64_t      i;
        const char * str;
        tr_ctor    * ctor = tr_ctorNew( session );

        /* set the optional arguments */
        if( tr_bencDictFindStr( args_in, "download-dir", &str ) )
            tr_ctorSetDownloadDir( ctor, TR_FORCE, str );
        if( tr_bencDictFindInt( args_in, "paused", &i ) )
            tr_ctorSetPaused( ctor, TR_FORCE, i );
        if( tr_bencDictFindInt( args_in, "peer-limit", &i ) )
            tr_ctorSetPeerLimit( ctor, TR_FORCE, i );

        dbgmsg( "torrentAdd: filename is \"%s\"", filename );

        if( isCurlURL( filename ) )
        {
            struct add_torrent_idle_data * d = tr_new0( struct add_torrent_idle_data, 1 );
            d->data = idle_data;
            d->ctor = ctor;
            tr_webRun( session, filename, NULL, gotMetadataFromURL, d );
        }
        else
        {
            if( filename != NULL )
                tr_ctorSetMetainfoFromFile( ctor, filename );
            else {
                int len;
                char * metainfo = tr_base64_decode( metainfo_base64, -1,  &len );
                tr_ctorSetMetainfo( ctor, (uint8_t*)metainfo, len );
                tr_free( metainfo );
            }
            addTorrentImpl( idle_data, ctor );
        }

    }

    return NULL;
}

/***
****
***/

static const char*
sessionSet( tr_session               * session,
            tr_benc                  * args_in,
            tr_benc                  * args_out UNUSED,
            struct tr_rpc_idle_data  * idle_data )
{
    int64_t      i;
    double       d;
    const char * str;

    assert( idle_data == NULL );

    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_ALT_LIMIT_ENABLED, &i ) )
        tr_sessionSetAltSpeedLimitEnabled( session, i!=0 );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_ALT_BEGIN, &i ) )
        tr_sessionSetAltSpeedLimitBegin( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_ALT_END, &i ) )
        tr_sessionSetAltSpeedLimitEnd( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_ALT_DL_LIMIT, &i ) )
        tr_sessionSetAltSpeedLimit( session, TR_DOWN, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_ALT_UL_LIMIT, &i ) )
        tr_sessionSetAltSpeedLimit( session, TR_UP, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_BLOCKLIST_ENABLED, &i ) )
        tr_blocklistSetEnabled( session, i!=0 );
    if( tr_bencDictFindStr( args_in, TR_PREFS_KEY_DOWNLOAD_DIR, &str ) )
        tr_sessionSetDownloadDir( session, str );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, &i ) )
        tr_sessionSetPeerLimit( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_PEER_LIMIT_TORRENT, &i ) )
        tr_sessionSetPeerLimitPerTorrent( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_PEX_ENABLED, &i ) )
        tr_sessionSetPexEnabled( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_PEER_PORT, &i ) )
        tr_sessionSetPeerPort( session, i );
    if( tr_bencDictFindInt( args_in, TR_PREFS_KEY_PORT_FORWARDING, &i ) )
        tr_sessionSetPortForwardingEnabled( session, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-down", &i ) )
        tr_sessionSetSpeedLimit( session, TR_DOWN, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-down-enabled", &i ) )
        tr_sessionSetSpeedLimitEnabled( session, TR_DOWN, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-up", &i ) )
        tr_sessionSetSpeedLimit( session, TR_UP, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-up-enabled", &i ) )
        tr_sessionSetSpeedLimitEnabled( session, TR_UP, i );
    if( tr_bencDictFindDouble( args_in, "ratio-limit", &d ) )
        tr_sessionSetRatioLimit( session, d );
    if( tr_bencDictFindInt( args_in, "ratio-limit-enabled", &i ) )
        tr_sessionSetRatioLimited( session, i );
    if( tr_bencDictFindStr( args_in, "encryption", &str ) )
    {
        if( !strcmp( str, "required" ) )
            tr_sessionSetEncryption( session, TR_ENCRYPTION_REQUIRED );
        else if( !strcmp( str, "tolerated" ) )
            tr_sessionSetEncryption( session, TR_CLEAR_PREFERRED );
        else
            tr_sessionSetEncryption( session, TR_ENCRYPTION_PREFERRED );
    }

    notify( session, TR_RPC_SESSION_CHANGED, NULL );

    return NULL;
}

static const char*
sessionStats( tr_session               * session,
              tr_benc                  * args_in UNUSED,
              tr_benc                  * args_out,
              struct tr_rpc_idle_data  * idle_data )
{
    int running = 0;
    int total = 0;
    tr_benc * d;  
    tr_session_stats currentStats = { 0.0f, 0, 0, 0, 0, 0 }; 
    tr_session_stats cumulativeStats = { 0.0f, 0, 0, 0, 0, 0 }; 
    tr_torrent * tor = NULL;

    assert( idle_data == NULL );

    while(( tor = tr_torrentNext( session, tor ))) {
        ++total;
        if( tor->isRunning )
            ++running;
    }

    tr_sessionGetStats( session, &currentStats ); 
    tr_sessionGetCumulativeStats( session, &cumulativeStats ); 

    tr_bencDictAddInt( args_out, "activeTorrentCount", running );
    tr_bencDictAddInt( args_out, "downloadSpeed", (int)( tr_sessionGetPieceSpeed( session, TR_DOWN ) * 1024 ) );
    tr_bencDictAddInt( args_out, "pausedTorrentCount", total - running );
    tr_bencDictAddInt( args_out, "torrentCount", total );
    tr_bencDictAddInt( args_out, "uploadSpeed", (int)( tr_sessionGetPieceSpeed( session, TR_UP ) * 1024 ) );

    d = tr_bencDictAddDict( args_out, "cumulative-stats", 5 );  
    tr_bencDictAddInt( d, "downloadedBytes", cumulativeStats.downloadedBytes ); 
    tr_bencDictAddInt( d, "filesAdded", cumulativeStats.filesAdded ); 
    tr_bencDictAddInt( d, "secondsActive", cumulativeStats.secondsActive ); 
    tr_bencDictAddInt( d, "sessionCount", cumulativeStats.sessionCount ); 
    tr_bencDictAddInt( d, "uploadedBytes", cumulativeStats.uploadedBytes ); 

    d = tr_bencDictAddDict( args_out, "current-stats", 5 );  
    tr_bencDictAddInt( d, "downloadedBytes", currentStats.downloadedBytes ); 
    tr_bencDictAddInt( d, "filesAdded", currentStats.filesAdded ); 
    tr_bencDictAddInt( d, "secondsActive", currentStats.secondsActive ); 
    tr_bencDictAddInt( d, "sessionCount", currentStats.sessionCount ); 
    tr_bencDictAddInt( d, "uploadedBytes", currentStats.uploadedBytes ); 

    return NULL;
}

static const char*
sessionGet( tr_session               * s,
            tr_benc                  * args_in UNUSED,
            tr_benc                  * args_out,
            struct tr_rpc_idle_data  * idle_data )
{
    const char * str;
    tr_benc *    d = args_out;

    assert( idle_data == NULL );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ALT_LIMIT_ENABLED, tr_sessionIsAltSpeedLimitEnabled( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ALT_UL_LIMIT, tr_sessionGetAltSpeedLimit( s, TR_UP ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ALT_DL_LIMIT, tr_sessionGetAltSpeedLimit( s, TR_DOWN ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ALT_BEGIN, tr_sessionGetAltSpeedLimitBegin( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_ALT_END, tr_sessionGetAltSpeedLimitEnd( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_BLOCKLIST_ENABLED, tr_blocklistIsEnabled( s ) );
    tr_bencDictAddInt( d, "blocklist-size", tr_blocklistGetRuleCount( s ) );
    tr_bencDictAddStr( d, TR_PREFS_KEY_DOWNLOAD_DIR, tr_sessionGetDownloadDir( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_GLOBAL, tr_sessionGetPeerLimit( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_LIMIT_TORRENT, tr_sessionGetPeerLimitPerTorrent( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEX_ENABLED, tr_sessionIsPexEnabled( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PEER_PORT, tr_sessionGetPeerPort( s ) );
    tr_bencDictAddInt( d, TR_PREFS_KEY_PORT_FORWARDING, tr_sessionIsPortForwardingEnabled( s ) );
    tr_bencDictAddInt( d, "rpc-version", 4 );
    tr_bencDictAddInt( d, "rpc-version-minimum", 1 );
    tr_bencDictAddInt( d, "speed-limit-up", tr_sessionGetSpeedLimit( s, TR_UP ) );
    tr_bencDictAddInt( d, "speed-limit-up-enabled", tr_sessionIsSpeedLimitEnabled( s, TR_UP ) );
    tr_bencDictAddInt( d, "speed-limit-down", tr_sessionGetSpeedLimit( s, TR_DOWN ) );
    tr_bencDictAddInt( d, "speed-limit-down-enabled", tr_sessionIsSpeedLimitEnabled( s, TR_DOWN ) );
    tr_bencDictAddDouble( d, "ratio-limit", tr_sessionGetRatioLimit( s ) );
    tr_bencDictAddInt( d, "ratio-limit-enabled", tr_sessionIsRatioLimited( s ) );
    tr_bencDictAddStr( d, "version", LONG_VERSION_STRING );
    switch( tr_sessionGetEncryption( s ) ) {
        case TR_CLEAR_PREFERRED: str = "tolerated"; break;
        case TR_ENCRYPTION_REQUIRED: str = "required"; break; 
        default: str = "preferred"; break;
    }
    tr_bencDictAddStr( d, "encryption", str );

    return NULL;
}

/***
****
***/

typedef const char* ( *handler )( tr_session*, tr_benc*, tr_benc*, struct tr_rpc_idle_data * );

static struct method
{
    const char *  name;
    tr_bool       immediate;
    handler       func;
}
methods[] =
{
    { "session-get",        TRUE,  sessionGet          },
    { "session-set",        TRUE,  sessionSet          },
    { "session-stats",      TRUE,  sessionStats        },
    { "torrent-add",        FALSE, torrentAdd          },
    { "torrent-get",        TRUE,  torrentGet          },
    { "torrent-remove",     TRUE,  torrentRemove       },
    { "torrent-set",        TRUE,  torrentSet          },
    { "torrent-start",      TRUE,  torrentStart        },
    { "torrent-stop",       TRUE,  torrentStop         },
    { "torrent-verify",     TRUE,  torrentVerify       },
    { "torrent-reannounce", TRUE,  torrentReannounce   }
};

static void
noop_response_callback( tr_session * session UNUSED,
                        const char * response UNUSED,
                        size_t       response_len UNUSED,
                        void       * user_data UNUSED )
{
}

static void
request_exec( tr_session             * session,
              tr_benc                * request,
              tr_rpc_response_func     callback,
              void                   * callback_user_data )
{
    int i;
    const char * str;
    tr_benc * args_in = tr_bencDictFind( request, "arguments" );
    const char * result = NULL;

    if( callback == NULL )
        callback = noop_response_callback;

    /* parse the request */
    if( !tr_bencDictFindStr( request, "method", &str ) )
        result = "no method name";
    else {
        const int n = TR_N_ELEMENTS( methods );
        for( i = 0; i < n; ++i )
            if( !strcmp( str, methods[i].name ) )
                break;
        if( i ==n )
            result = "method name not recognized";
    }

    /* if we couldn't figure out which method to use, return an error */
    if( result != NULL )
    {
        int64_t tag;
        tr_benc response;
        struct evbuffer * buf = tr_getBuffer( );

        tr_bencInitDict( &response, 3 );
        tr_bencDictAddDict( &response, "arguments", 0 );
        tr_bencDictAddStr( &response, "result", result );
        if( tr_bencDictFindInt( request, "tag", &tag ) )
            tr_bencDictAddInt( &response, "tag", tag );
        tr_bencSaveAsJSON( &response, buf );
        (*callback)( session, (const char*)EVBUFFER_DATA(buf),
                     EVBUFFER_LENGTH( buf ), callback_user_data );

        tr_releaseBuffer( buf );
        tr_bencFree( &response );
    }
    else if( methods[i].immediate )
    {
        int64_t tag;
        tr_benc response;
        tr_benc * args_out;
        struct evbuffer * buf = tr_getBuffer( );

        tr_bencInitDict( &response, 3 );
        args_out = tr_bencDictAddDict( &response, "arguments", 0 );
        result = (*methods[i].func)( session, args_in, args_out, NULL );
        if( result == NULL )
            result = "success";
        tr_bencDictAddStr( &response, "result", result );
        if( tr_bencDictFindInt( request, "tag", &tag ) )
            tr_bencDictAddInt( &response, "tag", tag );
        tr_bencSaveAsJSON( &response, buf );
        (*callback)( session, (const char*)EVBUFFER_DATA(buf),
                     EVBUFFER_LENGTH(buf), callback_user_data );

        tr_releaseBuffer( buf );
        tr_bencFree( &response );
    }
    else
    {
        int64_t tag;
        struct tr_rpc_idle_data * data = tr_new0( struct tr_rpc_idle_data, 1 );
        data->session = session;
        data->response = tr_new0( tr_benc, 1 );
        tr_bencInitDict( data->response, 3 );
        if( tr_bencDictFindInt( request, "tag", &tag ) )
            tr_bencDictAddInt( data->response, "tag", tag );
        data->args_out = tr_bencDictAddDict( data->response, "arguments", 0 );
        data->callback = callback;
        data->callback_user_data = callback_user_data;
        (*methods[i].func)( session, args_in, data->args_out, data );
    }
}

void
tr_rpc_request_exec_json( tr_session            * session,
                          const void            * request_json,
                          int                     request_len,
                          tr_rpc_response_func    callback,
                          void                  * callback_user_data )
{
    tr_benc top;
    int have_content;

    if( request_len < 0 )
        request_len = strlen( request_json );

    have_content = !tr_jsonParse( request_json, request_len, &top, NULL );
    request_exec( session, have_content ? &top : NULL, callback, callback_user_data );

    if( have_content )
        tr_bencFree( &top );
}

/**
 * Munge the URI into a usable form.
 *
 * We have very loose typing on this to make the URIs as simple as possible:
 * - anything not a 'tag' or 'method' is automatically in 'arguments'
 * - values that are all-digits are numbers
 * - values that are all-digits or commas are number lists
 * - all other values are strings
 */
void
tr_rpc_parse_list_str( tr_benc     * setme,
                       const char  * str,
                       int           len )

{
    int valueCount;
    int * values = tr_parseNumberRange( str, len, &valueCount );

    if( valueCount == 0 )
        tr_bencInitStr( setme, str, len );
    else if( valueCount == 1 )
        tr_bencInitInt( setme, values[0] );
    else {
        int i;
        tr_bencInitList( setme, valueCount );
        for( i=0; i<valueCount; ++i )
            tr_bencListAddInt( setme, values[i] );
    }

    tr_free( values );
}

void
tr_rpc_request_exec_uri( tr_session           * session,
                         const void           * request_uri,
                         int                    request_len,
                         tr_rpc_response_func   callback,
                         void                 * callback_user_data )
{
    tr_benc      top, * args;
    char *       request = tr_strndup( request_uri, request_len );
    const char * pch;

    tr_bencInitDict( &top, 3 );
    args = tr_bencDictAddDict( &top, "arguments", 0 );

    pch = strchr( request, '?' );
    if( !pch ) pch = request;
    while( pch )
    {
        const char * delim = strchr( pch, '=' );
        const char * next = strchr( pch, '&' );
        if( delim )
        {
            char *    key = tr_strndup( pch, delim - pch );
            int       isArg = strcmp( key, "method" ) && strcmp( key, "tag" );
            tr_benc * parent = isArg ? args : &top;
            tr_rpc_parse_list_str( tr_bencDictAdd( parent, key ),
                                  delim + 1,
                                  next ? (size_t)(
                                       next -
                                      ( delim + 1 ) ) : strlen( delim + 1 ) );
            tr_free( key );
        }
        pch = next ? next + 1 : NULL;
    }

    request_exec( session, &top, callback, callback_user_data );

    /* cleanup */
    tr_bencFree( &top );
    tr_free( request );
}
