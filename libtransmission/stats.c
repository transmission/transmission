/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id: peer-msgs.c 3906 2007-11-20 17:29:56Z charles $
 */

#include <string.h> /* memset */

#include "transmission.h"
#include "bencode.h"
#include "platform.h" /* tr_getPrefsDirectory */
#include "utils.h" /* tr_buildPath */

/***
****
***/

struct tr_stats_handle
{
    tr_session_stats single;
    tr_session_stats cumulative;
    time_t startTime;
};

static void
parseCumulativeStats( tr_session_stats  * setme,
                      const uint8_t     * content,
                      size_t              len )
{
    benc_val_t top;

    if( !tr_bencLoad( content, len, &top, NULL ) )
    {
        const benc_val_t * val;

        if(( val = tr_bencDictFindType( &top, "uploaded-bytes", TYPE_INT )))
            setme->uploadedBytes = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "downloaded-bytes", TYPE_INT )))
            setme->downloadedBytes = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "files-added", TYPE_INT )))
            setme->filesAdded = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "session-count", TYPE_INT )))
            setme->sessionCount = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "seconds-active", TYPE_INT )))
            setme->secondsActive = (uint64_t) tr_bencGetInt( val );

        tr_bencFree( &top );
    }
}

static char*
getFilename( char * buf, size_t buflen )
{
    tr_buildPath( buf, buflen, tr_getPrefsDirectory(), "stats.benc", NULL );
    return buf;
}

static void
loadCumulativeStats( tr_session_stats * setme )
{
    size_t len;
    uint8_t * content;
    char filename[MAX_PATH_LENGTH];

    getFilename( filename, sizeof(filename) );
    content = tr_loadFile( filename, &len );
    if( content != NULL )
        parseCumulativeStats( setme, content, len );

    tr_free( content );
}

static void
saveCumulativeStats( const tr_session_stats * stats )
{
    FILE * fp;
    char * str;
    char filename[MAX_PATH_LENGTH];
    int len;
    benc_val_t top;

    tr_bencInit( &top, TYPE_DICT );
    tr_bencDictReserve( &top, 5 );
    tr_bencInitInt( tr_bencDictAdd( &top, "uploaded-bytes" ), stats->uploadedBytes );
    tr_bencInitInt( tr_bencDictAdd( &top, "downloaded-bytes" ), stats->downloadedBytes );
    tr_bencInitInt( tr_bencDictAdd( &top, "files-added" ), stats->filesAdded );
    tr_bencInitInt( tr_bencDictAdd( &top, "session-count" ), stats->sessionCount );
    tr_bencInitInt( tr_bencDictAdd( &top, "seconds-active" ), stats->secondsActive );

    str = tr_bencSave( &top, &len );
    getFilename( filename, sizeof(filename) );
    fp = fopen( filename, "wb+" );
    fwrite( str, 1, len, fp );
    fclose( fp );
    tr_free( str );

    tr_bencFree( &top );
}

/***
****
***/

void
tr_statsInit( tr_handle * handle )
{
    struct tr_stats_handle * stats = tr_new0( struct tr_stats_handle, 1 );
    loadCumulativeStats( &stats->cumulative );
    stats->cumulative.sessionCount++;
    stats->startTime = time(NULL);
    handle->sessionStats = stats;
}

void
tr_statsClose( tr_handle * handle )
{
    tr_session_stats tmp;
    tr_getCumulativeSessionStats( handle, &tmp );
    saveCumulativeStats( &tmp );

    tr_free( handle->sessionStats );
    handle->sessionStats = NULL;
}

void
tr_getSessionStats( const tr_handle   * handle,
                    tr_session_stats  * setme )
{
    const struct tr_stats_handle * stats = handle->sessionStats;
    *setme = stats->single;
    setme->ratio = setme->downloadedBytes ? (double)setme->uploadedBytes / (double)setme->downloadedBytes : TR_RATIO_NA;
    setme->secondsActive += ( time(NULL) - stats->startTime );
}

void
tr_getCumulativeSessionStats( const tr_handle   * handle,
                              tr_session_stats  * setme )
{
    const struct tr_stats_handle * stats = handle->sessionStats;
    *setme = stats->cumulative;
    setme->ratio = setme->downloadedBytes ? (double)setme->uploadedBytes / (double)setme->downloadedBytes : TR_RATIO_NA;
    setme->secondsActive += ( time(NULL) - stats->startTime );
}

/**
***
**/

void
tr_statsAddUploaded( tr_handle * handle, uint32_t bytes )
{
    struct tr_stats_handle * stats = handle->sessionStats;
    stats->single.uploadedBytes += bytes;
    stats->cumulative.uploadedBytes += bytes;
}

void
tr_statsAddDownloaded( tr_handle * handle, uint32_t bytes )
{
    struct tr_stats_handle * stats = handle->sessionStats;
    stats->single.downloadedBytes += bytes;
    stats->cumulative.downloadedBytes += bytes;
}

void
tr_torrentAdded( tr_handle * handle, const tr_torrent * torrent )
{
    struct tr_stats_handle * stats = handle->sessionStats;
    stats->cumulative.filesAdded += torrent->info.fileCount;
    stats->single.filesAdded += torrent->info.fileCount;
}
