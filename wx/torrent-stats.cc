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
