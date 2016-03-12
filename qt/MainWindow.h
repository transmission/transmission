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

#include <QMainWindow>
#include <QNetworkReply>
#include <QPointer>
#include <QSystemTrayIcon>

#include "TorrentFilter.h"
#include "ui_MainWindow.h"

class QAction;
class QIcon;
class QMenu;

class AboutDialog;
class AddData;
class DetailsDialog;
class Prefs;
class PrefsDialog;
class Session;
class SessionDialog;
class StatsDialog;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;
class FilterBar;

struct tr_variant;

class MainWindow: public QMainWindow
{
    Q_OBJECT

  public:
    MainWindow (Session&, Prefs&, TorrentModel&, bool minized);
    virtual ~MainWindow ();

    QSystemTrayIcon& trayIcon () { return myTrayIcon; }

  public slots:
    void refreshActionSensitivity ();
    void refreshActionSensitivitySoon ();
    void wrongAuthentication ();

    void openSession ();

  protected:
    // QWidget
    void contextMenuEvent (QContextMenuEvent *) override;
    void dragEnterEvent (QDragEnterEvent *) override;
    void dropEvent (QDropEvent *) override;

  private:
    QIcon getStockIcon (const QString&, int fallback = -1);

    QSet<int> getSelectedTorrents (bool withMetadataOnly = false) const;
    void updateNetworkIcon ();

    QMenu * createOptionsMenu ();
    QMenu * createStatsModeMenu ();
    void initStatusBar ();

    void addTorrent (const AddData& addMe, bool showOptions);

    // QWidget
    void hideEvent (QHideEvent * event) override;
    void showEvent (QShowEvent * event) override;

  private slots:
    void refreshTitle ();
    void refreshStatusBar ();
    void refreshTrayIcon ();
    void refreshTrayIconSoon ();
    void refreshTorrentViewHeader ();
    void openTorrent ();
    void openURL ();
    void newTorrent ();
    void trayActivated (QSystemTrayIcon::ActivationReason);
    void refreshPref (int key);
    void addTorrents (const QStringList& filenames);
    void removeTorrents (const bool deleteFiles);
    void openFolder ();
    void setLocation ();
    void openProperties ();
    void toggleSpeedMode ();
    void onError (QNetworkReply::NetworkError);
    void toggleWindows (bool doShow);
    void onSetPrefs ();
    void onSetPrefs (bool);
    void onModelReset ();

  private:
    Session& mySession;
    Prefs& myPrefs;
    TorrentModel& myModel;

    Ui_MainWindow ui;

    time_t myLastFullUpdateTime;
    QPointer<SessionDialog> mySessionDialog;
    QPointer<PrefsDialog> myPrefsDialog;
    QPointer<AboutDialog> myAboutDialog;
    QPointer<StatsDialog> myStatsDialog;
    QPointer<DetailsDialog> myDetailsDialog;
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
    FilterBar * myFilterBar;
    QAction * myAltSpeedAction;
    QString myErrorMessage;
};

#endif // QTR_MAIN_WINDOW_H
