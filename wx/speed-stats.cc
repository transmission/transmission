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

#include <wx/dcclient.h>
#include <wx/dcmemory.h>
#include <wx/event.h>
#include <wx/gbsizer.h>
#include <wx/stattext.h>
#include <wx/intl.h>
#include <libtransmission/transmission.h>
#include "foreach.h"
#include "speed-stats.h"

#define SNAPSHOT_PIXEL_WIDTH 8

BEGIN_EVENT_TABLE( SpeedStats, wxPanel )
EVT_SIZE( SpeedStats::OnSize )
EVT_PAINT( SpeedStats::OnPaint )
END_EVENT_TABLE()

SpeedStats :: SpeedStats( wxWindow         * parent,
                          wxWindowID         id,
                          const wxPoint    & pos,
                          const wxSize     & size,
                          long               style,
                          const wxString   & name ):
    wxPanel( parent, id, pos, size, style|wxFULL_REPAINT_ON_RESIZE, name ),
    myBitmap( 0 ),
    myTorrent( 0 ),
    myMaxSpeed( -1 ),
    myHistory( 0 )
{
    myColors[BACKGROUND] = wxColour( 0, 0, 0 ); // black
    myColors[FRAME] = wxColour( 34, 139, 34 ); // forest green
    myColors[ALL_UP] = wxColour( 255, 0, 0 );
    myColors[TORRENT_UP] = wxColour( 255, 255, 0 );

    myColors[ALL_DOWN] = wxColour( 255, 0, 255 );
    myColors[TORRENT_DOWN] = wxColour( 0, 255, 128 );
}

SpeedStats :: ~SpeedStats()
{
    delete myBitmap;
}

void
SpeedStats :: OnSize( wxSizeEvent& event )
{
    delete myBitmap;

    const wxSize size = event.GetSize();
    myBitmap = new wxBitmap( size.GetWidth(), size.GetHeight() );
    myHistory = size.GetWidth() / SNAPSHOT_PIXEL_WIDTH;
}

void
SpeedStats :: OnPaint( wxPaintEvent& WXUNUSED(event) )
{
    const int draw_width = myBitmap->GetWidth();
    const int draw_height = myBitmap->GetHeight();

    const int top = (((int)myMaxSpeed + 11) / 10) * 10;
    const double y_scale = (double)draw_height / top;
    const int num_bars = 4;
    const int per_bar = top / num_bars;

    // clear
    wxMemoryDC memDC;
    memDC.SelectObject( *myBitmap );
    memDC.SetBackground( myColors[BACKGROUND] );
    memDC.Clear( );

    // draw the frame

    memDC.SetPen( wxPen ( myColors[FRAME] ) );
    memDC.SetTextForeground( myColors[FRAME] );

    const int fontsize = 10;
    const int dely = int( draw_height / num_bars );

    wxString xstr;

    memDC.SetFont( wxFont ( fontsize, wxFONTFAMILY_SWISS,
                                      wxFONTSTYLE_NORMAL,
                                      wxFONTWEIGHT_NORMAL ) );

    for( int i=0; i<=num_bars; ++i )
    {
        const int y = (int)(draw_height - (i*dely+0.5));

        // line
        memDC.DrawLine( wxCoord(0),          wxCoord(draw_height - (i*dely+0.5)),
                        wxCoord(draw_width), wxCoord(draw_height - (i*dely+0.5)) );

        xstr.Printf( _("%d KiB/s"), (per_bar*i) );
        memDC.DrawText( xstr, wxCoord(0), wxCoord(y+2) );
    }

    const int n = myStats.size( );
    if( n )
    {
        wxPoint * points = new wxPoint[ n ];

        int x = draw_width - (n * SNAPSHOT_PIXEL_WIDTH);
        for( int i=0; i<n; ++i ) {
            points[i].x = x;
            x += SNAPSHOT_PIXEL_WIDTH;
        }

        // torrent upload
        for( int i=0; i<n; ++i )
            points[i].y = draw_height - 10 - int(myStats[i].torrentUp * y_scale);
        memDC.SetPen( wxPen ( myColors[TORRENT_UP] ) );
        memDC.DrawLines( n, points, 0, 0 );

        // torrent download
        for( int i=0; i<n; ++i )
            points[i].y = draw_height - int(myStats[i].torrentDown * y_scale);
        memDC.SetPen( wxPen ( myColors[TORRENT_DOWN] ) );
        memDC.DrawLines( n, points, 0, 0 );

        // all upload
        for( int i=0; i<n; ++i )
            points[i].y = draw_height - int(myStats[i].torrentUp * y_scale);
        memDC.SetPen( wxPen ( myColors[ALL_UP] ) );
        memDC.DrawLines( n, points, 0, 0 );

        // all download
        for( int i=0; i<n; ++i )
            points[i].y = draw_height - int(myStats[i].torrentDown * y_scale);
        memDC.SetPen( wxPen ( myColors[ALL_DOWN] ) );
        memDC.DrawLines( n, points, 0, 0 );

        delete [] points;
    }

    wxPaintDC dc( this );
    dc.Blit( 0, 0, draw_width, draw_height, &memDC, 0, 0 );
    memDC.SelectObject( wxNullBitmap );
}

void
SpeedStats :: SetTorrent( tr_torrent_t * tor )
{
    if( tor != myTorrent )
    {
        myTorrent = tor;

        myMaxSpeed = 0;
        foreach( stats_t, myStats, it )
        {
            it->torrentUp = 0;
            it->torrentDown = 0;
            myMaxSpeed = std::max( myMaxSpeed, it->allUp );
            myMaxSpeed = std::max( myMaxSpeed, it->allDown );
        }
    }
}

void
SpeedStats :: Pulse( tr_handle_t * handle )
{
    // add a new record
    float allUp, allDown;
    tr_torrentRates( handle, &allDown, &allUp );
    Speed s;
    s.time = time( NULL );
    s.allUp = s.time % 30;//allUp;
    s.allDown = allDown;
    if( myTorrent ) {
        const tr_stat_t * stat = tr_torrentStat( myTorrent );
        s.torrentUp = stat->rateUpload;
        s.torrentDown = stat->rateDownload;
    }
    myStats.push_back( s );

    // age off old data
    const int eraseCount = myStats.size() - myHistory;
    if( eraseCount > 0 )
        myStats.erase( myStats.begin(),
                       myStats.begin() + eraseCount );

    // update max
    myMaxSpeed = std::max( myMaxSpeed, s.allUp );
    myMaxSpeed = std::max( myMaxSpeed, s.allDown );
    myMaxSpeed = std::max( myMaxSpeed, s.torrentUp );
    myMaxSpeed = std::max( myMaxSpeed, s.torrentDown );

    Refresh( false );
}
