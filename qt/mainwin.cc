/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id:$
 */

#include <cassert>
#include <iostream>

#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QSize>
#include <QStyle>
#include <QHBoxLayout>
#include <QSystemTrayIcon>
#include <QUrl>

#include "about.h"
#include "details.h"
#include "mainwin.h"
#include "make-dialog.h"
#include "options.h"
#include "prefs.h"
#include "prefs-dialog.h"
#include "session.h"
#include "speed.h"
#include "stats-dialog.h"
#include "torrent-delegate.h"
#include "torrent-delegate-min.h"
#include "torrent-filter.h"
#include "torrent-model.h"
#include "ui_mainwin.h"
#include "utils.h"
#include "qticonloader.h"

#define PREFS_KEY "prefs-key";

QIcon
TrMainWindow :: getStockIcon( const QString& freedesktop_name, int fallback )
{
    QIcon fallbackIcon;

    if( fallback > 0 )
        fallbackIcon = style()->standardIcon( QStyle::StandardPixmap( fallback ), 0, this );

    return QtIconLoader::icon( freedesktop_name, fallbackIcon );
}

namespace
{
    QSize calculateTextButtonSizeHint( QPushButton * button )
    {
        QStyleOptionButton opt;
        opt.initFrom( button );
        QString s( button->text( ) );
        if( s.isEmpty( ) )
            s = QString::fromLatin1( "XXXX" );
        QFontMetrics fm = button->fontMetrics( );
        QSize sz = fm.size( Qt::TextShowMnemonic, s );
        return button->style()->sizeFromContents( QStyle::CT_PushButton, &opt, sz, button ).expandedTo( QApplication::globalStrut( ) );
    }

    void setTextButtonSizeHint( QPushButton * button )
    {
        /* this is kind of a hack, possibly coming from my being new to Qt.
         * Qt 4.4's sizeHint calculations for QPushButton have it include
         * space for an icon, even if no icon is used.  because of this,
         * default pushbuttons look way too wide in the filterbar...
         * so this routine recalculates the sizeHint without icons.
         * If there's a Right Way to do this that I've missed, let me know */
        button->setMaximumSize( calculateTextButtonSizeHint( button ) );
    }
}


TrMainWindow :: TrMainWindow( Session& session, Prefs& prefs, TorrentModel& model, bool minimized ):
    myLastFullUpdateTime( 0 ),
    myPrefsDialog( new PrefsDialog( session, prefs, this ) ),
    myAboutDialog( new AboutDialog( this ) ),
    myStatsDialog( new StatsDialog( session, this ) ),
    myFileDialog( 0 ),
    myFilterModel( prefs ),
    myTorrentDelegate( new TorrentDelegate( this ) ),
    myTorrentDelegateMin( new TorrentDelegateMin( this ) ),
    mySession( session ),
    myPrefs( prefs ),
    myModel( model ),
    mySpeedModeOffIcon( ":/icons/alt-limit-off.png" ),
    mySpeedModeOnIcon( ":/icons/alt-limit-on.png" ),
    myLastSendTime( 0 ),
    myLastReadTime( 0 ),
    myNetworkTimer( this )
{
    QAction * sep = new QAction( this );
    sep->setSeparator( true );

    ui.setupUi( this );

    QString title( "Transmission" );
    const QUrl remoteUrl( session.getRemoteUrl( ) );
    if( !remoteUrl.isEmpty( ) )
        title += tr( " - %1" ).arg( remoteUrl.toString() );
    setWindowTitle( title );

    QStyle * style = this->style();

    int i = style->pixelMetric( QStyle::PM_SmallIconSize, 0, this );
    const QSize smallIconSize( i, i );

    // icons
    ui.action_Add->setIcon( getStockIcon( "list-add", QStyle::SP_DialogOpenButton ) );
    ui.action_New->setIcon( getStockIcon( "document-new", QStyle::SP_DesktopIcon ) );
    ui.action_Properties->setIcon( getStockIcon( "document-properties", QStyle::SP_DesktopIcon ) );
    ui.action_OpenFolder->setIcon( getStockIcon( "folder-open", QStyle::SP_DirOpenIcon ) );
    ui.action_Start->setIcon( getStockIcon( "media-playback-start", QStyle::SP_MediaPlay ) );
    ui.action_Announce->setIcon( getStockIcon( "network-transmit-receive" ) );
    ui.action_Pause->setIcon( getStockIcon( "media-playback-pause", QStyle::SP_MediaPause ) );
    ui.action_Remove->setIcon( getStockIcon( "list-remove", QStyle::SP_TrashIcon ) );
    ui.action_Delete->setIcon( getStockIcon( "edit-delete", QStyle::SP_TrashIcon ) );
    ui.action_StartAll->setIcon( getStockIcon( "media-playback-start", QStyle::SP_MediaPlay ) );
    ui.action_PauseAll->setIcon( getStockIcon( "media-playback-pause", QStyle::SP_MediaPause ) );
    ui.action_Quit->setIcon( getStockIcon( "application-exit" ) );
    ui.action_SelectAll->setIcon( getStockIcon( "edit-select-all" ) );
    ui.action_ReverseSortOrder->setIcon( getStockIcon( "view-sort-ascending", QStyle::SP_ArrowDown ) );
    ui.action_Preferences->setIcon( getStockIcon( "preferences-system" ) );
    ui.action_Contents->setIcon( getStockIcon( "help-contents", QStyle::SP_DialogHelpButton ) );
    ui.action_About->setIcon( getStockIcon( "help-about" ) );
    ui.statusbarStatsButton->setIcon( getStockIcon( "view-refresh", QStyle::SP_BrowserReload ) );
    ui.downloadIconLabel->setPixmap( getStockIcon( "go-down", QStyle::SP_ArrowDown ).pixmap( smallIconSize ) );
    ui.uploadIconLabel->setPixmap( getStockIcon( "go-up", QStyle::SP_ArrowUp ).pixmap( smallIconSize ) );
    ui.filterEntryModeButton->setIcon( getStockIcon( "edit-find", QStyle::SP_ArrowForward ) );
    ui.filterEntryClearButton->setIcon( getStockIcon( "edit-clear", QStyle::SP_DialogCloseButton ) );

    // ui signals
    // ccc
    connect( ui.action_Toolbar, SIGNAL(toggled(bool)), this, SLOT(setToolbarVisible(bool)));
    connect( ui.action_TrayIcon, SIGNAL(toggled(bool)), this, SLOT(setTrayIconVisible(bool)));
    connect( ui.action_Filterbar, SIGNAL(toggled(bool)), this, SLOT(setFilterbarVisible(bool)));
    connect( ui.action_Statusbar, SIGNAL(toggled(bool)), this, SLOT(setStatusbarVisible(bool)));
    connect( ui.action_MinimalView, SIGNAL(toggled(bool)), this, SLOT(setMinimalView(bool)));
    connect( ui.action_SortByActivity, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByActivity()) );
    connect( ui.action_SortByAge, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByAge()) );
    connect( ui.action_SortByETA, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByETA()));
    connect( ui.action_SortByName, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByName()));
    connect( ui.action_SortByProgress, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByProgress()));
    connect( ui.action_SortByRatio, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByRatio()));
    connect( ui.action_SortBySize, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortBySize()));
    connect( ui.action_SortByState, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByState()));
    connect( ui.action_SortByTracker, SIGNAL(toggled(bool)), &myFilterModel, SLOT(sortByTracker()));
    connect( ui.action_ReverseSortOrder, SIGNAL(toggled(bool)), &myFilterModel, SLOT(setAscending(bool)));
    connect( ui.action_Start, SIGNAL(triggered()), this, SLOT(startSelected()));
    connect( ui.action_Pause, SIGNAL(triggered()), this, SLOT(pauseSelected()));
    connect( ui.action_Remove, SIGNAL(triggered()), this, SLOT(removeSelected()));
    connect( ui.action_Delete, SIGNAL(triggered()), this, SLOT(deleteSelected()));
    connect( ui.action_Verify, SIGNAL(triggered()), this, SLOT(verifySelected()) );
    connect( ui.action_Announce, SIGNAL(triggered()), this, SLOT(reannounceSelected()) );
    connect( ui.action_StartAll, SIGNAL(triggered()), this, SLOT(startAll()));
    connect( ui.action_PauseAll, SIGNAL(triggered()), this, SLOT(pauseAll()));
    connect( ui.action_Add, SIGNAL(triggered()), this, SLOT(openTorrent()));
    connect( ui.action_New, SIGNAL(triggered()), this, SLOT(newTorrent()));
    connect( ui.action_Preferences, SIGNAL(triggered()), myPrefsDialog, SLOT(show()));
    connect( ui.action_Statistics, SIGNAL(triggered()), myStatsDialog, SLOT(show()));
    connect( ui.action_About, SIGNAL(triggered()), myAboutDialog, SLOT(show()));
    connect( ui.action_Contents, SIGNAL(triggered()), this, SLOT(openHelp()));
    connect( ui.action_OpenFolder, SIGNAL(triggered()), this, SLOT(openFolder()));
    connect( ui.action_Properties, SIGNAL(triggered()), this, SLOT(openProperties()));
    connect( ui.listView, SIGNAL(activated(const QModelIndex&)), ui.action_Properties, SLOT(trigger()));

    // context menu
    QList<QAction*> actions;
    actions << ui.action_Properties
            << ui.action_OpenFolder
            << sep
            << ui.action_Start
            << ui.action_Pause
            << ui.action_Verify
            << ui.action_Announce
            << sep
            << ui.action_Remove
            << ui.action_Delete;
    addActions( actions );
    setContextMenuPolicy( Qt::ActionsContextMenu );

    // signals
    connect( ui.speedLimitModeButton, SIGNAL(clicked()), this, SLOT(toggleSpeedMode()));
    connect( ui.filterAll, SIGNAL(clicked()), this, SLOT(showAll()));
    connect( ui.filterActive, SIGNAL(clicked()), this, SLOT(showActive()));
    connect( ui.filterDownloading, SIGNAL(clicked()), this, SLOT(showDownloading()));
    connect( ui.filterSeeding, SIGNAL(clicked()), this, SLOT(showSeeding()));
    connect( ui.filterPaused, SIGNAL(clicked()), this, SLOT(showPaused()));
    connect( ui.filterEntryClearButton, SIGNAL(clicked()), ui.filterEntry, SLOT(clear()));
    connect( ui.filterEntry, SIGNAL(textChanged(QString)), &myFilterModel, SLOT(setText(QString)));
    connect( ui.action_SelectAll, SIGNAL(triggered()), ui.listView, SLOT(selectAll()));
    connect( ui.action_DeselectAll, SIGNAL(triggered()), ui.listView, SLOT(clearSelection()));
    setTextButtonSizeHint( ui.filterAll );
    setTextButtonSizeHint( ui.filterActive );
    setTextButtonSizeHint( ui.filterDownloading );
    setTextButtonSizeHint( ui.filterSeeding );
    setTextButtonSizeHint( ui.filterPaused );
    setShowMode( myFilterModel.getShowMode( ) );

    connect( &myFilterModel, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(refreshVisibleCount()));
    connect( &myFilterModel, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(refreshVisibleCount()));
    connect( &myFilterModel, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(refreshActionSensitivity()));
    connect( &myFilterModel, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(refreshActionSensitivity()));

    connect( ui.action_Quit, SIGNAL(triggered()), QCoreApplication::instance(), SLOT(quit()) );

    // torrent view
    myFilterModel.setSourceModel( &myModel );
    ui.listView->setModel( &myFilterModel );
    connect( ui.listView->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&,const QItemSelection&)), this, SLOT(refreshActionSensitivity()));

    QActionGroup * actionGroup = new QActionGroup( this );
    actionGroup->addAction( ui.action_FilterByName );
    actionGroup->addAction( ui.action_FilterByFiles );
    actionGroup->addAction( ui.action_FilterByTracker );
    QMenu * menu = new QMenu( );
    menu->addAction( ui.action_FilterByName );
    menu->addAction( ui.action_FilterByFiles );
    menu->addAction( ui.action_FilterByTracker );
    ui.filterEntryModeButton->setMenu( menu );
    connect( ui.action_FilterByName, SIGNAL(triggered()), this, SLOT(filterByName()));
    connect( ui.action_FilterByFiles, SIGNAL(triggered()), this, SLOT(filterByFiles()));
    connect( ui.action_FilterByTracker, SIGNAL(triggered()), this, SLOT(filterByTracker()));
    ui.action_FilterByName->setChecked( true );

    actionGroup = new QActionGroup( this );
    actionGroup->addAction( ui.action_TotalRatio );
    actionGroup->addAction( ui.action_TotalTransfer );
    actionGroup->addAction( ui.action_SessionRatio );
    actionGroup->addAction( ui.action_SessionTransfer );
    menu = new QMenu( );
    menu->addAction( ui.action_TotalRatio );
    menu->addAction( ui.action_TotalTransfer );
    menu->addAction( ui.action_SessionRatio );
    menu->addAction( ui.action_SessionTransfer );
    connect( ui.action_TotalRatio, SIGNAL(triggered()), this, SLOT(showTotalRatio()));
    connect( ui.action_TotalTransfer, SIGNAL(triggered()), this, SLOT(showTotalTransfer()));
    connect( ui.action_SessionRatio, SIGNAL(triggered()), this, SLOT(showSessionRatio()));
    connect( ui.action_SessionTransfer, SIGNAL(triggered()), this, SLOT(showSessionTransfer()));
    ui.statusbarStatsButton->setMenu( menu );

    actionGroup = new QActionGroup( this );
    actionGroup->addAction( ui.action_SortByActivity );
    actionGroup->addAction( ui.action_SortByAge );
    actionGroup->addAction( ui.action_SortByETA );
    actionGroup->addAction( ui.action_SortByName );
    actionGroup->addAction( ui.action_SortByProgress );
    actionGroup->addAction( ui.action_SortByRatio );
    actionGroup->addAction( ui.action_SortBySize );
    actionGroup->addAction( ui.action_SortByState );
    actionGroup->addAction( ui.action_SortByTracker );

    menu = new QMenu( );
    menu->addAction( ui.action_Add );
    menu->addSeparator( );
    menu->addAction( ui.action_ShowMainWindow );
    menu->addAction( ui.action_ShowMessageLog );
    menu->addAction( ui.action_About );
    menu->addSeparator( );
    menu->addAction( ui.action_StartAll );
    menu->addAction( ui.action_PauseAll );
    menu->addSeparator( );
    menu->addAction( ui.action_Quit );
    myTrayIcon.setContextMenu( menu );
    myTrayIcon.setIcon( QApplication::windowIcon( ) );

    connect( ui.action_ShowMainWindow, SIGNAL(toggled(bool)), this, SLOT(toggleWindows()));
    connect( &myTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this, SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));
    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)) );

    ui.action_ShowMainWindow->setChecked( !minimized );
    ui.action_TrayIcon->setChecked( minimized || prefs.getBool( Prefs::SHOW_TRAY_ICON ) );

    QList<int> initKeys;
    initKeys << Prefs :: MAIN_WINDOW_X
             << Prefs :: SHOW_TRAY_ICON
             << Prefs :: SORT_REVERSED
             << Prefs :: SORT_MODE
             << Prefs :: FILTERBAR
             << Prefs :: STATUSBAR
             << Prefs :: STATUSBAR_STATS
             << Prefs :: TOOLBAR
             << Prefs :: ALT_SPEED_LIMIT_ENABLED
             << Prefs :: MINIMAL_VIEW;
    foreach( int key, initKeys )
        refreshPref( key );

    connect( &mySession, SIGNAL(statsUpdated()), this, SLOT(refreshStatusBar()) );
    connect( &mySession, SIGNAL(dataReadProgress()), this, SLOT(dataReadProgress()) );
    connect( &mySession, SIGNAL(dataSendProgress()), this, SLOT(dataSendProgress()) );

    if( mySession.isServer( ) )
        ui.networkLabel->hide( );
    else {
        connect( &myNetworkTimer, SIGNAL(timeout()), this, SLOT(onNetworkTimer()));
        myNetworkTimer.start( 1000 );
    }

    refreshActionSensitivity( );
    refreshStatusBar( );
    refreshVisibleCount( );
}

TrMainWindow :: ~TrMainWindow( )
{
}

/****
*****
****/

void
TrMainWindow :: openProperties( )
{
    const int id( *getSelectedTorrents().begin() );
    Torrent * torrent( myModel.getTorrentFromId( id ) );
    assert( torrent != 0 );
    QDialog * d( new Details( mySession, *torrent, this ) );
    d->show( );
}

void
TrMainWindow :: openFolder( )
{
    const int torrentId( *getSelectedTorrents().begin() );
    const Torrent * tor( myModel.getTorrentFromId( torrentId ) );
    const QString path( tor->getPath( ) );
    QDesktopServices :: openUrl( QUrl::fromLocalFile( path ) );
}

void
TrMainWindow :: openHelp( )
{
    const char * fmt = "http://www.transmissionbt.com/help/gtk/%d.%dx";
    int major, minor;
    sscanf( SHORT_VERSION_STRING, "%d.%d", &major, &minor );
    char url[128];
    snprintf( url, sizeof( url ), fmt, major, minor/10 );
    QDesktopServices :: openUrl( QUrl( QString( url ) ) );
}

void
TrMainWindow :: refreshVisibleCount( )
{
    const int visibleCount( myFilterModel.rowCount( ) );
    const int totalCount( visibleCount + myFilterModel.hiddenRowCount( ) );
    QString str;
    if( visibleCount == totalCount )
        str = tr( "%Ln Torrent(s)", 0, totalCount );
    else
        str = tr( "%L1 of %Ln Torrent(s)", 0, totalCount ).arg( visibleCount );
    ui.visibleCountLabel->setText( str );
}

void
TrMainWindow :: refreshStatusBar( )
{
    const Speed up( myModel.getUploadSpeed( ) );
    const Speed down( myModel.getDownloadSpeed( ) );
    ui.uploadTextLabel->setText( Utils :: speedToString( up ) );
    ui.downloadTextLabel->setText( Utils :: speedToString( down ) );
    const QString mode( myPrefs.getString( Prefs::STATUSBAR_STATS ) );
    QString str;

    if( mode == "session-ratio" )
    {
        str = tr( "Ratio: %1" ).arg( Utils :: ratioToString( mySession.getStats().ratio ) );
    }
    else if( mode == "session-transfer" )
    {
        const tr_session_stats& stats( mySession.getStats( ) );
        str = tr( "Down: %1, Up: %2" ).arg( Utils :: sizeToString( stats.downloadedBytes ) )
                                      .arg( Utils :: sizeToString( stats.uploadedBytes ) );
    }
    else if( mode == "total-transfer" )
    {
        const tr_session_stats& stats( mySession.getCumulativeStats( ) );
        str = tr( "Down: %1, Up: %2" ).arg( Utils :: sizeToString( stats.downloadedBytes ) )
                                      .arg( Utils :: sizeToString( stats.uploadedBytes ) );
    }
    else /* default is "total-ratio" */
    {
        str = tr( "Ratio: %1" ).arg( Utils :: ratioToString( mySession.getCumulativeStats().ratio ) );
    }

    ui.statusbarStatsLabel->setText( str );
}

void
TrMainWindow :: refreshActionSensitivity( )
{
    int selected( 0 );
    int paused( 0 );
    int selectedAndPaused( 0 );
    int canAnnounce( 0 );
    const QAbstractItemModel * model( ui.listView->model( ) );
    const QItemSelectionModel * selectionModel( ui.listView->selectionModel( ) );
    const int rowCount( model->rowCount( ) );

    /* count how many torrents are selected, paused, etc */
    for( int row=0; row<rowCount; ++row ) {
        const QModelIndex modelIndex( model->index( row, 0 ) );
        assert( model == modelIndex.model( ) );
        const Torrent * tor( model->data( modelIndex, TorrentModel::TorrentRole ).value<const Torrent*>( ) );
        const bool isSelected( selectionModel->isSelected( modelIndex ) );
        const bool isPaused( tor->isPaused( ) );
        if( isSelected )
            ++selected;
        if( isPaused )
            ++ paused;
        if( isSelected && isPaused )
            ++selectedAndPaused;
        if( tor->canManualAnnounce( ) )
            ++canAnnounce;
    }

    const bool haveSelection( selected > 0 );
    ui.action_Verify->setEnabled( haveSelection );
    ui.action_Remove->setEnabled( haveSelection );
    ui.action_Delete->setEnabled( haveSelection );
    ui.action_DeselectAll->setEnabled( haveSelection );

    const bool oneSelection( selected == 1 );
    ui.action_Properties->setEnabled( oneSelection );
    ui.action_OpenFolder->setEnabled( oneSelection );

    ui.action_SelectAll->setEnabled( selected < rowCount );
    ui.action_StartAll->setEnabled( paused > 0 );
    ui.action_PauseAll->setEnabled( paused < rowCount );
    ui.action_Start->setEnabled( selectedAndPaused > 0 );
    ui.action_Pause->setEnabled( selectedAndPaused < selected );
    ui.action_Announce->setEnabled( selected > 0 && ( canAnnounce == selected ) );
}

/**
***
**/

void
TrMainWindow :: clearSelection( )
{
    ui.action_DeselectAll->trigger( );
}

QSet<int>
TrMainWindow :: getSelectedTorrents( ) const
{
    QSet<int> ids;

    foreach( QModelIndex index, ui.listView->selectionModel( )->selectedRows( ) )
    {
        const Torrent * tor( index.model()->data( index, TorrentModel::TorrentRole ).value<const Torrent*>( ) );
        ids.insert( tor->id( ) );
    }

    return ids;
}

void
TrMainWindow :: startSelected( )
{
    mySession.start( getSelectedTorrents( ) );
}
void
TrMainWindow :: pauseSelected( )
{
    mySession.pause( getSelectedTorrents( ) );
}
void
TrMainWindow :: startAll( )
{
    mySession.start( );
}
void
TrMainWindow :: pauseAll( )
{
    mySession.pause( );
}
void
TrMainWindow :: removeSelected( )
{
    mySession.removeTorrents( getSelectedTorrents( ), false );
}
void
TrMainWindow :: deleteSelected( )
{
    mySession.removeTorrents( getSelectedTorrents( ), true );
}
void
TrMainWindow :: verifySelected( )
{
    mySession.verifyTorrents( getSelectedTorrents( ) );
}
void
TrMainWindow :: reannounceSelected( )
{
    mySession.reannounceTorrents( getSelectedTorrents( ) );
}

/**
***
**/

void
TrMainWindow :: setShowMode( TorrentFilter :: ShowMode mode )
{
    ui.filterAll->setChecked( mode == TorrentFilter::SHOW_ALL );
    ui.filterActive->setChecked( mode == TorrentFilter::SHOW_ACTIVE );
    ui.filterDownloading->setChecked( mode == TorrentFilter::SHOW_DOWNLOADING );
    ui.filterSeeding->setChecked( mode == TorrentFilter::SHOW_SEEDING );
    ui.filterPaused->setChecked( mode == TorrentFilter::SHOW_PAUSED );

    myFilterModel.setShowMode( mode );
}

void TrMainWindow :: showAll         ( ) { setShowMode( TorrentFilter :: SHOW_ALL ); }
void TrMainWindow :: showActive      ( ) { setShowMode( TorrentFilter :: SHOW_ACTIVE ); }
void TrMainWindow :: showDownloading ( ) { setShowMode( TorrentFilter :: SHOW_DOWNLOADING ); }
void TrMainWindow :: showSeeding     ( ) { setShowMode( TorrentFilter :: SHOW_SEEDING ); }
void TrMainWindow :: showPaused      ( ) { setShowMode( TorrentFilter :: SHOW_PAUSED ); }

void TrMainWindow :: filterByName    ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_NAME ); }
void TrMainWindow :: filterByTracker ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_TRACKER ); }
void TrMainWindow :: filterByFiles   ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_FILES ); }

void
TrMainWindow :: showTotalRatio( )
{
    myPrefs.set( Prefs::STATUSBAR_STATS, "total-ratio" );
}
void
TrMainWindow :: showTotalTransfer( )
{
    myPrefs.set( Prefs::STATUSBAR_STATS, "total-transfer" );
}
void
TrMainWindow :: showSessionRatio( )
{
    myPrefs.set( Prefs::STATUSBAR_STATS, "session-ratio" );
}
void
TrMainWindow :: showSessionTransfer( )
{
    myPrefs.set( Prefs::STATUSBAR_STATS, "session-transfer" );
}

/**
***
**/

void
TrMainWindow :: setMinimalView( bool visible )
{
    myPrefs.set( Prefs :: MINIMAL_VIEW, visible );
}
void
TrMainWindow :: setTrayIconVisible( bool visible )
{
    myPrefs.set( Prefs :: SHOW_TRAY_ICON, visible );
}
void
TrMainWindow :: toggleSpeedMode( )
{
    myPrefs.toggleBool( Prefs :: ALT_SPEED_LIMIT_ENABLED );
}
void
TrMainWindow :: setToolbarVisible( bool visible )
{
    myPrefs.set( Prefs::TOOLBAR, visible );
}
void
TrMainWindow :: setFilterbarVisible( bool visible )
{
    myPrefs.set( Prefs::FILTERBAR, visible );
}
void
TrMainWindow :: setStatusbarVisible( bool visible )
{
    myPrefs.set( Prefs::STATUSBAR, visible );
}

/**
***
**/

void
TrMainWindow :: toggleWindows( )
{
    setVisible( !isVisible( ) );
}

void
TrMainWindow :: trayActivated( QSystemTrayIcon::ActivationReason reason )
{
    if( reason == QSystemTrayIcon::Trigger )
        ui.action_ShowMainWindow->toggle( );
}


void
TrMainWindow :: refreshPref( int key )
{
    bool b;
    QString str;

    switch( key )
    {
        case Prefs::STATUSBAR_STATS:
            str = myPrefs.getString( key );
            ui.action_TotalRatio->setChecked     ( str == "total-ratio" );
            ui.action_TotalTransfer->setChecked  ( str == "total-transfer" );
            ui.action_SessionRatio->setChecked   ( str == "session-ratio" );
            ui.action_SessionTransfer->setChecked( str == "session-transfer" );
            refreshStatusBar( );
            break;

        case Prefs::SORT_REVERSED:
            ui.action_ReverseSortOrder->setChecked( myPrefs.getBool( key ) );
            break;

        case Prefs::SORT_MODE:
            str = myPrefs.getString( key );
            ui.action_SortByActivity->setChecked ( str == "sort-by-activity" );
            ui.action_SortByAge->setChecked      ( str == "sort-by-age" );
            ui.action_SortByETA->setChecked      ( str == "sort-by-eta" );
            ui.action_SortByName->setChecked     ( str == "sort-by-name" );
            ui.action_SortByProgress->setChecked ( str == "sort-by-progress" );
            ui.action_SortByRatio->setChecked    ( str == "sort-by-ratio" );
            ui.action_SortBySize->setChecked     ( str == "sort-by-size" );
            ui.action_SortByState->setChecked    ( str == "sort-by-state" );
            ui.action_SortByTracker->setChecked  ( str == "sort-by-tracker" );
            break;

        case Prefs::FILTERBAR:
            b = myPrefs.getBool( key );
            ui.filterbar->setVisible( b );
            ui.action_Filterbar->setChecked( b );
            break;

        case Prefs::STATUSBAR:
            b = myPrefs.getBool( key );
            ui.statusbar->setVisible( b );
            ui.action_Statusbar->setChecked( b );
            break;

        case Prefs::TOOLBAR:
            b = myPrefs.getBool( key );
            ui.toolBar->setVisible( b );
            ui.action_Toolbar->setChecked( b );
            break;

        case Prefs::SHOW_TRAY_ICON:
            b = myPrefs.getBool( key );
            ui.action_TrayIcon->setChecked( b );
            myTrayIcon.setVisible( b );
            break;

        case Prefs::MINIMAL_VIEW:
            b = myPrefs.getBool( key );
            ui.action_MinimalView->setChecked( b );
            ui.listView->setItemDelegate( b ? myTorrentDelegateMin : myTorrentDelegate );
            ui.listView->reset( ); // force the rows to resize
            break;

        case Prefs::MAIN_WINDOW_X:
        case Prefs::MAIN_WINDOW_Y:
        case Prefs::MAIN_WINDOW_WIDTH:
        case Prefs::MAIN_WINDOW_HEIGHT:
            setGeometry( myPrefs.getInt( Prefs::MAIN_WINDOW_X ),
                         myPrefs.getInt( Prefs::MAIN_WINDOW_Y ),
                         myPrefs.getInt( Prefs::MAIN_WINDOW_WIDTH ),
                         myPrefs.getInt( Prefs::MAIN_WINDOW_HEIGHT ) );
            break;

        case Prefs :: ALT_SPEED_LIMIT_ENABLED:
            b = myPrefs.getBool( key );
            ui.speedLimitModeButton->setChecked( b );
            ui.speedLimitModeButton->setIcon( b ? mySpeedModeOnIcon : mySpeedModeOffIcon );
            ui.speedLimitModeButton->setToolTip( b ? tr( "Click to disable Speed Limit Mode" )
                                                   : tr( "Click to enable Speed Limit Mode" ) );
            break;

        default:
            break;
    }
}

/***
****
***/

void
TrMainWindow :: newTorrent( )
{
    MakeDialog * d = new MakeDialog( mySession, this );
    d->show( );
}

void
TrMainWindow :: openTorrent( )
{
    if( myFileDialog == 0 )
    {
        myFileDialog = new QFileDialog( this,
                                        tr( "Add Torrent" ),
                                        myPrefs.getString( Prefs::OPEN_DIALOG_FOLDER ),
                                        tr( "Torrent Files (*.torrent);;All Files (*.*)" ) );
        myFileDialog->setFileMode( QFileDialog::ExistingFiles );


        QCheckBox * button = new QCheckBox( tr( "Display &options dialog" ) );
        button->setChecked( myPrefs.getBool( Prefs::OPTIONS_PROMPT ) );
        QGridLayout * layout = dynamic_cast<QGridLayout*>(myFileDialog->layout());
        layout->addWidget( button, layout->rowCount( ), 0, 1, -1, Qt::AlignLeft );
        myFileDialogOptionsCheck = button;

        connect( myFileDialog, SIGNAL(filesSelected(const QStringList&)), this, SLOT(addTorrents(const QStringList&)));
    }

    myFileDialog->show( );
}

void
TrMainWindow :: addTorrents( const QStringList& filenames )
{
    foreach( const QString& filename, filenames )
        addTorrent( filename );
}

void
TrMainWindow :: addTorrent( const QString& filename )
{
    if( !myFileDialogOptionsCheck->isChecked( ) ) {
        mySession.addTorrent( filename );
        QApplication :: alert ( this );
    } else {
        Options * o = new Options( mySession, myPrefs, filename, this );
        o->show( );
        QApplication :: alert( o );
    }
}

/***
****
***/

void
TrMainWindow :: updateNetworkIcon( )
{
    const time_t now = time( NULL );
    const int period = 3;
    const bool isSending = now - myLastSendTime <= period;
    const bool isReading = now - myLastReadTime <= period;
    const char * key;

    if( isSending && isReading )
        key = "network-transmit-receive";
    else if( isSending )
        key = "network-transmit";
    else if( isReading )
        key = "network-receive";
    else
        key = "network-idle";

    QIcon icon = getStockIcon( key, QStyle::SP_DriveNetIcon );
    QPixmap pixmap = icon.pixmap ( 16, 16 );
    ui.networkLabel->setPixmap( pixmap );
    ui.networkLabel->setToolTip( isSending || isReading
        ? tr( "Transmission server is responding" )
        : tr( "Last response from server was %1 ago" ).arg( Utils::timeToString( now-std::max(myLastReadTime,myLastSendTime))));
}

void
TrMainWindow :: onNetworkTimer( )
{
    updateNetworkIcon( );
}

void
TrMainWindow :: dataReadProgress( )
{
    myLastReadTime = time( NULL );
    updateNetworkIcon( );
}

void
TrMainWindow :: dataSendProgress( )
{
    myLastSendTime = time( NULL );
    updateNetworkIcon( );
}
