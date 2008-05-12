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
#include <string.h> /* strcmp */

#include "transmission.h"
#include "bencode.h"
#include "rpc.h"
#include "torrent.h"
#include "utils.h"

#define TR_N_ELEMENTS( ary ) ( sizeof( ary ) / sizeof( (ary)[0] ) )

/***
****
***/

static tr_torrent **
getTorrents( tr_handle * handle, tr_benc * args, int * setmeCount )
{
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
    else /* all of them */
    {
        tr_torrent * tor = NULL;
        const int n = tr_torrentCount( handle );
        torrents = tr_new0( tr_torrent*, n );
        while(( tor = tr_torrentNext( handle, tor )))
            torrents[torrentCount++] = tor;
    }

    *setmeCount = torrentCount;
    return torrents;
}

typedef void( *tor_func )( tr_torrent * tor );

static void callTorrentFunc( tr_handle * h, tr_benc * args_in, tor_func func )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
        ( *func )( torrents[i] );
    tr_free( torrents );
}

static const char*
torrentStart( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    callTorrentFunc( h, args_in, tr_torrentStart );
    return NULL;
}

static const char*
torrentStop( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    callTorrentFunc( h, args_in, tr_torrentStop );
    return NULL;
}

static const char*
torrentClose( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    callTorrentFunc( h, args_in, tr_torrentClose );
    return NULL;
}

static const char*
torrentVerify( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    callTorrentFunc( h, args_in, tr_torrentVerify );
    return NULL;
}

static const char*
torrentStatus( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "status", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        const tr_stat * st = tr_torrentStat( tor );
        tr_benc * d = tr_bencListAddDict( list, 32 );
        tr_benc * t;
        const int * f = st->peersFrom;
        const struct tr_tracker_stat * s = &st->tracker_stat;

        tr_bencDictAddInt( d, "id", tor->uniqueId );
        tr_bencDictAddInt( d, "status", st->status );
        tr_bencDictAddInt( d, "error", st->error );
        tr_bencDictAddStr( d, "errorString", st->errorString );
        tr_bencDictAddDouble( d, "recheckProgress", st->recheckProgress );
        tr_bencDictAddDouble( d, "percentComplete", st->percentComplete );
        tr_bencDictAddDouble( d, "percentDone", st->percentDone );
        tr_bencDictAddDouble( d, "rateDownload", st->rateDownload );
        tr_bencDictAddDouble( d, "rateUpload", st->rateUpload );
        tr_bencDictAddDouble( d, "swarmspeed", st->swarmspeed );
        tr_bencDictAddDouble( d, "ratio", st->ratio );
        tr_bencDictAddInt( d, "eta", st->eta );
        tr_bencDictAddInt( d, "peersKnown", st->peersKnown );
        tr_bencDictAddInt( d, "peersConnected", st->peersConnected );
        tr_bencDictAddInt( d, "peersSendingToUs", st->peersSendingToUs );
        tr_bencDictAddInt( d, "peersGettingFromUs", st->peersGettingFromUs );
        tr_bencDictAddInt( d, "seeders", st->seeders );
        tr_bencDictAddInt( d, "leechers", st->leechers );
        tr_bencDictAddInt( d, "completedFromTracker", st->completedFromTracker );
        tr_bencDictAddInt( d, "manualAnnounceTime", st->manualAnnounceTime );
        tr_bencDictAddInt( d, "sizeWhenDone", st->sizeWhenDone );
        tr_bencDictAddInt( d, "leftUntilDone", st->leftUntilDone );
        tr_bencDictAddInt( d, "desiredAvailable", st->desiredAvailable );
        tr_bencDictAddInt( d, "corruptEver", st->corruptEver );
        tr_bencDictAddInt( d, "uploadedEver", st->uploadedEver );
        tr_bencDictAddInt( d, "downloadedEver", st->downloadedEver );
        tr_bencDictAddInt( d, "haveValid", st->haveValid );
        tr_bencDictAddInt( d, "haveUnchecked", st->haveUnchecked );
        tr_bencDictAddInt( d, "startDate", st->startDate );
        tr_bencDictAddInt( d, "activityDate", st->activityDate );
        t = tr_bencDictAddDict( d, "peersFrom", 4 );
            tr_bencDictAddInt( t, "cache",    f[TR_PEER_FROM_CACHE] );
            tr_bencDictAddInt( t, "incoming", f[TR_PEER_FROM_INCOMING] );
            tr_bencDictAddInt( t, "pex",      f[TR_PEER_FROM_PEX] );
            tr_bencDictAddInt( t, "tracker",  f[TR_PEER_FROM_TRACKER] );
        t = tr_bencDictAddDict( d, "tracker_stat", 7 );
            tr_bencDictAddStr( t, "scrapeResponse", s->scrapeResponse );
            tr_bencDictAddStr( t, "announceResponse", s->announceResponse );
            tr_bencDictAddInt( t, "lastScrapeTime", s->lastScrapeTime );
            tr_bencDictAddInt( t, "nextScrapeTime", s->nextScrapeTime );
            tr_bencDictAddInt( t, "lastAnnounceTime", s->lastAnnounceTime );
            tr_bencDictAddInt( t, "nextAnnounceTime", s->nextAnnounceTime );
            tr_bencDictAddInt( t, "nextManualAnnounceTime", s->nextManualAnnounceTime );
    }

    /* cleanup */
    tr_free( torrents );
    return NULL;
}

static void
addFiles( const tr_info * info, tr_benc * files )
{
    unsigned int i;
    for( i=0; i<info->fileCount; ++i )
    {
        const tr_file * file = &info->files[i];
        tr_benc * d = tr_bencListAddDict( files, 4 );
        tr_bencDictAddInt( d, "length", file->length );
        tr_bencDictAddStr( d, "name", file->name );
        tr_bencDictAddInt( d, "priority", file->priority );
        tr_bencDictAddInt( d, "dnd", file->dnd );
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
        tr_bencDictAddInt( d, "tier", t->tier );
        tr_bencDictAddStr( d, "announce", t->announce );
        tr_bencDictAddStr( d, "scrape", t->scrape );
    }
}

static void
serializeInfo( const tr_torrent * tor, tr_benc * d )
{
    const tr_info * inf = tr_torrentInfo( tor );
    tr_bencInitDict( d, 14 ); 
    tr_bencDictAddInt( d, "id", tor->uniqueId );
    tr_bencDictAddStr( d, "torrent", inf->torrent );
    tr_bencDictAddStr( d, "hashString", inf->hashString );
    tr_bencDictAddStr( d, "name", inf->name );
    tr_bencDictAddStr( d, "comment", inf->comment ? inf->comment : "" );
    tr_bencDictAddStr( d, "creator", inf->creator ? inf->creator : "" );
    tr_bencDictAddInt( d, "isPrivate", inf->isPrivate );
    tr_bencDictAddInt( d, "isMultifile", inf->isMultifile );
    tr_bencDictAddInt( d, "dateCreated", inf->dateCreated );
    tr_bencDictAddInt( d, "pieceSize", inf->pieceSize );
    tr_bencDictAddInt( d, "pieceCount", inf->pieceCount );
    tr_bencDictAddInt( d, "totalSize", inf->totalSize );
    addFiles   ( inf, tr_bencDictAddList( d, "files", inf->fileCount ) );
    addTrackers( inf, tr_bencDictAddList( d, "files", inf->trackerCount ) );
}

static const char*
torrentInfo( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "status", torrentCount );

    for( i=0; i<torrentCount; ++i )
        serializeInfo( torrents[i], tr_bencListAdd( list ) );

    tr_free( torrents );
    return NULL;
}

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
        tr_bencDictAddInt( d, "id", tor->uniqueId );
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
torrentSet( tr_handle * handle, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );

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
torrentGetFile( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "torrents", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        const tr_torrent * tor = torrents[i];
        tr_benc * d = tr_bencListAddDict( list, 6 );
        tr_bencDictAddInt( d, "id", tor->uniqueId );
        buildFileList( tor, d, "priority-high", testFileHigh );
        buildFileList( tor, d, "priority-low", testFileLow );
        buildFileList( tor, d, "priority-normal", testFileNormal );
        buildFileList( tor, d, "download", testFileDownload );
        buildFileList( tor, d, "no-download", testFileDND );
    }

    tr_free( torrents );
    return NULL;
}

static void
setFilePriorities( tr_torrent * tor, int priority, tr_benc * list )
{
    const int n = tr_bencListSize( list );
    int i;
    int64_t tmp;
    int fileCount = 0;
    tr_file_index_t * files = tr_new0( tr_file_index_t, n );

    for( i=0; i<n; ++i )
        if( tr_bencGetInt( tr_bencListChild(list,i), &tmp ) )
            files[fileCount++] = tmp;

    if( fileCount )
        tr_torrentSetFilePriorities( tor, files, fileCount, priority );

    tr_free( files );
}

static void
setFileDLs( tr_torrent * tor, int do_download, tr_benc * list )
{
    const int n = tr_bencListSize( list );
    int i;
    int64_t tmp;
    int fileCount = 0;
    tr_file_index_t * files = tr_new0( tr_file_index_t, n );

    for( i=0; i<n; ++i )
        if( tr_bencGetInt( tr_bencListChild(list,i), &tmp ) )
            files[fileCount++] = tmp;

    if( fileCount )
        tr_torrentSetFileDLs( tor, files, fileCount, do_download );

    tr_free( files );
}

static const char*
torrentSetFile( tr_handle * h, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( h, args_in, &torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_benc * files;
        tr_torrent * tor = torrents[i];

        if( tr_bencDictFindList( args_in, "priority-high", &files ) )
            setFilePriorities( tor, TR_PRI_HIGH, files );
        if( tr_bencDictFindList( args_in, "priority-low", &files ) )
            setFilePriorities( tor, TR_PRI_LOW, files );
        if( tr_bencDictFindList( args_in, "priority-normal", &files ) )
            setFilePriorities( tor, TR_PRI_NORMAL, files );
        if( tr_bencDictFindList( args_in, "download", &files ) )
            setFileDLs( tor, TRUE, files );
        if( tr_bencDictFindList( args_in, "no-download", &files ) )
            setFileDLs( tor, FALSE, files );
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
    const char * filename;
    if( !tr_bencDictFindStr( args_in, "filename", &filename ) )
        return "no filename specified";
    else {
        int64_t i;
        int err = 0;
        const char * str;
        tr_ctor * ctor;
        tr_torrent * tor;

        ctor = tr_ctorNew( h );
        tr_ctorSetMetainfoFromFile( ctor, filename );
        if( tr_bencDictFindInt( args_in, "paused", &i ) )
            tr_ctorSetPaused( ctor, TR_FORCE, i );
        if( tr_bencDictFindInt( args_in, "peer-limit", &i ) )
            tr_ctorSetPeerLimit( ctor, TR_FORCE, i );
        if( tr_bencDictFindStr( args_in, "destination", &str ) )
            tr_ctorSetDestination( ctor, TR_FORCE, str );
        tor = tr_torrentNew( h, ctor, &err );
        tr_ctorFree( ctor );

        if( tor )
            serializeInfo( tor, tr_bencDictAdd( args_out, "torrent-added" ) );
        else if( err == TR_EDUPLICATE )
            return "duplicate torrent";
        else if( err == TR_EINVALID )
            return "invalid or corrupt torrent file";
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

    if( tr_bencDictFindInt( args_in, "peer-limit", &i ) )
        tr_sessionSetPeerLimit( h, i );
    if( tr_bencDictFindInt( args_in, "port", &i ) )
        tr_sessionSetPublicPort( h, i );
    if( tr_bencDictFindInt( args_in, "port-forwarding-enabled", &i ) )
        tr_sessionSetPortForwardingEnabled( h, i );
    if( tr_bencDictFindInt( args_in, "pex-allowed", &i ) )
        tr_sessionSetPexEnabled( h, i );
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

    return NULL;
}

static const char*
sessionGet( tr_handle * h, tr_benc * args_in UNUSED, tr_benc * args_out )
{
    const char * str;
    tr_benc * d = tr_bencDictAddDict( args_out, "session", 9 );

    tr_bencDictAddInt( d, "peer-limit",
                          tr_sessionGetPeerLimit( h ) );
    tr_bencDictAddInt( d, "port",
                          tr_sessionGetPublicPort( h ) );
    tr_bencDictAddInt( d, "port-forwarding-enabled",
                          tr_sessionIsPortForwardingEnabled( h ) );
    tr_bencDictAddInt( d, "pex-allowed",
                          tr_sessionIsPexEnabled( h ) );
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

struct request_handler
{
    const char * method;
    handler * func;
} request_handlers[] = { 
    { "torrent-start", torrentStart },
    { "torrent-stop", torrentStop },
    { "torrent-close", torrentClose },
    { "torrent-verify", torrentVerify },
    { "torrent-status", torrentStatus },
    { "torrent-info", torrentInfo },
    { "torrent-add", torrentAdd },
    { "torrent-set", torrentSet },
    { "torrent-get", torrentGet },
    { "torrent-set-file", torrentSetFile },
    { "torrent-get-file", torrentGetFile },
    { "session-set", sessionSet },
    { "session-get", sessionGet }
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
    tr_benc * args_in = tr_bencDictFind( request, "args" );
    tr_benc * args_out = NULL;
    const char * result = NULL;

    /* build the response skeleton */
    tr_bencInitDict( &response, 3 );
    if( tr_bencDictFindInt( request, "tag", &i ) )
        tr_bencDictAddInt( request, "tag", i );
    args_out = tr_bencDictAddDict( &response, "args", 0 );

    /* parse the request */
    if( !tr_bencDictFindStr( request, "method", &str ) )
        result = "no method name";
    else {
        const int n = TR_N_ELEMENTS( request_handlers );
        for( i=0; i<n; ++i )
            if( !strcmp( str, request_handlers[i].method ) )
                break;
        result = i==n
            ? "method name not recognized"
            : (*request_handlers[i].func)( handle, args_in, args_out );
    }

    /* serialize & return the response */
    if( !result )
         result = "success";
    tr_bencDictAddStr( &response, "result", result );
    out = tr_bencSaveAsJSON( &response, response_len );
    tr_bencFree( &response );
    return out;
}

char*
tr_rpc_request_exec( struct tr_handle  * handle,
                     const void        * request_json,
                     int                 request_len,
                     int               * response_len )
{
    tr_benc top;
    int have_content = !tr_jsonParse( request_json, (const char*)request_json + request_len, &top, NULL );
    char * ret = request_exec( handle, have_content ? &top : NULL, response_len );
    if( have_content )
        tr_bencFree( &top );
    return ret;
}
