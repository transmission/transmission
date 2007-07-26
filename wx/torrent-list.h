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
                const std::set<tr_torrent_t*>& ) = 0;
        };

    private:
        typedef std::set<Listener*> listeners_t;
        listeners_t myListeners;
        void fire_selection_changed( const std::set<tr_torrent_t*>& t ) {
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

    public:
        typedef std::vector<tr_torrent_t*> torrents_v;
        void Add( const torrents_v& torrents );
        void Assign( const torrents_v& torrents );

    private:
        void Sort( int column );
        void Resort( );
        void RefreshTorrent( tr_torrent_t*, int, const std::vector<int>& );
        void Remove( const std::set<tr_torrent_t*>& );
        static int Compare( long, long, long );

        /** torrent hash -> the torrent's row in myTorrentList */
        typedef std::map<std::string,int> str2int_t;
        str2int_t myHashToItem;

    private:
        struct TorStat {
            time_t time;
            const tr_stat_t * stat;
            TorStat(): time(0), stat(0) {}
        };
        typedef std::map<std::string,TorStat> hash2stat_t;
        hash2stat_t myHashToStat;
        const tr_stat_t* getStat( tr_torrent_t* );

    private:
        void OnSort( wxListEvent& );
        void OnItemSelected( wxListEvent& );
        void OnItemDeselected( wxListEvent& );

    private:
        tr_handle_t * myHandle;
        wxConfig * myConfig;
        torrents_v myTorrents;
        int prevSortCol;

    private:
       DECLARE_EVENT_TABLE()
};


#endif
