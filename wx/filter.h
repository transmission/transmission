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

#ifndef __XMISSION_TORRENT_FILTER_H__
#define __XMISSION_TORRENT_FILTER_H__

#include <vector>
#include <wx/intl.h>
#include <libtransmission/transmission.h>

class TorrentFilter
{
    private:

        enum ShowFlags
        {
            FLAG_ALL        = (1<<0),
            FLAG_COMPLETE   = (1<<1),
            FLAG_INCOMPLETE = (1<<2),
            FLAG_SEEDING    = (1<<3),
            FLAG_LEECHING   = (1<<4),
            FLAG_ACTIVE     = (1<<5),
            FLAG_IDLE       = (1<<6)
        };

    public:

        typedef std::vector<tr_torrent_t*> torrents_v;

        enum Show {
            ALL,
            COMPLETE, INCOMPLETE,
            SEEDING, LEECHING,
            ACTIVE, IDLE,
            N_FILTERS
        };

        static int GetFlags( const tr_torrent_t * );

        static void CountHits( const torrents_v & torrents,
                               int              * counts );

        static wxString GetName( int show, int count=0 );

        static void RemoveFailures( int           show,
                                    torrents_v  &  torrents );
};


#endif
