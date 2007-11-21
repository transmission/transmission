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

static void
parseCumulativeStats( tr_session_stats  * setme,
                      const uint8_t     * content,
                      size_t              len )
{
    benc_val_t top;

    if( !tr_bencLoad( content, len, &top, NULL ) )
    {
        const benc_val_t * val;

        if(( val = tr_bencDictFindType( &top, "uploaded-gigabytes", TYPE_INT )))
            setme->uploadedGigs = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "uploaded-bytes", TYPE_INT )))
            setme->uploadedBytes = (uint64_t) tr_bencGetInt( val );

        if(( val = tr_bencDictFindType( &top, "downloaded-gigabytes", TYPE_INT )))
            setme->downloadedGigs = (uint64_t) tr_bencGetInt( val );

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

static void
loadCumulativeStats( tr_session_stats * setme )
{
    size_t len;
    uint8_t * content;
    char path[MAX_PATH_LENGTH];

    tr_buildPath( path, sizeof(path), tr_getPrefsDirectory(), "stats.benc", NULL );
    content = tr_loadFile( path, &len );
    if( content != NULL )
        parseCumulativeStats( setme, content, len );

    tr_free( content );
}

/***
****
***/

void
tr_statsInit( tr_handle * handle )
{
    memset( &handle->sessionStats, 0, sizeof( tr_session_stats ) );
    memset( &handle->cumulativeStats, 0, sizeof( tr_session_stats ) );

    loadCumulativeStats( &handle->cumulativeStats );
}

void
tr_statsClose( const tr_handle * handle UNUSED )
{
    fprintf( stderr, "FIXME" );
}

static void
updateRatio( tr_session_stats * stats UNUSED )
{
    fprintf( stderr, "FIXME" );
}

void
tr_getSessionStats( const tr_handle   * handle,
                    tr_session_stats  * setme )
{
    *setme = handle->sessionStats;
    updateRatio( setme );
}

void
tr_getCumulativeSessionStats( const tr_handle   * handle,
                              tr_session_stats  * setme )
{
    *setme = handle->cumulativeStats;
    updateRatio( setme );
}

/**
***
**/

static void
add( uint64_t * gigs, uint64_t * bytes, uint32_t addme )
{
    uint64_t i;
    const uint64_t GIGABYTE = 1073741824;
    i = *bytes;
    i += addme;
    *gigs += i / GIGABYTE;
    *bytes = i % GIGABYTE;
}

void
tr_statsAddUploaded( tr_handle * handle, uint32_t bytes )
{
    add( &handle->sessionStats.uploadedGigs,
         &handle->sessionStats.uploadedBytes, bytes );
    add( &handle->cumulativeStats.uploadedGigs,
         &handle->cumulativeStats.uploadedBytes, bytes );
}

void
tr_statsAddDownloaded( tr_handle * handle, uint32_t bytes )
{
    add( &handle->sessionStats.downloadedGigs,
         &handle->sessionStats.downloadedBytes, bytes );
    add( &handle->cumulativeStats.downloadedGigs,
         &handle->cumulativeStats.downloadedBytes, bytes );
}
