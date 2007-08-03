/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include "foreach.h"
#include "torrent-filter.h"

int
TorrentFilter :: GetFlags( const tr_torrent_t * tor )
{
    int ret = 0;
    const tr_stat_t * s = tr_torrentStat( (tr_torrent_t*)tor );

    if( s->rateUpload > 0.01 ) ret |= FLAG_UPLOADING;
    if( s->rateDownload > 0.01 ) ret |= FLAG_DOWNLOADING;
    if( !ret ) ret |= (s->status & TR_STATUS_ACTIVE ) ? FLAG_IDLE : FLAG_STOPPED;
    ret |= (s->left ? FLAG_INCOMPLETE : FLAG_COMPLETE);

    return ret;
}

void
TorrentFilter :: CountHits( const torrents_v & torrents,
                            int              * counts )
{
    memset( counts, '\0', sizeof(int) * N_FILTERS );
    foreach_const( torrents_v, torrents, it ) {
        const int flags = GetFlags( *it );
        if( flags & FLAG_UPLOADING )    ++counts[UPLOADING];
        if( flags & FLAG_DOWNLOADING )  ++counts[DOWNLOADING];
        if( flags & FLAG_IDLE )         ++counts[IDLE];
        if( flags & FLAG_STOPPED )      ++counts[STOPPED];
        if( flags & FLAG_COMPLETE )     ++counts[COMPLETE];
        if( flags & FLAG_INCOMPLETE )   ++counts[INCOMPLETE];
    }
}

wxString
TorrentFilter :: GetName( int show, int count )
{
    wxString xstr;

    switch( show )
    {
        case UPLOADING:   xstr = _("&Uploading");   break;
        case DOWNLOADING: xstr = _("&Uploading");   break;
        case IDLE:        xstr = _("&Idle");        break;
        case STOPPED:     xstr = _("&Stopped");     break;
        case COMPLETE:    xstr = _("&Done");    break;
        case INCOMPLETE:  xstr = _("&Not Done");  break;
        default: assert(0);
    }

    xstr += wxString::Format(_T(" (%d)"), count );

    return xstr;
}


void
TorrentFilter :: RemoveFailures( int flags, torrents_v& torrents )
{
    torrents_v tmp;

    for( torrents_v::iterator it(torrents.begin()), end(torrents.end()); it!=end; ++it )
        if( flags & GetFlags ( *it ) )
            tmp.push_back( *it );

    torrents.swap( tmp );
}
