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
#include <ctype.h> /* isdigit */
#include <stdlib.h> /* strtol */
#include <string.h> /* strcmp */

#include "transmission.h"
#include "bencode.h"
#include "ratecontrol.h"
#include "rpc.h"
#include "json.h"
#include "session.h"
#include "torrent.h"
#include "utils.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

/***
****
***/

static tr_rpc_callback_status
notify( tr_handle * session, int type, tr_torrent * tor )
{
    tr_rpc_callback_status status = 0;

    if( session->rpc_func )
        status = session->rpc_func( session, type, tor, session->rpc_func_user_data );

    return status;
}

/***
****
***/

static tr_torrent **
getTorrents( tr_handle * handle, tr_benc * args, int * setmeCount )
{
    int torrentCount = 0;
    int64_t id;
    tr_torrent ** torrents = NULL;
    tr_benc * ids;

    if( tr_bencDictFindList( args, "ids", &ids ) )
    {
        int i;
        const int n = tr_bencListSize( ids );

        torrents = tr_new0( tr_torrent*, n );

        for( i=0; i<n; ++i )
        {
            tr_torrent * tor = NULL;
            tr_benc * node = tr_bencListChild( ids, i );
            int64_t id;
            const char * str;
            if( tr_bencGetInt( node, &id ) )
                tor = tr_torrentFindFromId( handle, id );
            else if( tr_bencGetStr( node, &str ) )
                tor = tr_torrentFindFromHashString( handle, str );
            if( tor )
                torrents[torrentCount++] = tor;
        }
    }
    else if( tr_bencDictFindInt( args, "ids", &id ) 
          || tr_bencDictFindInt( args, "id", &id ) )
    {
        tr_torrent * tor;
        torrents = tr_new0( tr_torrent*, 1 );
        if(( tor = tr_torrentFindFromId( handle, id )))
            torrents[torrentCount++] = tor;
    }
    else /* all of them */
    {
        tr_torrent * tor = NULL;
        const int n = tr_sessionCountTorrents( handle );
        torrents = tr_new0( tr_torrent*, n );
        while(( tor = tr_torrentNext( handle, tor )))
            torrents[torrentCount++] = tor;
    }

    *setmeCount = torrentCount;
    return torrents;
}

static const char*
torrentStart( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentStart( tor );
        notify( h, TR_RPC_TORRENT_STARTED, tor );
    }
    tr_free( torrents );
    return NULL;
}

static const char*
torrentStop( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentStop( tor );
        notify( h, TR_RPC_TORRENT_STOPPED, tor );
    }
    tr_free( torrents );
    return NULL;
}

static const char*
torrentRemove( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        const tr_rpc_callback_status status = notify( h, TR_RPC_TORRENT_REMOVING, tor );
        if( ! ( status & TR_RPC_NOREMOVE ) )
            tr_torrentRemove( tor );
    }
    tr_free( torrents );
    return NULL;
}

static const char*
torrentVerify( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_torrentVerify( tor );
        notify( h, TR_RPC_TORRENT_CHANGED, tor );
    }
    tr_free( torrents );
    return NULL;
}

/***
****
***/

static void
addFiles( const tr_torrent * tor, tr_benc * list )
{
    tr_file_index_t i;
    tr_file_index_t n;
    const tr_info * info = tr_torrentInfo( tor );
    tr_file_stat * files = tr_torrentFiles( tor, &n );

    for( i=0; i<info->fileCount; ++i )
    {
        const tr_file * file = &info->files[i];
        tr_benc * d = tr_bencListAddDict( list, 3 );
        tr_bencDictAddInt( d, "bytesCompleted", files[i].bytesCompleted );
        tr_bencDictAddInt( d, "length", file->length );
        tr_bencDictAddStr( d, "name", file->name );
    }

    tr_torrentFilesFree( files, n );
}

static void
addWebseeds( const tr_info * info, tr_benc * webseeds )
{
    int i;
    for( i=0; i<info->webseedCount; ++i )
        tr_bencListAddStr( webseeds, info->webseeds[i] );
}

static void
addTrackers( const tr_info * info, tr_benc * trackers )
{
    int i;
    for( i=0; i<info->trackerCount; ++i )
    {
        const tr_tracker_info * t = &info->trackers[i];
        tr_benc * d = tr_bencListAddDict( trackers, 3 );
        tr_bencDictAddStr( d, "announce", t->announce );
        tr_bencDictAddStr( d, "scrape", t->scrape );
        tr_bencDictAddInt( d, "tier", t->tier );
    }
}

static void
addField( const tr_torrent * tor, tr_benc * d, const char * key )
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
    else if( !strcmp( key, "downloadedEver" ) )
        tr_bencDictAddInt( d, key, st->downloadedEver );
    else if( !strcmp( key, "downloadLimitMode" ) )
        tr_bencDictAddInt( d, key, tr_torrentGetSpeedMode( tor, TR_DOWN ) );
    else if( !strcmp( key, "downloadLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
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
    else if( !strcmp( key, "peersConnected" ) )
        tr_bencDictAddInt( d, key, st->peersConnected );
    else if( !strcmp( key, "peersFrom" ) ) {
        tr_benc * tmp = tr_bencDictAddDict( d, key, 4 );
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
    else if( !strcmp( key, "pieceCount" ) )
        tr_bencDictAddInt( d, key, inf->pieceCount );
    else if( !strcmp( key, "pieceSize" ) )
        tr_bencDictAddInt( d, key, inf->pieceSize );
    else if( !strcmp( key, "priorities" ) ) {
        tr_file_index_t i;
        tr_benc * p = tr_bencDictAddList( d, key, inf->fileCount );
        for( i=0; i<inf->fileCount; ++i )
            tr_bencListAddInt( p, inf->files[i].priority );
    }
    else if( !strcmp( key, "rateDownload" ) )
        tr_bencDictAddInt( d, key, (int)(st->rateDownload*1024) );
    else if( !strcmp( key, "rateUpload" ) )
        tr_bencDictAddInt( d, key, (int)(st->rateUpload*1024) );
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
        tr_bencDictAddInt( d, key, st->status );
    else if( !strcmp( key, "swarmSpeed" ) )
        tr_bencDictAddInt( d, key, (int)(st->swarmSpeed*1024) );
    else if( !strcmp( key, "timesCompleted" ) )
        tr_bencDictAddInt( d, key, st->timesCompleted );
    else if( !strcmp( key, "trackers" ) )
        addTrackers( inf, tr_bencDictAddList( d, key, inf->trackerCount ) );
    else if( !strcmp( key, "totalSize" ) )
        tr_bencDictAddInt( d, key, inf->totalSize );
    else if( !strcmp( key, "uploadedEver" ) )
        tr_bencDictAddInt( d, key, st->uploadedEver );
    else if( !strcmp( key, "uploadLimitMode" ) )
        tr_bencDictAddInt( d, key,   tr_torrentGetSpeedMode( tor, TR_UP ) );
    else if( !strcmp( key, "uploadLimit" ) )
        tr_bencDictAddInt( d, key, tr_torrentGetSpeedLimit( tor, TR_UP ) );
    else if( !strcmp( key, "uploadRatio" ) )
        tr_bencDictAddDouble( d, key, tr_getRatio( st->uploadedEver, st->downloadedEver ) );
    else if( !strcmp( key, "wanted" ) ) {
        tr_file_index_t i;
        tr_benc * w = tr_bencDictAddList( d, key, inf->fileCount );
        for( i=0; i<inf->fileCount; ++i )
            tr_bencListAddInt( w, inf->files[i].dnd ? 0 : 1 );
    }
    else if( !strcmp( key, "webseeds" ) )
        addWebseeds( inf, tr_bencDictAddList( d, key, inf->trackerCount ) );
    else if( !strcmp( key, "webseedsSendingToUs" ) )
        tr_bencDictAddInt( d, key, st->webseedsSendingToUs );
}

static void
addInfo( const tr_torrent * tor, tr_benc * d, tr_benc * fields )
{
    int i;
    const int n = tr_bencListSize( fields );
    const char * str;

    tr_bencInitDict( d, n );

    for( i=0; i<n; ++i )
        if( tr_bencGetStr( tr_bencListChild( fields, i ), &str ) )
            addField( tor, d, str );
}

static const char*
torrentGet( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrents", torrentCount );
    tr_benc * fields;
    char *msg = NULL;

    if( !tr_bencDictFindList( args_in, "fields", &fields ) )
        msg = "no fields specified";
    else for( i=0; i<torrentCount; ++i )
        addInfo( torrents[i], tr_bencListAdd( list ), fields );

    tr_free( torrents );
    return msg;
}

/***
****
***/

static void
setFilePriorities( tr_torrent * tor, int priority, tr_benc * list )
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_bencListSize( list );
    tr_file_index_t * files = tr_new0( tr_file_index_t, tor->info.fileCount );

    if( n )
    {
        for( i=0; i<n; ++i )
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) )
                if( 0<=tmp && tmp<tor->info.fileCount )
                    files[fileCount++] = tmp;
    }
    else // if empty set, apply to all
    {
        tr_file_index_t t;
        for( t=0; t<tor->info.fileCount; ++t )
            files[fileCount++] = t;
    }

    if( fileCount )
        tr_torrentSetFilePriorities( tor, files, fileCount, priority );

    tr_free( files );
}

static void
setFileDLs( tr_torrent * tor, int do_download, tr_benc * list )
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_bencListSize( list );
    tr_file_index_t * files = tr_new0( tr_file_index_t, tor->info.fileCount );

    if( n ) // if argument list, process them
    {
        for( i=0; i<n; ++i )
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) )
                if( 0<=tmp && tmp<tor->info.fileCount )
                    files[fileCount++] = tmp;
    }
    else // if empty set, apply to all
    {
        tr_file_index_t t;
        for( t=0; t<tor->info.fileCount; ++t )
            files[fileCount++] = t;
    }

    if( fileCount )
        tr_torrentSetFileDLs( tor, files, fileCount, do_download );

    tr_free( files );
}

static const char*
torrentSet( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        int64_t tmp;
        tr_benc * files;
        tr_torrent * tor = torrents[i];

        if( tr_bencDictFindList( args_in, "files-unwanted", &files ) )
            setFileDLs( tor, FALSE, files );
        if( tr_bencDictFindList( args_in, "files-wanted", &files ) )
            setFileDLs( tor, TRUE, files );
        if( tr_bencDictFindInt( args_in, "peer-limit", &tmp ) )
            tr_torrentSetPeerLimit( tor, tmp );
        if( tr_bencDictFindList( args_in, "priority-high", &files ) )
            setFilePriorities( tor, TR_PRI_HIGH, files );
        if( tr_bencDictFindList( args_in, "priority-low", &files ) )
            setFilePriorities( tor, TR_PRI_LOW, files );
        if( tr_bencDictFindList( args_in, "priority-normal", &files ) )
            setFilePriorities( tor, TR_PRI_NORMAL, files );
        if( tr_bencDictFindInt( args_in, "speed-limit-down", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_DOWN, tmp );
        if( tr_bencDictFindInt( args_in, "speed-limit-down-enabled", &tmp ) )
            tr_torrentSetSpeedMode( tor, TR_DOWN, tmp ? TR_SPEEDLIMIT_SINGLE
                                                      : TR_SPEEDLIMIT_GLOBAL );
        if( tr_bencDictFindInt( args_in, "speed-limit-up", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_UP, tmp );
        if( tr_bencDictFindInt( args_in, "speed-limit-up-enabled", &tmp ) )
            tr_torrentSetSpeedMode( tor, TR_UP, tmp ? TR_SPEEDLIMIT_SINGLE
                                                    : TR_SPEEDLIMIT_GLOBAL );

        notify( h, TR_RPC_TORRENT_CHANGED, tor );
    }

    tr_free( torrents );
    return NULL;
}

/***
****
***/

static const char*
torrentAdd( tr_handle * h, tr_benc * args_in, tr_benc * args_out )
{
    const char * filename = NULL;
    const char * metainfo_base64 = NULL;
    tr_bencDictFindStr( args_in, "filename", &filename );
    tr_bencDictFindStr( args_in, "metainfo", &metainfo_base64 );
    if( !filename && !metainfo_base64 )
        return "no filename or metainfo specified";
    else
    {
        int64_t i;
        int err = 0;
        const char * str;
        tr_ctor * ctor;
        tr_torrent * tor;

        ctor = tr_ctorNew( h );

        /* set the metainfo */
        if( filename )
            tr_ctorSetMetainfoFromFile( ctor, filename );
        else {
            int len;
            char * metainfo = tr_base64_decode( metainfo_base64, -1,  &len );
            tr_ctorSetMetainfo( ctor, (uint8_t*)metainfo, len );
            tr_free( metainfo );
        }

        /* set the optional arguments */
        if( tr_bencDictFindStr( args_in, "download-dir", &str ) )
            tr_ctorSetDownloadDir( ctor, TR_FORCE, str );
        if( tr_bencDictFindInt( args_in, "paused", &i ) )
            tr_ctorSetPaused( ctor, TR_FORCE, i );
        if( tr_bencDictFindInt( args_in, "peer-limit", &i ) )
            tr_ctorSetPeerLimit( ctor, TR_FORCE, i );

        tor = tr_torrentNew( h, ctor, &err );
        tr_ctorFree( ctor );

        if( tor ) {
            tr_benc fields;
            tr_bencInitList( &fields, 3 );
            tr_bencListAddStr( &fields, "id" );
            tr_bencListAddStr( &fields, "name" );
            tr_bencListAddStr( &fields, "hashString" );
            addInfo( tor, tr_bencDictAdd( args_out, "torrent-added" ), &fields );
            notify( h, TR_RPC_TORRENT_ADDED, tor );
            tr_bencFree( &fields );
        } else if( err == TR_EDUPLICATE ) {
            return "duplicate torrent";
        } else if( err == TR_EINVALID ) {
            return "invalid or corrupt torrent file";
        }
    }

    return NULL;
}

/***
****
***/

static const char*
sessionSet( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int64_t i;
    const char * str;

    if( tr_bencDictFindStr( args_in, "download-dir", &str ) )
        tr_sessionSetDownloadDir( h, str );
    if( tr_bencDictFindInt( args_in, "peer-limit", &i ) )
        tr_sessionSetPeerLimit( h, i );
    if( tr_bencDictFindInt( args_in, "pex-allowed", &i ) )
        tr_sessionSetPexEnabled( h, i );
    if( tr_bencDictFindInt( args_in, "port", &i ) )
        tr_sessionSetPeerPort( h, i );
    if( tr_bencDictFindInt( args_in, "port-forwarding-enabled", &i ) )
        tr_sessionSetPortForwardingEnabled( h, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-down", &i ) )
        tr_sessionSetSpeedLimit( h, TR_DOWN, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-down-enabled", &i ) )
        tr_sessionSetSpeedLimitEnabled( h, TR_DOWN, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-up", &i ) )
        tr_sessionSetSpeedLimit( h, TR_UP, i );
    if( tr_bencDictFindInt( args_in, "speed-limit-up-enabled", &i ) )
        tr_sessionSetSpeedLimitEnabled( h, TR_UP, i );
    if( tr_bencDictFindStr( args_in, "encryption", &str ) ) {
        if( !strcmp( str, "required" ) )
            tr_sessionSetEncryption( h, TR_ENCRYPTION_REQUIRED );
        else if( !strcmp( str, "tolerated" ) )
            tr_sessionSetEncryption( h, TR_CLEAR_PREFERRED );
        else
            tr_sessionSetEncryption( h, TR_ENCRYPTION_PREFERRED );
    }

    notify( h, TR_RPC_SESSION_CHANGED, NULL );

    return NULL;
}

static const char*
sessionStats( tr_handle * h, tr_benc * args_in UNUSED, tr_benc * args_out )
{
    tr_benc * d = tr_bencDictAddDict( args_out, "session-stats", 10 );
    tr_torrent * tor = NULL;
    float up, down;
    int running = 0;
    int total = 0;

    tr_sessionGetSpeed( h, &down, &up );
    while(( tor = tr_torrentNext( h, tor ))) {
        ++total;
        if( tor->isRunning )
            ++running;
    }
        
    tr_bencDictAddInt( d, "activeTorrentCount", running );
    tr_bencDictAddInt( d, "downloadSpeed", (int)(down*1024) );
    tr_bencDictAddInt( d, "pausedTorrentCount", total-running );
    tr_bencDictAddInt( d, "torrentCount", total );
    tr_bencDictAddInt( d, "uploadSpeed", (int)(up*1024) );
    return NULL;
}

static const char*
sessionGet( tr_handle * h, tr_benc * args_in UNUSED, tr_benc * args_out )
{
    const char * str;
    tr_benc * d = args_out;

    tr_bencDictAddStr( d, "download-dir",
                          tr_sessionGetDownloadDir( h ) );
    tr_bencDictAddInt( d, "peer-limit",
                          tr_sessionGetPeerLimit( h ) );
    tr_bencDictAddInt( d, "pex-allowed",
                          tr_sessionIsPexEnabled( h ) );
    tr_bencDictAddInt( d, "port",
                          tr_sessionGetPeerPort( h ) );
    tr_bencDictAddInt( d, "port-forwarding-enabled",
                          tr_sessionIsPortForwardingEnabled( h ) );
    tr_bencDictAddInt( d, "speed-limit-up",
                          tr_sessionGetSpeedLimit( h, TR_UP ) * 1024 );
    tr_bencDictAddInt( d, "speed-limit-up-enabled",
                          tr_sessionIsSpeedLimitEnabled( h, TR_UP ) );
    tr_bencDictAddInt( d, "speed-limit-down",
                          tr_sessionGetSpeedLimit( h, TR_DOWN ) * 1024 );
    tr_bencDictAddInt( d, "speed-limit-down-enabled",
                          tr_sessionIsSpeedLimitEnabled( h, TR_DOWN ) );
    switch( tr_sessionGetEncryption( h ) ) {
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

typedef const char* (handler)( tr_handle*, tr_benc*, tr_benc* );

static struct method {
    const char * name;
    handler * func;
} methods[] = { 
    { "session-get", sessionGet },
    { "session-set", sessionSet },
    { "session-stats", sessionStats },
    { "torrent-add", torrentAdd },
    { "torrent-get", torrentGet },
    { "torrent-remove", torrentRemove },
    { "torrent-set", torrentSet },
    { "torrent-start", torrentStart },
    { "torrent-stop", torrentStop },
    { "torrent-verify", torrentVerify }
};

static char*
request_exec( struct tr_handle * handle,
              tr_benc          * request,
              int              * response_len )
{
    int64_t i;
    const char * str;
    char * out;
    tr_benc response;
    tr_benc * args_in = tr_bencDictFind( request, "arguments" );
    tr_benc * args_out = NULL;
    const char * result = NULL;

    /* build the response skeleton */
    tr_bencInitDict( &response, 3 );
    args_out = tr_bencDictAddDict( &response, "arguments", 0 );

    /* parse the request */
    if( !tr_bencDictFindStr( request, "method", &str ) )
        result = "no method name";
    else {
        const int n = TR_N_ELEMENTS( methods );
        for( i=0; i<n; ++i )
            if( !strcmp( str, methods[i].name ) )
                break;
        result = i==n
            ? "method name not recognized"
            : (*methods[i].func)( handle, args_in, args_out );
    }

    /* serialize & return the response */
    if( !result )
         result = "success";
    tr_bencDictAddStr( &response, "result", result );
    if( tr_bencDictFindInt( request, "tag", &i ) )
        tr_bencDictAddInt( &response, "tag", i );
    out = tr_bencSaveAsJSON( &response, response_len );
    tr_bencFree( &response );
    return out;
}

char*
tr_rpc_request_exec_json( struct tr_handle  * handle,
                          const void        * request_json,
                          int                 request_len,
                          int               * response_len )
{
    tr_benc top;
    int have_content;
    char * ret;

    if( request_len < 0 )
        request_len = strlen( request_json );

    have_content = !tr_jsonParse( request_json, request_len, &top, NULL );
    ret = request_exec( handle, have_content ? &top : NULL, response_len );

    if( have_content )
        tr_bencFree( &top );
    return ret;
}

static void
addToken( tr_benc * list, const char * token, size_t len )
{
    char * p;
    const char * end = token + len;
    const long a = strtol( token, &p, 10 );
    if( p == end )
        tr_bencListAddInt( list, a );
    else if( *p == '-' && isdigit(p[1]) ) {
        const long b = strtol( p+1, &p, 10 );
        if( ( p == end ) && ( b > a ) ) {
            long i;
            for( i=a; i<=b; ++i )
                tr_bencListAddInt( list, i );
        }
    }
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
                       const char  * str_in,
                       size_t        len )

{
    char * str = tr_strndup( str_in, len );
    int isNum;
    int isNumList;
    int commaCount;
    const char * walk;

    isNum = 1;
    isNumList = 1;
    commaCount = 0;
    walk = str;
    for( ; *walk && (isNumList || isNum); ++walk ) {
        if( isNumList ) isNumList = *walk=='-' || isdigit(*walk) || *walk==',';
        if( isNum     ) isNum     = *walk=='-' || isdigit(*walk);
        if( *walk == ',' ) ++commaCount;
    }

    if( isNum )
        tr_bencInitInt( setme, strtol( str, NULL, 10 ) );
    else if( !isNumList )
        tr_bencInitStrDup( setme, str );
    else {
        tr_bencInitList( setme, commaCount + 1 );
        walk = str;
        while( *walk ) {
            const char * p = strchr( walk, ',' );
            if( !p )
                p = walk + strlen( walk );
            addToken( setme, walk, p-walk );
            if( *p!=',' )
                break;
            walk = p + 1;
        }
    }

    tr_free( str );
}


char*
tr_rpc_request_exec_uri( struct tr_handle  * handle,
                         const void        * request_uri,
                         int                 request_len,
                         int               * response_len )
{
    char * ret = NULL;
    tr_benc top, * args;
    char * request = tr_strndup( request_uri, request_len );
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
            char * key = tr_strndup( pch, delim-pch );
            int isArg = strcmp( key, "method" ) && strcmp( key, "tag" );
            tr_benc * parent = isArg ? args : &top;
            tr_rpc_parse_list_str( tr_bencDictAdd( parent, key ),
                                   delim+1,
                                   next ? (size_t)(next-(delim+1)) : strlen(delim+1) );
            tr_free( key );
        }
        pch = next ? next+1 : NULL;
    }

    ret = request_exec( handle, &top, response_len );

    /* cleanup */
    tr_bencFree( &top );
    tr_free( request );
    return ret;
}
