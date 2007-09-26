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

#include <wx/intl.h>
#include <torrent-list.h>

namespace
{
    typedef std::vector<tr_torrent*> torrents_t;

    enum
    {
        COL_POSITION,
        COL_PERCENT_DONE,
        COL_DOWNLOAD_SPEED,
        COL_ETA,
        COL_HASH,
        COL_NAME,
        COL_PEERS,
        COL_RATIO,
        COL_RECEIVED,
        COL_REMAINING,
        COL_SEEDS,
        COL_SENT,
        COL_SIZE,
        COL_STATE,
        COL_STATUS,
        COL_TOTAL,
        COL_UPLOAD_SPEED,
        N_COLS
    };

    const wxString columnKeys[N_COLS] =
    {
        _T("position"),
        _T("done"),
        _T("download-speed"),
        _T("eta"),
        _T("hash"),
        _T("name"),
        _T("peers"),
        _T("ratio"),
        _T("received"),
        _T("remaining"),
        _T("seeds"),
        _T("sent"),
        _T("size"),
        _T("state"),
        _T("status"),
        _T("total"),
        _T("upload-speed")
    };

    int getTorrentColumn( const wxString& key )
    {
        typedef std::map<wxString,int> string2key_t;
        static string2key_t columns;

        if( columns.empty() )
        {
            columns[_T("position")]        = COL_POSITION;
            columns[_T("done")]            = COL_PERCENT_DONE;
            columns[_T("download-speed")]  = COL_DOWNLOAD_SPEED;
            columns[_T("eta")]             = COL_ETA;
            columns[_T("hash")]            = COL_HASH;
            columns[_T("name")]            = COL_NAME;
            columns[_T("peers")]           = COL_PEERS;
            columns[_T("ratio")]           = COL_RATIO;
            columns[_T("received")]        = COL_RECEIVED;
            columns[_T("remaining")]       = COL_REMAINING;
            columns[_T("seeds")]           = COL_SEEDS; 
            columns[_T("sent")]            = COL_SENT; 
            columns[_T("size")]            = COL_SIZE; 
            columns[_T("state")]           = COL_STATE; 
            columns[_T("status")]          = COL_STATUS; 
            columns[_T("total")]           = COL_TOTAL; 
            columns[_T("upload-speed")]    = COL_UPLOAD_SPEED;
        }

        int i = -1;
        string2key_t::const_iterator it = columns.find( key );
        if( it != columns.end() )
            i = it->second;

        return i;
    }

    typedef std::vector<int> int_v;

    int_v getTorrentColumns( wxConfig * config )
    {
        const wxString key = _T("torrent-list-columns");
        wxString columnStr;
        if( !config->Read( key, &columnStr, _T("name|download-speed|upload-speed|eta|peers|size|done|status|seeds") ) )
            config->Write( key, columnStr );

        int_v cols;
        while( !columnStr.IsEmpty() )
        {
            const wxString key = columnStr.BeforeFirst(_T('|'));
            columnStr.Remove( 0, key.Len() + 1 );
            cols.push_back( getTorrentColumn( key ) );
        }
        return cols;
    }

    int bestDecimal( double num ) {
        if ( num < 10 ) return 2;
        if ( num < 100 ) return 1;
        return 0;
    }

    wxString toWxStr( const std::string& s )
    {
        return wxString( s.c_str(), wxConvUTF8 );
    }

    wxString toWxStr( const char * s )
    {
        return wxString( s, wxConvUTF8 );
    }

    wxString getReadableSize( uint64_t size )
    {
        int i;
        static const char *sizestrs[] = { "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB" };
        for ( i=0; size>>10; ++i )
            size = size >> 10;
        char buf[512];
        snprintf( buf, sizeof(buf), "%.*f %s", bestDecimal(size), (double)size, sizestrs[i] );
        return toWxStr( buf );
    }

    wxString getReadableSize( float f )
    {
        return getReadableSize( (uint64_t)f );
    }

    wxString getReadableSpeed( float kib_sec )
    {
        wxString xstr = getReadableSize(1024*kib_sec);
        xstr += _T("/s");
        return xstr;
    }

    wxString getReadableTime( int i /*seconds*/ )  /*FIXME*/
    {
        const int s = i % 60; i /= 60;
        const int m = i % 60; i /= 60;
        const int h = i;
        return wxString::Format( _T("%d:%02d:%02d"), h, m, s );
    }
}

enum
{
    TORRENT_LIST_CTRL = 1000
};

BEGIN_EVENT_TABLE(TorrentListCtrl, wxListCtrl)
    EVT_LIST_COL_CLICK( TORRENT_LIST_CTRL, TorrentListCtrl::OnSort )
    EVT_LIST_ITEM_SELECTED( TORRENT_LIST_CTRL, TorrentListCtrl::OnItemSelected )
    EVT_LIST_ITEM_DESELECTED( TORRENT_LIST_CTRL, TorrentListCtrl::OnItemDeselected )
END_EVENT_TABLE()



TorrentListCtrl :: TorrentListCtrl( tr_handle       * handle,
                                    wxConfig        * config,
                                    wxWindow        * parent,
                                    const wxPoint   & pos,
                                    const wxSize    & size):
    wxListCtrl( parent, TORRENT_LIST_CTRL, pos, size, wxLC_REPORT|wxLC_HRULES ),
    myHandle( handle ),
    myConfig( config )
{
    wxString sortColStr;
    myConfig->Read( _T("torrent-sort-column"), &sortColStr, columnKeys[COL_NAME] );
    prevSortCol = getTorrentColumn( sortColStr );
    bool descending;
    myConfig->Read( _T("torrent-sort-is-descending"), &descending, FALSE );
    if( descending )
        prevSortCol = -prevSortCol;
    Rebuild ();
}

TorrentListCtrl :: ~TorrentListCtrl()
{
}

void
TorrentListCtrl :: SetCell( int item, int column, const wxString& xstr )
{
    wxListItem i;
    i.SetId( item );
    i.SetColumn( column );
    i.SetMask( wxLIST_MASK_TEXT );
    GetItem( i );
    if( i.GetText() != xstr )
        SetItem( item, column, xstr );
}

void
TorrentListCtrl :: RefreshTorrent( tr_torrent   * tor,
                                   int            myTorrents_index,
                                   const int_v  & cols )
{
    int row = -1;
    int col = 0;
    char buf[512];
    std::string str;
    const tr_stat * s = getStat( tor );
    const tr_info * info = tr_torrentInfo( tor );

    for( int_v::const_iterator it(cols.begin()), end(cols.end()); it!=end; ++it )
    {
        wxString xstr;

        switch( *it )
        {
            case COL_POSITION:
                snprintf( buf, sizeof(buf), "%d", 666 );
                xstr = toWxStr( buf );
                break;

            case COL_PERCENT_DONE:
                snprintf( buf, sizeof(buf), "%d%%", (int)(s->percentDone*100.0) );
                xstr = toWxStr( buf );
                break;

            case COL_DOWNLOAD_SPEED:
                if( s->rateDownload > 0.01 )
                    xstr = getReadableSpeed( s->rateDownload );
                else
                    xstr.Clear( );
                break;

            case COL_ETA:
                if( (int)(s->percentDone*100) >= 100 )
                    xstr.Clear ();
                else if( s->eta < 0 )
                    xstr = toWxStr( "\xE2\x88\x9E" ); /* infinity, in utf-8 */
                else
                    xstr = getReadableTime( s->eta );
                break;
                
            case COL_HASH:
                xstr = toWxStr( info->hashString );
                break;

            case COL_NAME:
                xstr = toWxStr( info->name );
                break;

            case COL_PEERS:
                xstr = wxString::Format( _("%d (%d)"), s->peersConnected, s->leechers );
                break;

            case COL_RATIO:
                xstr = wxString::Format( _T("%%%d"), (int)(s->uploadedEver / (double)s->downloadedEver) );
                break;

            case COL_RECEIVED:
                xstr = getReadableSize( s->downloadedEver );
                break;

            case COL_REMAINING:
                xstr = getReadableSize( s->leftUntilDone );
                break;

            case COL_SEEDS:
                if( s->seeders > 0 )
                    xstr = wxString::Format( _T("%d"), s->seeders );
                else
                    xstr.Clear ();
                break;

            case COL_SENT:
                xstr = getReadableSize( s->uploadedEver );
                break;

            case COL_SIZE:
                xstr = getReadableSize( info->totalSize );
                break;

            case COL_STATE: /* FIXME: divine the meaning of these two columns */
            case COL_STATUS:
                switch( s->status ) {
                    case TR_STATUS_STOPPING:    xstr = _("Stopping"); break;
                    case TR_STATUS_STOPPED:     xstr = _("Stopped"); break;
                    case TR_STATUS_CHECK:       xstr = wxString::Format ( _("Checking Files (%.0f)"), s->recheckProgress );  break;
                    case TR_STATUS_CHECK_WAIT:  xstr = _("Waiting to Check"); break;
                    case TR_STATUS_DOWNLOAD:    xstr = _("Downloading"); break;
                    case TR_STATUS_DONE:
                    case TR_STATUS_SEED:        xstr = _("Seeding"); break;
                    default: assert( 0 );
                }
                break;

            case COL_TOTAL:
                xstr = _T("Fixme");
                break;

            case COL_UPLOAD_SPEED:
                if( s->rateUpload > 0.01 )
                    xstr = getReadableSpeed( s->rateUpload );
                else
                    xstr.Clear( );
                break;

            default:
                xstr = _T("Fixme");
        }

        if( col )
            SetCell( row, col++, xstr );
        else {
            // first column... find the right row to put the info in.
            // if the torrent's in the list already, update that row.
            // otherwise, add a new row.
            if( row < 0 ) {
                str2int_t::const_iterator it = myHashToItem.find( info->hashString );
                if( it != myHashToItem.end() ) {
                    row = it->second;
                }
            }
            if( row >= 0 ) {
                SetCell( row, col++, xstr );
            }
            else {
                row = InsertItem( GetItemCount(), xstr );
                col = 1;
                myHashToItem[info->hashString] = row;
                SetItemData( row, myTorrents_index );
            }
        }
    }
}

/***
****
***/

void
TorrentListCtrl :: OnSort( wxListEvent& event )
{
    const int_v  cols = getTorrentColumns( myConfig );
    const int key = cols[ event.GetColumn() ];
    Sort( key );
}

void
TorrentListCtrl :: OnItemSelected( wxListEvent& WXUNUSED(event) )
{
    std::set<tr_torrent*> sel;
    long item = -1;
    for ( ;; ) {
        item = GetNextItem(item, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
        if ( item == -1 )
            break;
        sel.insert( myTorrents[GetItemData(item)] );
    }
    fire_selection_changed( sel );
}

void
TorrentListCtrl :: OnItemDeselected( wxListEvent& event )
{
    OnItemSelected( event );
}

/***
****
***/

static TorrentListCtrl * uglyHack = NULL;

int wxCALLBACK
TorrentListCtrl :: Compare( long item1, long item2, long sortData )
{
    TorrentListCtrl * self = uglyHack;
    tr_torrent * a = self->myTorrents[item1];
    tr_torrent * b = self->myTorrents[item2];
    const tr_info* ia = tr_torrentInfo( a );
    const tr_info* ib = tr_torrentInfo( b );
    const tr_stat* sa = self->getStat( a );
    const tr_stat* sb = self->getStat( b );
    int ret = 0;

    switch( abs(sortData) )
    {
        case COL_POSITION:
            ret = item1 - item2;
            break;

        case COL_PERCENT_DONE:
            if( sa->percentDone < sb->percentDone )
                ret = -1;
            else if( sa->percentDone > sb->percentDone )
                ret =  1;
            else
                ret = 0;
            break;

        case COL_DOWNLOAD_SPEED:
            if( sa->rateDownload < sb->rateDownload )
                ret = -1;
            else if( sa->rateDownload > sb->rateDownload )
                ret =  1;
            else
                ret = 0;
            break;

        case COL_ETA:
            ret = sa->eta - sb->eta;
            break;
            
        case COL_HASH:
            ret = strcmp( ia->hashString, ib->hashString );
            break;

        case COL_NAME:
            ret = strcmp( ia->name, ib->name );
            break;

        case COL_PEERS:
            /* FIXME: this is all peers, not just leechers 
            snprintf( buf, sizeof(buf), "%d (%d)", s->peersTotal, s->peersConnected );
            xstr = toWxStr( buf );*/
            break;

        case COL_RATIO: {
            const double ra = sa->uploadedEver / (double)(sa->downloadedEver + 0.01);
            const double rb = sb->uploadedEver / (double)(sb->downloadedEver + 0.01);
            if( ra < rb )
                ret = -1;
            else if( ra > rb )
                ret = 1;
            else
                ret = 0;
            break;
        }

        case COL_RECEIVED:
            if( sa->downloadedEver < sb->downloadedEver )
                ret = -1;
            else if( sa->downloadedEver > sb->downloadedEver )
                ret = 1;
            else
                ret = 0;
            break;

        case COL_REMAINING:
            if( sa->leftUntilDone < sb->leftUntilDone )
                ret = -1;
            else if( sa->leftUntilDone > sb->leftUntilDone )
                ret = 1;
            else
                ret = 0;
            break;

        case COL_SEEDS:
            /*snprintf( buf, sizeof(buf), "%d", s->seeders );
            xstr = toWxStr( buf );*/
            break;

        case COL_SENT:
            if( sa->uploadedEver < sb->uploadedEver )
                ret = -1;
            else if( sa->uploadedEver > sb->uploadedEver )
                ret = 1;
            else
                ret = 0;
            break;

        case COL_SIZE:
            if( ia->totalSize < ib->totalSize ) ret = -1;
            else if( ia->totalSize > ib->totalSize ) ret = 1;
            else ret = 0;
            break;

        case COL_STATE: /* FIXME */
        case COL_STATUS:
            ret = sa->status - sb->status;
            break;

        case COL_TOTAL:
            /*xstr = _T("Fixme");*/
            break;

        case COL_UPLOAD_SPEED:
            if( sa->rateUpload < sb->rateUpload )
                ret = -1;
            else if( sa->rateUpload > sb->rateUpload )
                ret = 1;
            else
                ret = 0;
            break;

        default:
            abort ();
    }

    if( sortData < 0 )
       ret = -ret;

    return ret;
}

void
TorrentListCtrl :: Sort( int column )
{
    if( column == prevSortCol )
        column = -column;
    prevSortCol = column;
    Resort ();
}

bool
TorrentListCtrl :: IsSorted( ) const
{
    bool is_sorted = true;
    long prevItem=-1, curItem=-1;

    uglyHack = const_cast<TorrentListCtrl*>(this);
    while( is_sorted )
    {
        prevItem = curItem;
        curItem = GetNextItem( curItem, wxLIST_NEXT_ALL, wxLIST_STATE_DONTCARE );
        if ( curItem == -1 )
            break;
        if( prevItem>=0 && curItem>=0 )
            if( Compare( prevItem, curItem, prevSortCol ) > 0 )
                is_sorted = false;
    }
    uglyHack = 0;

    return is_sorted;
}

void
TorrentListCtrl :: Resort( )
{
    myConfig->Write( _T("torrent-sort-column"), columnKeys[abs(prevSortCol)] );
    myConfig->Write( _T("torrent-sort-is-descending"), prevSortCol < 0 );

    if( !IsSorted ( ) )
    {
        uglyHack = this;
        SortItems( Compare, prevSortCol );

        const int n = GetItemCount ();
        str2int_t tmp;
        for( int i=0; i<n; ++i ) {
            int idx = GetItemData( i );
            const tr_info * info = tr_torrentInfo( myTorrents[idx] );
            tmp[info->hashString] = i;
        }
        myHashToItem.swap( tmp );
        uglyHack = NULL;
    }
}

/***
****
***/

void
TorrentListCtrl :: Refresh ()
{
    const int_v  cols = getTorrentColumns( myConfig );
    const int rowCount = GetItemCount();
    for( int row=0; row<rowCount; ++row )
    {
        int array_index = GetItemData( row );
        tr_torrent * tor = myTorrents[array_index];
        RefreshTorrent( tor, array_index, cols );
    }
}

void
TorrentListCtrl :: Repopulate ()
{
    DeleteAllItems();
    myHashToItem.clear ();

    const int_v cols = getTorrentColumns( myConfig );
    int i = 0;
    for( torrents_t::const_iterator it(myTorrents.begin()),
                                   end(myTorrents.end()); it!=end; ++it )
        RefreshTorrent( *it, i++, cols );

    Resort( );
}

void
TorrentListCtrl :: Rebuild()
{
    ClearAll( );
    myHashToItem.clear ();

    int i = 0;
    const int_v  cols = getTorrentColumns( myConfig );
    for( int_v ::const_iterator it(cols.begin()), end(cols.end()); it!=end; ++it )
    {
        int format = wxLIST_FORMAT_LEFT;
        int width = -1;
        wxString h;

        switch( *it )
        {
            case COL_POSITION:        h = _("#"); format = wxLIST_FORMAT_CENTRE; break;
            case COL_PERCENT_DONE:    h = _("Done"); width = 50; format = wxLIST_FORMAT_RIGHT; break;
            case COL_DOWNLOAD_SPEED:  h = _("Down"); width = 80; format = wxLIST_FORMAT_RIGHT; break;
            case COL_ETA:             h = _("ETA"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_HASH:            h = _("Checksum"); break;
            case COL_NAME:            h = _("Name"); width = 500; break;
            case COL_PEERS:           h = _("Peers"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RATIO:           h = _("Ratio"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RECEIVED:        h = _("Received"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_REMAINING:       h = _("Remaining"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SEEDS:           h = _("Seeds"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SENT:            h = _("Sent"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SIZE:            h = _("Size");  format = wxLIST_FORMAT_RIGHT; break;
            case COL_STATE:           h = _("State"); width = 120; break;
            case COL_STATUS:          h = _("Status"); width = 120; break;
            case COL_TOTAL:           h = _("Total"); break;
            case COL_UPLOAD_SPEED:    h = _("Up"); width = 80; format = wxLIST_FORMAT_RIGHT;break;
            default:                  h = _("Error"); break;
        }

        InsertColumn( i++, h, format, width );
    }

    Repopulate( );
}

typedef std::set<tr_torrent*> torrent_set;

void
TorrentListCtrl :: Assign( const torrents_t& torrents )
{
    torrent_set prev, cur, removed;
    torrents_v added;
    prev.insert( myTorrents.begin(), myTorrents.end() );
    cur.insert( torrents.begin(), torrents.end() );
    std::set_difference (prev.begin(), prev.end(),
                         cur.begin(), cur.end(), inserter(removed, removed.begin()));
    std::set_difference (cur.begin(), cur.end(),
                         prev.begin(), prev.end(), inserter(added, added.begin()));
    Remove( removed );
    Add( added );
    Refresh( );
    Resort( );
}

void
TorrentListCtrl :: Add( const torrents_v& add )
{
    const int_v  cols = getTorrentColumns( myConfig );
    int i = myTorrents.size();
    myTorrents.insert( myTorrents.end(), add.begin(), add.end() );
    for( torrents_v::const_iterator it(add.begin()), end(add.end()); it!=end; ++it )
        RefreshTorrent( *it, i++, cols );
}

void
TorrentListCtrl :: Remove( const torrent_set& remove )
{
    torrents_v vtmp;
    str2int_t htmp;

    for( int item=0; item<GetItemCount(); )
    {
        tr_torrent * tor = myTorrents[GetItemData(item)];
        const tr_info * info = tr_torrentInfo( tor );

        if( remove.count( tor ) )
        {
            DeleteItem( item );
            continue;
        }

        vtmp.push_back( tor );
        SetItemData( item, vtmp.size()-1 );
        htmp[ info->hashString ] = item;
        ++item;
    }

    myHashToItem.swap( htmp );
    myTorrents.swap( vtmp );
}

/***
****
***/

const tr_stat*
TorrentListCtrl :: getStat( tr_torrent * tor )
{
    const tr_info * info = tr_torrentInfo( tor );
    const time_t now = time( 0 );
    TorStat& ts = myHashToStat[ info->hashString ];
    if( ts.time < now ) {
        ts.time = now;
        ts.stat = tr_torrentStat( tor );
    }
    return ts.stat;
}

/***
****
***/

void
TorrentListCtrl :: SelectAll( )
{
    for( int i=0, n=GetItemCount(); i<n; ++i )
        SetItemState( i, ~0, wxLIST_STATE_SELECTED );
}

void
TorrentListCtrl :: DeselectAll( )
{
    for( int i=0, n=GetItemCount(); i<n; ++i )
        SetItemState( i, 0, wxLIST_STATE_SELECTED );
}

