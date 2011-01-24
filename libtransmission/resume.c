/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This file is licensed by the GPL version 2. Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <unistd.h> /* unlink */

#include <string.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "metainfo.h" /* tr_metainfoGetBasename() */
#include "peer-mgr.h" /* pex */
#include "platform.h" /* tr_getResumeDir() */
#include "resume.h"
#include "session.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */

#define KEY_ACTIVITY_DATE       "activity-date"
#define KEY_ADDED_DATE          "added-date"
#define KEY_CORRUPT             "corrupt"
#define KEY_DONE_DATE           "done-date"
#define KEY_DOWNLOAD_DIR        "destination"
#define KEY_DND                 "dnd"
#define KEY_DOWNLOADED          "downloaded"
#define KEY_INCOMPLETE_DIR      "incomplete-dir"
#define KEY_MAX_PEERS           "max-peers"
#define KEY_PAUSED              "paused"
#define KEY_PEERS               "peers2"
#define KEY_PEERS6              "peers2-6"
#define KEY_FILE_PRIORITIES     "priority"
#define KEY_BANDWIDTH_PRIORITY  "bandwidth-priority"
#define KEY_PROGRESS            "progress"
#define KEY_SPEEDLIMIT_OLD      "speed-limit"
#define KEY_SPEEDLIMIT_UP       "speed-limit-up"
#define KEY_SPEEDLIMIT_DOWN     "speed-limit-down"
#define KEY_RATIOLIMIT          "ratio-limit"
#define KEY_IDLELIMIT           "idle-limit"
#define KEY_UPLOADED            "uploaded"

#define KEY_SPEED_KiBps            "speed"
#define KEY_SPEED_Bps              "speed-Bps"
#define KEY_USE_GLOBAL_SPEED_LIMIT "use-global-speed-limit"
#define KEY_USE_SPEED_LIMIT        "use-speed-limit"
#define KEY_TIME_SEEDING           "seeding-time-seconds"
#define KEY_TIME_DOWNLOADING       "downloading-time-seconds"
#define KEY_SPEEDLIMIT_DOWN_SPEED  "down-speed"
#define KEY_SPEEDLIMIT_DOWN_MODE   "down-mode"
#define KEY_SPEEDLIMIT_UP_SPEED    "up-speed"
#define KEY_SPEEDLIMIT_UP_MODE     "up-mode"
#define KEY_RATIOLIMIT_RATIO       "ratio-limit"
#define KEY_RATIOLIMIT_MODE        "ratio-mode"
#define KEY_IDLELIMIT_MINS         "idle-limit"
#define KEY_IDLELIMIT_MODE         "idle-mode"

#define KEY_PROGRESS_CHECKTIME "time-checked"
#define KEY_PROGRESS_MTIMES    "mtimes"
#define KEY_PROGRESS_BITFIELD  "bitfield"
#define KEY_PROGRESS_HAVE      "have"

enum
{
    MAX_REMEMBERED_PEERS = 200
};

static char*
getResumeFilename( const tr_torrent * tor )
{
    char * base = tr_metainfoGetBasename( tr_torrentInfo( tor ) );
    char * filename = tr_strdup_printf( "%s" TR_PATH_DELIMITER_STR "%s.resume",
                                        tr_getResumeDir( tor->session ), base );
    tr_free( base );
    return filename;
}

/***
****
***/

static void
savePeers( tr_benc * dict, const tr_torrent * tor )
{
    int count;
    tr_pex * pex;

    count = tr_peerMgrGetPeers( (tr_torrent*) tor, &pex, TR_AF_INET, TR_PEERS_ALL, MAX_REMEMBERED_PEERS );
    if( count > 0 )
        tr_bencDictAddRaw( dict, KEY_PEERS, pex, sizeof( tr_pex ) * count );
    tr_free( pex );

    count = tr_peerMgrGetPeers( (tr_torrent*) tor, &pex, TR_AF_INET6, TR_PEERS_ALL, MAX_REMEMBERED_PEERS );
    if( count > 0 )
        tr_bencDictAddRaw( dict, KEY_PEERS6, pex, sizeof( tr_pex ) * count );

    tr_free( pex );
}

static int
addPeers( tr_torrent * tor, const uint8_t * buf, int buflen )
{
    int i;
    int numAdded = 0;
    const int count = buflen / sizeof( tr_pex );

    for( i=0; i<count && numAdded<MAX_REMEMBERED_PEERS; ++i )
    {
        tr_pex pex;
        memcpy( &pex, buf + ( i * sizeof( tr_pex ) ), sizeof( tr_pex ) );
        if( tr_isPex( &pex ) )
        {
            tr_peerMgrAddPex( tor, TR_PEER_FROM_RESUME, &pex, -1 );
            ++numAdded;
        }
    }

    return numAdded;
}


static uint64_t
loadPeers( tr_benc * dict, tr_torrent * tor )
{
    uint64_t        ret = 0;
    const uint8_t * str;
    size_t          len;

    if( tr_bencDictFindRaw( dict, KEY_PEERS, &str, &len ) )
    {
        const int numAdded = addPeers( tor, str, len );
        tr_tordbg( tor, "Loaded %d IPv4 peers from resume file", numAdded );
        ret = TR_FR_PEERS;
    }

    if( tr_bencDictFindRaw( dict, KEY_PEERS6, &str, &len ) )
    {
        const int numAdded = addPeers( tor, str, len );
        tr_tordbg( tor, "Loaded %d IPv6 peers from resume file", numAdded );
        ret = TR_FR_PEERS;
    }

    return ret;
}

/***
****
***/

static void
saveDND( tr_benc *          dict,
         const tr_torrent * tor )
{
    const tr_info *       inf = tr_torrentInfo( tor );
    const tr_file_index_t n = inf->fileCount;
    tr_file_index_t       i;
    tr_benc *             list;

    list = tr_bencDictAddList( dict, KEY_DND, n );
    for( i = 0; i < n; ++i )
        tr_bencListAddInt( list, inf->files[i].dnd ? 1 : 0 );
}

static uint64_t
loadDND( tr_benc *    dict,
         tr_torrent * tor )
{
    uint64_t              ret = 0;
    tr_info *             inf = &tor->info;
    const tr_file_index_t n = inf->fileCount;
    tr_benc *             list = NULL;

    if( tr_bencDictFindList( dict, KEY_DND, &list )
      && ( tr_bencListSize( list ) == n ) )
    {
        int64_t           tmp;
        tr_file_index_t * dl = tr_new( tr_file_index_t, n );
        tr_file_index_t * dnd = tr_new( tr_file_index_t, n );
        tr_file_index_t   i, dlCount = 0, dndCount = 0;

        for( i = 0; i < n; ++i )
        {
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) && tmp )
                dnd[dndCount++] = i;
            else
                dl[dlCount++] = i;
        }

        if( dndCount )
        {
            tr_torrentInitFileDLs ( tor, dnd, dndCount, FALSE );
            tr_tordbg( tor, "Resume file found %d files listed as dnd",
                       dndCount );
        }
        if( dlCount )
        {
            tr_torrentInitFileDLs ( tor, dl, dlCount, TRUE );
            tr_tordbg( tor,
                       "Resume file found %d files marked for download",
                       dlCount );
        }

        tr_free( dnd );
        tr_free( dl );
        ret = TR_FR_DND;
    }
    else
    {
        tr_tordbg(
            tor,
            "Couldn't load DND flags. DND list (%p) has %zu children; torrent has %d files",
            list, tr_bencListSize( list ), (int)n );
    }

    return ret;
}

/***
****
***/

static void
saveFilePriorities( tr_benc * dict, const tr_torrent * tor )
{
    const tr_info *       inf = tr_torrentInfo( tor );
    const tr_file_index_t n = inf->fileCount;
    tr_file_index_t       i;
    tr_benc *             list;

    list = tr_bencDictAddList( dict, KEY_FILE_PRIORITIES, n );
    for( i = 0; i < n; ++i )
        tr_bencListAddInt( list, inf->files[i].priority );
}

static uint64_t
loadFilePriorities( tr_benc * dict, tr_torrent * tor )
{
    uint64_t              ret = 0;
    tr_info *             inf = &tor->info;
    const tr_file_index_t n = inf->fileCount;
    tr_benc *             list;

    if( tr_bencDictFindList( dict, KEY_FILE_PRIORITIES, &list )
      && ( tr_bencListSize( list ) == n ) )
    {
        int64_t priority;
        tr_file_index_t i;
        for( i = 0; i < n; ++i )
            if( tr_bencGetInt( tr_bencListChild( list, i ), &priority ) )
                tr_torrentInitFilePriority( tor, i, priority );
        ret = TR_FR_FILE_PRIORITIES;
    }

    return ret;
}

/***
****
***/

static void
saveSingleSpeedLimit( tr_benc * d, const tr_torrent * tor, tr_direction dir )
{
    tr_bencDictReserve( d, 3 );
    tr_bencDictAddInt( d, KEY_SPEED_Bps, tr_torrentGetSpeedLimit_Bps( tor, dir ) );
    tr_bencDictAddBool( d, KEY_USE_GLOBAL_SPEED_LIMIT, tr_torrentUsesSessionLimits( tor ) );
    tr_bencDictAddBool( d, KEY_USE_SPEED_LIMIT, tr_torrentUsesSpeedLimit( tor, dir ) );
}

static void
saveSpeedLimits( tr_benc * dict, const tr_torrent * tor )
{
    saveSingleSpeedLimit( tr_bencDictAddDict( dict, KEY_SPEEDLIMIT_DOWN, 0 ), tor, TR_DOWN );
    saveSingleSpeedLimit( tr_bencDictAddDict( dict, KEY_SPEEDLIMIT_UP, 0 ), tor, TR_UP );
}

static void
saveRatioLimits( tr_benc * dict, const tr_torrent * tor )
{
    tr_benc * d = tr_bencDictAddDict( dict, KEY_RATIOLIMIT, 2 );
    tr_bencDictAddReal( d, KEY_RATIOLIMIT_RATIO, tr_torrentGetRatioLimit( tor ) );
    tr_bencDictAddInt( d, KEY_RATIOLIMIT_MODE, tr_torrentGetRatioMode( tor ) );
}

static void
saveIdleLimits( tr_benc * dict, const tr_torrent * tor )
{
    tr_benc * d = tr_bencDictAddDict( dict, KEY_IDLELIMIT, 2 );
    tr_bencDictAddInt( d, KEY_IDLELIMIT_MINS, tr_torrentGetIdleLimit( tor ) );
    tr_bencDictAddInt( d, KEY_IDLELIMIT_MODE, tr_torrentGetIdleMode( tor ) );
}

static void
loadSingleSpeedLimit( tr_benc * d, tr_direction dir, tr_torrent * tor )
{
    int64_t i;
    tr_bool boolVal;

    if( tr_bencDictFindInt( d, KEY_SPEED_Bps, &i ) )
        tr_torrentSetSpeedLimit_Bps( tor, dir, i );
    else if( tr_bencDictFindInt( d, KEY_SPEED_KiBps, &i ) )
        tr_torrentSetSpeedLimit_Bps( tor, dir, i*1024 );

    if( tr_bencDictFindBool( d, KEY_USE_SPEED_LIMIT, &boolVal ) )
        tr_torrentUseSpeedLimit( tor, dir, boolVal );

    if( tr_bencDictFindBool( d, KEY_USE_GLOBAL_SPEED_LIMIT, &boolVal ) )
        tr_torrentUseSessionLimits( tor, boolVal );
}

enum old_speed_modes
{
    TR_SPEEDLIMIT_GLOBAL,   /* only follow the overall speed limit */
    TR_SPEEDLIMIT_SINGLE    /* only follow the per-torrent limit */
};

static uint64_t
loadSpeedLimits( tr_benc * dict, tr_torrent * tor )
{
    uint64_t  ret = 0;
    tr_benc * d;

    if( tr_bencDictFindDict( dict, KEY_SPEEDLIMIT_UP, &d ) )
    {
        loadSingleSpeedLimit( d, TR_UP, tor );
        ret = TR_FR_SPEEDLIMIT;
    }
    if( tr_bencDictFindDict( dict, KEY_SPEEDLIMIT_DOWN, &d ) )
    {
        loadSingleSpeedLimit( d, TR_DOWN, tor );
        ret = TR_FR_SPEEDLIMIT;
    }

    /* older speedlimit structure */
    if( !ret && tr_bencDictFindDict( dict, KEY_SPEEDLIMIT_OLD, &d ) )
    {

        int64_t i;
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_SPEED, &i ) )
            tr_torrentSetSpeedLimit_Bps( tor, TR_DOWN, i*1024 );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_MODE, &i ) ) {
            tr_torrentUseSpeedLimit( tor, TR_DOWN, i==TR_SPEEDLIMIT_SINGLE );
            tr_torrentUseSessionLimits( tor, i==TR_SPEEDLIMIT_GLOBAL );
         }
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_SPEED, &i ) )
            tr_torrentSetSpeedLimit_Bps( tor, TR_UP, i*1024 );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_MODE, &i ) ) {
            tr_torrentUseSpeedLimit( tor, TR_UP, i==TR_SPEEDLIMIT_SINGLE );
            tr_torrentUseSessionLimits( tor, i==TR_SPEEDLIMIT_GLOBAL );
        }
        ret = TR_FR_SPEEDLIMIT;
    }

    return ret;
}

static uint64_t
loadRatioLimits( tr_benc *    dict,
                 tr_torrent * tor )
{
    uint64_t  ret = 0;
    tr_benc * d;

    if( tr_bencDictFindDict( dict, KEY_RATIOLIMIT, &d ) )
    {
        int64_t i;
        double dratio;
        if( tr_bencDictFindReal( d, KEY_RATIOLIMIT_RATIO, &dratio ) )
            tr_torrentSetRatioLimit( tor, dratio );
        if( tr_bencDictFindInt( d, KEY_RATIOLIMIT_MODE, &i ) )
            tr_torrentSetRatioMode( tor, i );
      ret = TR_FR_RATIOLIMIT;
    }

    return ret;
}

static uint64_t
loadIdleLimits( tr_benc *    dict,
                      tr_torrent * tor )
{
    uint64_t  ret = 0;
    tr_benc * d;

    if( tr_bencDictFindDict( dict, KEY_IDLELIMIT, &d ) )
    {
        int64_t i;
        int64_t imin;
        if( tr_bencDictFindInt( d, KEY_IDLELIMIT_MINS, &imin ) )
            tr_torrentSetIdleLimit( tor, imin );
        if( tr_bencDictFindInt( d, KEY_IDLELIMIT_MODE, &i ) )
            tr_torrentSetIdleMode( tor, i );
      ret = TR_FR_IDLELIMIT;
    }

    return ret;
}
/***
****
***/

static void
saveProgress( tr_benc *          dict,
              const tr_torrent * tor )
{
    size_t              i, n;
    tr_benc *           p;
    tr_benc *           m;
    const tr_bitfield * bitfield;

    p = tr_bencDictAdd( dict, KEY_PROGRESS );
    tr_bencInitDict( p, 2 );

    /* add each piece's timeChecked */
    n = tor->info.pieceCount;
    m = tr_bencDictAddList( p, KEY_PROGRESS_CHECKTIME, n );
    for( i=0; i<n; ++i )
        tr_bencListAddInt( m, tor->info.pieces[i].timeChecked );

    /* add the progress */
    if( tor->completeness == TR_SEED )
        tr_bencDictAddStr( p, KEY_PROGRESS_HAVE, "all" );
    bitfield = tr_cpBlockBitfield( &tor->completion );
    tr_bencDictAddRaw( p, KEY_PROGRESS_BITFIELD,
                       bitfield->bits, bitfield->byteCount );
}

static uint64_t
loadProgress( tr_benc *    dict,
              tr_torrent * tor )
{
    size_t    i, n;
    uint64_t  ret = 0;
    tr_benc * p;

    for( i=0, n=tor->info.pieceCount; i<n; ++i )
        tor->info.pieces[i].timeChecked = 0;

    if( tr_bencDictFindDict( dict, KEY_PROGRESS, &p ) )
    {
        const char * err;
        const char * str;
        const uint8_t * raw;
        size_t          rawlen;
        tr_benc *       m;
        int64_t  timeChecked;

        if( tr_bencDictFindList( p, KEY_PROGRESS_CHECKTIME, &m ) )
        {
            /* This key was added in 2.20.
               Load in the timestamp of when we last checked each piece */
            for( i=0, n=tor->info.pieceCount; i<n; ++i )
                if( tr_bencGetInt( tr_bencListChild( m, i ), &timeChecked ) )
                    tor->info.pieces[i].timeChecked = (time_t)timeChecked;
        }
        else if( tr_bencDictFindList( p, KEY_PROGRESS_MTIMES, &m ) )
        {
            /* This is how it was done pre-2.20... per file. */
            for( i=0, n=tr_bencListSize(m); i<n; ++i )
            {
                /* get the timestamp of file #i */
                if( tr_bencGetInt( tr_bencListChild( m, i ), &timeChecked ) )
                {
                    /* walk through all the pieces that are in that file... */
                    tr_piece_index_t j;
                    tr_file * file = &tor->info.files[i];
                    for( j=file->firstPiece; j<=file->lastPiece; ++j )
                    {
                        tr_piece * piece = &tor->info.pieces[j];

                        /* If the piece's timestamp is unset from earlier,
                         * set it here. */
                        if( piece->timeChecked == 0 ) 
                            piece->timeChecked = timeChecked;

                        /* If the piece's timestamp is *newer* timeChecked,
                         * the piece probably spans more than one file.
                         * To be safe, let's use the older timestamp. */
                        if( piece->timeChecked > timeChecked )
                            piece->timeChecked = timeChecked;
                    }
                }
            }
        }

        err = NULL;
        if( tr_bencDictFindStr( p, KEY_PROGRESS_HAVE, &str ) )
        {
            if( !strcmp( str, "all" ) )
                tr_cpSetHaveAll( &tor->completion );
            else
                err = "Invalid value for HAVE";
        }
        else if( tr_bencDictFindRaw( p, KEY_PROGRESS_BITFIELD, &raw, &rawlen ) )
        {
            tr_bitfield tmp;
            tmp.byteCount = rawlen;
            tmp.bitCount = tmp.byteCount * 8;
            tmp.bits = (uint8_t*) raw;
            if( !tr_cpBlockBitfieldSet( &tor->completion, &tmp ) )
                err = "Error loading bitfield";
        }
        else err = "Couldn't find 'have' or 'bitfield'";

        if( err != NULL )
            tr_tordbg( tor, "Torrent needs to be verified - %s", err );

        ret = TR_FR_PROGRESS;
    }

    return ret;
}

/***
****
***/

void
tr_torrentSaveResume( tr_torrent * tor )
{
    int err;
    tr_benc top;
    char * filename;

    if( !tr_isTorrent( tor ) )
        return;

    tr_bencInitDict( &top, 50 ); /* arbitrary "big enough" number */
    tr_bencDictAddInt( &top, KEY_TIME_SEEDING, tor->secondsSeeding );
    tr_bencDictAddInt( &top, KEY_TIME_DOWNLOADING, tor->secondsDownloading );
    tr_bencDictAddInt( &top, KEY_ACTIVITY_DATE, tor->activityDate );
    tr_bencDictAddInt( &top, KEY_ADDED_DATE, tor->addedDate );
    tr_bencDictAddInt( &top, KEY_CORRUPT, tor->corruptPrev + tor->corruptCur );
    tr_bencDictAddInt( &top, KEY_DONE_DATE, tor->doneDate );
    tr_bencDictAddStr( &top, KEY_DOWNLOAD_DIR, tor->downloadDir );
    if( tor->incompleteDir != NULL )
        tr_bencDictAddStr( &top, KEY_INCOMPLETE_DIR, tor->incompleteDir );
    tr_bencDictAddInt( &top, KEY_DOWNLOADED, tor->downloadedPrev + tor->downloadedCur );
    tr_bencDictAddInt( &top, KEY_UPLOADED, tor->uploadedPrev + tor->uploadedCur );
    tr_bencDictAddInt( &top, KEY_MAX_PEERS, tor->maxConnectedPeers );
    tr_bencDictAddInt( &top, KEY_BANDWIDTH_PRIORITY, tr_torrentGetPriority( tor ) );
    tr_bencDictAddBool( &top, KEY_PAUSED, !tor->isRunning );
    savePeers( &top, tor );
    if( tr_torrentHasMetadata( tor ) )
    {
        saveFilePriorities( &top, tor );
        saveDND( &top, tor );
        saveProgress( &top, tor );
    }
    saveSpeedLimits( &top, tor );
    saveRatioLimits( &top, tor );
    saveIdleLimits( &top, tor );

    filename = getResumeFilename( tor );
    if(( err = tr_bencToFile( &top, TR_FMT_BENC, filename )))
        tr_torrentSetLocalError( tor, "Unable to save resume file: %s", tr_strerror( err ) );
    tr_free( filename );

    tr_bencFree( &top );
}

static uint64_t
loadFromFile( tr_torrent * tor,
              uint64_t     fieldsToLoad )
{
    int64_t  i;
    const char * str;
    uint64_t fieldsLoaded = 0;
    char * filename;
    tr_benc top;
    tr_bool boolVal;
    const tr_bool  wasDirty = tor->isDirty;

    assert( tr_isTorrent( tor ) );

    filename = getResumeFilename( tor );

    if( tr_bencLoadFile( &top, TR_FMT_BENC, filename ) )
    {
        tr_tordbg( tor, "Couldn't read \"%s\"", filename );

        tr_free( filename );
        return fieldsLoaded;
    }

    tr_tordbg( tor, "Read resume file \"%s\"", filename );

    if( ( fieldsToLoad & TR_FR_CORRUPT )
      && tr_bencDictFindInt( &top, KEY_CORRUPT, &i ) )
    {
        tor->corruptPrev = i;
        fieldsLoaded |= TR_FR_CORRUPT;
    }

    if( ( fieldsToLoad & ( TR_FR_PROGRESS | TR_FR_DOWNLOAD_DIR ) )
      && ( tr_bencDictFindStr( &top, KEY_DOWNLOAD_DIR, &str ) )
      && ( str && *str ) )
    {
        tr_free( tor->downloadDir );
        tor->downloadDir = tr_strdup( str );
        fieldsLoaded |= TR_FR_DOWNLOAD_DIR;
    }

    if( ( fieldsToLoad & ( TR_FR_PROGRESS | TR_FR_INCOMPLETE_DIR ) )
      && ( tr_bencDictFindStr( &top, KEY_INCOMPLETE_DIR, &str ) )
      && ( str && *str ) )
    {
        tr_free( tor->incompleteDir );
        tor->incompleteDir = tr_strdup( str );
        fieldsLoaded |= TR_FR_INCOMPLETE_DIR;
    }

    if( ( fieldsToLoad & TR_FR_DOWNLOADED )
      && tr_bencDictFindInt( &top, KEY_DOWNLOADED, &i ) )
    {
        tor->downloadedPrev = i;
        fieldsLoaded |= TR_FR_DOWNLOADED;
    }

    if( ( fieldsToLoad & TR_FR_UPLOADED )
      && tr_bencDictFindInt( &top, KEY_UPLOADED, &i ) )
    {
        tor->uploadedPrev = i;
        fieldsLoaded |= TR_FR_UPLOADED;
    }

    if( ( fieldsToLoad & TR_FR_MAX_PEERS )
      && tr_bencDictFindInt( &top, KEY_MAX_PEERS, &i ) )
    {
        tor->maxConnectedPeers = i;
        fieldsLoaded |= TR_FR_MAX_PEERS;
    }

    if( ( fieldsToLoad & TR_FR_RUN )
      && tr_bencDictFindBool( &top, KEY_PAUSED, &boolVal ) )
    {
        tor->isRunning = !boolVal;
        fieldsLoaded |= TR_FR_RUN;
    }

    if( ( fieldsToLoad & TR_FR_ADDED_DATE )
      && tr_bencDictFindInt( &top, KEY_ADDED_DATE, &i ) )
    {
        tor->addedDate = i;
        fieldsLoaded |= TR_FR_ADDED_DATE;
    }

    if( ( fieldsToLoad & TR_FR_DONE_DATE )
      && tr_bencDictFindInt( &top, KEY_DONE_DATE, &i ) )
    {
        tor->doneDate = i;
        fieldsLoaded |= TR_FR_DONE_DATE;
    }

    if( ( fieldsToLoad & TR_FR_ACTIVITY_DATE )
      && tr_bencDictFindInt( &top, KEY_ACTIVITY_DATE, &i ) )
    {
        tr_torrentSetActivityDate( tor, i );
        fieldsLoaded |= TR_FR_ACTIVITY_DATE;
    }

    if( ( fieldsToLoad & TR_FR_TIME_SEEDING )
      && tr_bencDictFindInt( &top, KEY_TIME_SEEDING, &i ) )
    {
        tor->secondsSeeding = i;
        fieldsLoaded |= TR_FR_TIME_SEEDING;
    }

    if( ( fieldsToLoad & TR_FR_TIME_DOWNLOADING )
      && tr_bencDictFindInt( &top, KEY_TIME_DOWNLOADING, &i ) )
    {
        tor->secondsDownloading = i;
        fieldsLoaded |= TR_FR_TIME_DOWNLOADING;
    }

    if( ( fieldsToLoad & TR_FR_BANDWIDTH_PRIORITY )
      && tr_bencDictFindInt( &top, KEY_BANDWIDTH_PRIORITY, &i )
      && tr_isPriority( i ) )
    {
        tr_torrentSetPriority( tor, i );
        fieldsLoaded |= TR_FR_BANDWIDTH_PRIORITY;
    }

    if( fieldsToLoad & TR_FR_PEERS )
        fieldsLoaded |= loadPeers( &top, tor );

    if( fieldsToLoad & TR_FR_FILE_PRIORITIES )
        fieldsLoaded |= loadFilePriorities( &top, tor );

    if( fieldsToLoad & TR_FR_PROGRESS )
        fieldsLoaded |= loadProgress( &top, tor );

    if( fieldsToLoad & TR_FR_DND )
        fieldsLoaded |= loadDND( &top, tor );

    if( fieldsToLoad & TR_FR_SPEEDLIMIT )
        fieldsLoaded |= loadSpeedLimits( &top, tor );

    if( fieldsToLoad & TR_FR_RATIOLIMIT )
        fieldsLoaded |= loadRatioLimits( &top, tor );

    if( fieldsToLoad & TR_FR_IDLELIMIT )
        fieldsLoaded |= loadIdleLimits( &top, tor );

    /* loading the resume file triggers of a lot of changes,
     * but none of them needs to trigger a re-saving of the
     * same resume information... */
    tor->isDirty = wasDirty;

    tr_bencFree( &top );
    tr_free( filename );
    return fieldsLoaded;
}

static uint64_t
setFromCtor( tr_torrent *    tor,
             uint64_t        fields,
             const tr_ctor * ctor,
             int             mode )
{
    uint64_t ret = 0;

    if( fields & TR_FR_RUN )
    {
        uint8_t isPaused;
        if( !tr_ctorGetPaused( ctor, mode, &isPaused ) )
        {
            tor->isRunning = !isPaused;
            ret |= TR_FR_RUN;
        }
    }

    if( fields & TR_FR_MAX_PEERS )
        if( !tr_ctorGetPeerLimit( ctor, mode, &tor->maxConnectedPeers ) )
            ret |= TR_FR_MAX_PEERS;

    if( fields & TR_FR_DOWNLOAD_DIR )
    {
        const char * path;
        if( !tr_ctorGetDownloadDir( ctor, mode, &path ) && path && *path )
        {
            ret |= TR_FR_DOWNLOAD_DIR;
            tr_free( tor->downloadDir );
            tor->downloadDir = tr_strdup( path );
        }
    }

    return ret;
}

static uint64_t
useManditoryFields( tr_torrent *    tor,
                    uint64_t        fields,
                    const tr_ctor * ctor )
{
    return setFromCtor( tor, fields, ctor, TR_FORCE );
}

static uint64_t
useFallbackFields( tr_torrent *    tor,
                   uint64_t        fields,
                   const tr_ctor * ctor )
{
    return setFromCtor( tor, fields, ctor, TR_FALLBACK );
}

uint64_t
tr_torrentLoadResume( tr_torrent *    tor,
                      uint64_t        fieldsToLoad,
                      const tr_ctor * ctor )
{
    uint64_t ret = 0;

    assert( tr_isTorrent( tor ) );

    ret |= useManditoryFields( tor, fieldsToLoad, ctor );
    fieldsToLoad &= ~ret;
    ret |= loadFromFile( tor, fieldsToLoad );
    fieldsToLoad &= ~ret;
    ret |= useFallbackFields( tor, fieldsToLoad, ctor );

    return ret;
}

void
tr_torrentRemoveResume( const tr_torrent * tor )
{
    char * filename = getResumeFilename( tor );
    unlink( filename );
    tr_free( filename );
}

