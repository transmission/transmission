/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include <wx/gbsizer.h>
#include <wx/stattext.h>
#include <wx/intl.h>
#include "torrent-stats.h"

BEGIN_EVENT_TABLE(TorrentStats, wxPanel)
END_EVENT_TABLE()

namespace
{
    enum
    {
        STAT_TIME_ELAPSED,
        STAT_DOWNLOADED,
        STAT_UPLOADED,
        STAT_SHARE_RATIO,
        STAT_SEEDS,
        STAT_REMAINING,
        STAT_DOWNLOAD_SPEED,
        STAT_DOWNLOAD_SPEED_LIMIT,
        STAT_UPLOAD_SPEED,
        STAT_UPLOAD_SPEED_LIMIT,
        N_STATS
    };

    wxString getLabelText( int n )
    {
        wxString xstr;

        switch( n )
        {
            case STAT_TIME_ELAPSED:          xstr = _("Time Elapsed"); break;
            case STAT_DOWNLOADED:            xstr = _("Downloaded"); break;
            case STAT_UPLOADED:              xstr = _("Uploaded"); break;
            case STAT_SHARE_RATIO:           xstr = _("Share Ratio"); break;
            case STAT_SEEDS:                 xstr = _("Seeds"); break;
            case STAT_REMAINING:             xstr = _("Time Remaining"); break;
            case STAT_DOWNLOAD_SPEED:        xstr = _("Download Speed"); break;
            case STAT_DOWNLOAD_SPEED_LIMIT:  xstr = _("Download Speed Limit"); break;
            case STAT_UPLOAD_SPEED:          xstr = _("Upload Speed"); break;
            case STAT_UPLOAD_SPEED_LIMIT:    xstr = _("Upload Speed Limit"); break;
            default: break;
        }

        return xstr;
    }
}

TorrentStats :: TorrentStats( wxWindow         * parent,
                              wxWindowID         id,
                              const wxPoint    & pos,
                              const wxSize     & size,
                              long               style,
                              const wxString   & name ):
    wxPanel( parent, id, pos, size, style, name )
{
}
