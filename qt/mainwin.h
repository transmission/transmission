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

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <ctime>
#include <QCheckBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QIcon>
#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetList>

extern "C" {
    struct tr_benc;
};

#include "filters.h"
#include "torrent-filter.h"
#include "ui_mainwin.h"

class ActionDelegator;
class Prefs;
class Details;
class Session;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;
class QAction;
class QLabel;
class QMenu;
class QModelIndex;
class QSortFilterProxyModel;

class TrMainWindow: public QMainWindow
{
        Q_OBJECT

    private:
        time_t myLastFullUpdateTime;
        QDialog * mySessionDialog;
        QDialog * myPrefsDialog;
        QDialog * myAboutDialog;
        QDialog * myStatsDialog;
        Details * myDetailsDialog;
        QFileDialog * myFileDialog;
        QCheckBox * myFileDialogOptionsCheck;
        QSystemTrayIcon myTrayIcon;
        TorrentFilter myFilterModel;
        TorrentDelegate * myTorrentDelegate;
        TorrentDelegateMin * myTorrentDelegateMin;
        Session& mySession;
        Prefs& myPrefs;
        TorrentModel& myModel;
        Ui_MainWindow ui;
        QIcon mySpeedModeOffIcon;
        QIcon mySpeedModeOnIcon;
        time_t myLastSendTime;
        time_t myLastReadTime;
        QTimer myNetworkTimer;
        QAction * myDlimitOffAction;
        QAction * myDlimitOnAction;
        QAction * myUlimitOffAction;
        QAction * myUlimitOnAction;
        QAction * myRatioOffAction;
        QAction * myRatioOnAction;

    private:
        QIcon getStockIcon( const QString&, int fallback=-1 );

    private:
        void setShowMode( int );
        QSet<int> getSelectedTorrents( ) const;
        void updateNetworkIcon( );
        QWidgetList myHidden;

    private slots:
        void onDetailsDestroyed( );
        void onShowModeClicked( );
        void showAll( );
        void showActive( );
        void showDownloading( );
        void showSeeding( );
        void showPaused( );
        void filterByName( );
        void filterByFiles( );
        void filterByTracker( );
        void showTotalRatio( );
        void showTotalTransfer( );
        void showSessionRatio( );
        void showSessionTransfer( );
        void refreshVisibleCount( );
        void refreshTitle( );
        void refreshStatusBar( );
        void openTorrent( );
        void newTorrent( );
        void trayActivated( QSystemTrayIcon::ActivationReason );
        void refreshPref( int key );
        void addTorrents( const QStringList& filenames );
        void openHelp( );
        void openFolder( );
        void setLocation( );
        void openProperties( );
        void toggleSpeedMode( );
        void dataReadProgress( );
        void dataSendProgress( );
        void toggleWindows( );
        void onSetPrefs( );
        void onSetPrefs( bool );
        void onSessionSourceChanged( );
        void onModelReset( );

    private slots:
        void setSortPref             ( int );
        void setSortAscendingPref    ( bool );
        void onSortByActivityToggled ( bool );
        void onSortByAgeToggled      ( bool );
        void onSortByETAToggled      ( bool );
        void onSortByNameToggled     ( bool );
        void onSortByProgressToggled ( bool );
        void onSortByRatioToggled    ( bool );
        void onSortBySizeToggled     ( bool );
        void onSortByStateToggled    ( bool );
        void onSortByTrackerToggled  ( bool );

    private:
        QWidget * createFilterBar( void );
        QWidget * myFilterBar;
        QPushButton * myFilterButtons[FilterMode::NUM_MODES];
        QPushButton * myFilterTextButton;
        QLineEdit * myFilterTextLineEdit;

    private:
        QMenu * createOptionsMenu( void );
        QWidget * createStatusBar( void );
        QWidget * myStatusBar;
        QWidgetList myUpStatusWidgets;
        QWidgetList myDownStatusWidgets;
        QPushButton * myAltSpeedButton;
        QPushButton * myOptionsButton;
        QLabel * myVisibleCountLabel;
        QPushButton * myStatsModeButton;
        QLabel * myStatsLabel;
        QLabel * myDownloadSpeedLabel;
        QLabel * myUploadSpeedLabel;
        QLabel * myNetworkLabel;

    public slots:
        void startAll( );
        void startSelected( );
        void pauseAll( );
        void pauseSelected( );
        void removeSelected( );
        void deleteSelected( );
        void verifySelected( );
        void reannounceSelected( );
        void addTorrent( const QString& filename );
        void onNetworkTimer( );

    private:
        void clearSelection( );

    public slots:
        void setToolbarVisible( bool );
        void setFilterbarVisible( bool );
        void setStatusbarVisible( bool );
        void setTrayIconVisible( bool );
        void setMinimalView( bool );
        void refreshActionSensitivity( );
        void wrongAuthentication( );

    public:
        TrMainWindow( Session&, Prefs&, TorrentModel&, bool minized );
        virtual ~TrMainWindow( );
};

#endif
