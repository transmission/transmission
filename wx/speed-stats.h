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

#ifndef __XMISSION_SPEED_STATS_H__
#define __XMISSION_SPEED_STATS_H__

#include <ctime>
#include <vector>
#include <wx/colour.h>
#include <wx/panel.h>
#include <libtransmission/transmission.h>

extern "C"
{
    struct tr_torrent;
}

class SpeedStats: public wxPanel
{
    public:

        SpeedStats( wxWindow * parent,
                    wxWindowID id = wxID_ANY,
                    const wxPoint& pos = wxDefaultPosition,
                    const wxSize& size = wxDefaultSize,
                    long style = wxTAB_TRAVERSAL,
                    const wxString& name = _T("panel"));

        virtual ~SpeedStats();

    public:

        void SetTorrent( struct tr_torrent * );

        void Pulse( tr_handle * handle );

    public:

        enum {
            BACKGROUND,
            FRAME,
            TORRENT_UP,
            TORRENT_DOWN,
            ALL_UP,
            ALL_DOWN,
            N_COLORS
        };

        void SetColor( int, const wxColour& );

        static wxString GetColorName( int );

    private:

        virtual void OnSize( wxSizeEvent& event );

        void OnPaint( wxPaintEvent& );

        DECLARE_EVENT_TABLE()

    private:

        wxBitmap * myBitmap;

        struct tr_torrent * myTorrent;

        struct Speed
        {
            time_t time;
            double torrentUp;
            double torrentDown;
            double allUp;
            double allDown;
            Speed(): time(0),
                     torrentUp(0), torrentDown(0),
                     allUp(0), allDown(0) {}
        };
            
        typedef std::vector<Speed> stats_t;

        stats_t myStats;

        double myMaxSpeed;

        int myHistory;

        wxColour myColors[N_COLORS];
};

#endif
