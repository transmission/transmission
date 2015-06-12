/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_MAIN_WINDOW_H
#define QTR_MAIN_WINDOW_H

#include <ctime>

#include <QMainWindow>
#include <QNetworkReply>
#include <QPointer>
#include <QSet>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetList>

#include "Filters.h"
#include "TorrentFilter.h"
#include "ui_MainWindow.h"

class QAction;
class QIcon;
class QMenu;

class AddData;
class Prefs;
class DetailsDialog;
class Session;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;

extern "C"
{
  struct tr_variant;
}

class MainWindow: public QMainWindow
{
    Q_OBJECT

  public:
    MainWindow (Session&, Prefs&, TorrentModel&, bool minized);
    virtual ~MainWindow ();

  public slots:
    void startAll ();
    void startSelected ();
    void startSelectedNow ();
    void pauseAll ();
    void pauseSelected ();
    void removeSelected ();
    void deleteSelected ();
    void verifySelected ();
    void queueMoveTop ();
    void queueMoveUp ();
    void queueMoveDown ();
    void queueMoveBottom ();
    void reannounceSelected ();
    void onNetworkTimer ();

    void setToolbarVisible (bool);
    void setFilterbarVisible (bool);
    void setStatusbarVisible (bool);
    void setCompactView (bool);
    void refreshActionSensitivity ();
    void refreshActionSensitivitySoon ();
    void wrongAuthentication ();

  protected:
    // QWidget
    virtual void contextMenuEvent (QContextMenuEvent *);
    virtual void dragEnterEvent (QDragEnterEvent *);
    virtual void dropEvent (QDropEvent *);

  private:
    QIcon getStockIcon (const QString&, int fallback = -1);

    QSet<int> getSelectedTorrents () const;
    void updateNetworkIcon ();

    QMenu * createOptionsMenu ();
    QMenu * createStatsModeMenu ();
    void initStatusBar ();

    void clearSelection ();
    void addTorrent (const AddData& addMe, bool showOptions);

    // QWidget
    virtual void hideEvent (QHideEvent * event);
    virtual void showEvent (QShowEvent * event);

  private slots:
    void openPreferences ();
    void onDetailsDestroyed ();
    void showTotalRatio ();
    void showTotalTransfer ();
    void showSessionRatio ();
    void showSessionTransfer ();
    void refreshTitle ();
    void refreshStatusBar ();
    void refreshTrayIcon ();
    void refreshTrayIconSoon ();
    void openTorrent ();
    void openURL ();
    void newTorrent ();
    void trayActivated (QSystemTrayIcon::ActivationReason);
    void refreshPref (int key);
    void addTorrents (const QStringList& filenames);
    void removeTorrents (const bool deleteFiles);
    void openDonate ();
    void openHelp ();
    void openFolder ();
    void copyMagnetLinkToClipboard ();
    void setLocation ();
    void openProperties ();
    void toggleSpeedMode ();
    void dataReadProgress ();
    void dataSendProgress ();
    void onError (QNetworkReply::NetworkError);
    void errorMessage (const QString&);
    void toggleWindows (bool doShow);
    void onSetPrefs ();
    void onSetPrefs (bool);
    void onSessionSourceChanged ();
    void onModelReset ();

    void setSortPref (int);
    void setSortAscendingPref (bool);
    void onSortByActivityToggled (bool);
    void onSortByAgeToggled (bool);
    void onSortByETAToggled (bool);
    void onSortByNameToggled (bool);
    void onSortByProgressToggled (bool);
    void onSortByQueueToggled (bool);
    void onSortByRatioToggled (bool);
    void onSortBySizeToggled (bool);
    void onSortByStateToggled (bool);

  private:
    Session& mySession;
    Prefs& myPrefs;
    TorrentModel& myModel;

    Ui_MainWindow ui;

    time_t myLastFullUpdateTime;
    QDialog * mySessionDialog;
    QPointer<QDialog> myPrefsDialog;
    QDialog * myAboutDialog;
    QDialog * myStatsDialog;
    DetailsDialog * myDetailsDialog;
    QSystemTrayIcon myTrayIcon;
    TorrentFilter myFilterModel;
    TorrentDelegate * myTorrentDelegate;
    TorrentDelegateMin * myTorrentDelegateMin;
    time_t myLastSendTime;
    time_t myLastReadTime;
    QTimer myNetworkTimer;
    bool myNetworkError;
    QTimer myRefreshTrayIconTimer;
    QTimer myRefreshActionSensitivityTimer;
    QAction * myDlimitOffAction;
    QAction * myDlimitOnAction;
    QAction * myUlimitOffAction;
    QAction * myUlimitOnAction;
    QAction * myRatioOffAction;
    QAction * myRatioOnAction;
    QWidgetList myHidden;
    QWidget * myFilterBar;
    QAction * myAltSpeedAction;
    QString myErrorMessage;
};

#endif // QTR_MAIN_WINDOW_H
