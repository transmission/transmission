/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include "transmission.h"
#include "bencode.h"
#include "platform.h" /* tr_sessionGetConfigDir() */
#include "utils.h" /* tr_buildPath */

/***
****
***/

struct tr_stats_handle
{
    tr_session_stats single;
    tr_session_stats old;
    time_t startTime;
};

static char*
getFilename( const tr_handle * handle, char * buf, size_t buflen )
{
    tr_buildPath( buf, buflen, tr_sessionGetConfigDir(handle),
                               "stats.benc",
                               NULL );
    return buf;
}

static void
loadCumulativeStats( const tr_handle * handle, tr_session_stats * setme )
{
    char filename[MAX_PATH_LENGTH];
    tr_benc top;

    getFilename( handle, filename, sizeof(filename) );
    if( !tr_bencLoadFile( filename, &top ) )
    {
        int64_t i;

        if( tr_bencDictFindInt( &top, "uploaded-bytes", &i ) )
            setme->uploadedBytes = (uint64_t) i;

        if( tr_bencDictFindInt( &top, "downloaded-bytes", &i ) )
            setme->downloadedBytes = (uint64_t) i;

        if( tr_bencDictFindInt( &top, "files-added", &i ) )
            setme->filesAdded = (uint64_t) i;

        if( tr_bencDictFindInt( &top, "session-count", &i ) )
            setme->sessionCount = (uint64_t) i;

        if( tr_bencDictFindInt( &top, "seconds-active", &i ) )
            setme->secondsActive = (uint64_t) i;

        tr_bencFree( &top );
    }
}

static void
saveCumulativeStats( const tr_handle * handle, const tr_session_stats * s )
{
    char filename[MAX_PATH_LENGTH];
    tr_benc top;

    tr_bencInitDict( &top, 5 );
    tr_bencDictAddInt( &top, "uploaded-bytes",   s->uploadedBytes );
    tr_bencDictAddInt( &top, "downloaded-bytes", s->downloadedBytes );
    tr_bencDictAddInt( &top, "files-added",      s->filesAdded );
    tr_bencDictAddInt( &top, "session-count",    s->sessionCount );
    tr_bencDictAddInt( &top, "seconds-active",   s->secondsActive );

    getFilename( handle, filename, sizeof(filename) );
    tr_deepLog( __FILE__, __LINE__, NULL, "Saving stats to \"%s\"", filename );
    tr_bencSaveFile( filename, &top );

    tr_bencFree( &top );
}

/***
****
***/

void
tr_statsInit( tr_handle * handle )
{
    struct tr_stats_handle * stats = tr_new0( struct tr_stats_handle, 1 );
    loadCumulativeStats( handle, &stats->old );
    stats->single.sessionCount = 1;
    stats->startTime = time( NULL );
    handle->sessionStats = stats;
}

void
tr_statsClose( tr_handle * handle )
{
    tr_session_stats cumulative;
    tr_sessionGetCumulativeStats( handle, &cumulative );
    saveCumulativeStats( handle, &cumulative );

    tr_free( handle->sessionStats );
    handle->sessionStats = NULL;
}

static struct tr_stats_handle *
getStats( const tr_handle * handle )
{
    static struct tr_stats_handle nullObject;

    return handle && handle->sessionStats
        ? handle->sessionStats
        : &nullObject;
}

/***
****
***/

static void
updateRatio( tr_session_stats * setme )
{
    setme->ratio = tr_getRatio( setme->uploadedBytes,
                                setme->downloadedBytes );
}

static void
addStats( tr_session_stats       * setme,
          const tr_session_stats * a,
          const tr_session_stats * b )
{
    setme->uploadedBytes   = a->uploadedBytes   + b->uploadedBytes;
    setme->downloadedBytes = a->downloadedBytes + b->downloadedBytes;
    setme->filesAdded      = a->filesAdded      + b->filesAdded;
    setme->sessionCount    = a->sessionCount    + b->sessionCount;
    setme->secondsActive   = a->secondsActive   + b->secondsActive;
    updateRatio( setme );
}

void
tr_sessionGetStats( const tr_handle   * handle,
                    tr_session_stats  * setme )
{
    const struct tr_stats_handle * stats = getStats( handle );
    *setme = stats->single;
    setme->secondsActive = time( NULL ) - stats->startTime;
    updateRatio( setme );
}

void
tr_sessionGetCumulativeStats( const tr_handle   * handle,
                              tr_session_stats  * setme )
{
    tr_session_stats current;
    tr_sessionGetStats( handle, &current );
    addStats( setme, &getStats(handle)->old, &current );
}

void
tr_sessionClearStats( tr_handle * handle )
{
    tr_session_stats zero;
    zero.uploadedBytes = 0;
    zero.downloadedBytes = 0;
    zero.ratio = TR_RATIO_NA;
    zero.filesAdded = 0;
    zero.sessionCount = 0;
    zero.secondsActive = 0;
    handle->sessionStats->single = handle->sessionStats->old = zero;

    handle->sessionStats->startTime = time( NULL );
}

/**
***
**/

void
tr_statsAddUploaded( tr_handle * handle, uint32_t bytes )
{
    getStats(handle)->single.uploadedBytes += bytes;
}

void
tr_statsAddDownloaded( tr_handle * handle, uint32_t bytes )
{
    getStats(handle)->single.downloadedBytes += bytes;
}

void
tr_statsFileCreated( tr_handle * handle )
{
    getStats(handle)->single.filesAdded++;
}
