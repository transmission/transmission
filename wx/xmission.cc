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
#include <wx/artprov.h>
#include <wx/defs.h>
#include <wx/config.h>
#include <wx/toolbar.h>
#include <wx/splitter.h>
#include <wx/notebook.h>
#include <wx/wx.h>
#if wxCHECK_VERSION(2,8,0)
#include <wx/aboutdlg.h>
#endif
extern "C" {
  #include <libtransmission/transmission.h>
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

protected:
    wxConfig * myConfig;

private:
    void rebuildTorrentList();
    wxListCtrl * myTorrentList;
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
    frame->Connect( wxID_EXIT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnQuit );
    frame->Connect( wxID_ABOUT, wxEVT_COMMAND_MENU_SELECTED, (wxObjectEventFunction) &MyFrame::OnAbout );

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
        COL_SEND,
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
            columns[_T("sent")]      = COL_SEND; 
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

    std::vector<int> getTorrentColumns( wxConfig * config )
    {
        const wxString key = _T("TorrentListColumns");
        wxString columnStr;
        if( !config->Read( key, &columnStr ) )
            columnStr = _T("name|#|size|done|status|seeds|peers|eta|uspeed|dspeed");

        std::vector<int> cols;
        while( !columnStr.IsEmpty() )
        {
            const wxString key = columnStr.BeforeFirst(_T('|'));
            columnStr.Remove( 0, key.Len() + 1 );
            cols.push_back( getTorrentColumn( key ) );
        }
        return cols;
    }
}

void
MyFrame :: rebuildTorrentList()
{
    myTorrentList->ClearAll( );

    int i = 0;
    const std::vector<int> cols = getTorrentColumns( myConfig );
    for( std::vector<int>::const_iterator it(cols.begin()), end(cols.end()); it!=end; ++it )
    {
        wxString h;

        switch( *it )
        {
            case COL_NUMBER:          h = _T("#"); break;
            case COL_DONE:            h = _T("Done"); break;
            case COL_DOWNLOAD_SPEED:  h = _T("Download"); break;
            case COL_ETA:             h = _T("ETA"); break;
            case COL_HASH:            h = _T("SHA1 Hash"); break;
            case COL_NAME:            h = _T("Name"); break;
            case COL_PEERS:           h = _T("Peers"); break;
            case COL_RATIO:           h = _T("Ratio"); break;
            case COL_RECEIVED:        h = _T("Received"); break;
            case COL_REMAINING:       h = _T("Remaining"); break;
            case COL_SEEDS:           h = _T("Seeds"); break;
            case COL_SEND:            h = _T("Send"); break;
            case COL_SIZE:            h = _T("Size"); break;
            case COL_STATE:           h = _T("State"); break;
            case COL_STATUS:          h = _T("Status"); break;
            case COL_TOTAL:           h = _T("Total"); break;
            case COL_UPLOAD_SPEED:    h = _T("Upload"); break;
            default:                  h = _T("Error"); break;
        }

        myTorrentList->InsertColumn( i++, h );
    }
}

/***
****
***/

MyFrame::~MyFrame()
{
    delete myConfig;
}

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size):
    wxFrame((wxFrame*)NULL,-1,title,pos,size),
    myConfig( new wxConfig( _T("xmission") ) )
{
    wxImage::AddHandler( new wxPNGHandler );
    wxImage transmission_logo ( _T("images/transmission.png"), wxBITMAP_TYPE_PNG );
    wxIcon ico;
    ico.CopyFromBitmap( wxBitmap( transmission_logo ) );
    SetIcon( ico );

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

    wxPanel * row1 = new wxPanel( hsplit, wxID_ANY );
    wxFlexGridSizer * row_sizer = new wxFlexGridSizer( 2, 0, 5 );
    row_sizer->SetNonFlexibleGrowMode( wxFLEX_GROWMODE_ALL );
    row_sizer->SetFlexibleDirection( wxHORIZONTAL );
    row1->SetSizer( row_sizer );

    /**
    ***  Filters
    **/

    wxListCtrl * filters = new wxListCtrl( row1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                           wxLC_REPORT|wxLC_SINGLE_SEL|wxLC_NO_HEADER );
    filters->InsertColumn( wxLIST_FORMAT_LEFT, _T("YYZ") );
    int i = 0;
    filters->InsertItem( i++, _T("All") );
    filters->InsertItem( i++, _T("Downloading (1)") );
    filters->InsertItem( i++, _T("Completed") );
    filters->InsertItem( i++, _T("Active (1)") );
    filters->InsertItem( i++, _T("Inactive") );
    row_sizer->Add( filters, wxSizerFlags().Expand() );

    /**
    ***  Torrent List
    **/

    myTorrentList = new wxListCtrl( row1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                    wxLC_REPORT|wxLC_SINGLE_SEL );
    rebuildTorrentList();
    row_sizer->Add( myTorrentList, wxSizerFlags().Expand() );
    row_sizer->AddGrowableCol( 1, 1 );

    i = myTorrentList->InsertItem( 0, _T("Fedora.iso") );
    myTorrentList->SetItem( i, 1, _T("*"));
    myTorrentList->SetItem( i, 2, _T("4.4 GiB"));
    myTorrentList->SetItem( i, 3, _T("50%"));
    myTorrentList->SetItem( i, 4, _T("0 (77)"));
    myTorrentList->SetItem( i, 5, _T("1 (128)"));


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
}

void MyFrame::OnQuit(wxCommandEvent& WXUNUSED(event))
{
    Close(TRUE);
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
    info.AddDeveloper( "Josh Elsasser (Back-end; GTK+)" );
    info.AddDeveloper ("Charles Kerr (Back-end, GTK+, wxWidgets)");
    info.AddDeveloper( "Mitchell Livingston (Back-end; OS X)" );
    info.AddDeveloper( "Eric Petit (Back-end; OS X)" );
    info.AddDeveloper( "Bryan Varner (BeOS)" );
    wxAboutBox( info );
#else
    wxMessageBox(_T("Xmission " LONG_VERSION_STRING "\n"
                    "Copyright 2005-2007 The Transmission Project"),
                 _T("About Xmission"),
                wxOK|wxICON_INFORMATION, this);
#endif

}
