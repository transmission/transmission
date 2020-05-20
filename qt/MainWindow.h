/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <ctime>

#include <QMainWindow>
#include <QNetworkReply>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QWidgetList>

#include "Filters.h"
#include "Speed.h"
#include "TorrentFilter.h"
#include "Typedefs.h"

#include "ui_MainWindow.h"

class QAction;
class QIcon;
class QMenu;
class QStringList;

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

extern "C"
{
struct tr_variant;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session&, Prefs&, TorrentModel&, bool minized);
    virtual ~MainWindow();

    QSystemTrayIcon& trayIcon()
    {
        return myTrayIcon;
    }

public slots:
    void startAll();
    void startSelected();
    void startSelectedNow();
    void pauseAll();
    void pauseSelected();
    void removeSelected();
    void deleteSelected();
    void verifySelected();
    void queueMoveTop();
    void queueMoveUp();
    void queueMoveDown();
    void queueMoveBottom();
    void reannounceSelected();
    void onNetworkTimer();

    void setToolbarVisible(bool);
    void setFilterbarVisible(bool);
    void setStatusbarVisible(bool);
    void setCompactView(bool);
    void wrongAuthentication();

    void openSession();

protected:
    // QWidget
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    QIcon getStockIcon(QString const&, int fallback = -1);
    QIcon addEmblem(QIcon icon, QStringList const& emblemNames);

    torrent_ids_t getSelectedTorrents(bool withMetadataOnly = false) const;
    void updateNetworkIcon();

    QMenu* createOptionsMenu();
    QMenu* createStatsModeMenu();
    void initStatusBar();

    void clearSelection();
    void addTorrent(AddData const& addMe, bool showOptions);

    // QWidget
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private slots:
    void addTorrents(QStringList const& filenames);
    void copyMagnetLinkToClipboard();
    void dataReadProgress();
    void dataSendProgress();
    void newTorrent();
    void onNetworkResponse(QNetworkReply::NetworkError code, QString const& message);
    void onRefreshTimer();
    void onSessionSourceChanged();
    void onSetPrefs();
    void onSetPrefs(bool);
    void onSortModeChanged(QAction* action);
    void onStatsModeChanged(QAction* action);
    void openAbout();
    void openDonate();
    void openFolder();
    void openHelp();
    void openPreferences();
    void openProperties();
    void openStats();
    void openTorrent();
    void openURL();
    void refreshPref(int key);
    void refreshSoon(int fields = ~0);
    void removeTorrents(bool const deleteFiles);
    void setLocation();
    void setSortAscendingPref(bool);
    void toggleSpeedMode();
    void toggleWindows(bool doShow);
    void trayActivated(QSystemTrayIcon::ActivationReason);

private:
    Session& mySession;
    Prefs& myPrefs;
    TorrentModel& myModel;

    QPixmap myPixmapNetworkError;
    QPixmap myPixmapNetworkIdle;
    QPixmap myPixmapNetworkReceive;
    QPixmap myPixmapNetworkTransmit;
    QPixmap myPixmapNetworkTransmitReceive;

    Ui_MainWindow ui;

    time_t myLastFullUpdateTime;
    QPointer<SessionDialog> mySessionDialog;
    QPointer<PrefsDialog> myPrefsDialog;
    QPointer<AboutDialog> myAboutDialog;
    QPointer<StatsDialog> myStatsDialog;
    QPointer<DetailsDialog> myDetailsDialog;
    QSystemTrayIcon myTrayIcon;
    TorrentFilter myFilterModel;
    TorrentDelegate* myTorrentDelegate;
    TorrentDelegateMin* myTorrentDelegateMin;
    time_t myLastSendTime;
    time_t myLastReadTime;
    QTimer myNetworkTimer;
    bool myNetworkError;
    QAction* myDlimitOffAction;
    QAction* myDlimitOnAction;
    QAction* myUlimitOffAction;
    QAction* myUlimitOnAction;
    QAction* myRatioOffAction;
    QAction* myRatioOnAction;
    QWidgetList myHidden;
    QWidget* myFilterBar;
    QAction* myAltSpeedAction;
    QString myErrorMessage;

    struct TransferStats
    {
        Speed speedUp;
        Speed speedDown;
        size_t peersSending = 0;
        size_t peersReceiving = 0;
    };
    TransferStats getTransferStats() const;

    enum
    {
        REFRESH_TITLE = (1 << 0),
        REFRESH_STATUS_BAR = (1 << 1),
        REFRESH_TRAY_ICON = (1 << 2),
        REFRESH_TORRENT_VIEW_HEADER = (1 << 3),
        REFRESH_ACTION_SENSITIVITY = (1 << 4)
    };
    int myRefreshFields = 0;
    QTimer myRefreshTimer;
    void refreshActionSensitivity();
    void refreshStatusBar(TransferStats const&);
    void refreshTitle();
    void refreshTorrentViewHeader();
    void refreshTrayIcon(TransferStats const&);
};
