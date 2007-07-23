/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include "torrent-filter.h"

bool
TorrentFilter :: Test( int show, tr_torrent_t * tor )
{
    if( show == SHOW_ALL )
        return true;

    const tr_stat_t * stat = tr_torrentStat( tor );

    if( show == SHOW_DOWNLOADING )
        return stat->status == TR_STATUS_DOWNLOAD;

    if( show == SHOW_UPLOADING )
        return stat->status == TR_STATUS_SEED;

    if( show == SHOW_COMPLETE )
        return stat->cpStatus != TR_CP_INCOMPLETE;

    if( show == SHOW_INCOMPLETE )
        return stat->cpStatus == TR_CP_INCOMPLETE;

    if( show == SHOW_ACTIVE )
        return ( stat->rateUpload + stat->rateDownload ) >= 0.01;

    if( show == SHOW_INACTIVE )
        return ( stat->rateUpload + stat->rateDownload ) < 0.01;

    abort ();
}

int
TorrentFilter :: CountHits ( int show, const torrents_v& torrents )
{
    int i = 0;

    for( torrents_v::const_iterator it(torrents.begin()), end(torrents.end()); it!=end; ++it )
        if( Test( show, *it ) )
            ++i;

    return i;
}

void
TorrentFilter :: RemoveFailures( int show, torrents_v& torrents )
{
    torrents_v tmp;

    for( torrents_v::iterator it(torrents.begin()), end(torrents.end()); it!=end; ++it )
        if( Test( show, *it ) )
            tmp.push_back( *it );

    torrents.swap( tmp );
}


wxString
TorrentFilter :: getFilterName( int show )
{
    switch( show )
    {
        case SHOW_ALL:         return _("All");
        case SHOW_DOWNLOADING: return _("Downloading");
        case SHOW_UPLOADING:   return _("Uploading");
        case SHOW_COMPLETE:    return _("Complete");
        case SHOW_INCOMPLETE:  return _("Incomplete");
        case SHOW_ACTIVE:      return _("Active");
        case SHOW_INACTIVE:    return _("Inactive");
        default:               abort();
    }

    return _T(""); //notreached
}
