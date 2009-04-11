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

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <ctime>
#include <QCheckBox>
#include <QFileDialog>
#include <QIcon>
#include <QMainWindow>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetList>

extern "C" {
    struct tr_benc;
};

#include "torrent-filter.h"
#include "ui_mainwin.h"

class ActionDelegator;
class Prefs;
class Session;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;
class QModelIndex;
class QSortFilterProxyModel;

class TrMainWindow: public QMainWindow
{
        Q_OBJECT

    private:
        time_t myLastFullUpdateTime;
        QDialog * myPrefsDialog;
        QDialog * myAboutDialog;
        QDialog * myStatsDialog;
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

    private:
        QIcon getStockIcon( const QString&, int fallback=-1 );

    private:
        void setShowMode( int );
        QSet<int> getSelectedTorrents( ) const;
        void updateNetworkIcon( );
        QWidgetList myHidden;

    private slots:
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
        void refreshStatusBar( );
        void openTorrent( );
        void newTorrent( );
        void trayActivated( QSystemTrayIcon::ActivationReason );
        void refreshPref( int key );
        void addTorrents( const QStringList& filenames );
        void openHelp( );
        void openFolder( );
        void openProperties( );
        void toggleSpeedMode( );
        void dataReadProgress( );
        void dataSendProgress( );
        void toggleWindows( );

     private slots:
        void setSortPref( int );
        void setSortAscendingPref( bool );
        void onSortByActivityToggled ( bool b );
        void onSortByAgeToggled      ( bool b );
        void onSortByETAToggled      ( bool b );
        void onSortByNameToggled     ( bool b );
        void onSortByProgressToggled ( bool b );
        void onSortByRatioToggled    ( bool b );
        void onSortBySizeToggled     ( bool b );
        void onSortByStateToggled    ( bool b );
        void onSortByTrackerToggled  ( bool b );


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

    public:
        TrMainWindow( Session&, Prefs&, TorrentModel&, bool minized );
        virtual ~TrMainWindow( );
};

#endif
