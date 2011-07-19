/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 *
 * $Id$
 */

#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <ctime>
#include <QCheckBox>
#include <QLineEdit>
#include <QIcon>
#include <QMainWindow>
#include <QMap>
#include <QPushButton>
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
class Filterbar;

class TrMainWindow: public QMainWindow
{
        Q_OBJECT

    private:
        virtual void closeEvent( QCloseEvent * event );

    private:
        time_t myLastFullUpdateTime;
        QDialog * mySessionDialog;
        QDialog * myPrefsDialog;
        QDialog * myAboutDialog;
        QDialog * myStatsDialog;
        Details * myDetailsDialog;
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
        QTimer myRefreshTrayIconTimer;
        QTimer myRefreshActionSensitivityTimer;
        QAction * myDlimitOffAction;
        QAction * myDlimitOnAction;
        QAction * myUlimitOffAction;
        QAction * myUlimitOnAction;
        QAction * myRatioOffAction;
        QAction * myRatioOnAction;

    private:
        QIcon getStockIcon( const QString&, int fallback=-1 );

    private:
        QSet<int> getSelectedTorrents( ) const;
        void updateNetworkIcon( );
        QWidgetList myHidden;

    public slots:
        void openURL( QString );

    private slots:
        void onPrefsDestroyed( );
        void openPreferences( );
        void onDetailsDestroyed( );
        void showTotalRatio( );
        void showTotalTransfer( );
        void showSessionRatio( );
        void showSessionTransfer( );
        void refreshVisibleCount( );
        void refreshTitle( );
        void refreshStatusBar( );
        void refreshTrayIcon( );
        void refreshTrayIconSoon( );
        void openTorrent( );
        void openURL( );
        void newTorrent( );
        void trayActivated( QSystemTrayIcon::ActivationReason );
        void refreshPref( int key );
        void addTorrents( const QStringList& filenames );
        void removeTorrents( const bool deleteFiles );
        void openDonate( );
        void openHelp( );
        void openFolder( );
        void copyMagnetLinkToClipboard( );
        void setLocation( );
        void openProperties( );
        void toggleSpeedMode( );
        void dataReadProgress( );
        void dataSendProgress( );
        void toggleWindows( bool doShow );
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

    private:
        QWidget * myFilterBar;

    private:
        QMenu * createOptionsMenu( void );
        QWidget * createStatusBar( void );
        QWidget * myStatusBar;
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
        void setCompactView( bool );
        void refreshActionSensitivity( );
        void refreshActionSensitivitySoon( );
        void wrongAuthentication( );

    public:
        TrMainWindow( Session&, Prefs&, TorrentModel&, bool minized );
        virtual ~TrMainWindow( );

    protected:
        virtual void dragEnterEvent( QDragEnterEvent * );
        virtual void dropEvent( QDropEvent * );
};

#endif
