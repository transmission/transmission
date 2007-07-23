/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef __XMISSION_TORRENT_FILTER_H__
#define __XMISSION_TORRENT_FILTER_H__

#include <vector>
#include <wx/intl.h>
#include <libtransmission/transmission.h>

class TorrentFilter
{
    public:

        typedef std::vector<tr_torrent_t*> torrents_v;

        enum Show
        {
            SHOW_ALL,
            SHOW_DOWNLOADING,
            SHOW_UPLOADING,
            SHOW_COMPLETE,
            SHOW_INCOMPLETE,
            SHOW_ACTIVE,
            SHOW_INACTIVE,
            N_FILTERS
        };

        static wxString getFilterName( int show );


        static void RemoveFailures( int           show,
                                    torrents_v  &  torrents );

        static int CountHits ( int                  show,
                               const torrents_v  &  torrents );

        static bool Test( int            show,
                          tr_torrent_t * torrent );
};


#endif
