/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef __XMISSION_TORRENT_LIST_H__
#define __XMISSION_TORRENT_LIST_H__

#include <set>
#include <map>
#include <vector>
#include <wx/listctrl.h>
#include <wx/config.h>
#include <libtransmission/transmission.h>

class TorrentListCtrl: public wxListCtrl
{
    public:
        TorrentListCtrl( tr_handle_t       * handle,
                         wxConfig          * config,
                         wxWindow          * parent, 
                         const wxPoint     & pos = wxDefaultPosition,
                         const wxSize      & size = wxDefaultSize );
        virtual ~TorrentListCtrl();

    public:
        void Rebuild ();
        void Repopulate ();
        void Refresh ();

    public:
        typedef std::vector<tr_torrent_t*> torrents_t;
        void Add( const torrents_t& add );

    private:
        void Sort( int column );
        void Resort( );
        void OnSort( wxListEvent& );
        void RefreshTorrent( tr_torrent_t*, int, const std::vector<int>& );
        static int Compare( long, long, long );

        /** torrent hash -> the torrent's row in myTorrentList */
        typedef std::map<std::string,int> str2int_t;
        str2int_t myHashToRow;

    private:
        tr_handle_t * myHandle;
        wxConfig * myConfig;
        torrents_t myTorrents;
        int prevSortCol;

    private:
       DECLARE_EVENT_TABLE()
};


#endif
