/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#ifndef __XMISSION_SPEED_STATS_H__
#define __XMISSION_SPEED_STATS_H__

#include <wx/panel.h>
#include <libtransmission/transmission.h>

class SpeedStats: public wxPanel
{
    public:

        SpeedStats( wxWindow * parent,
                      wxWindowID id = wxID_ANY,
                      const wxPoint& pos = wxDefaultPosition,
                      const wxSize& size = wxDefaultSize,
                      long style = wxTAB_TRAVERSAL,
                      const wxString& name = _T("panel"));

        virtual ~SpeedStats() {}

        void Update( tr_handle_t * handle );

    public:

        void OnPaint( wxPaintEvent& );

    private:

       DECLARE_EVENT_TABLE()
};

#endif
