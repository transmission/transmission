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
  #include <images/transmission.xpm>
}

#include "torrent-list.h"

class MyApp : public wxApp
{
    virtual bool OnInit();
};

IMPLEMENT_APP(MyApp)

namespace
{
    tr_handle_t * handle = NULL;

    typedef std::vector<tr_torrent_t*> torrents_t;
}

class MyFrame : public wxFrame
{
public:
    MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size);
    virtual ~MyFrame();
    void OnQuit( wxCommandEvent& );
    void OnAbout( wxCommandEvent& );
    void OnOpen( wxCommandEvent& );
    void OnTimer( wxTimerEvent& );

protected:
    wxConfig * myConfig;
    wxTimer myPulseTimer;

private:
    TorrentListCtrl * myTorrentList;
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
                                   wxSize(900,600));

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
    int bestDecimal( double num ) {
        if ( num < 10 ) return 2;
        if ( num < 100 ) return 1;
        return 0;
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
}

void
MyFrame :: OnTimer(wxTimerEvent& event)
{
    myTorrentList->Refresh ();

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
    myLogoIcon = new wxIcon( transmission_xpm );
    SetIcon( *myLogoIcon );

/*#if wxCHECK_VERSION(2,8,0)
    transmission_logo.Rescale( 24, 24, wxIMAGE_QUALITY_HIGH );
#else
    transmission_logo.Rescale( 24, 24 );
#endif
    myTrayLogo = new wxIcon;
    myTrayLogo->CopyFromBitmap( wxBitmap( transmission_logo ) );*/
    myTrayLogo = myLogoIcon;


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
    toolbar->AddTool( ID_TORRENT_INFO, _("Torrent Info"), bitmap );
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

    myTorrentList = new TorrentListCtrl( handle, myConfig, row1 );

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

    /**
    ***  Load the torrents
    **/

    const int flags = TR_FLAG_PAUSED;
    const char * destination = "/home/charles/torrents";
    int count = 0;
    tr_torrent_t ** torrents = tr_loadTorrents ( handle, destination, flags, &count );
    myTorrentList->Add( std::vector<tr_torrent_t*>( torrents, torrents+count ) );
    tr_free( torrents );
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    Close( true );
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
