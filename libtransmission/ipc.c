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

#include "transmission.h"
#include "bencode.h"
#include "ipc.h"
#include "torrent.h"
#include "utils.h"

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
            
static const char*
torrentStart( tr_handle * handle, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
        tr_torrentStart( torrents[i] );
    tr_free( torrents );
    return NULL;
}

static const char*
torrentStop( tr_handle * handle, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
        tr_torrentStop( torrents[i] );
    tr_free( torrents );
    return NULL;
}

static const char*
torrentClose( tr_handle * handle, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
        tr_torrentClose( torrents[i] );
    tr_free( torrents );
    return NULL;
}

static const char*
torrentVerify( tr_handle * handle, tr_benc * args_in, tr_benc * args_out UNUSED )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    for( i=0; i<torrentCount; ++i )
        tr_torrentVerify( torrents[i] );
    tr_free( torrents );
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
            tr_bencDictAddInt( t, "cache",    st->peersFrom[TR_PEER_FROM_CACHE] );
            tr_bencDictAddInt( t, "incoming", st->peersFrom[TR_PEER_FROM_INCOMING] );
            tr_bencDictAddInt( t, "pex",      st->peersFrom[TR_PEER_FROM_PEX] );
            tr_bencDictAddInt( t, "tracker",  st->peersFrom[TR_PEER_FROM_TRACKER] );
        t = tr_bencDictAddDict( d, "tracker_stat", 7 );
            tr_bencDictAddStr( t, "scrapeResponse", st->tracker_stat.scrapeResponse );
            tr_bencDictAddStr( t, "announceResponse", st->tracker_stat.announceResponse );
            tr_bencDictAddInt( t, "lastScrapeTime", st->tracker_stat.lastScrapeTime );
            tr_bencDictAddInt( t, "nextScrapeTime", st->tracker_stat.nextScrapeTime );
            tr_bencDictAddInt( t, "lastAnnounceTime", st->tracker_stat.lastAnnounceTime );
            tr_bencDictAddInt( t, "nextAnnounceTime", st->tracker_stat.nextAnnounceTime );
            tr_bencDictAddInt( t, "nextManualAnnounceTime", st->tracker_stat.nextManualAnnounceTime );
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

static const char*
torrentInfo( tr_handle * handle, tr_benc * args_in, tr_benc * args_out )
{
    int i, torrentCount;
    tr_torrent ** torrents = getTorrents( handle, args_in, &torrentCount );
    tr_benc * list = tr_bencDictAddList( args_out, "status", torrentCount );

    for( i=0; i<torrentCount; ++i )
    {
        tr_torrent * tor = torrents[i];
        const tr_info * inf = tr_torrentInfo( tor );
        tr_benc * d = tr_bencListAddDict( list, 13 );
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

    /* cleanup */
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
        tr_benc * d = tr_bencListAddDict( list, 3 );
        tr_bencDictAddInt( d, "max-peers", tr_torrentGetMaxConnectedPeers( tor ) );
        tr_bencDictAddInt( d, "speed-limit-down", tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
        tr_bencDictAddInt( d, "speed-limit-up", tr_torrentGetSpeedLimit( tor, TR_UP ) );
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
        if( tr_bencDictFindInt( args_in, "max-peers", &tmp ) )
            tr_torrentSetMaxConnectedPeers( tor, tmp );
        if( tr_bencDictFindInt( args_in, "speed-limit-down", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_DOWN, tmp );
        if( tr_bencDictFindInt( args_in, "speed-limit-up", &tmp ) )
            tr_torrentSetSpeedLimit( tor, TR_UP, tmp );
    }

    tr_free( torrents );
    return NULL;
}

static const char*
torrentGetFile( tr_handle * handle UNUSED, tr_benc * args_in UNUSED, tr_benc * args_out UNUSED )
{
    return NULL;
}

static const char*
torrentSetFile( tr_handle * handle UNUSED, tr_benc * args_in UNUSED, tr_benc * args_out UNUSED )
{
    return NULL;
}

static const char*
sessionSet( tr_handle * handle UNUSED, tr_benc * args_in UNUSED, tr_benc * args_out UNUSED )
{
    return NULL;
}

static const char*
sessionGet( tr_handle * handle UNUSED, tr_benc * args_in UNUSED, tr_benc * args_out UNUSED )
{
    return NULL;
}

static const char*
torrentAdd( tr_handle * handle UNUSED, tr_benc * args_in UNUSED, tr_benc * args_out UNUSED )
{
    return NULL;
}

/***
****
***/

static char*
request_exec( struct tr_handle * handle,
              tr_benc          * request,
              int              * response_len )
{
    int64_t i;
    const char * str;
    char * out;
    tr_benc response;
    tr_benc * headers_in = NULL;
    tr_benc * body_in = NULL;
    tr_benc * args_in = NULL;
    tr_benc * headers_out = NULL;
    tr_benc * body_out = NULL;
    tr_benc * args_out = NULL;
    const char * result = NULL;

    headers_in = tr_bencDictFind( request, "headers" );
    body_in = tr_bencDictFind( request, "body" );
    args_in = tr_bencDictFind( body_in, "args" );

    /* build the response skeleton */
    tr_bencInitDict( &response, 2 );
    headers_out = tr_bencDictAddDict( &response, "headers", 2 );
    tr_bencDictAddStr( headers_out, "type", "response" );
    if( tr_bencDictFindInt( headers_in, "tag", &i ) )
        tr_bencDictAddInt( headers_out, "tag", i );
    body_out = tr_bencDictAddDict( &response, "body", 2 );
    args_out = tr_bencDictAddDict( body_out, "args", 0 );

    /* parse the request */
    if( !tr_bencDictFindStr( body_in, "name", &str ) )
        result = "no request name given";
    else {
             if( !strcmp( str, "torrent-start" ) )    result = torrentStart  ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-stop" ) )     result = torrentStop   ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-close" ) )    result = torrentClose  ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-verify" ) )   result = torrentVerify ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-status" ) )   result = torrentStatus ( handle, args_in, args_out );

        else if( !strcmp( str, "torrent-info" ) )     result = torrentInfo ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-add" ) )      result = torrentAdd    ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-set" ) )      result = torrentSet    ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-get" ) )      result = torrentGet    ( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-set-file" ) ) result = torrentSetFile( handle, args_in, args_out );
        else if( !strcmp( str, "torrent-get-file" ) ) result = torrentGetFile( handle, args_in, args_out );
        else if( !strcmp( str, "session-set" ) )      result = sessionSet    ( handle, args_in, args_out );
        else if( !strcmp( str, "session-get" ) )      result = sessionGet    ( handle, args_in, args_out );
        else                                          result = "request name not recognized";
    }

    if( !result )
        result = "success";
    tr_bencDictAddStr( body_out, "result", result );
    out = tr_bencSave( &response, response_len ); /* TODO: json, not benc */
    tr_bencFree( &response );
    return out;
}

char*
tr_ipc_request_exec( struct tr_handle  * handle,
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
