/*
 * This file Copyright (C) 2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include <cassert>
#include <iostream>

#include <QCheckBox>
#include <QDesktopServices>
#include <QFileDialog>
#include <QLabel>
#include <QSize>
#include <QStyle>
#include <QHBoxLayout>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QSignalMapper>

#include <libtransmission/version.h>

#include "about.h"
#include "details.h"
#include "filters.h"
#include "hig.h"
#include "mainwin.h"
#include "make-dialog.h"
#include "options.h"
#include "prefs.h"
#include "prefs-dialog.h"
#include "relocate.h"
#include "session.h"
#include "session-dialog.h"
#include "speed.h"
#include "stats-dialog.h"
#include "torrent-delegate.h"
#include "torrent-delegate-min.h"
#include "torrent-filter.h"
#include "torrent-model.h"
#include "triconpushbutton.h"
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
}


TrMainWindow :: TrMainWindow( Session& session, Prefs& prefs, TorrentModel& model, bool minimized ):
    myLastFullUpdateTime( 0 ),
    mySessionDialog( new SessionDialog( session, prefs, this ) ),
    myPrefsDialog( new PrefsDialog( session, prefs, this ) ),
    myAboutDialog( new AboutDialog( this ) ),
    myStatsDialog( new StatsDialog( session, this ) ),
    myDetailsDialog( 0 ),
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

    // ui signals
    connect( ui.action_Toolbar, SIGNAL(toggled(bool)), this, SLOT(setToolbarVisible(bool)));
    connect( ui.action_TrayIcon, SIGNAL(toggled(bool)), this, SLOT(setTrayIconVisible(bool)));
    connect( ui.action_Filterbar, SIGNAL(toggled(bool)), this, SLOT(setFilterbarVisible(bool)));
    connect( ui.action_Statusbar, SIGNAL(toggled(bool)), this, SLOT(setStatusbarVisible(bool)));
    connect( ui.action_MinimalView, SIGNAL(toggled(bool)), this, SLOT(setMinimalView(bool)));
    connect( ui.action_SortByActivity, SIGNAL(toggled(bool)), this, SLOT(onSortByActivityToggled(bool)));
    connect( ui.action_SortByAge,      SIGNAL(toggled(bool)), this, SLOT(onSortByAgeToggled(bool)));
    connect( ui.action_SortByETA,      SIGNAL(toggled(bool)), this, SLOT(onSortByETAToggled(bool)));
    connect( ui.action_SortByName,     SIGNAL(toggled(bool)), this, SLOT(onSortByNameToggled(bool)));
    connect( ui.action_SortByProgress, SIGNAL(toggled(bool)), this, SLOT(onSortByProgressToggled(bool)));
    connect( ui.action_SortByRatio,    SIGNAL(toggled(bool)), this, SLOT(onSortByRatioToggled(bool)));
    connect( ui.action_SortBySize,     SIGNAL(toggled(bool)), this, SLOT(onSortBySizeToggled(bool)));
    connect( ui.action_SortByState,    SIGNAL(toggled(bool)), this, SLOT(onSortByStateToggled(bool)));
    connect( ui.action_SortByTracker,  SIGNAL(toggled(bool)), this, SLOT(onSortByTrackerToggled(bool)));
    connect( ui.action_ReverseSortOrder, SIGNAL(toggled(bool)), this, SLOT(setSortAscendingPref(bool)));
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
    connect( ui.action_SetLocation, SIGNAL(triggered()), this, SLOT(setLocation()));
    connect( ui.action_Properties, SIGNAL(triggered()), this, SLOT(openProperties()));
    connect( ui.action_SessionDialog, SIGNAL(triggered()), mySessionDialog, SLOT(show()));
    connect( ui.listView, SIGNAL(activated(const QModelIndex&)), ui.action_Properties, SLOT(trigger()));

    QAction * sep2 = new QAction( this );
    sep2->setSeparator( true );

    // context menu
    QList<QAction*> actions;
    actions << ui.action_Properties
            << ui.action_OpenFolder
            << ui.action_SetLocation
            << sep2
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
    connect( ui.action_SelectAll, SIGNAL(triggered()), ui.listView, SLOT(selectAll()));
    connect( ui.action_DeselectAll, SIGNAL(triggered()), ui.listView, SLOT(clearSelection()));

    connect( &myFilterModel, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(refreshVisibleCount()));
    connect( &myFilterModel, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(refreshVisibleCount()));
    connect( &myFilterModel, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(refreshActionSensitivity()));
    connect( &myFilterModel, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(refreshActionSensitivity()));

    connect( ui.action_Quit, SIGNAL(triggered()), QCoreApplication::instance(), SLOT(quit()) );

    // torrent view
    myFilterModel.setSourceModel( &myModel );
    connect( &myModel, SIGNAL(modelReset()), this, SLOT(onModelReset()));
    connect( &myModel, SIGNAL(rowsRemoved(const QModelIndex&,int,int)), this, SLOT(onModelReset()));
    connect( &myModel, SIGNAL(rowsInserted(const QModelIndex&,int,int)), this, SLOT(onModelReset()));
    ui.listView->setModel( &myFilterModel );
    connect( ui.listView->selectionModel(), SIGNAL(selectionChanged(const QItemSelection&,const QItemSelection&)), this, SLOT(refreshActionSensitivity()));

    QActionGroup * actionGroup = new QActionGroup( this );
    actionGroup->addAction( ui.action_SortByActivity );
    actionGroup->addAction( ui.action_SortByAge );
    actionGroup->addAction( ui.action_SortByETA );
    actionGroup->addAction( ui.action_SortByName );
    actionGroup->addAction( ui.action_SortByProgress );
    actionGroup->addAction( ui.action_SortByRatio );
    actionGroup->addAction( ui.action_SortBySize );
    actionGroup->addAction( ui.action_SortByState );
    actionGroup->addAction( ui.action_SortByTracker );

    QMenu * menu = new QMenu( );
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

    connect( &myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)) );
    connect( ui.action_ShowMainWindow, SIGNAL(toggled(bool)), this, SLOT(toggleWindows(bool)));
    connect( &myTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
             this, SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));

    ui.action_ShowMainWindow->setChecked( !minimized );
    ui.action_TrayIcon->setChecked( minimized || prefs.getBool( Prefs::SHOW_TRAY_ICON ) );

    ui.verticalLayout->addWidget( createStatusBar( ) );
    ui.verticalLayout->insertWidget( 0, createFilterBar( ) );

    QList<int> initKeys;
    initKeys << Prefs :: MAIN_WINDOW_X
             << Prefs :: SHOW_TRAY_ICON
             << Prefs :: SORT_REVERSED
             << Prefs :: SORT_MODE
             << Prefs :: FILTER_MODE
             << Prefs :: FILTERBAR
             << Prefs :: STATUSBAR
             << Prefs :: STATUSBAR_STATS
             << Prefs :: TOOLBAR
             << Prefs :: ALT_SPEED_LIMIT_ENABLED
             << Prefs :: MINIMAL_VIEW
             << Prefs :: DSPEED
             << Prefs :: DSPEED_ENABLED
             << Prefs :: USPEED
             << Prefs :: USPEED_ENABLED
             << Prefs :: RATIO
             << Prefs :: RATIO_ENABLED;
    foreach( int key, initKeys )
        refreshPref( key );

    connect( &mySession, SIGNAL(sourceChanged()), this, SLOT(onSessionSourceChanged()) );
    connect( &mySession, SIGNAL(statsUpdated()), this, SLOT(refreshStatusBar()) );
    connect( &mySession, SIGNAL(dataReadProgress()), this, SLOT(dataReadProgress()) );
    connect( &mySession, SIGNAL(dataSendProgress()), this, SLOT(dataSendProgress()) );
    connect( &mySession, SIGNAL(httpAuthenticationRequired()), this, SLOT(wrongAuthentication()) );

    if( mySession.isServer( ) )
        myNetworkLabel->hide( );
    else {
        connect( &myNetworkTimer, SIGNAL(timeout()), this, SLOT(onNetworkTimer()));
        myNetworkTimer.start( 1000 );
    }

    refreshActionSensitivity( );
    refreshStatusBar( );
    refreshTitle( );
    refreshVisibleCount( );
}

TrMainWindow :: ~TrMainWindow( )
{
}

/****
*****
****/

void
TrMainWindow :: onSessionSourceChanged( )
{
    myModel.clear( );
}

void
TrMainWindow :: onModelReset( )
{
    refreshTitle( );
    refreshVisibleCount( );
    refreshActionSensitivity( );
    refreshStatusBar( );
}

/****
*****
****/

#define PREF_VARIANTS_KEY "pref-variants-list"

void
TrMainWindow :: onSetPrefs( )
{
    const QVariantList p = sender()->property( PREF_VARIANTS_KEY ).toList( );
    assert( ( p.size( ) % 2 ) == 0 );
    for( int i=0, n=p.size(); i<n; i+=2 )
        myPrefs.set( p[i].toInt(), p[i+1] );
}

void
TrMainWindow :: onSetPrefs( bool isChecked )
{
    if( isChecked )
        onSetPrefs( );
}

#define SHOW_KEY "show-mode"

void
TrMainWindow :: onShowModeClicked( )
{
    setShowMode( sender()->property(SHOW_KEY).toInt() );
}

QWidget *
TrMainWindow :: createFilterBar( )
{
    int i;
    QMenu * m;
    QLineEdit * e;
    QPushButton * p;
    QHBoxLayout * h;
    QActionGroup * a;
    const int smallSize = style( )->pixelMetric( QStyle::PM_SmallIconSize, 0, this );
    const QSize smallIconSize( smallSize, smallSize );

    QWidget * top = myFilterBar = new QWidget;
    h = new QHBoxLayout( top );
    h->setContentsMargins( HIG::PAD_SMALL, HIG::PAD_SMALL, HIG::PAD_SMALL, HIG::PAD_SMALL );
    h->setSpacing( HIG::PAD_SMALL );
#ifdef Q_OS_MAC
    top->setStyleSheet( "QPushButton{ "
                        "  border-radius: 10px; "
                        "  padding: 0 5px; "
                        "  border: 1px none; "
                        "} "
                        "QPushButton:pressed, QPushButton:checked{ "
                        "  border-width: 1px; "
                        "  border-style: solid; "
                        "  border-color: #5f5f5f #979797 #979797; "
                        "  background-color: #979797; "
                        "  color: white; "
                        "} ");
#endif

        QList<QString> titles;
        titles << tr( "A&ll" ) << tr( "&Active" ) << tr( "&Downloading" ) << tr( "&Seeding" ) << tr( "&Paused" );
        for( i=0; i<titles.size(); ++i ) {
            p = myFilterButtons[i] = new QPushButton( titles[i] );
            p->setProperty( SHOW_KEY, i );
            p->setFlat( true );
            p->setCheckable( true );
            p->setMaximumSize( calculateTextButtonSizeHint( p ) );
            connect( p, SIGNAL(clicked()), this, SLOT(onShowModeClicked()));
            h->addWidget( p );
        }

    h->addStretch( 1 );

        a = new QActionGroup( this );
        a->addAction( ui.action_FilterByName );
        a->addAction( ui.action_FilterByFiles );
        a->addAction( ui.action_FilterByTracker );
        m = new QMenu( );
        m->addAction( ui.action_FilterByName );
        m->addAction( ui.action_FilterByFiles );
        m->addAction( ui.action_FilterByTracker );
        connect( ui.action_FilterByName, SIGNAL(triggered()), this, SLOT(filterByName()));
        connect( ui.action_FilterByFiles, SIGNAL(triggered()), this, SLOT(filterByFiles()));
        connect( ui.action_FilterByTracker, SIGNAL(triggered()), this, SLOT(filterByTracker()));
        ui.action_FilterByName->setChecked( true );
        p = myFilterTextButton = new TrIconPushButton;
        p->setIcon( getStockIcon( "edit-find", QStyle::SP_ArrowForward ) );
        p->setFlat( true );
        p->setMenu( m );
        h->addWidget( p );

        e = myFilterTextLineEdit = new QLineEdit;
        connect( e, SIGNAL(textChanged(QString)), &myFilterModel, SLOT(setText(QString)));
        h->addWidget( e );

        p = myFilterTextButton = new TrIconPushButton;
        p->setIcon( getStockIcon( "edit-clear", QStyle::SP_DialogCloseButton ) );
        p->setFlat( true );
        connect( p, SIGNAL(clicked()), myFilterTextLineEdit, SLOT(clear()));
        h->addWidget( p );

    return top;
}

QWidget *
TrMainWindow :: createStatusBar( )
{
    QMenu * m;
    QLabel *l, *l2;
    QWidget *w;
    QHBoxLayout * h;
    QPushButton * p;
    QActionGroup * a;
    const int i = style( )->pixelMetric( QStyle::PM_SmallIconSize, 0, this );
    const QSize smallIconSize( i, i );

    QWidget * top = myStatusBar = new QWidget;
    h = new QHBoxLayout( top );
    h->setContentsMargins( HIG::PAD_SMALL, HIG::PAD_SMALL, HIG::PAD_SMALL, HIG::PAD_SMALL );
    h->setSpacing( HIG::PAD_SMALL );

        p = myOptionsButton = new TrIconPushButton( this );
        p->setIcon( QIcon( ":/icons/options.png" ) );
        p->setFlat( true );
        p->setMenu( createOptionsMenu( ) );
        h->addWidget( p );

        p = myAltSpeedButton = new TrIconPushButton( this );
        p->setIcon( myPrefs.get<bool>(Prefs::ALT_SPEED_LIMIT_ENABLED) ? mySpeedModeOnIcon : mySpeedModeOffIcon );
        p->setFlat( true );
        h->addWidget( p );
        connect( p, SIGNAL(clicked()), this, SLOT(toggleSpeedMode()));

        l = myNetworkLabel = new QLabel;
        h->addWidget( l );

    h->addStretch( 1 );

        l = myVisibleCountLabel = new QLabel( this );
        h->addWidget( l );

    h->addStretch( 1 );
  
        a = new QActionGroup( this );
        a->addAction( ui.action_TotalRatio );
        a->addAction( ui.action_TotalTransfer );
        a->addAction( ui.action_SessionRatio );
        a->addAction( ui.action_SessionTransfer );
        m = new QMenu( );
        m->addAction( ui.action_TotalRatio );
        m->addAction( ui.action_TotalTransfer );
        m->addAction( ui.action_SessionRatio );
        m->addAction( ui.action_SessionTransfer );
        connect( ui.action_TotalRatio, SIGNAL(triggered()), this, SLOT(showTotalRatio()));
        connect( ui.action_TotalTransfer, SIGNAL(triggered()), this, SLOT(showTotalTransfer()));
        connect( ui.action_SessionRatio, SIGNAL(triggered()), this, SLOT(showSessionRatio()));
        connect( ui.action_SessionTransfer, SIGNAL(triggered()), this, SLOT(showSessionTransfer()));
        p = myStatsModeButton = new TrIconPushButton( this );
        p->setIcon( getStockIcon( "view-refresh", QStyle::SP_BrowserReload ) );
        p->setFlat( true );
        p->setMenu( m );
        h->addWidget( p );  
        l = myStatsLabel = new QLabel( this );
        h->addWidget( l );  
   
        w = new QWidget( this );
        w->setMinimumSize( HIG::PAD_BIG, 1 );
        w->setMaximumSize( HIG::PAD_BIG, 1 );
        h->addWidget( w );
        l = new QLabel( this );
        l->setPixmap( getStockIcon( "go-down", QStyle::SP_ArrowDown ).pixmap( smallIconSize ) );
        h->addWidget( l );
        l2 = myDownloadSpeedLabel = new QLabel( this );
        h->addWidget( l2 );
        myDownStatusWidgets << w << l << l2;

        w = new QWidget( this );
        w->setMinimumSize( HIG::PAD_BIG, 1 );
        w->setMaximumSize( HIG::PAD_BIG, 1 );
        h->addWidget( w );
        l = new QLabel;
        l->setPixmap( getStockIcon( "go-up", QStyle::SP_ArrowUp ).pixmap( smallIconSize ) );
        h->addWidget( l );
        l2 = myUploadSpeedLabel = new QLabel;
        h->addWidget( l2 );
        myUpStatusWidgets << w << l << l2;

    return top;
}

QMenu *
TrMainWindow :: createOptionsMenu( )
{
    QMenu * menu;
    QMenu * sub;
    QAction * a;
    QActionGroup * g;

    QList<int> stockSpeeds;
    stockSpeeds << 5 << 10 << 20 << 30 << 40 << 50 << 75 << 100 << 150 << 200 << 250 << 500 << 750;
    QList<double> stockRatios;
    stockRatios << 0.25 << 0.50 << 0.75 << 1 << 1.5 << 2 << 3;

    menu = new QMenu;
    sub = menu->addMenu( tr( "Limit Download Speed" ) );
        int currentVal = myPrefs.get<int>( Prefs::DSPEED );
        g = new QActionGroup( this );
        a = myDlimitOffAction = sub->addAction( tr( "Unlimited" ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::DSPEED_ENABLED << false );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        a = myDlimitOnAction = sub->addAction( tr( "Limited at %1" ).arg( Utils::speedToString( Speed::fromKbps( currentVal ) ) ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::DSPEED << currentVal << Prefs::DSPEED_ENABLED << true );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        sub->addSeparator( );
        foreach( int i, stockSpeeds ) {
            a = sub->addAction( Utils::speedToString( Speed::fromKbps(i) ) );
            a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::DSPEED << i << Prefs::DSPEED_ENABLED << true );
            connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
        }

    sub = menu->addMenu( tr( "Limit Upload Speed" ) );
        currentVal = myPrefs.get<int>( Prefs::USPEED );
        g = new QActionGroup( this );
        a = myUlimitOffAction = sub->addAction( tr( "Unlimited" ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::USPEED_ENABLED << false );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        a = myUlimitOnAction = sub->addAction( tr( "Limited at %1" ).arg( Utils::speedToString( Speed::fromKbps( currentVal ) ) ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::USPEED << currentVal << Prefs::USPEED_ENABLED << true );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        sub->addSeparator( );
        foreach( int i, stockSpeeds ) {
            a = sub->addAction( Utils::speedToString( Speed::fromKbps(i) ) );
            a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::USPEED << i << Prefs::USPEED_ENABLED << true );
            connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
        }

    menu->addSeparator( );
    sub = menu->addMenu( tr( "Stop Seeding at Ratio" ) );

        double d = myPrefs.get<double>( Prefs::RATIO );
        g = new QActionGroup( this );
        a = myRatioOffAction = sub->addAction( tr( "Seed Forever" ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::RATIO_ENABLED << false );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        a = myRatioOnAction = sub->addAction( tr( "Stop at Ratio (%1)" ).arg( Utils::ratioToString( d ) ) );
        a->setCheckable( true );
        a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::RATIO << d << Prefs::RATIO_ENABLED << true );
        g->addAction( a );
        connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)) );
        sub->addSeparator( );
        foreach( double i, stockRatios ) {
            a = sub->addAction( Utils::ratioToString( i ) );
            a->setProperty( PREF_VARIANTS_KEY, QVariantList() << Prefs::RATIO << i << Prefs::RATIO_ENABLED << true );
            connect( a, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
        }

    return menu;
}

/****
*****
****/

void
TrMainWindow :: setSortPref( int i )
{
    myPrefs.set( Prefs::SORT_MODE, SortMode( i ) );
}
void TrMainWindow :: onSortByActivityToggled ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_ACTIVITY ); }
void TrMainWindow :: onSortByAgeToggled      ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_AGE );      }
void TrMainWindow :: onSortByETAToggled      ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_ETA );      }
void TrMainWindow :: onSortByNameToggled     ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_NAME );     }
void TrMainWindow :: onSortByProgressToggled ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_PROGRESS ); }
void TrMainWindow :: onSortByRatioToggled    ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_RATIO );    }
void TrMainWindow :: onSortBySizeToggled     ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_SIZE );     }
void TrMainWindow :: onSortByStateToggled    ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_STATE );    }
void TrMainWindow :: onSortByTrackerToggled  ( bool b ) { if( b ) setSortPref( SortMode::SORT_BY_TRACKER );  }

void
TrMainWindow :: setSortAscendingPref( bool b )
{
    myPrefs.set( Prefs::SORT_REVERSED, b );
}

/****
*****
****/

void
TrMainWindow :: onDetailsDestroyed( )
{
    myDetailsDialog = 0;
}

void
TrMainWindow :: openProperties( )
{
    if( myDetailsDialog == 0 ) {
        myDetailsDialog = new Details( mySession, myModel, this );
        connect( myDetailsDialog, SIGNAL(destroyed(QObject*)), this, SLOT(onDetailsDestroyed()));
    }

    myDetailsDialog->setIds( getSelectedTorrents( ) );
    myDetailsDialog->show( );
}

void
TrMainWindow :: setLocation( )
{
    QDialog * d = new RelocateDialog( mySession, getSelectedTorrents(), this );
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
TrMainWindow :: refreshTitle( )
{
    QString title( "Transmission" );
    const QUrl url( mySession.getRemoteUrl( ) );
    if( !url.isEmpty() )
        title += tr( " - %1" ).arg( url.toString(QUrl::RemoveUserInfo) );
    setWindowTitle( title );
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
    myVisibleCountLabel->setText( str );
    myVisibleCountLabel->setVisible( totalCount > 0 );
}

void
TrMainWindow :: refreshStatusBar( )
{
    const Speed up( myModel.getUploadSpeed( ) );
    const Speed down( myModel.getDownloadSpeed( ) );
    myUploadSpeedLabel->setText( Utils :: speedToString( up ) );
    myDownloadSpeedLabel->setText( Utils :: speedToString( down ) );
    foreach( QWidget * w, myUpStatusWidgets ) w->setVisible( !up.isZero( ) );
    foreach( QWidget * w, myDownStatusWidgets ) w->setVisible( !down.isZero( ) );

    myNetworkLabel->setVisible( !mySession.isServer( ) );

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
    else // default is "total-ratio"
    {
        str = tr( "Ratio: %1" ).arg( Utils :: ratioToString( mySession.getCumulativeStats().ratio ) );
    }

    myStatsLabel->setText( str );
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

    // count how many torrents are selected, paused, etc
    for( int row=0; row<rowCount; ++row ) {
        const QModelIndex modelIndex( model->index( row, 0 ) );
        assert( model == modelIndex.model( ) );
        const Torrent * tor( model->data( modelIndex, TorrentModel::TorrentRole ).value<const Torrent*>( ) );
        if( tor ) {
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
    }

    const bool haveSelection( selected > 0 );
    ui.action_Verify->setEnabled( haveSelection );
    ui.action_Remove->setEnabled( haveSelection );
    ui.action_Delete->setEnabled( haveSelection );
    ui.action_Properties->setEnabled( haveSelection );
    ui.action_DeselectAll->setEnabled( haveSelection );
    ui.action_SetLocation->setEnabled( haveSelection );

    const bool oneSelection( selected == 1 );
    ui.action_OpenFolder->setEnabled( oneSelection && mySession.isLocal( ) );

    ui.action_SelectAll->setEnabled( selected < rowCount );
    ui.action_StartAll->setEnabled( paused > 0 );
    ui.action_PauseAll->setEnabled( paused < rowCount );
    ui.action_Start->setEnabled( selectedAndPaused > 0 );
    ui.action_Pause->setEnabled( selectedAndPaused < selected );
    ui.action_Announce->setEnabled( selected > 0 && ( canAnnounce == selected ) );

    if( myDetailsDialog )
        myDetailsDialog->setIds( getSelectedTorrents( ) );
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
    mySession.startTorrents( getSelectedTorrents( ) );
}
void
TrMainWindow :: pauseSelected( )
{
    mySession.pauseTorrents( getSelectedTorrents( ) );
}
void
TrMainWindow :: startAll( )
{
    mySession.startTorrents( );
}
void
TrMainWindow :: pauseAll( )
{
    mySession.pauseTorrents( );
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

void TrMainWindow :: setShowMode     ( int i ) { myPrefs.set( Prefs::FILTER_MODE, FilterMode( i ) ); }
void TrMainWindow :: showAll         ( ) { setShowMode( FilterMode :: SHOW_ALL ); }
void TrMainWindow :: showActive      ( ) { setShowMode( FilterMode :: SHOW_ACTIVE ); }
void TrMainWindow :: showDownloading ( ) { setShowMode( FilterMode :: SHOW_DOWNLOADING ); }
void TrMainWindow :: showSeeding     ( ) { setShowMode( FilterMode :: SHOW_SEEDING ); }
void TrMainWindow :: showPaused      ( ) { setShowMode( FilterMode :: SHOW_PAUSED ); }

void TrMainWindow :: filterByName    ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_NAME ); }
void TrMainWindow :: filterByTracker ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_TRACKER ); }
void TrMainWindow :: filterByFiles   ( ) { myFilterModel.setTextMode( TorrentFilter :: FILTER_BY_FILES ); }

void TrMainWindow :: showTotalRatio      ( ) { myPrefs.set( Prefs::STATUSBAR_STATS, "total-ratio"); }
void TrMainWindow :: showTotalTransfer   ( ) { myPrefs.set( Prefs::STATUSBAR_STATS, "total-transfer"); }
void TrMainWindow :: showSessionRatio    ( ) { myPrefs.set( Prefs::STATUSBAR_STATS, "session-ratio"); }
void TrMainWindow :: showSessionTransfer ( ) { myPrefs.set( Prefs::STATUSBAR_STATS, "session-transfer"); }

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
TrMainWindow :: toggleWindows( bool doShow )
{
    if( !doShow )
    {
        hide( );
    }
    else
    {
        if ( !isVisible( ) ) show( );
        if ( isMinimized( ) ) showNormal( );
        activateWindow( );
        raise( );
    }
}

void
TrMainWindow :: trayActivated( QSystemTrayIcon::ActivationReason reason )
{
    if( reason == QSystemTrayIcon::Trigger )
    {
        if( isMinimized ( ) )
            toggleWindows( true );
        else
            ui.action_ShowMainWindow->toggle( );
    }
}


void
TrMainWindow :: refreshPref( int key )
{
    bool b;
    int i;
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
            i = myPrefs.get<SortMode>(key).mode( );
            ui.action_SortByActivity->setChecked ( i == SortMode::SORT_BY_ACTIVITY );
            ui.action_SortByAge->setChecked      ( i == SortMode::SORT_BY_AGE );
            ui.action_SortByETA->setChecked      ( i == SortMode::SORT_BY_ETA );
            ui.action_SortByName->setChecked     ( i == SortMode::SORT_BY_NAME );
            ui.action_SortByProgress->setChecked ( i == SortMode::SORT_BY_PROGRESS );
            ui.action_SortByRatio->setChecked    ( i == SortMode::SORT_BY_RATIO );
            ui.action_SortBySize->setChecked     ( i == SortMode::SORT_BY_SIZE );
            ui.action_SortByState->setChecked    ( i == SortMode::SORT_BY_STATE );
            ui.action_SortByTracker->setChecked  ( i == SortMode::SORT_BY_TRACKER );
            break;

        case Prefs::DSPEED_ENABLED:
            (myPrefs.get<bool>(key) ? myDlimitOnAction : myDlimitOffAction)->setChecked( true );
            break;
     
        case Prefs::DSPEED:
            myDlimitOnAction->setText( tr( "Limited at %1" ).arg( Utils::speedToString( Speed::fromKbps( myPrefs.get<int>(key) ) ) ) );
            break;

        case Prefs::USPEED_ENABLED:
            (myPrefs.get<bool>(key) ? myUlimitOnAction : myUlimitOffAction)->setChecked( true );
            break;
     
        case Prefs::USPEED:
            myUlimitOnAction->setText( tr( "Limited at %1" ).arg( Utils::speedToString( Speed::fromKbps( myPrefs.get<int>(key) ) ) ) );
            break;

        case Prefs::RATIO_ENABLED:
            (myPrefs.get<bool>(key) ? myRatioOnAction : myRatioOffAction)->setChecked( true );
            break;

        case Prefs::RATIO:
            myRatioOnAction->setText( tr( "Stop at Ratio (%1)" ).arg( Utils::ratioToString( myPrefs.get<double>(key) ) ) );
            break;

        case Prefs::FILTER_MODE:
            i = myPrefs.get<FilterMode>(key).mode( );
            for( int j=0; j<FilterMode::NUM_MODES; ++j )
                myFilterButtons[j]->setChecked( i==j );
            break;

        case Prefs::FILTERBAR:
            b = myPrefs.getBool( key );
            myFilterBar->setVisible( b );
            ui.action_Filterbar->setChecked( b );
            break;

        case Prefs::STATUSBAR:
            b = myPrefs.getBool( key );
            myStatusBar->setVisible( b );
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
        case Prefs :: ALT_SPEED_LIMIT_UP:
        case Prefs :: ALT_SPEED_LIMIT_DOWN: {
            b = myPrefs.getBool( Prefs :: ALT_SPEED_LIMIT_ENABLED );
            myAltSpeedButton->setChecked( b );
            myAltSpeedButton->setIcon( b ? mySpeedModeOnIcon : mySpeedModeOffIcon );
            const QString fmt = b ? tr( "Click to disable Temporary Speed Limits\n(%1 down, %2 up)" )
                                  : tr( "Click to enable Temporary Speed Limits\n(%1 down, %2 up)" );
            const Speed d = Speed::fromKbps( myPrefs.getInt( Prefs::ALT_SPEED_LIMIT_DOWN ) );
            const Speed u = Speed::fromKbps( myPrefs.getInt( Prefs::ALT_SPEED_LIMIT_UP ) );
            myAltSpeedButton->setToolTip( fmt.arg( Utils::speedToString( d ) )
                                             .arg( Utils::speedToString( u ) ) );
            break;
        }

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
    myNetworkLabel->setPixmap( pixmap );
    myNetworkLabel->setToolTip( isSending || isReading
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

void
TrMainWindow :: wrongAuthentication( )
{
    mySession.stop( );
    mySessionDialog->show( );
}
