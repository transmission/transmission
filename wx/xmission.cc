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

#include <ctype.h>
#include <inttypes.h>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <iostream>

#include <wx/artprov.h>
#include <wx/bitmap.h>
#include <wx/cmdline.h>
#include <wx/config.h>
#include <wx/dcmemory.h>
#include <wx/defs.h>
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/snglinst.h>
#include <wx/splitter.h>
#include <wx/statline.h>
#include <wx/taskbar.h>
#include <wx/tglbtn.h>
#include <wx/toolbar.h>
#include <wx/wx.h>
#if wxCHECK_VERSION(2,8,0)
#include <wx/aboutdlg.h>
#endif

extern "C"
{
  #include <libtransmission/transmission.h>
  #include <libtransmission/utils.h>

  #include <images/play.xpm>
  #include <images/stop.xpm>
  #include <images/plus.xpm>
  #include <images/minus.xpm>

  #include <images/systray.xpm>
  #include <images/transmission.xpm>
}

#include "foreach.h"
#include "speed-stats.h"
#include "filter.h"
#include "torrent-list.h"
#include "torrent-stats.h"

/***
****
***/

namespace
{
    int bestDecimal( double num ) {
        if ( num < 10 ) return 2;
        if ( num < 100 ) return 1;
        return 0;
    }

    wxString toWxStr( const char * s )
    {
        return wxString( s, wxConvUTF8 );
    }

    std::string toStr( const wxString& xstr )
    {
        return std::string( xstr.mb_str( *wxConvCurrent ) );
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

    wxString getReadableSpeed( float kib_sec )
    {
        wxString xstr = getReadableSize(1024*kib_sec);
        xstr += _T("/s");
        return xstr;
    }
}

namespace
{
    const wxCmdLineEntryDesc cmdLineDesc[] =
    {
        { wxCMD_LINE_SWITCH, _T("p"), _("pause"), _("pauses all the torrents on startup"), wxCMD_LINE_VAL_STRING, wxCMD_LINE_PARAM_OPTIONAL },
        { wxCMD_LINE_NONE, NULL, NULL, NULL, wxCMD_LINE_VAL_STRING, 0 }
    };
}

/***
****
***/

class MyApp : public wxApp
{
    virtual bool OnInit();
    virtual ~MyApp();
    wxSingleInstanceChecker * myChecker;
};

namespace
{
    tr_handle * handle = NULL;

    typedef std::vector<tr_torrent*> torrents_v;
}

class MyFrame : public wxFrame, public TorrentListCtrl::Listener
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size, bool paused);
    virtual ~MyFrame();

public:
    void OnExit( wxCommandEvent& );
    void OnAbout( wxCommandEvent& );
    void OnOpen( wxCommandEvent& );

    void OnStart( wxCommandEvent& );
    void OnStartUpdate( wxUpdateUIEvent& );

    void OnStop( wxCommandEvent& );
    void OnStopUpdate( wxUpdateUIEvent& );

    void OnRemove( wxCommandEvent& );
    void OnRemoveUpdate( wxUpdateUIEvent& );

    void OnRecheck( wxCommandEvent& );
    void OnRecheckUpdate( wxUpdateUIEvent& );

    void OnSelectAll( wxCommandEvent& );
    void OnSelectAllUpdate( wxUpdateUIEvent& );
    void OnDeselectAll( wxCommandEvent& );
    void OnDeselectAllUpdate( wxUpdateUIEvent& );

    void OnFilterChanged( wxCommandEvent& );

    void OnPulse( wxTimerEvent& );

    virtual void OnTorrentListSelectionChanged( TorrentListCtrl*, const std::set<tr_torrent*>& );

private:
    void RefreshFilterCounts( );
    void ApplyCurrentFilter( );

protected:
    wxConfig * myConfig;
    wxTimer myPulseTimer;

private:
    TorrentListCtrl * myTorrentList;
    TorrentStats * myTorrentStats;
    wxListCtrl * myFilters;
    wxTaskBarIcon * myTrayIcon;
    wxIcon myLogoIcon;
    wxIcon myTrayIconIcon;
    SpeedStats * mySpeedStats;
    torrents_v myTorrents;
    torrents_v mySelectedTorrents;
    int myFilter;
    std::string mySavePath;
    time_t myExitTime;
    wxToggleButton* myFilterButtons[TorrentFilter::N_FILTERS];

private:
    DECLARE_EVENT_TABLE()
};

enum
{
    ID_START,
    ID_DESELECTALL,
    ID_EDIT_PREFS,
    ID_SHOW_DEBUG_WINDOW,
    ID_Pulse,
    ID_Filter
};

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_COMMAND_RANGE( ID_Filter, ID_Filter+TorrentFilter::N_FILTERS, wxEVT_COMMAND_TOGGLEBUTTON_CLICKED, MyFrame::OnFilterChanged )
    EVT_MENU     ( wxID_ABOUT, MyFrame::OnAbout )
    EVT_TIMER    ( ID_Pulse, MyFrame::OnPulse )
    EVT_MENU     ( wxID_EXIT, MyFrame::OnExit )
    EVT_MENU     ( wxID_OPEN, MyFrame::OnOpen )
    EVT_MENU     ( ID_START, MyFrame::OnStart )
    EVT_UPDATE_UI( ID_START, MyFrame::OnStartUpdate )
    EVT_MENU     ( wxID_STOP, MyFrame::OnStop )
    EVT_UPDATE_UI( wxID_STOP, MyFrame::OnStopUpdate )
    EVT_MENU     ( wxID_REFRESH, MyFrame::OnRecheck )
    EVT_UPDATE_UI( wxID_REFRESH, MyFrame::OnRecheckUpdate )
    EVT_MENU     ( wxID_REMOVE, MyFrame::OnRemove )
    EVT_UPDATE_UI( wxID_REMOVE, MyFrame::OnRemoveUpdate )
    EVT_MENU     ( wxID_SELECTALL, MyFrame::OnSelectAll )
    EVT_UPDATE_UI( wxID_SELECTALL, MyFrame::OnSelectAllUpdate )
    EVT_MENU     ( ID_DESELECTALL, MyFrame::OnDeselectAll )
    EVT_UPDATE_UI( ID_DESELECTALL, MyFrame::OnDeselectAllUpdate )
END_EVENT_TABLE()

IMPLEMENT_APP(MyApp)


void
MyFrame :: OnFilterChanged( wxCommandEvent& e )
{
    static bool ignore = false;

    if( !ignore )
    {
        myFilter = e.GetId() - ID_Filter;
        std::cerr << " OnFilterChanged, myFilter is now " << myFilter << std::endl;
        ApplyCurrentFilter ( );

        ignore = true;
        for( int i=0; i<TorrentFilter::N_FILTERS; ++i )
            myFilterButtons[i]->SetValue( i==myFilter );
        ignore = false;
    }
}

void
MyFrame :: OnSelectAll( wxCommandEvent& )
{
    myTorrentList->SelectAll( );
}

void
MyFrame :: OnSelectAllUpdate( wxUpdateUIEvent& event )
{
    event.Enable( mySelectedTorrents.size() < myTorrents.size() );
}

void
MyFrame :: OnDeselectAll( wxCommandEvent& )
{
    myTorrentList->DeselectAll ( );
}

void
MyFrame :: OnDeselectAllUpdate( wxUpdateUIEvent& event )
{
    event.Enable( !mySelectedTorrents.empty() );
}

/**
**/

void
MyFrame :: OnStartUpdate( wxUpdateUIEvent& event )
{
    bool enable = false;
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStatCached(*it)->status == TR_STATUS_STOPPED )
            enable = true;
    event.Enable( enable );
}
void
MyFrame :: OnStart( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStatCached(*it)->status == TR_STATUS_STOPPED )
            tr_torrentStart( *it );
}

/**
**/

void
MyFrame :: OnStopUpdate( wxUpdateUIEvent& event )
{
    bool enable = false;
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStatCached(*it)->status != TR_STATUS_STOPPED )
            enable = true;
    event.Enable( enable );
}
void
MyFrame :: OnStop( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStat(*it)->status != TR_STATUS_STOPPED )
            tr_torrentStop( *it );
}

/**
**/

void
MyFrame :: OnRemoveUpdate( wxUpdateUIEvent& event )
{
    event.Enable( !mySelectedTorrents.empty() );
}
void
MyFrame :: OnRemove( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it ) {
        tr_torrentRemoveSaved( *it );
        tr_torrentClose( *it );
    }
}

/**
**/

void
MyFrame :: OnRecheckUpdate( wxUpdateUIEvent& event )
{
   event.Enable( !mySelectedTorrents.empty() );
}
void
MyFrame :: OnRecheck( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it )
        tr_torrentRecheck( *it );
}

/**
**/

void MyFrame :: OnOpen( wxCommandEvent& WXUNUSED(event) )
{
    const wxString key = _T("prev-directory");
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
            const std::string filename = toStr( paths[i] );
            tr_ctor * ctor = tr_ctorNew( handle );
            tr_ctorSetMetainfoFromFile( ctor, filename.c_str() );
            tr_ctorSetDestination( ctor, TR_FALLBACK, mySavePath.c_str() );
            tr_torrent * tor = tr_torrentNew( handle, ctor, NULL );
            tr_ctorFree( ctor );

            if( tor )
                myTorrents.push_back( tor );
        }
        ApplyCurrentFilter( );

        myConfig->Write( key, w->GetDirectory() );
    }

    delete w;
}


bool
MyApp :: OnInit( )
{
    handle = tr_init( "wx" );

    wxCmdLineParser cmdParser( cmdLineDesc, argc, argv );
    if( cmdParser.Parse ( ) )
        return false;

    const wxString name = wxString::Format( _T("Xmission-%s"), wxGetUserId().c_str());
    myChecker = new wxSingleInstanceChecker( name );
    if ( myChecker->IsAnotherRunning() ) {
        wxLogError(_("An instance of Xmission is already running."));
        return false;
    }

    const bool paused = cmdParser.Found( _("p") );

    MyFrame * frame = new MyFrame( _("Xmission"),
                                   wxPoint(50,50),
                                   wxSize(900,600),
                                   paused);

    frame->Show( true );
    SetTopWindow( frame );
    return true;
}

MyApp :: ~MyApp()
{
    delete myChecker;
}

/***
****
***/

void
MyFrame :: RefreshFilterCounts( )
{
    int hits[ TorrentFilter :: N_FILTERS ];
    TorrentFilter::CountHits( myTorrents, hits );
    for( int i=0; i<TorrentFilter::N_FILTERS; ++i )
        myFilterButtons[i]->SetLabel( TorrentFilter::GetName( i, hits[i] ) );
}

void
MyFrame :: ApplyCurrentFilter( )
{
    torrents_v tmp( myTorrents );
    TorrentFilter :: RemoveFailures( myFilter, tmp );
    myTorrentList->Assign( tmp );
}

void
MyFrame :: OnTorrentListSelectionChanged( TorrentListCtrl* list,
                                          const std::set<tr_torrent*>& torrents )
{
    assert( list == myTorrentList );
    mySelectedTorrents.assign( torrents.begin(), torrents.end() );

    tr_torrent * tor = mySelectedTorrents.empty() ? NULL : mySelectedTorrents.front();
    mySpeedStats->SetTorrent( tor );
}

void
MyFrame :: OnPulse(wxTimerEvent& WXUNUSED(event) )
{
    if( myExitTime ) {
        if ( !tr_torrentCount(handle) ||  myExitTime<time(0) ) {
            delete myTrayIcon;
            myTrayIcon = 0;
            Destroy( );
            return;
        }
    }

    RefreshFilterCounts( );
    ApplyCurrentFilter( );

    mySpeedStats->Pulse( handle );

    float down, up;
    tr_torrentRates( handle, &down, &up );
    wxString xstr = _("Total DL: ");
    xstr += getReadableSpeed( down );
    SetStatusText( xstr, 1 );
    xstr = _("Total UL: ");
    xstr += getReadableSpeed( up );
    SetStatusText( xstr, 2 );

    xstr = _("Download: ");
    xstr += getReadableSpeed( down );
    xstr += _T("\n");
    xstr +=_("Upload: ");
    xstr +=  getReadableSpeed( up );
    myTrayIcon->SetIcon( myTrayIconIcon, xstr );

    myTorrentList->Refresh ( );
}

MyFrame::~MyFrame()
{
    myTorrentList->RemoveListener( this );
    delete myTorrentList;

    delete myConfig;
}

MyFrame :: MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size, bool paused):
    wxFrame((wxFrame*)NULL,-1,title,pos,size),
    myConfig( new wxConfig( _T("xmission") ) ),
    myPulseTimer( this, ID_Pulse ),
    myLogoIcon( transmission_xpm ),
    myTrayIconIcon( systray_xpm ),
    mySpeedStats( 0 ),
    myFilter( TorrentFilter::ALL ),
    myExitTime( 0 )
{
    myTrayIcon = new wxTaskBarIcon;
    SetIcon( myLogoIcon );

    /**
    ***  Prefs
    **/

    long port;
    wxString key = _T("port");
    if( !myConfig->Read( key, &port, TR_DEFAULT_PORT ) )
        myConfig->Write( key, port );
    tr_setBindPort( handle, port );

    key = _T("save-path");
    wxString wxstr;
    if( !myConfig->Read( key, &wxstr, wxFileName::GetHomeDir() ) )
         myConfig->Write( key, wxstr );
    mySavePath = toStr( wxstr );

    const wxString default_colors[SpeedStats::N_COLORS] = {
        _T("#000000"), // background
        _T("#32cd32"), // frame
        _T("#ff0000"), // all up
        _T("#ff00ff"), // all down
        _T("#ffff00"), // torrent up
        _T("#00ff88")  // torrent down
    };
    wxColor speedColors[SpeedStats::N_COLORS];
    for( int i=0; i<SpeedStats::N_COLORS; ++i )
    {
        key = SpeedStats::GetColorName( i );
        myConfig->Read( key, &wxstr, _T("") );
        std::string str = toStr( wxstr );
        if( (str.size()!=7)
            || (str[0]!='#')
            || !isxdigit(str[1]) || !isxdigit(str[2])
            || !isxdigit(str[3]) || !isxdigit(str[4])
            || !isxdigit(str[5]) || !isxdigit(str[6]))
        {
            myConfig->Write( key, default_colors[i] );
            str = toStr( default_colors[i] );
        }
        int r, g, b;
        sscanf( str.c_str()+1, "%02x%02x%02x", &r, &g, &b );
        speedColors[i].Set( r, g, b );
    }

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
    m->Append( wxID_SELECTALL, _T("Select &All") );
    m->Append( ID_DESELECTALL, _T("&Deselect All") );
    m->AppendSeparator();
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

    wxIcon open_icon( plus_xpm );
    wxIcon exec_icon( play_xpm );
    wxIcon stop_icon( stop_xpm );
    wxIcon drop_icon( minus_xpm );
    wxBitmap bitmap;

    wxToolBar* toolbar = CreateToolBar( wxTB_FLAT );
    toolbar->SetToolBitmapSize( wxSize( 24, 24 ) );
    bitmap.CopyFromIcon( open_icon );
    toolbar->AddTool( wxID_OPEN,   _T("Open"), bitmap );
    bitmap.CopyFromIcon( exec_icon );
    toolbar->AddTool( ID_START,    _T("Start"), bitmap );
    bitmap.CopyFromIcon( stop_icon );
    toolbar->AddTool( wxID_STOP,   _T("Stop"), bitmap );
    bitmap.CopyFromIcon( drop_icon );
    toolbar->AddTool( wxID_REMOVE, _T("Remove"), bitmap );
    toolbar->Realize();

    /**
    ***  Row 1
    **/

    wxSplitterWindow * hsplit = new wxSplitterWindow( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxSP_3DSASH );
#if wxCHECK_VERSION(2,5,4)
    hsplit->SetSashGravity( 0.8 );
#endif

    wxPanel * panel_1 = new wxPanel( hsplit, wxID_ANY );

    wxBoxSizer * buttonSizer = new wxBoxSizer( wxVERTICAL );

    for( int i=0; i<TorrentFilter::N_FILTERS; ++i ) {
        wxToggleButton * b = new wxToggleButton( panel_1, ID_Filter+i, TorrentFilter::GetName(i), wxDefaultPosition, wxDefaultSize, (i==0)?wxRB_GROUP:0 );
        b->SetValue( i==0 );
        myFilterButtons[i] = b;
        buttonSizer->Add( b, 1, wxGROW, 0 );
    }

    myTorrentList = new TorrentListCtrl( handle, myConfig, panel_1 );
    myTorrentList->AddListener( this );

    wxBoxSizer * panelSizer = new wxBoxSizer( wxHORIZONTAL );
    panelSizer->Add( buttonSizer, 0, wxALL, 5 );
    panelSizer->Add( myTorrentList, 1, wxGROW|wxALL, 5 );

    panel_1->SetSizer( panelSizer );


    wxNotebook * notebook = new wxNotebook( hsplit, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxNB_TOP );
    myTorrentStats = new TorrentStats( notebook );
    notebook->AddPage( myTorrentStats, _T("General"), false );
    wxButton * tmp = new wxButton( notebook, wxID_ANY, _T("Hello &World"));
    notebook->AddPage( tmp, _T("Peers") );
    tmp = new wxButton( notebook, wxID_ANY, _T("&Hello World"));
    notebook->AddPage( tmp, _T("Pieces") );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Files") );
    mySpeedStats = new SpeedStats( notebook, wxID_ANY );
    for( int i=0; i<SpeedStats::N_COLORS; ++i )
        mySpeedStats->SetColor( i, speedColors[i] );
    notebook->AddPage( mySpeedStats, _T("Speed"), true );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Logger") );

    hsplit->SplitHorizontally( panel_1, notebook );

    /**
    ***  Statusbar
    **/

    const int widths[] = { -1, 150, 150 };
    wxStatusBar * statusBar = CreateStatusBar( WXSIZEOF(widths) );
    SetStatusWidths( WXSIZEOF(widths), widths );
    const int styles[] = { wxSB_FLAT, wxSB_NORMAL, wxSB_NORMAL };
    statusBar->SetStatusStyles(  WXSIZEOF(widths), styles );

    /**
    ***  Refresh
    **/

    myPulseTimer.Start( 500 );

    /**
    ***  Load the torrents
    **/

    int count = 0;
    tr_ctor * ctor = tr_ctorNew( handle );
    tr_ctorSetPaused( ctor, TR_FORCE, paused );
    tr_ctorSetDestination( ctor, TR_FALLBACK, mySavePath.c_str() );
    tr_torrent ** torrents = tr_loadTorrents ( handle, ctor, &count );
    myTorrents.insert( myTorrents.end(), torrents, torrents+count );
    tr_free( torrents );
    tr_ctorFree( ctor );

    wxTimerEvent dummy;
    OnPulse( dummy );
}

void
MyFrame :: OnExit( wxCommandEvent& WXUNUSED( event ) )
{
    Enable( false );

    myTorrents.clear( );
    mySelectedTorrents.clear( );
    ApplyCurrentFilter( );

    tr_close( handle );

    /* give the connections a max of 10 seconds to shut themselves down */
    myExitTime = time(0) + 10;
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
    wxIcon ico( transmission_xpm );

#if wxCHECK_VERSION(2,8,0)
    wxAboutDialogInfo info;
    info.SetName(_T("Xmission"));
    info.SetVersion(_T(LONG_VERSION_STRING));
    info.SetCopyright(_T("Copyright 2005-2007 The Transmission Project"));
    info.SetDescription(_T("A fast and easy BitTorrent client"));
    info.SetWebSite( _T( "http://www.transmissionbt.com/" ) );
    info.SetIcon( ico );
    info.AddDeveloper( _T("Charles Kerr (Back-end, GTK+, wxWidgets)") );
    info.AddDeveloper( _T("Mitchell Livingston (Back-end; OS X)")  );
    info.AddDeveloper( _T("Eric Petit (Back-end; OS X)")  );
    info.AddDeveloper( _T("Josh Elsasser (Back-end; GTK+)") );
    info.AddDeveloper( _T("Bryan Varner (BeOS)")  );
    wxAboutBox( info );
#else
    wxMessageBox(_T("Xmission " LONG_VERSION_STRING "\n"
                    "Copyright 2005-2007 The Transmission Project"),
                 _T("About Xmission"),
                wxOK|wxICON_INFORMATION, this);
#endif

}
