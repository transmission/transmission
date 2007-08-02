/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
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
