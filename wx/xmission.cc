/*
 * This file Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 */

#include <set>
#include <map>
#include <string>
#include <vector>
#include <iostream>
#include <stdint.h>
#include <wx/artprov.h>
#include <wx/defs.h>
#include <wx/config.h>
#include <wx/listctrl.h>
#include <wx/taskbar.h>
#include <wx/toolbar.h>
#include <wx/splitter.h>
#include <wx/notebook.h>
#include <wx/wx.h>
#if wxCHECK_VERSION(2,8,0)
#include <wx/aboutdlg.h>
#endif
extern "C" {
  #include <libtransmission/transmission.h>
  #include <libtransmission/utils.h>
}

class MyApp : public wxApp
{
    virtual bool OnInit();
};

IMPLEMENT_APP(MyApp)

tr_handle_t * handle = NULL;

class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~MyFrame();
    void OnQuit(wxCommandEvent& event);
    void OnAbout(wxCommandEvent& event);
    void OnOpen(wxCommandEvent& event);
    void OnTimer(wxTimerEvent& event);

protected:
    wxConfig * myConfig;
    wxTimer myPulseTimer;

private:
    void rebuildTorrentList();
    void repopulateTorrentList ();
    void refreshTorrentList ();
    typedef std::vector<tr_torrent_t*> torrents_t;
    torrents_t myTorrents;
    void refreshTorrent( tr_torrent_t*, int, const std::vector<int>& );

    typedef std::map<std::string,int> str2int_t;

    /** torrent hash -> the torrent's row in myTorrentList */
    str2int_t myHashToRow;

    wxListCtrl * myTorrentList;
    wxTaskBarIcon * myTaskBarIcon;
    wxIcon * myLogoIcon;
    wxIcon * myTrayLogo;
};

enum
{
    ID_START,
    ID_STOP,
    ID_REMOVE,
    ID_QUIT,
    ID_TORRENT_INFO,
    ID_EDIT_PREFS,
    ID_SHOW_DEBUG_WINDOW,
    ID_ABOUT,
    ID_Pulse,
    N_IDS
};

void MyFrame :: OnOpen( wxCommandEvent& event )
{
    const wxString key = _T("LastDirectory");
    wxString directory;
    myConfig->Read( key, &directory );
    wxFileDialog * w = new wxFileDialog( this, _T("message"),
                                         directory,
                                         _T(""), /* default file */
                                         _T("Torrent files|*.torrent"),
                                         wxOPEN|wxMULTIPLE );

    if( w->ShowModal() == wxID_OK )
    {
        wxArrayString paths;
        w->GetPaths( paths );
        size_t nPaths = paths.GetCount();
        for( size_t i=0; i<nPaths; ++i )
        {
            const wxString& w = paths[i];
            std::cerr << w.ToAscii() << std::endl;
        }
        myConfig->Write( key, w->GetDirectory() );
    }

    delete w;
}


bool MyApp::OnInit()
{
    handle = tr_init( "wx" );

    MyFrame * frame = new MyFrame( _T("Xmission"),
                                   wxPoint(50,50),
                                   wxSize(450,350));

    frame->Connect( wxID_OPEN, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnOpen );
    frame->Connect( wxID_ABOUT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnAbout );
    frame->Connect( wxID_EXIT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnQuit );
    frame->Connect( ID_Pulse, wxEVT_TIMER, (wxObjectEventFunction) &MyFrame::OnTimer );

    frame->Show( true );
    SetTopWindow( frame );
    return true;
}

/***
****
***/


namespace
{
    enum
    {
        COL_NUMBER,
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

    int getTorrentColumn( const wxString& key )
    {
        typedef std::map<wxString,int> string2key_t;
        static string2key_t columns;

        if( columns.empty() )
        {
            columns[_T("#")]         = COL_NUMBER;
            columns[_T("done")]      = COL_DONE;
            columns[_T("dspeed")]    = COL_DOWNLOAD_SPEED;
            columns[_T("eta")]       = COL_ETA;
            columns[_T("hash")]      = COL_HASH;
            columns[_T("name")]      = COL_NAME;
            columns[_T("peers")]     = COL_PEERS;
            columns[_T("ratio")]     = COL_RATIO;
            columns[_T("received")]  = COL_RECEIVED;
            columns[_T("remaining")] = COL_REMAINING;
            columns[_T("seeds")]     = COL_SEEDS; 
            columns[_T("sent")]      = COL_SENT; 
            columns[_T("size")]      = COL_SIZE; 
            columns[_T("state")]     = COL_STATE; 
            columns[_T("status")]    = COL_STATUS; 
            columns[_T("total")]     = COL_TOTAL; 
            columns[_T("uspeed")]    = COL_UPLOAD_SPEED;
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
        const wxString key = _T("TorrentListColumns");
        wxString columnStr;
        if( !config->Read( key, &columnStr ) )
            columnStr = _T("name|dspeed|uspeed|eta|peers|size|done|status|seeds");

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


/***
****
****  PEERS LIST
****
****
***/


/***
****
****  TORRENT LIST
****
****
***/

void
MyFrame :: refreshTorrent( tr_torrent_t  * tor,
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
            case COL_NUMBER:
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

        if( col ) {
            myTorrentList->SetItem( row, col++, xstr );
        }
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
                myTorrentList->SetItem( row, col++, xstr );
            }
            else {
                row = myTorrentList->InsertItem( myTorrentList->GetItemCount(), xstr );
                col = 1;
                myHashToRow[info->hashString] = row;
                myTorrentList->SetItemData( row, myTorrents_index );
            }
        }
    }
}

void
MyFrame :: refreshTorrentList ()
{
    const int_v  cols = getTorrentColumns( myConfig );
    const int rowCount = myTorrentList->GetItemCount();
    for( int row=0; row<rowCount; ++row )
    {
        int array_index = myTorrentList->GetItemData( row );
        tr_torrent_t * tor = myTorrents[array_index];
        refreshTorrent( tor, array_index, cols );
    }
}

void
MyFrame :: repopulateTorrentList ()
{
    myTorrentList->DeleteAllItems();
    myHashToRow.clear ();

    const int_v cols = getTorrentColumns( myConfig );
    int i = 0;
    for( torrents_t::const_iterator it(myTorrents.begin()),
                                   end(myTorrents.end()); it!=end; ++it )
        refreshTorrent( *it, i++, cols );
}

void
MyFrame :: rebuildTorrentList()
{
    myTorrentList->ClearAll( );
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
            case COL_NUMBER:          h = _T("#"); format = wxLIST_FORMAT_CENTRE; break;
            case COL_DONE:            h = _T("Done"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_DOWNLOAD_SPEED:  h = _T("Download"); break;
            case COL_ETA:             h = _T("ETA"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_HASH:            h = _T("SHA1 Hash"); break;
            case COL_NAME:            h = _T("Name"); width = 500; break;
            case COL_PEERS:           h = _T("Peers"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RATIO:           h = _T("Ratio"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_RECEIVED:        h = _T("Received"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_REMAINING:       h = _T("Remaining"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SEEDS:           h = _T("Seeds"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SENT:            h = _T("Sent"); format = wxLIST_FORMAT_RIGHT; break;
            case COL_SIZE:            h = _T("Size");  format = wxLIST_FORMAT_RIGHT; break;
            case COL_STATE:           h = _T("State"); break;
            case COL_STATUS:          h = _T("Status"); break;
            case COL_TOTAL:           h = _T("Total"); break;
            case COL_UPLOAD_SPEED:    h = _T("Upload"); format = wxLIST_FORMAT_RIGHT;break;
            default:                  h = _T("Error"); break;
        }

        myTorrentList->InsertColumn( i++, h, format, width );
    }

    repopulateTorrentList ();
}

/***
****
***/

void
MyFrame :: OnTimer(wxTimerEvent& event)
{
    refreshTorrentList ();


    float dl, ul;
    tr_torrentRates( handle, &dl, &ul );
    wxString s = _("Download: ");
    s += getReadableSpeed( dl );
    s += _T("\n");
    s +=_("Upload: ");
    s +=  getReadableSpeed( ul );
    myTaskBarIcon->SetIcon( *myTrayLogo, s );
}

MyFrame::~MyFrame()
{
    delete myConfig;
}

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size):
    wxFrame((wxFrame*)NULL,-1,title,pos,size),
    myConfig( new wxConfig( _T("xmission") ) ),
    myPulseTimer( this, ID_Pulse )
{
    wxImage::AddHandler( new wxPNGHandler );
    wxImage transmission_logo ( _T("images/transmission.png"), wxBITMAP_TYPE_PNG );
    myLogoIcon = new wxIcon;
    myLogoIcon->CopyFromBitmap( wxBitmap( transmission_logo ) );
    SetIcon( *myLogoIcon );
#if wxCHECK_VERSION(2,8,0)
    transmission_logo.Rescale( 24, 24, wxIMAGE_QUALITY_HIGH );
#else
    transmission_logo.Rescale( 24, 24 );
#endif
    myTrayLogo = new wxIcon;
    myTrayLogo->CopyFromBitmap( wxBitmap( transmission_logo ) );

    /**
    ***  Torrents
    **/

    const int flags = TR_FLAG_PAUSED;
    const char * destination = "/home/charles/torrents";
    int count = 0;
    tr_torrent_t ** torrents = tr_loadTorrents ( handle, destination, flags, &count );
    myTorrents.insert( myTorrents.end(), torrents, torrents+count );
    tr_free( torrents );


    /**
    ***  Menu
    **/

    wxMenuBar *menuBar = new wxMenuBar;

    wxMenu * m = new wxMenu;
    m->Append( wxID_OPEN, _T("&Open") );
    m->Append( ID_START, _T("&Start") );
    m->Append( wxID_STOP, _T("Sto&p") ) ;
    m->Append( wxID_REFRESH, _T("Re&check") );
    m->Append( wxID_REMOVE, _T("&Remove") );
    m->AppendSeparator();
    m->Append( wxID_NEW, _T("Create &New Torrent") );
    m->AppendSeparator();
    m->Append( wxID_CLOSE, _T("&Close") );
    m->Append( wxID_EXIT, _T("&Exit") );
    menuBar->Append( m, _T("&File") );

    m = new wxMenu;
    m->Append( ID_TORRENT_INFO, _T("Torrent &Info") );
    m->Append( wxID_PREFERENCES, _T("Edit &Preferences") );
    menuBar->Append( m, _T("&Edit") );

    m = new wxMenu;
    m->Append( ID_SHOW_DEBUG_WINDOW, _T("Show &Debug Window") );
    m->AppendSeparator();
    m->Append( wxID_ABOUT, _T("&About Xmission") );
    menuBar->Append( m, _T("&Help") );

    SetMenuBar(menuBar);

    /**
    ***  Toolbar
    **/

    wxImage open_image( _T("images/fileopen.png"),        wxBITMAP_TYPE_PNG );
    wxImage exec_image( _T("images/exec.png"),            wxBITMAP_TYPE_PNG );
    wxImage stop_image( _T("images/stop.png"),            wxBITMAP_TYPE_PNG );
    wxImage drop_image( _T("images/gtk-remove.png"),      wxBITMAP_TYPE_PNG );
    wxImage info_image( _T("images/gtk-properties.png"),  wxBITMAP_TYPE_PNG );

    wxToolBar* toolbar = CreateToolBar( wxNO_BORDER | wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT );
    toolbar->SetToolBitmapSize( wxSize( 16, 16 ) );
    toolbar->AddTool( wxID_OPEN,   _T("Open"), open_image );
    toolbar->AddTool( ID_START,    _T("Start"), exec_image );
    toolbar->AddTool( wxID_STOP,   _T("Stop"), stop_image );
    toolbar->AddTool( wxID_REMOVE, _T("Remove"), drop_image );
    toolbar->AddSeparator();
    toolbar->AddTool( ID_TORRENT_INFO, _("Torrent Info"), info_image );
    toolbar->Realize();

    /**
    ***  Row 1
    **/

    wxSplitterWindow * hsplit = new wxSplitterWindow( this );
    hsplit->SetSashGravity( 0.8 );

    wxPanel * row1 = new wxPanel( hsplit, wxID_ANY );

    /* Filters */

    wxListCtrl * filters = new wxListCtrl( row1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER );
    filters->InsertColumn( wxLIST_FORMAT_LEFT, _T("YYZ") );
    int i = 0;
    filters->InsertItem( i++, _T("All") );
    filters->InsertItem( i++, _T("Downloading (1)") );
    filters->InsertItem( i++, _T("Completed") );
    filters->InsertItem( i++, _T("Active (1)") );
    filters->InsertItem( i++, _T("Inactive") );

    /* Torrent List */

    myTorrentList = new wxListCtrl( row1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT|wxLC_SINGLE_SEL );
    rebuildTorrentList();

    wxBoxSizer * boxSizer = new wxBoxSizer( wxHORIZONTAL );
    boxSizer->Add( filters, 0, wxEXPAND|wxRIGHT, 5 );
    boxSizer->Add( myTorrentList, 1, wxEXPAND, 0 );
    row1->SetSizer( boxSizer );


    wxNotebook * notebook = new wxNotebook( hsplit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP );
    wxButton * tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("General"), false );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Peers"), false );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Pieces"), false );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Files"), false );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Logger"), false );

    hsplit->SplitHorizontally( row1, notebook );

    /**
    ***  Statusbar
    **/

    CreateStatusBar();
    SetStatusText(_T("Welcome to Xmission!"));

    /**
    ***  Refresh
    **/

    myPulseTimer.Start( 1500 );

    myTaskBarIcon = new wxTaskBarIcon( );
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    Close( true );
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxImage transmission_logo ( _T("images/transmission.png"), wxBITMAP_TYPE_PNG );
    wxIcon ico;
    ico.CopyFromBitmap( wxBitmap( transmission_logo ) );

#if wxCHECK_VERSION(2,8,0)
    wxAboutDialogInfo info;
    info.SetName(_T("Xmission"));
    info.SetVersion(_T(LONG_VERSION_STRING));
    info.SetCopyright(_T("Copyright 2005-2007 The Transmission Project"));
    info.SetDescription(_T("A fast, lightweight bittorrent client"));
    info.SetWebSite( _T( "http://transmission.m0k.org/" ) );
    info.SetIcon( ico );
    info.AddDeveloper( _T("Josh Elsasser (Back-end; GTK+)") );
    info.AddDeveloper( _T("Charles Kerr (Back-end, GTK+, wxWidgets)") );
    info.AddDeveloper( _T("Mitchell Livingston (Back-end; OS X)")  );
    info.AddDeveloper( _T("Eric Petit (Back-end; OS X)")  );
    info.AddDeveloper( _T("Bryan Varner (BeOS)")  );
    wxAboutBox( info );
#else
    wxMessageBox(_T("Xmission " LONG_VERSION_STRING "\n"
                    "Copyright 2005-2007 The Transmission Project"),
                 _T("About Xmission"),
                wxOK|wxICON_INFORMATION, this);
#endif

}
