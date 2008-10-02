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

#include <unistd.h> /* unlink */

#include <string.h>

#include "transmission.h"
#include "bencode.h"
#include "completion.h"
#include "fastresume.h"
#include "peer-mgr.h" /* pex */
#include "platform.h" /* tr_getResumeDir */
#include "resume.h"
#include "torrent.h"
#include "utils.h" /* tr_buildPath */

#define KEY_ACTIVITY_DATE   "activity-date"
#define KEY_ADDED_DATE      "added-date"
#define KEY_CORRUPT         "corrupt"
#define KEY_DONE_DATE       "done-date"
#define KEY_DOWNLOAD_DIR    "destination"
#define KEY_DND             "dnd"
#define KEY_DOWNLOADED      "downloaded"
#define KEY_MAX_PEERS       "max-peers"
#define KEY_PAUSED          "paused"
#define KEY_PEERS           "peers"
#define KEY_PRIORITY        "priority"
#define KEY_PROGRESS        "progress"
#define KEY_SPEEDLIMIT      "speed-limit"
#define KEY_UPLOADED        "uploaded"

#define KEY_SPEEDLIMIT_DOWN_SPEED "down-speed"
#define KEY_SPEEDLIMIT_DOWN_MODE  "down-mode"
#define KEY_SPEEDLIMIT_UP_SPEED   "up-speed"
#define KEY_SPEEDLIMIT_UP_MODE    "up-mode"

#define KEY_PROGRESS_MTIMES   "mtimes"
#define KEY_PROGRESS_BITFIELD "bitfield"

static char*
getResumeFilename( const tr_torrent * tor )
{
    return tr_strdup_printf( "%s%c%s.%16.16s.resume",
                             tr_getResumeDir( tor->session ),
                             TR_PATH_DELIMITER,
                             tor->info.name,
                             tor->info.hashString );
}

/***
****
***/

static void
savePeers( tr_benc *          dict,
           const tr_torrent * tor )
{
    tr_pex *  pex = NULL;
    const int count = tr_peerMgrGetPeers( tor->session->peerMgr,
                                          tor->info.hash, &pex );

    if( count > 0 )
        tr_bencDictAddRaw( dict, KEY_PEERS, pex, sizeof( tr_pex ) * count );
    tr_free( pex );
}

static uint64_t
loadPeers( tr_benc *    dict,
           tr_torrent * tor )
{
    uint64_t        ret = 0;
    const uint8_t * str;
    size_t          len;

    if( tr_bencDictFindRaw( dict, KEY_PEERS, &str, &len ) )
    {
        int       i;
        const int count = len / sizeof( tr_pex );
        for( i = 0; i < count; ++i )
        {
            tr_pex pex;
            memcpy( &pex, str + ( i * sizeof( tr_pex ) ), sizeof( tr_pex ) );
            tr_peerMgrAddPex( tor->session->peerMgr,
                              tor->info.hash, TR_PEER_FROM_CACHE, &pex );
        }
        tr_tordbg( tor, "Loaded %d peers from resume file", count );
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
        ret = TR_FR_PRIORITY;
    }
    else
    {
        tr_tordbg(
            tor,
            "Couldn't load DND flags.  dnd list (%p) has %zu children; torrent has %d files",
            list, tr_bencListSize( list ), (int)n );
    }

    return ret;
}

/***
****
***/

static void
savePriorities( tr_benc *          dict,
                const tr_torrent * tor )
{
    const tr_info *       inf = tr_torrentInfo( tor );
    const tr_file_index_t n = inf->fileCount;
    tr_file_index_t       i;
    tr_benc *             list;

    list = tr_bencDictAddList( dict, KEY_PRIORITY, n );
    for( i = 0; i < n; ++i )
        tr_bencListAddInt( list, inf->files[i].priority );
}

static uint64_t
loadPriorities( tr_benc *    dict,
                tr_torrent * tor )
{
    uint64_t              ret = 0;
    tr_info *             inf = &tor->info;
    const tr_file_index_t n = inf->fileCount;
    tr_benc *             list;

    if( tr_bencDictFindList( dict, KEY_PRIORITY, &list )
      && ( tr_bencListSize( list ) == n ) )
    {
        int64_t         tmp;
        tr_file_index_t i;
        for( i = 0; i < n; ++i )
            if( tr_bencGetInt( tr_bencListChild( list, i ), &tmp ) )
                inf->files[i].priority = tmp;
        ret = TR_FR_PRIORITY;
    }

    return ret;
}

/***
****
***/

static void
saveSpeedLimits( tr_benc *          dict,
                 const tr_torrent * tor )
{
    tr_benc * d = tr_bencDictAddDict( dict, KEY_SPEEDLIMIT, 4 );

    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_DOWN_SPEED,
                      tr_torrentGetSpeedLimit( tor, TR_DOWN ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_DOWN_MODE,
                      tr_torrentGetSpeedMode( tor, TR_DOWN ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_UP_SPEED,
                      tr_torrentGetSpeedLimit( tor, TR_UP ) );
    tr_bencDictAddInt( d, KEY_SPEEDLIMIT_UP_MODE,
                      tr_torrentGetSpeedMode( tor, TR_UP ) );
}

static uint64_t
loadSpeedLimits( tr_benc *    dict,
                 tr_torrent * tor )
{
    uint64_t  ret = 0;
    tr_benc * d;

    if( tr_bencDictFindDict( dict, KEY_SPEEDLIMIT, &d ) )
    {
        int64_t i;
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_SPEED, &i ) )
            tr_torrentSetSpeedLimit( tor, TR_DOWN, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_DOWN_MODE, &i ) )
            tr_torrentSetSpeedMode( tor, TR_DOWN, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_SPEED, &i ) )
            tr_torrentSetSpeedLimit( tor, TR_UP, i );
        if( tr_bencDictFindInt( d, KEY_SPEEDLIMIT_UP_MODE, &i ) )
            tr_torrentSetSpeedMode( tor, TR_UP, i );
        ret = TR_FR_SPEEDLIMIT;
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
    time_t *            mtimes;
    tr_benc *           p;
    tr_benc *           m;
    const tr_bitfield * bitfield;

    p = tr_bencDictAdd( dict, KEY_PROGRESS );
    tr_bencInitDict( p, 2 );

    /* add the mtimes */
    mtimes = tr_torrentGetMTimes( tor, &n );
    m = tr_bencDictAddList( p, KEY_PROGRESS_MTIMES, n );
    for( i = 0; i < n; ++i )
    {
        if( !tr_torrentIsFileChecked( tor, i ) )
            mtimes[i] = ~(time_t)0; /* force a recheck */
        tr_bencListAddInt( m, mtimes[i] );
    }

    /* add the bitfield */
    bitfield = tr_cpBlockBitfield( tor->completion );
    tr_bencDictAddRaw( p, KEY_PROGRESS_BITFIELD,
                       bitfield->bits, bitfield->byteCount );

    /* cleanup */
    tr_free( mtimes );
}

static uint64_t
loadProgress( tr_benc *    dict,
              tr_torrent * tor )
{
    uint64_t  ret = 0;
    tr_benc * p;

    if( tr_bencDictFindDict( dict, KEY_PROGRESS, &p ) )
    {
        const uint8_t * raw;
        size_t          rawlen;
        tr_benc *       m;
        size_t          n;
        time_t *        curMTimes = tr_torrentGetMTimes( tor, &n );

        if( tr_bencDictFindList( p, KEY_PROGRESS_MTIMES, &m )
          && ( n == tor->info.fileCount )
          && ( n == tr_bencListSize( m ) ) )
        {
            size_t i;
            for( i = 0; i < n; ++i )
            {
                int64_t tmp;
                if( !tr_bencGetInt( tr_bencListChild( m, i ), &tmp ) )
                {
                    tr_tordbg(
                        tor,
                        "File #%zu needs to be verified - couldn't find benc entry",
                        i );
                    tr_torrentSetFileChecked( tor, i, FALSE );
                }
                else
                {
                    const time_t t = (time_t) tmp;
                    if( t == curMTimes[i] )
                        tr_torrentSetFileChecked( tor, i, TRUE );
                    else
                    {
                        tr_tordbg(
                            tor,
                            "File #%zu needs to be verified - times %lu and %lu don't match",
                            i, t, curMTimes[i] );
                        tr_torrentSetFileChecked( tor, i, FALSE );
                    }
                }
            }
        }
        else
        {
            tr_torrentUncheck( tor );
            tr_tordbg(
                tor, "Torrent needs to be verified - unable to find mtimes" );
        }

        if( tr_bencDictFindRaw( p, KEY_PROGRESS_BITFIELD, &raw, &rawlen ) )
        {
            tr_bitfield tmp;
            tmp.byteCount = rawlen;
            tmp.bitCount = tmp.byteCount * 8;
            tmp.bits = (uint8_t*) raw;
            if( !tr_cpBlockBitfieldSet( tor->completion, &tmp ) )
            {
                tr_torrentUncheck( tor );
                tr_tordbg(
                    tor,
                    "Torrent needs to be verified - error loading bitfield" );
            }
        }
        else
        {
            tr_torrentUncheck( tor );
            tr_tordbg(
                tor,
                "Torrent needs to be verified - unable to find bitfield" );
        }

        tr_free( curMTimes );
        ret = TR_FR_PROGRESS;
    }

    return ret;
}

/***
****
***/

void
tr_torrentSaveResume( const tr_torrent * tor )
{
    tr_benc top;
    char *  filename;

    if( !tor )
        return;

    tr_bencInitDict( &top, 14 );
    tr_bencDictAddInt( &top, KEY_ACTIVITY_DATE,
                       tor->activityDate );
    tr_bencDictAddInt( &top, KEY_ADDED_DATE,
                       tor->addedDate );
    tr_bencDictAddInt( &top, KEY_CORRUPT,
                       tor->corruptPrev + tor->corruptCur );
    tr_bencDictAddInt( &top, KEY_DONE_DATE,
                       tor->doneDate );
    tr_bencDictAddStr( &top, KEY_DOWNLOAD_DIR,
                       tor->downloadDir );
    tr_bencDictAddInt( &top, KEY_DOWNLOADED,
                       tor->downloadedPrev + tor->downloadedCur );
    tr_bencDictAddInt( &top, KEY_UPLOADED,
                       tor->uploadedPrev + tor->uploadedCur );
    tr_bencDictAddInt( &top, KEY_MAX_PEERS,
                       tor->maxConnectedPeers );
    tr_bencDictAddInt( &top, KEY_PAUSED,
                       tor->isRunning ? 0 : 1 );
    savePeers( &top, tor );
    savePriorities( &top, tor );
    saveDND( &top, tor );
    saveProgress( &top, tor );
    saveSpeedLimits( &top, tor );

    filename = getResumeFilename( tor );
    tr_bencSaveFile( filename, &top );
    tr_free( filename );

    tr_bencFree( &top );
}

static uint64_t
loadFromFile( tr_torrent * tor,
              uint64_t     fieldsToLoad )
{
    int64_t      i;
    const char * str;
    uint64_t     fieldsLoaded = 0;
    char *       filename;
    tr_benc      top;

    filename = getResumeFilename( tor );

    if( tr_bencLoadFile( filename, &top ) )
    {
        tr_tordbg( tor, "Couldn't read \"%s\"; trying old format.",
                   filename );
        fieldsLoaded = tr_fastResumeLoad( tor, fieldsToLoad );

        if( ( fieldsLoaded != 0 ) && ( fieldsToLoad == ~(uint64_t)0 ) )
        {
            tr_torrentSaveResume( tor );
            tr_fastResumeRemove( tor );
            tr_tordbg( tor, "Migrated resume file to \"%s\"", filename );
        }

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
      && tr_bencDictFindStr( &top, KEY_DOWNLOAD_DIR, &str ) )
    {
        tr_free( tor->downloadDir );
        tor->downloadDir = tr_strdup( str );
        fieldsLoaded |= TR_FR_DOWNLOAD_DIR;
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
      && tr_bencDictFindInt( &top, KEY_PAUSED, &i ) )
    {
        tor->isRunning = i ? 0 : 1;
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
        tor->activityDate = i;
        fieldsLoaded |= TR_FR_ACTIVITY_DATE;
    }

    if( fieldsToLoad & TR_FR_PEERS )
        fieldsLoaded |= loadPeers( &top, tor );

    if( fieldsToLoad & TR_FR_PRIORITY )
        fieldsLoaded |= loadPriorities( &top, tor );

    if( fieldsToLoad & TR_FR_PROGRESS )
        fieldsLoaded |= loadProgress( &top, tor );

    if( fieldsToLoad & TR_FR_DND )
        fieldsLoaded |= loadDND( &top, tor );

    if( fieldsToLoad & TR_FR_SPEEDLIMIT )
        fieldsLoaded |= loadSpeedLimits( &top, tor );

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
        const char * downloadDir;
        if( !tr_ctorGetDownloadDir( ctor, mode, &downloadDir ) )
        {
            ret |= TR_FR_DOWNLOAD_DIR;
            tr_free( tor->downloadDir );
            tor->downloadDir = tr_strdup( downloadDir );
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
    tr_fastResumeRemove( tor );
    tr_free( filename );
}

