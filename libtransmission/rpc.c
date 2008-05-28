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
#include "rpc.h"
#include "json.h"
#include "session.h"
#include "torrent.h"
#include "utils.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( *ary ) )

/***
****
***/

static void
notify( tr_handle * session, int type, tr_torrent * tor )
{
    if( session->rpc_func != NULL )
        session->rpc_func( session, type, tor, session->rpc_func_user_data );
}

/***
****
***/

static tr_torrent **
getTorrents( tr_handle * handle, tr_benc * args, int * setmeCount )
{
    int64_t id;
    tr_torrent ** torrents = NULL;
    int torrentCount = 0;
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
        notify( h, TR_RPC_TORRENT_REMOVING, tor );
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

static const char*
torrentStatus( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrent-status", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        const tr_stat * st = tr_torrentStat( tor );
        const int * f = st->peersFrom;
        tr_benc * d = tr_bencListAddDict( list, 41 );
        tr_benc * t;

        tr_bencDictAddInt( d, "activityDate", st->activityDate );
        tr_bencDictAddStr( d, "announceResponse", st->announceResponse );
        tr_bencDictAddStr( d, "announceURL", st->announceURL );
        tr_bencDictAddInt( d, "completedFromTracker", st->completedFromTracker );
        tr_bencDictAddInt( d, "corruptEver", st->corruptEver );
        tr_bencDictAddInt( d, "desiredAvailable", st->desiredAvailable );
        tr_bencDictAddInt( d, "downloadedEver", st->downloadedEver );
        tr_bencDictAddInt( d, "error", st->error );
        tr_bencDictAddStr( d, "errorString", st->errorString );
        tr_bencDictAddInt( d, "eta", st->eta );
        tr_bencDictAddStr( d, "hashString", tor->info.hashString );
        tr_bencDictAddInt( d, "haveUnchecked", st->haveUnchecked );
        tr_bencDictAddInt( d, "haveValid", st->haveValid );
        tr_bencDictAddInt( d, "id", st->id );
        tr_bencDictAddInt( d, "lastAnnounceTime", st->lastAnnounceTime );
        tr_bencDictAddInt( d, "lastScrapeTime", st->lastScrapeTime );
        tr_bencDictAddInt( d, "leechers", st->leechers );
        tr_bencDictAddInt( d, "leftUntilDone", st->leftUntilDone );
        tr_bencDictAddInt( d, "manualAnnounceTime", st->manualAnnounceTime );
        tr_bencDictAddInt( d, "nextAnnounceTime", st->nextAnnounceTime );
        tr_bencDictAddInt( d, "nextScrapeTime", st->nextScrapeTime );
        tr_bencDictAddInt( d, "peersConnected", st->peersConnected );
        t = tr_bencDictAddDict( d, "peersFrom", 4 );
            tr_bencDictAddInt( t, "cache",    f[TR_PEER_FROM_CACHE] );
            tr_bencDictAddInt( t, "incoming", f[TR_PEER_FROM_INCOMING] );
            tr_bencDictAddInt( t, "pex",      f[TR_PEER_FROM_PEX] );
            tr_bencDictAddInt( t, "tracker",  f[TR_PEER_FROM_TRACKER] );
        tr_bencDictAddInt( d, "peersGettingFromUs", st->peersGettingFromUs );
        tr_bencDictAddInt( d, "peersKnown", st->peersKnown );
        tr_bencDictAddInt( d, "peersSendingToUs", st->peersSendingToUs );
        tr_bencDictAddDouble( d, "percentComplete", st->percentComplete );
        tr_bencDictAddDouble( d, "percentDone", st->percentDone );
        tr_bencDictAddDouble( d, "rateDownload", st->rateDownload );
        tr_bencDictAddDouble( d, "rateUpload", st->rateUpload );
        tr_bencDictAddDouble( d, "ratio", st->ratio );
        tr_bencDictAddDouble( d, "recheckProgress", st->recheckProgress );
        tr_bencDictAddStr( d, "scrapeResponse", st->scrapeResponse );
        tr_bencDictAddStr( d, "scrapeURL", st->scrapeURL );
        tr_bencDictAddInt( d, "seeders", st->seeders );
        tr_bencDictAddInt( d, "sizeWhenDone", st->sizeWhenDone );
        tr_bencDictAddInt( d, "startDate", st->startDate );
        tr_bencDictAddInt( d, "status", st->status );
        tr_bencDictAddDouble( d, "swarmSpeed", st->swarmSpeed );
        tr_bencDictAddInt( d, "uploadedEver", st->uploadedEver );
    }

    tr_free( torrents );
    return NULL;
}

/**
***
**/

static void
addFiles( const tr_info * info, tr_benc * files )
{
    tr_file_index_t i;
    for( i=0; i<info->fileCount; ++i )
    {
        const tr_file * file = &info->files[i];
        tr_benc * d = tr_bencListAddDict( files, 2 );
        tr_bencDictAddInt( d, "length", file->length );
        tr_bencDictAddStr( d, "name", file->name );
    }
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
addInfo( const tr_torrent * tor, tr_benc * d )
{
    const tr_info * inf = tr_torrentInfo( tor );
    tr_bencInitDict( d, 14 ); 
    tr_bencDictAddStr( d, "comment", inf->comment ? inf->comment : "" );
    tr_bencDictAddStr( d, "creator", inf->creator ? inf->creator : "" );
    tr_bencDictAddInt( d, "dateCreated", inf->dateCreated );
    addFiles( inf, tr_bencDictAddList( d, "files", inf->fileCount ) );
    tr_bencDictAddStr( d, "hashString", inf->hashString );
    tr_bencDictAddInt( d, "id", tr_torrentId( tor ) );
    tr_bencDictAddInt( d, "isMultifile", inf->isMultifile );
    tr_bencDictAddInt( d, "isPrivate", inf->isPrivate );
    tr_bencDictAddStr( d, "name", inf->name );
    tr_bencDictAddInt( d, "pieceCount", inf->pieceCount );
    tr_bencDictAddInt( d, "pieceSize", inf->pieceSize );
    tr_bencDictAddStr( d, "torrent", inf->torrent );
    tr_bencDictAddInt( d, "totalSize", inf->totalSize );
    addTrackers( inf, tr_bencDictAddList( d, "trackers", inf->trackerCount ) );
}

static const char*
torrentInfo( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrent-info", torrentCount );

    for( i=0; i<torrentCount; ++i )
        addInfo( torrents[i], tr_bencListAdd( list ) );

    tr_free( torrents );
    return NULL;
}

/**
***
**/

static const char*
torrentList( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "list", torrentCount );

    for( i=0; i<torrentCount; ++i ) {
        tr_torrent * tor = torrents[i];
        const tr_stat * st = tr_torrentStat( tor );
        tr_benc * d = tr_bencListAddDict( list, 7 );
        tr_bencDictAddStr( d, "hashString", tor->info.hashString );
        tr_bencDictAddInt( d, "id", tr_torrentId( tor ) );
        tr_bencDictAddStr( d, "name", tor->info.name );
        tr_bencDictAddDouble( d, "rateDownload", st->rateDownload );
        tr_bencDictAddDouble( d, "rateUpload", st->rateUpload );
        tr_bencDictAddDouble( d, "ratio", st->ratio );
        tr_bencDictAddInt( d, "status", st->status );
    }

    tr_free( torrents );
    return NULL;
}

/**
***
**/

static const char*
torrentGet( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrents", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        tr_benc * d = tr_bencListAddDict( list, 6 );
        tr_bencDictAddInt( d, "id", tr_torrentId( tor ) );
        tr_bencDictAddInt( d, "peer-limit",
                           tr_torrentGetPeerLimit( tor ) );
        tr_bencDictAddInt( d, "speed-limit-down",
                           tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
        tr_bencDictAddInt( d, "speed-limit-down-enabled",
                           tr_torrentGetSpeedMode( tor, TR_DOWN )
                               == TR_SPEEDLIMIT_SINGLE );
        tr_bencDictAddInt( d, "speed-limit-up",
                           tr_torrentGetSpeedLimit( tor, TR_UP ) );
        tr_bencDictAddInt( d, "speed-limit-up-enabled",
                           tr_torrentGetSpeedMode( tor, TR_UP )
                               == TR_SPEEDLIMIT_SINGLE );
    }

    tr_free( torrents );
    return NULL;
}

static const char*
torrentSet( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        int64_t tmp;
        tr_torrent * tor = torrents[i];
        if( tr_bencDictFindInt( args_in, "peer-limit", &tmp ) )
            tr_torrentSetPeerLimit( tor, tmp );
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

typedef int( *fileTestFunc )( const tr_torrent * tor, int i );

static int
testFileHigh( const tr_torrent * tor, int i )
{
    return tor->info.files[i].priority == TR_PRI_HIGH;
}
static int
testFileLow( const tr_torrent * tor, int i )
{
    return tor->info.files[i].priority == TR_PRI_LOW;
}
static int
testFileNormal( const tr_torrent * tor, int i )
{
    return tor->info.files[i].priority == TR_PRI_NORMAL;
}
static int
testFileDND( const tr_torrent * tor, int i )
{
    return tor->info.files[i].dnd != 0;
}
static int
testFileDownload( const tr_torrent * tor, int i )
{
    return tor->info.files[i].dnd == 0;
}

static void
buildFileList( const tr_torrent * tor, tr_benc * dict,
               const char * key, fileTestFunc func )
{
    int i;
    const int n = tor->info.fileCount;
    tr_benc * list;
    int * files = tr_new0( int, n );
    int fileCount = 0;
    
    for( i=0; i<n; ++i )
        if( func( tor, i ) )
            files[fileCount++] = i;

    list = tr_bencDictAddList( dict, key, fileCount );

    for( i=0; i<fileCount; ++i )
        tr_bencListAddInt( list, files[i] );

    tr_free( files );
}

static const char*
torrentGetPriorities( tr_handle * handle,
                      tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrents", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        const tr_torrent * tor = torrents[i];
        tr_benc * d = tr_bencListAddDict( list, 6 );
        tr_bencDictAddInt( d, "id", tr_torrentId( tor ) );
        buildFileList( tor, d, "files-unwanted", testFileDND );
        buildFileList( tor, d, "files-wanted", testFileDownload );
        buildFileList( tor, d, "priority-low", testFileLow );
        buildFileList( tor, d, "priority-normal", testFileNormal );
        buildFileList( tor, d, "priority-high", testFileHigh );
    }

    tr_free( torrents );
    return NULL;
}

static void
setFilePriorities( tr_torrent * tor, int priority, tr_benc * list )
{
    int i;
    int64_t tmp;
    int fileCount = 0;
    const int n = tr_bencListSize( list );
    tr_file_index_t * files = tr_new0( tr_file_index_t, n );

    for( i=0; i<n; ++i )
        if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) )
            files[fileCount++] = tmp;

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
    tr_file_index_t * files = tr_new0( tr_file_index_t, n );

    for( i=0; i<n; ++i )
        if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) )
            files[fileCount++] = tmp;

    if( fileCount )
        tr_torrentSetFileDLs( tor, files, fileCount, do_download );

    tr_free( files );
}

static const char*
torrentSetPriorities( tr_handle * h,
                      tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_benc * files;
        tr_torrent * tor = torrents[i];

        if( tr_bencDictFindList( args_in, "files-unwanted", &files ) )
            setFileDLs( tor, FALSE, files );
        if( tr_bencDictFindList( args_in, "files-wanted", &files ) )
            setFileDLs( tor, TRUE, files );
        if( tr_bencDictFindList( args_in, "priority-high", &files ) )
            setFilePriorities( tor, TR_PRI_HIGH, files );
        if( tr_bencDictFindList( args_in, "priority-low", &files ) )
            setFilePriorities( tor, TR_PRI_LOW, files );
        if( tr_bencDictFindList( args_in, "priority-normal", &files ) )
            setFilePriorities( tor, TR_PRI_NORMAL, files );

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
            addInfo( tor, tr_bencDictAdd( args_out, "torrent-added" ) );
            notify( h, TR_RPC_TORRENT_ADDED, tor );
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
            tr_sessionSetEncryption( h, TR_PLAINTEXT_PREFERRED );
        else
            tr_sessionSetEncryption( h, TR_ENCRYPTION_PREFERRED );
    }

    notify( h, TR_RPC_SESSION_CHANGED, NULL );

    return NULL;
}

static const char*
sessionGet( tr_handle * h, tr_benc * args_in UNUSED, tr_benc * args_out )
{
    const char * str;
    tr_benc * d = tr_bencDictAddDict( args_out, "session", 10 );

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
                          tr_sessionGetSpeedLimit( h, TR_UP ) );
    tr_bencDictAddInt( d, "speed-limit-up-enabled",
                          tr_sessionIsSpeedLimitEnabled( h, TR_UP ) );
    tr_bencDictAddInt( d, "speed-limit-down",
                          tr_sessionGetSpeedLimit( h, TR_DOWN ) );
    tr_bencDictAddInt( d, "speed-limit-down-enabled",
                          tr_sessionIsSpeedLimitEnabled( h, TR_DOWN ) );
    switch( tr_sessionGetEncryption( h ) ) {
        case TR_PLAINTEXT_PREFERRED: str = "tolerated"; break;
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

struct method {
    const char * name;
    handler * func;
} methods[] = { 
    { "session-get", sessionGet },
    { "session-set", sessionSet },
    { "torrent-add", torrentAdd },
    { "torrent-get-priorities", torrentGetPriorities },
    { "torrent-get", torrentGet },
    { "torrent-info", torrentInfo },
    { "torrent-list", torrentList },
    { "torrent-remove", torrentRemove },
    { "torrent-set-priorities", torrentSetPriorities },
    { "torrent-set", torrentSet },
    { "torrent-start", torrentStart },
    { "torrent-status", torrentStatus },
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
            char * p;
            tr_bencListAddInt( setme, strtol( walk, &p, 10 ) );
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

    /* munge the URI into a usable form.
     * we have very loose typing on this to make the URIs as simple as possible:
     * - anything not a 'tag' or 'method' is automatically in 'arguments'
     * - values that are all-digits are numbers
     * - values that are all-digits or commas are number lists
     * - all other values are strings
     */

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
