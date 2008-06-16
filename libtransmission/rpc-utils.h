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

#ifndef RPC_UTILS_H
#define RPC_UTILS_H

typedef enum
{
    TR_SORT_ACTIVITY,
    TR_SORT_AGE,
    TR_SORT_ID,
    TR_SORT_NAME,
    TR_SORT_PROGRESS,
    TR_SORT_RATIO,
    TR_SORT_STATE,
    TR_SORT_TRACKER
}
tr_sort_method;

void tr_torrentSort( tr_torrent      ** torrents,
                     int                torrentCount,
                     tr_sort_method     sortMethod,
                     int                isAscending );

typedef enum
{
    TR_FILTER_ACTIVE,
    TR_FILTER_ALL,
    TR_FILTER_DOWNLOADING,
    TR_FILTER_PAUSED,
    TR_FILTER_SEEDING
}
tr_filter_method;

void tr_torrentFilter( tr_torrent        ** torrents,
                       int                * torrentCount,
                       tr_filter_method     filterMethod );
   
#endif 
