#include <iostream>
#include <wx/aboutdlg.h>
#include <wx/artprov.h>
#include <wx/defs.h>
#include <wx/config.h>
#include <wx/toolbar.h>
#include <wx/splitter.h>
#include <wx/notebook.h>
#include <wx/wx.h>
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

    wxListCtrl * torrents = new wxListCtrl( row1, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                            wxLC_REPORT|wxLC_SINGLE_SEL );
    torrents->InsertColumn( 0, _T("Name") );
    torrents->InsertColumn( 1, _T("#") );
    torrents->InsertColumn( 2, _T("Size") );
    torrents->InsertColumn( 3, _T("Done") );
    torrents->InsertColumn( 4, _T("Status") );
    torrents->InsertColumn( 5, _T("Seeds") );
    torrents->InsertColumn( 6, _T("Peers") );
    row_sizer->Add( torrents, wxSizerFlags().Expand() );
    row_sizer->AddGrowableCol( 1, 1 );

    i = torrents->InsertItem( 0, _T("Fedora.iso") );
    torrents->SetItem( i, 1, _T("*"));
    torrents->SetItem( i, 2, _T("4.4 GiB"));
    torrents->SetItem( i, 3, _T("50%"));
    torrents->SetItem( i, 4, _T("0 (77)"));
    torrents->SetItem( i, 5, _T("1 (128)"));


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
}
