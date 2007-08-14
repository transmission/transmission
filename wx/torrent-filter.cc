/*
 * Xmission - a cross-platform bittorrent client
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "foreach.h"
#include "torrent-filter.h"

int
TorrentFilter :: GetFlags( const tr_torrent_t * tor )
{
    int flags = 0;
    const tr_stat_t * s = tr_torrentStat( (tr_torrent_t*)tor );

    switch( s->status )
    {
        case TR_STATUS_STOPPING:
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK:
        case TR_STATUS_CHECK_WAIT:
            flags |= FLAG_STOPPED;
            break;

        case TR_STATUS_DOWNLOAD:
            flags |= FLAG_LEECHING;
            break;

        case TR_STATUS_DONE:
        case TR_STATUS_SEED:
            flags |= FLAG_SEEDING;
            break;
    }

    flags |= ( ( s->rateUpload + s->rateDownload ) > 0.01 )
        ? FLAG_ACTIVE
        : FLAG_IDLE;

    flags |= s->left
        ? FLAG_DONE
        : FLAG_NOT_DONE;

    return flags;
}

void
TorrentFilter :: CountHits( const torrents_v & torrents,
                            int              * counts )
{
    memset( counts, '\0', sizeof(int) * N_FILTERS );
    foreach_const( torrents_v, torrents, it ) {
        const int flags = GetFlags( *it );
        if( flags & FLAG_STOPPED )  ++counts[STOPPED];
        if( flags & FLAG_LEECHING ) ++counts[LEECHING];
        if( flags & FLAG_SEEDING )  ++counts[SEEDING];
        if( flags & FLAG_ACTIVE )   ++counts[ACTIVE];
        if( flags & FLAG_IDLE )     ++counts[IDLE];
        if( flags & FLAG_DONE )     ++counts[DONE];
        if( flags & FLAG_NOT_DONE ) ++counts[NOT_DONE];
    }
}

wxString
TorrentFilter :: GetName( int show, int count )
{
    wxString xstr;

    switch( show )
    {
        case SEEDING:  xstr = _("&Seeds");  break;
        case LEECHING: xstr = _("&Leeches"); break;
        case STOPPED:  xstr = _("Sto&pped");  break;
        case ACTIVE:   xstr = _("&Active");   break;
        case IDLE:     xstr = _("&Idle");     break;
        case DONE:     xstr = _("&Done");     break;
        case NOT_DONE: xstr = _("&Not Done"); break;
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
