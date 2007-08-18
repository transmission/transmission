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

#include <iostream>
#include <wx/gbsizer.h>
#include <wx/stattext.h>
#include <wx/intl.h>
#include "speed-stats.h"

BEGIN_EVENT_TABLE( SpeedStats, wxPanel )
END_EVENT_TABLE()
    //EVT_PAINT( SpeedStats::OnPaint )

SpeedStats :: SpeedStats( wxWindow         * parent,
                          wxWindowID         id,
                          const wxPoint    & pos,
                          const wxSize     & size,
                          long               style,
                          const wxString   & name ):
    wxPanel( parent, id, pos, size, style, name )
{
}

void
SpeedStats :: Update( tr_handle_t * WXUNUSED(handle) )
{
}

void
SpeedStats :: OnPaint( wxPaintEvent& WXUNUSED(event) )
{
#if 0
    int w, h;
    mySpeedPanel->GetSize( &w, &h );
    wxMemoryDC dc;
    wxBitmap bitmap( w, h );
    dc.SelectObject( bitmap ); 

    wxColour backgroundColor = *wxBLACK;
    dc.SetBrush( wxBrush( backgroundColor ) );
    dc.SetPen( wxBrush( backgroundColor ) );
    dc.DrawRectangle( 0, 0, w, h );

    std::cerr << "paint" << std::endl;
#endif
}
