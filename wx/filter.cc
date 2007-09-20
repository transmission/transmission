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
 *
 * $Id$
 */

#include "foreach.h"
#include "filter.h"

int
TorrentFilter :: GetFlags( const tr_torrent * tor )
{
    int flags = 0;
    const tr_stat * s = tr_torrentStat( (tr_torrent*)tor );

    switch( s->status )
    {
        case TR_STATUS_DOWNLOAD:
            flags |= FLAG_LEECHING;
            break;

        case TR_STATUS_DONE:
        case TR_STATUS_SEED:
            flags |= FLAG_SEEDING;
            break;

        case TR_STATUS_STOPPING:
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK:
        case TR_STATUS_CHECK_WAIT:
            break;
    }

    flags |= ( ( s->rateUpload + s->rateDownload ) > 0.01 )
        ? FLAG_ACTIVE
        : FLAG_IDLE;

    flags |= s->left
        ? FLAG_INCOMPLETE
        : FLAG_COMPLETE;

    flags |= FLAG_ALL;

    return flags;
}

void
TorrentFilter :: CountHits( const torrents_v & torrents,
                            int              * counts )
{
    memset( counts, '\0', sizeof(int) * N_FILTERS );
    foreach_const( torrents_v, torrents, it ) {
        const int flags = GetFlags( *it );
        if( flags & FLAG_ALL )        ++counts[ALL];
        if( flags & FLAG_LEECHING )   ++counts[LEECHING];
        if( flags & FLAG_SEEDING )    ++counts[SEEDING];
        if( flags & FLAG_ACTIVE )     ++counts[ACTIVE];
        if( flags & FLAG_IDLE )       ++counts[IDLE];
        if( flags & FLAG_COMPLETE )   ++counts[COMPLETE];
        if( flags & FLAG_INCOMPLETE ) ++counts[INCOMPLETE];
    }
}

wxString
TorrentFilter :: GetName( int show, int count )
{
    static const wxString names[N_FILTERS] = {
        _("&All"),
        _("&Complete"),
        _("&Incomplete"),
        _("&Seeding"),
        _("&Leeching"),
        _("Acti&ve"),
        _("I&dle")
    };

    assert( 0<=show && show<N_FILTERS );

    wxString xstr = names[show];
    if( count )
        xstr += wxString::Format(_T(" (%d)"), count );
    return xstr;
}

void
TorrentFilter :: RemoveFailures( int            show,
                                 torrents_v  &  torrents )
{
    torrents_v tmp;

    foreach_const( torrents_v, torrents, it ) {
        const int flags = GetFlags( *it );
        if(   ( ( show == ALL )        && ( flags & FLAG_ALL ) )
           || ( ( show == LEECHING )   && ( flags & FLAG_LEECHING ) )
           || ( ( show == SEEDING )    && ( flags & FLAG_SEEDING ) )
           || ( ( show == ACTIVE )     && ( flags & FLAG_ACTIVE ) )
           || ( ( show == IDLE )       && ( flags & FLAG_IDLE ) )
           || ( ( show == COMPLETE )   && ( flags & FLAG_COMPLETE ) )
           || ( ( show == INCOMPLETE ) && ( flags & FLAG_INCOMPLETE ) ) )
            tmp.push_back( *it );
    }

    torrents.swap( tmp );
}
