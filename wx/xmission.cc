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
#include <wx/defs.h>
#include <wx/config.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
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

#include "torrent-filter.h"
#include "torrent-list.h"

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

    wxString getReadableSpeed( float f )
    {
        wxString xstr = getReadableSize(f);
        xstr += _T("/s");
        return xstr;
    }
}

/***
****
***/

class MyApp : public wxApp
{
    virtual bool OnInit();
};

namespace
{
    const char * destination = "/home/charles/torrents"; /*FIXME*/

    tr_handle_t * handle = NULL;

    typedef std::vector<tr_torrent_t*> torrents_v;
}

class MyFrame : public wxFrame, public TorrentListCtrl::Listener
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~MyFrame();

public:
    void OnExit( wxCommandEvent& );
    void OnAbout( wxCommandEvent& );
    void OnOpen( wxCommandEvent& );
    void OnRecheck( wxCommandEvent& );
    void OnTimer( wxTimerEvent& );
    void OnFilterSelected( wxListEvent& );
    void OnStartUpdate( wxUpdateUIEvent& );
    void OnStopUpdate( wxUpdateUIEvent& );
    void OnRemoveUpdate( wxUpdateUIEvent& );
    void OnRefreshUpdate( wxUpdateUIEvent& );
    void OnInfoUpdate( wxUpdateUIEvent& );
    virtual void OnTorrentListSelectionChanged( TorrentListCtrl*, const std::set<tr_torrent_t*>& );

private:
    void RefreshFilterCounts( );
    void ApplyCurrentFilter( );

protected:
    wxConfig * myConfig;
    wxTimer myPulseTimer;

private:
    TorrentListCtrl * myTorrentList;
    wxListCtrl * myFilters;
    wxTaskBarIcon myTrayIcon;
    wxIcon myLogoIcon;
    wxIcon myTrayIconIcon;
    torrents_v myTorrents;
    torrents_v mySelectedTorrents;
    int myFilter;

private:
    DECLARE_EVENT_TABLE()
};

enum
{
    ID_START,
    ID_EDIT_PREFS,
    ID_SHOW_DEBUG_WINDOW,
    ID_Pulse,
    ID_Filter
};

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
    EVT_LIST_ITEM_SELECTED( ID_Filter, MyFrame::OnFilterSelected )
    EVT_MENU( wxID_REFRESH, MyFrame::OnRecheck )
    EVT_UPDATE_UI( ID_START, MyFrame::OnStartUpdate )
    EVT_UPDATE_UI( wxID_STOP, MyFrame::OnStopUpdate )
    EVT_UPDATE_UI( wxID_REMOVE, MyFrame::OnRemoveUpdate )
    EVT_UPDATE_UI( wxID_REFRESH, MyFrame::OnRefreshUpdate )
    EVT_UPDATE_UI( wxID_PROPERTIES, MyFrame::OnInfoUpdate )
END_EVENT_TABLE()

IMPLEMENT_APP(MyApp)

void
MyFrame :: OnStartUpdate( wxUpdateUIEvent& event )
{
    unsigned long l = 0;
    for( torrents_v::iterator it(mySelectedTorrents.begin()),
                             end(mySelectedTorrents.end()); it!=end; ++it )
        l |= tr_torrentStat(*it)->status; /* FIXME: expensive */
    event.Enable( (l & TR_STATUS_INACTIVE)!=0 );
}

void
MyFrame :: OnStopUpdate( wxUpdateUIEvent& event )
{
    unsigned long l = 0;
    for( torrents_v::iterator it(mySelectedTorrents.begin()),
                             end(mySelectedTorrents.end()); it!=end; ++it )
        l |= tr_torrentStat(*it)->status; /* FIXME: expensive */
    event.Enable( (l & TR_STATUS_ACTIVE)!=0 );
}

void
MyFrame :: OnRemoveUpdate( wxUpdateUIEvent& event )
{
   event.Enable( !mySelectedTorrents.empty() );
}
void
MyFrame :: OnRefreshUpdate( wxUpdateUIEvent& event )
{
   event.Enable( !mySelectedTorrents.empty() );
}
void
MyFrame :: OnInfoUpdate( wxUpdateUIEvent& event )
{
   event.Enable( !mySelectedTorrents.empty() );
}

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
                                                 destination,
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

    MyFrame * frame = new MyFrame( _T("Xmission"),
                                   wxPoint(50,50),
                                   wxSize(900,600));

    frame->Connect( wxID_OPEN, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnOpen );
    frame->Connect( wxID_ABOUT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnAbout );
    frame->Connect( wxID_EXIT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnExit );
    frame->Connect( ID_Pulse, wxEVT_TIMER, (wxObjectEventFunction) &MyFrame::OnTimer );

    frame->Show( true );
    SetTopWindow( frame );
    return true;
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
MyFrame :: OnRecheck( wxCommandEvent& WXUNUSED(unused) )
{
    for( torrents_v::iterator it(mySelectedTorrents.begin()),
                             end(mySelectedTorrents.end()); it!=end; ++it )
        tr_torrentRecheck( *it );
}

void
MyFrame :: OnTorrentListSelectionChanged( TorrentListCtrl* list,
                                          const std::set<tr_torrent_t*>& torrents )
{
    assert( list == myTorrentList );
    mySelectedTorrents.assign( torrents.begin(), torrents.end() );
}

void
MyFrame :: OnTimer(wxTimerEvent& WXUNUSED(event) )
{
    RefreshFilterCounts( );

    myTorrentList->Refresh ( );

    float dl, ul;
    tr_torrentRates( handle, &dl, &ul );
    wxString s = _("Download: ");
    s += getReadableSpeed( dl );
    s += _T("\n");
    s +=_("Upload: ");
    s +=  getReadableSpeed( ul );
    myTrayIcon.SetIcon( myTrayIconIcon, s );
}

MyFrame::~MyFrame()
{
    myTorrentList->RemoveListener( this );
    delete myTorrentList;

    delete myConfig;
}

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size):
    wxFrame((wxFrame*)NULL,-1,title,pos,size),
    myConfig( new wxConfig( _T("xmission") ) ),
    myPulseTimer( this, ID_Pulse ),
    myLogoIcon( transmission_xpm ),
    myTrayIconIcon( systray_xpm ),
    myFilter( TorrentFilter::SHOW_ALL )
{
    SetIcon( myLogoIcon );

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
    hsplit->SetSashGravity( 0.8 );

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

    /**
    ***  Load the torrents
    **/

    const int flags = TR_FLAG_PAUSED;
    int count = 0;
    tr_torrent_t ** torrents = tr_loadTorrents ( handle, destination, flags, &count );
    myTorrents.insert( myTorrents.end(), torrents, torrents+count );
    myTorrentList->Add( myTorrents );
    tr_free( torrents );

    wxTimerEvent dummy;
    OnTimer( dummy );
}

void MyFrame::OnExit(wxCommandEvent& WXUNUSED(event))
{
    Destroy( );
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
