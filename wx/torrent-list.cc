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
#include <wx/intl.h>
#include <torrent-list.h>

namespace
{
    typedef std::vector<tr_torrent_t*> torrents_t;

    enum
    {
        COL_POSITION,
        COL_DONE,
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
            columns[_T("done")]            = COL_DONE;
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
        for ( i=0; size>>10; ++i ) size = size>>10;
        char buf[512];
        snprintf( buf, sizeof(buf), "%.*f %s", bestDecimal(size), (double)size, sizestrs[i] );
        return toWxStr( buf );
    }

    wxString getReadableSize( float f )
    {
        return getReadableSize( (uint64_t)f );
    }

    wxString getReadableSpeed( float f )
    {
        wxString xstr = getReadableSize(f);
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
    EVT_LIST_COL_CLICK(TORRENT_LIST_CTRL, TorrentListCtrl::OnSort)
END_EVENT_TABLE()

TorrentListCtrl :: TorrentListCtrl( tr_handle_t       * handle,
                                    wxConfig          * config,
                                    wxWindow          * parent,
                                    const wxPoint     & pos,
                                    const wxSize      & size):
    wxListCtrl( parent, TORRENT_LIST_CTRL, pos, size, wxLC_REPORT|wxLC_SINGLE_SEL ),
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
TorrentListCtrl :: RefreshTorrent( tr_torrent_t  * tor,
                                   int             myTorrents_index,
                                   const int_v   & cols )
{
    int row = -1;
    int col = 0;
    char buf[512];
    std::string str;
    const tr_stat_t * s = tr_torrentStat( tor );
    const tr_info_t* info = tr_torrentInfo( tor );

    for( int_v::const_iterator it(cols.begin()), end(cols.end()); it!=end; ++it )
    {
        wxString xstr;

        switch( *it )
        {
            case COL_POSITION:
                snprintf( buf, sizeof(buf), "%d", 666 );
                xstr = toWxStr( buf );
                break;

            case COL_DONE:
                snprintf( buf, sizeof(buf), "%d%%", (int)(s->percentDone*100.0) );
                xstr = toWxStr( buf );
                break;

            case COL_DOWNLOAD_SPEED: break;
                xstr = getReadableSpeed( s->rateDownload );
                break;

            case COL_ETA:
                if( (int)(s->percentDone*100) >= 100 )
                    xstr = wxString ();
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
                /* FIXME: this is all peers, not just leechers */
                snprintf( buf, sizeof(buf), "%d (%d)", s->peersTotal, s->peersConnected );
                xstr = toWxStr( buf );
                break;

            case COL_RATIO:
                snprintf( buf, sizeof(buf), "%%%d", (int)(s->uploaded / (double)s->downloadedValid) );
                xstr = toWxStr( buf );
                break;

            case COL_RECEIVED:
                xstr = getReadableSize( s->downloaded );
                break;

            case COL_REMAINING:
                xstr = getReadableSize( s->left );
                break;

            case COL_SEEDS:
                snprintf( buf, sizeof(buf), "%d", s->seeders ); /* FIXME: %d (%d) */
                xstr = toWxStr( buf );
                break;

            case COL_SENT:
                xstr = getReadableSize( s->uploaded );
                break;

            case COL_SIZE:
                xstr = getReadableSize( info->totalSize );
                break;

            case COL_STATE:
                xstr = _T("Fixme");
                break;

            case COL_STATUS:
                xstr = _T("Fixme");
                break;

            case COL_TOTAL:
                xstr = _T("Fixme");
                break;

            case COL_UPLOAD_SPEED:
                xstr = getReadableSpeed( s->rateUpload );
                break;

            default:
                xstr = _T("Fixme");
        }

        if( col )
            SetItem( row, col++, xstr );
        else {
            // first column... find the right row to put the info in.
            // if the torrent's in the list already, update that row.
            // otherwise, add a new row.
            if( row < 0 ) {
                str2int_t::const_iterator it = myHashToRow.find( info->hashString );
                if( it != myHashToRow.end() ) {
                    row = it->second;
                }
            }
            if( row >= 0 ) {
                SetItem( row, col++, xstr );
            }
            else {
                row = InsertItem( GetItemCount(), xstr );
                col = 1;
                myHashToRow[info->hashString] = row;
                SetItemData( row, myTorrents_index );
            }
        }
    }
}

static torrents_t * uglyHack = NULL;

int
TorrentListCtrl :: Compare( long item1, long item2, long sortData )
{
    const tr_torrent_t * a = (*uglyHack)[item1];
    const tr_torrent_t * b = (*uglyHack)[item2];
    const tr_info_t* ia = tr_torrentInfo( a );
    const tr_info_t* ib = tr_torrentInfo( b );
    int ret = 0;

    switch( abs(sortData) )
    {
        case COL_POSITION:
            ret = item1 - item2;

        case COL_DONE:
/*            ccc
            snprintf( buf, sizeof(buf), "%d%%", (int)(s->percentDone*100.0) );
            xstr = toWxStr( buf );*/
            break;

        case COL_DOWNLOAD_SPEED: break;
            /*xstr = getReadableSpeed( s->rateDownload );*/
            break;

        case COL_ETA:
/*            if( (int)(s->percentDone*100) >= 100 ) */
            break;
            
        case COL_HASH:
            /*xstr = toWxStr( info->hashString );*/
            break;

        case COL_NAME:
            ret = strcmp( ia->name, ib->name );
            break;

        case COL_PEERS:
            /* FIXME: this is all peers, not just leechers 
            snprintf( buf, sizeof(buf), "%d (%d)", s->peersTotal, s->peersConnected );
            xstr = toWxStr( buf );*/
            break;

        case COL_RATIO:
            /*snprintf( buf, sizeof(buf), "%%%d", (int)(s->uploaded / (double)s->downloadedValid) );
            xstr = toWxStr( buf );*/
            break;

        case COL_RECEIVED:
            /*xstr = getReadableSize( s->downloaded );*/
            break;

        case COL_REMAINING:
            /*xstr = getReadableSize( s->left );*/
            break;

        case COL_SEEDS:
            /*snprintf( buf, sizeof(buf), "%d", s->seeders );
            xstr = toWxStr( buf );*/
            break;

        case COL_SENT:
            /*xstr = getReadableSize( s->uploaded );*/
            break;

        case COL_SIZE:
            if( ia->totalSize < ib->totalSize ) ret = -1;
            else if( ia->totalSize > ib->totalSize ) ret = 1;
            else ret = 0;
            break;

        case COL_STATE:
            /*xstr = _T("Fixme");*/
            break;

        case COL_STATUS:
            /*xstr = _T("Fixme");*/
            break;

        case COL_TOTAL:
            /*xstr = _T("Fixme");*/
            break;

        case COL_UPLOAD_SPEED:
            /*xstr = getReadableSpeed( s->rateUpload );*/
            break;

        default:
            abort ();
    }

    if( sortData < 0 )
       ret = -ret;

    return ret;
}

void
TorrentListCtrl :: OnSort( wxListEvent& event )
{
    const int_v  cols = getTorrentColumns( myConfig );
    const int key = cols[ event.GetColumn() ];
    Sort( key );
}

void
TorrentListCtrl :: Sort( int column )
{
    if( column == prevSortCol )
        column = -column;
    prevSortCol = column;
    Resort ();
}

void
TorrentListCtrl :: Resort( )
{
    uglyHack = &myTorrents;

    myConfig->Write( _T("torrent-sort-column"), columnKeys[abs(prevSortCol)] );
    myConfig->Write( _T("torrent-sort-is-descending"), prevSortCol < 0 );

    SortItems( Compare, prevSortCol );

    const int n = GetItemCount ();
    str2int_t tmp;
    for( int i=0; i<n; ++i ) {
        int idx = GetItemData( i );
        const tr_info_t* info = tr_torrentInfo( myTorrents[idx] );
        tmp[info->hashString] = i;
    }
    myHashToRow.swap( tmp );
    uglyHack = NULL;
}

void
TorrentListCtrl :: Refresh ()
{
    const int_v  cols = getTorrentColumns( myConfig );
    const int rowCount = GetItemCount();
    for( int row=0; row<rowCount; ++row )
    {
        int array_index = GetItemData( row );
        tr_torrent_t * tor = myTorrents[array_index];
        RefreshTorrent( tor, array_index, cols );
    }
}

void
TorrentListCtrl :: Repopulate ()
{
    DeleteAllItems();
    myHashToRow.clear ();

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
    myHashToRow.clear ();

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
            case COL_DONE:            h = _("Done"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_DOWNLOAD_SPEED:  h = _("Download"); break;
            case COL_ETA:             h = _("ETA"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_HASH:            h = _("SHA1 Hash"); break;
            case COL_NAME:            h = _("Name"); width = 500; break;
            case COL_PEERS:           h = _("Peers"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RATIO:           h = _("Ratio"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RECEIVED:        h = _("Received"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_REMAINING:       h = _("Remaining"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SEEDS:           h = _("Seeds"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SENT:            h = _("Sent"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SIZE:            h = _("Size");  format = wxLIST_FORMAT_RIGHT; break;
            case COL_STATE:           h = _("State"); break;
            case COL_STATUS:          h = _("Status"); break;
            case COL_TOTAL:           h = _("Total"); break;
            case COL_UPLOAD_SPEED:    h = _("Upload"); format = wxLIST_FORMAT_RIGHT;break;
            default:                  h = _("Error"); break;
        }

        InsertColumn( i++, h, format, width );
    }

    Repopulate ();
}

void
TorrentListCtrl :: Add( const torrents_t& add )
{
    myTorrents.insert( myTorrents.end(), add.begin(), add.end() );
    Repopulate ();
}


