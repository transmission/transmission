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

#include <stdlib.h> /* qsort */
#include <string.h> /* memcpy */

#include "transmission.h"
#include "rpc-utils.h"
#include "torrent.h"
#include "utils.h"

/****
*****
****/

static int
compareTorrentsByActivity( const void * a, const void * b )
{
    const tr_stat * sa = tr_torrentStatCached( (tr_torrent*) a );
    const tr_stat * sb = tr_torrentStatCached( (tr_torrent*) b );
    int i;
    if(( i = tr_compareDouble( sa->rateUpload + sa->rateDownload,
                               sb->rateUpload + sb->rateDownload ) ))
        return i;
    if( sa->uploadedEver != sb->uploadedEver )
        return sa->uploadedEver < sa->uploadedEver ? -1 : 1;
    return 0;
}

static int
compareTorrentsByAge( const void * a, const void * b )
{
    return tr_compareTime( tr_torrentStatCached( (tr_torrent*)a )->addedDate,
                           tr_torrentStatCached( (tr_torrent*)b )->addedDate );
}

static int
compareTorrentsByID( const void * a, const void * b )
{
    return ((tr_torrent*)a)->uniqueId - ((tr_torrent*)b)->uniqueId;
}

static int
compareTorrentsByName( const void * a, const void * b )
{
    const tr_torrent * ta = a;
    const tr_torrent * tb = b;
    return tr_strcasecmp( ta->info.name, tb->info.name );
}

static int
compareRatio( double a, double b )
{
    if( (int)a == TR_RATIO_INF && (int)b == TR_RATIO_INF ) return 0;
    if( (int)a == TR_RATIO_INF ) return 1;
    if( (int)b == TR_RATIO_INF ) return -1;
    return tr_compareDouble( a, b );
}

static int
compareTorrentsByProgress( const void * a, const void * b )
{
    const tr_stat * sa = tr_torrentStatCached( (tr_torrent*) a );
    const tr_stat * sb = tr_torrentStatCached( (tr_torrent*) b );
    int ret = tr_compareDouble( sa->percentDone, sb->percentDone );
    if( !ret )
        ret = compareRatio( sa->ratio, sb->ratio );
    return ret;
}

static int
compareTorrentsByRatio( const void * a, const void * b )
{
    const tr_stat * sa = tr_torrentStatCached( (tr_torrent*) a );
    const tr_stat * sb = tr_torrentStatCached( (tr_torrent*) b );
    return compareRatio( sa->ratio, sb->ratio );
}

static int
compareTorrentsByState( const void * a, const void * b )
{
    const tr_stat * sa = tr_torrentStatCached( (tr_torrent*) a );
    const tr_stat * sb = tr_torrentStatCached( (tr_torrent*) b );
    int ret = sa->status - sb->status;
    if( !ret )
        ret = compareTorrentsByRatio( a, b );
    return 0;
}

static int
compareTorrentsByTracker( const void * a, const void * b )
{
    const tr_stat * sa = tr_torrentStatCached( (tr_torrent*) a );
    const tr_stat * sb = tr_torrentStatCached( (tr_torrent*) b );
    return tr_strcmp( sa->announceURL, sb->announceURL );
}

typedef int( *compareFunc )( const void *, const void * );

void
tr_torrentSort( tr_torrent     ** torrents,
                int               torrentCount,
                tr_sort_method    sortMethod,
                int               isAscending )
{
    compareFunc func = NULL;

    switch( sortMethod )
    {
        case TR_SORT_ACTIVITY: func = &compareTorrentsByActivity; break;
        case TR_SORT_AGE:      func = compareTorrentsByAge; break;
        case TR_SORT_NAME:     func = compareTorrentsByName; break;
        case TR_SORT_PROGRESS: func = compareTorrentsByProgress; break;
        case TR_SORT_RATIO:    func = compareTorrentsByRatio; break;
        case TR_SORT_STATE:    func = compareTorrentsByState; break;
        case TR_SORT_TRACKER:  func = compareTorrentsByTracker; break;
        default:               func = compareTorrentsByID; break;
    }

    qsort( torrents, torrentCount, sizeof(tr_torrent*), func );

    if( !isAscending )
    {
        int left = 0;
        int right = torrentCount - 1;
        while( left < right ) {
            tr_torrent * tmp = torrents[left];
            torrents[left] = torrents[right];
            torrents[right] = tmp;
            ++left;
            --right;
        }
    }
}

/****
*****
****/

static int
testActive( const tr_torrent * tor )
{
    const tr_stat * s = tr_torrentStatCached( ( tr_torrent * ) tor );
    return s->peersSendingToUs>0 || s->peersGettingFromUs>0;
}
static int
testStatus( const tr_torrent * tor, cp_status_t status )
{
    return tr_torrentGetStatus( ( tr_torrent * ) tor ) == status;
}
static int
testDownloading( const tr_torrent * tor )
{
    return testStatus( tor, TR_STATUS_DOWNLOAD );
}
static int
testSeeding( const tr_torrent * tor )
{
    return testStatus( tor, TR_STATUS_SEED );
}
static int
testPaused( const tr_torrent * tor )
{
    return testStatus( tor, TR_STATUS_STOPPED );
}
static int
testTrue( const tr_torrent * tor UNUSED )
{
    return TRUE;
}

typedef int( *test_func )( const tr_torrent * );

void
tr_torrentFilter( tr_torrent        ** torrents,
                  int                * torrentCount,
                  tr_filter_method     filterMethod )
{
    int i;
    int newCount = 0;
    test_func func;
    tr_torrent ** tmp = tr_new0( tr_torrent*, torrentCount );

    switch( filterMethod ) {
        case TR_FILTER_ACTIVE:       func = testActive; break;
        case TR_FILTER_DOWNLOADING:  func = testDownloading; break;
        case TR_FILTER_PAUSED:       func = testPaused; break;
        case TR_FILTER_SEEDING:      func = testSeeding; break;
        default:                     func = testTrue; break;
    }

    for( i=0; i<*torrentCount; ++i )
        if( func( torrents[i] ) )
            tmp[newCount++] = torrents[i];

    memcpy( torrents, tmp, sizeof(tr_torrent*) * newCount );
    *torrentCount = newCount;
    tr_free( tmp );
}
