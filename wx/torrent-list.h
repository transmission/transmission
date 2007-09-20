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

        enum ShowMode
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

        void SetShowMode( ShowMode );

        int GetShowModeCounts( ShowMode ) const;

    public:

        struct Listener
        {
            Listener() {}

            virtual ~Listener() {}

            virtual void OnTorrentListSelectionChanged(
                TorrentListCtrl*,
                const std::set<tr_torrent*>& ) = 0;
        };

    private:
        typedef std::set<Listener*> listeners_t;
        listeners_t myListeners;
        void fire_selection_changed( const std::set<tr_torrent*>& t ) {
            for( listeners_t::iterator it(myListeners.begin()), end(myListeners.end()); it!=end; )
                (*it++)->OnTorrentListSelectionChanged( this, t );
        }
    public:
        void AddListener( Listener* l ) { myListeners.insert(l); }
        void RemoveListener( Listener* l ) { myListeners.erase(l); }

    public:
        void Rebuild ();
        void Repopulate ();
        void Refresh ();
        void SelectAll ();
        void DeselectAll ();

    public:
        typedef std::vector<tr_torrent*> torrents_v;
        void Assign( const torrents_v& torrents );

    private:
        void Add( const torrents_v& torrents );
        void Sort( int column );
        void Resort( );
        void RefreshTorrent( tr_torrent*, int, const std::vector<int>& );
        void Remove( const std::set<tr_torrent*>& );
        static int wxCALLBACK Compare( long, long, long );

        /** torrent hash -> the torrent's row in myTorrentList */
        typedef std::map<std::string,int> str2int_t;
        str2int_t myHashToItem;

    private:
        void SetCell( int item, int col, const wxString& xstr );

    private:
        struct TorStat {
            time_t time;
            const tr_stat * stat;
            TorStat(): time(0), stat(0) {}
        };
        typedef std::map<std::string,TorStat> hash2stat_t;
        hash2stat_t myHashToStat;
        const tr_stat* getStat( tr_torrent* );

    private:
        void OnSort( wxListEvent& );
        void OnItemSelected( wxListEvent& );
        void OnItemDeselected( wxListEvent& );
        bool IsSorted( ) const;

    private:
        tr_handle * myHandle;
        wxConfig * myConfig;
        torrents_v myTorrents;
        int prevSortCol;

    private:
       DECLARE_EVENT_TABLE()
};


#endif
