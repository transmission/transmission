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
#include <wx/taskbar.h>
#include <wx/toolbar.h>
#include <wx/wx.h>
#if wxCHECK_VERSION(2,8,0)
#include <wx/aboutdlg.h>
#endif

extern "C"
{
  #include <libtransmission/transmission.h>
  #include <libtransmission/utils.h>

  #include <images/exec.xpm>
  #include <images/fileopen.xpm>
  #include <images/gtk-properties.xpm>
  #include <images/gtk-remove.xpm>
  #include <images/stop.xpm>
  #include <images/systray.xpm>
  #include <images/transmission.xpm>
}

#include "foreach.h"
#include "speed-stats.h"
#include "torrent-filter.h"
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
    tr_handle_t * handle = NULL;

    typedef std::vector<tr_torrent_t*> torrents_v;
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
    void OnFilterSelected( wxListEvent& );

    void OnStart( wxCommandEvent& );
    void OnStartUpdate( wxUpdateUIEvent& );

    void OnStop( wxCommandEvent& );
    void OnStopUpdate( wxUpdateUIEvent& );

    void OnRemove( wxCommandEvent& );
    void OnRemoveUpdate( wxUpdateUIEvent& );

    void OnRecheck( wxCommandEvent& );
    void OnRecheckUpdate( wxUpdateUIEvent& );

    void OnInfo( wxCommandEvent& );
    void OnInfoUpdate( wxUpdateUIEvent& );

    void OnSelectAll( wxCommandEvent& );
    void OnSelectAllUpdate( wxUpdateUIEvent& );
    void OnDeselectAll( wxCommandEvent& );
    void OnDeselectAllUpdate( wxUpdateUIEvent& );

    void OnPulse( wxTimerEvent& );

    virtual void OnTorrentListSelectionChanged( TorrentListCtrl*, const std::set<tr_torrent_t*>& );

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
    wxTaskBarIcon myTrayIcon;
    wxIcon myLogoIcon;
    wxIcon myTrayIconIcon;
    SpeedStats * mySpeedStats;
    torrents_v myTorrents;
    torrents_v mySelectedTorrents;
    int myFilter;
    std::string mySavePath;
    time_t myExitTime;

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
    EVT_MENU     ( wxID_ABOUT, MyFrame::OnAbout )
    EVT_TIMER    ( ID_Pulse, MyFrame::OnPulse )
    EVT_LIST_ITEM_SELECTED( ID_Filter, MyFrame::OnFilterSelected )
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
    EVT_MENU     ( wxID_PROPERTIES, MyFrame::OnInfo )
    EVT_UPDATE_UI( wxID_PROPERTIES, MyFrame::OnInfoUpdate )
    EVT_MENU     ( wxID_SELECTALL, MyFrame::OnSelectAll )
    EVT_UPDATE_UI( wxID_SELECTALL, MyFrame::OnSelectAllUpdate )
    EVT_MENU     ( ID_DESELECTALL, MyFrame::OnDeselectAll )
    EVT_UPDATE_UI( ID_DESELECTALL, MyFrame::OnDeselectAllUpdate )
END_EVENT_TABLE()

IMPLEMENT_APP(MyApp)


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
    unsigned long l = 0;
    foreach( torrents_v, mySelectedTorrents, it )
        l |= tr_torrentStat(*it)->status; /* FIXME: expensive */
    event.Enable( (l & TR_STATUS_INACTIVE)!=0 );
}
void
MyFrame :: OnStart( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStat(*it)->status & TR_STATUS_INACTIVE )
            tr_torrentStart( *it );
}

/**
**/

void
MyFrame :: OnStopUpdate( wxUpdateUIEvent& event )
{
    unsigned long l = 0;
    foreach( torrents_v, mySelectedTorrents, it )
        l |= tr_torrentStat(*it)->status; /* FIXME: expensive */
    event.Enable( (l & TR_STATUS_ACTIVE)!=0 );
}
void
MyFrame :: OnStop( wxCommandEvent& WXUNUSED(unused) )
{
    foreach( torrents_v, mySelectedTorrents, it )
        if( tr_torrentStat(*it)->status & TR_STATUS_ACTIVE )
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

void
MyFrame :: OnInfoUpdate( wxUpdateUIEvent& event )
{
   event.Enable( !mySelectedTorrents.empty() );
}
void
MyFrame :: OnInfo( wxCommandEvent& WXUNUSED(unused) )
{
    std::cerr << "FIXME: info" << std::endl;
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
            tr_torrent_t * tor = tr_torrentInit( handle,
                                                 filename.c_str(),
                                                 mySavePath.c_str(),
                                                 0, NULL );
            if( tor )
                myTorrents.push_back( tor );
        }
        ApplyCurrentFilter( );

        myConfig->Write( key, w->GetDirectory() );
    }

    delete w;
}


bool MyApp::OnInit()
{
    handle = tr_init( "wx" );

    wxCmdLineParser cmdParser( cmdLineDesc, argc, argv );
    if( cmdParser.Parse ( ) )
        return false;

    const wxString name = wxString::Format( _T("MyApp-%s"), wxGetUserId().c_str());
    myChecker = new wxSingleInstanceChecker( name );
    if ( myChecker->IsAnotherRunning() ) {
        wxLogError(_("An instance of Transmission is already running."));
        return false;
    }

    const bool paused = cmdParser.Found( _("p") );

    MyFrame * frame = new MyFrame( _T("Xmission"),
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
    for( int i=0; i<TorrentFilter::N_FILTERS; ++i )
    {
        wxString xstr = TorrentFilter::getFilterName( i );
        const int count = TorrentFilter::CountHits( i, myTorrents );
        if( count )
            xstr += wxString::Format(_T(" (%d)"), count );
        myFilters->SetItem( i, 0, xstr );
    }
}

void
MyFrame :: ApplyCurrentFilter( )
{
    torrents_v tmp( myTorrents );
    TorrentFilter :: RemoveFailures( myFilter, tmp );
    myTorrentList->Assign( tmp );
}

void
MyFrame :: OnFilterSelected( wxListEvent& event )
{
    myFilter = event.GetIndex( );
    ApplyCurrentFilter( );
}


void
MyFrame :: OnTorrentListSelectionChanged( TorrentListCtrl* list,
                                          const std::set<tr_torrent_t*>& torrents )
{
    assert( list == myTorrentList );
    mySelectedTorrents.assign( torrents.begin(), torrents.end() );
}

void
MyFrame :: OnPulse(wxTimerEvent& WXUNUSED(event) )
{
    if( myExitTime ) {
        std::cerr << __FILE__ << ':' << __LINE__ << ' ' << tr_torrentCount(handle) << " torrents left" << std::endl;
        if ( !tr_torrentCount(handle) ||  myExitTime<time(0) ) {
            Destroy( );
            return;
        }
    }

    RefreshFilterCounts( );

    mySpeedStats->Update( handle );

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
    myTrayIcon.SetIcon( myTrayIconIcon, xstr );

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
    myFilter( TorrentFilter::SHOW_ALL ),
    myExitTime( 0 )
{
    SetIcon( myLogoIcon );

    long port;
    wxString key = _T("port");
    if( !myConfig->Read( key, &port, 9090 ) )
        myConfig->Write( key, port );
    tr_setBindPort( handle, port );

    key = _T("save-path");
    wxString wxstr;
    if( !myConfig->Read( key, &wxstr, wxFileName::GetHomeDir() ) )
        myConfig->Write( key, wxstr );
    mySavePath = toStr( wxstr );
    std::cerr << __FILE__ << ':' << __LINE__ << " save-path is [" << mySavePath << ']' << std::endl;

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
    m->Append( wxID_PROPERTIES, _T("Torrent &Info") );
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

    wxIcon open_icon( fileopen_xpm );
    wxIcon exec_icon( exec_xpm );
    wxIcon stop_icon( stop_xpm );
    wxIcon drop_icon( gtk_remove_xpm );
    wxIcon info_icon( gtk_properties_xpm );
    wxBitmap bitmap;

    wxToolBar* toolbar = CreateToolBar( wxNO_BORDER | wxTB_HORIZONTAL | wxTB_FLAT | wxTB_TEXT );
    toolbar->SetToolBitmapSize( wxSize( 16, 16 ) );
    bitmap.CopyFromIcon( open_icon );
    toolbar->AddTool( wxID_OPEN,   _T("Open"), bitmap );
    bitmap.CopyFromIcon( exec_icon );
    toolbar->AddTool( ID_START,    _T("Start"), bitmap );
    bitmap.CopyFromIcon( stop_icon );
    toolbar->AddTool( wxID_STOP,   _T("Stop"), bitmap );
    bitmap.CopyFromIcon( drop_icon );
    toolbar->AddTool( wxID_REMOVE, _T("Remove"), bitmap );
    toolbar->AddSeparator();
    bitmap.CopyFromIcon( info_icon );
    toolbar->AddTool( wxID_PROPERTIES, _("Torrent Info"), bitmap );
    toolbar->Realize();

    /**
    ***  Row 1
    **/

    wxSplitterWindow * hsplit = new wxSplitterWindow( this );
#if wxCHECK_VERSION(2,5,4)
    hsplit->SetSashGravity( 0.8 );
#endif

    wxPanel * row1 = new wxPanel( hsplit, wxID_ANY );

    /* Filters */

    myFilters = new wxListCtrl( row1, ID_Filter, wxDefaultPosition, wxSize(120,-1),
                                wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER );
    myFilters->InsertColumn( wxLIST_FORMAT_LEFT, _("Filters"), wxLIST_FORMAT_LEFT, 120 );
    for( int i=0; i<TorrentFilter::N_FILTERS; ++i )
        myFilters->InsertItem( i, TorrentFilter::getFilterName(i) );

    /* Torrent List */

    myTorrentList = new TorrentListCtrl( handle, myConfig, row1 );
    myTorrentList->AddListener( this );

    wxBoxSizer * boxSizer = new wxBoxSizer( wxHORIZONTAL );
    boxSizer->Add( myFilters, 0, wxEXPAND|wxRIGHT, 5 );
    boxSizer->Add( myTorrentList, 1, wxEXPAND, 0 );
    row1->SetSizer( boxSizer );
    //boxSizer->SetSizeHints( row1 );


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
    notebook->AddPage( mySpeedStats, _T("Speed"), true );
    tmp = new wxButton( notebook, wxID_ANY, _T("Hello World"));
    notebook->AddPage( tmp, _T("Logger") );

    hsplit->SplitHorizontally( row1, notebook );

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

    myPulseTimer.Start( 1500 );

    /**
    ***  Load the torrents
    **/

    int flags = 0;
    if( paused )
        flags |= TR_FLAG_PAUSED;
    int count = 0;
    tr_torrent_t ** torrents = tr_loadTorrents ( handle, mySavePath.c_str(), flags, &count );
    myTorrents.insert( myTorrents.end(), torrents, torrents+count );
    myTorrentList->Add( myTorrents );
    tr_free( torrents );

    wxTimerEvent dummy;
    OnPulse( dummy );
}

void MyFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
    Enable( false );

    foreach( torrents_v, myTorrents, it )
        tr_torrentClose( *it );

    myTorrents.clear ();
    mySelectedTorrents.clear ();

    ApplyCurrentFilter ();

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
