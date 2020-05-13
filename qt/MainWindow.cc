/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>

#include <QCheckBox>
#include <QFileDialog>
#include <QIcon>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProxyStyle>
#include <QtGui>

#include <libtransmission/transmission.h>
#include <libtransmission/version.h>

#include "AboutDialog.h"
#include "AddData.h"
#include "Application.h"
#include "DetailsDialog.h"
#include "FilterBar.h"
#include "Filters.h"
#include "Formatter.h"
#include "MainWindow.h"
#include "MakeDialog.h"
#include "OptionsDialog.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "RelocateDialog.h"
#include "Session.h"
#include "SessionDialog.h"
#include "Speed.h"
#include "StatsDialog.h"
#include "TorrentDelegate.h"
#include "TorrentDelegateMin.h"
#include "TorrentFilter.h"
#include "TorrentModel.h"
#include "Utils.h"

#define PREF_VARIANTS_KEY "pref-variants-list"
#define STATS_MODE_KEY "stats-mode"
#define SORT_MODE_KEY "sort-mode"

namespace
{

QLatin1String const TotalRatioStatsModeName("total-ratio");
QLatin1String const TotalTransferStatsModeName("total-transfer");
QLatin1String const SessionRatioStatsModeName("session-ratio");
QLatin1String const SessionTransferStatsModeName("session-transfer");

} // namespace

/**
 * This is a proxy-style for that forces it to be always disabled.
 * We use this to make our torrent list view behave consistently on
 * both GTK and Qt implementations.
 */
class ListViewProxyStyle : public QProxyStyle
{
public:
    int styleHint(StyleHint hint, QStyleOption const* option = nullptr, QWidget const* widget = nullptr,
        QStyleHintReturn* returnData = nullptr) const
    {
        if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        {
            return 0;
        }

        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

QIcon MainWindow::getStockIcon(QString const& name, int fallback)
{
    QIcon icon = QIcon::fromTheme(name);

    if (icon.isNull() && fallback >= 0)
    {
        icon = style()->standardIcon(QStyle::StandardPixmap(fallback), 0, this);
    }

    return icon;
}

QIcon MainWindow::addEmblem(QIcon baseIcon, QStringList const& emblemNames)
{
    if (baseIcon.isNull())
    {
        return baseIcon;
    }

    QIcon emblemIcon;

    for (QString const& emblemName : emblemNames)
    {
        emblemIcon = QIcon::fromTheme(emblemName);

        if (!emblemIcon.isNull())
        {
            break;
        }
    }

    if (emblemIcon.isNull())
    {
        return baseIcon;
    }

    QIcon icon;

    for (QSize const& size : baseIcon.availableSizes())
    {
        QSize const emblemSize = size / 2;
        QRect const emblemRect = QStyle::alignedRect(layoutDirection(), Qt::AlignBottom | Qt::AlignRight, emblemSize,
            QRect(QPoint(0, 0), size));

        QPixmap pixmap = baseIcon.pixmap(size);
        QPixmap emblemPixmap = emblemIcon.pixmap(emblemSize);

        {
            QPainter painter(&pixmap);
            painter.drawPixmap(emblemRect, emblemPixmap, emblemPixmap.rect());
        }

        icon.addPixmap(pixmap);
    }

    return icon;
}

MainWindow::MainWindow(Session& session, Prefs& prefs, TorrentModel& model, bool minimized) :
    mySession(session),
    myPrefs(prefs),
    myModel(model),
    myLastFullUpdateTime(0),
    mySessionDialog(),
    myPrefsDialog(),
    myAboutDialog(),
    myStatsDialog(),
    myDetailsDialog(),
    myFilterModel(prefs),
    myTorrentDelegate(new TorrentDelegate(this)),
    myTorrentDelegateMin(new TorrentDelegateMin(this)),
    myLastSendTime(0),
    myLastReadTime(0),
    myNetworkTimer(this),
    myNetworkError(false),
    myRefreshTimer(this)
{
    setAcceptDrops(true);

    QAction* sep = new QAction(this);
    sep->setSeparator(true);

    ui.setupUi(this);

    ui.listView->setStyle(new ListViewProxyStyle);
    ui.listView->setAttribute(Qt::WA_MacShowFocusRect, false);

    // icons
    QIcon const iconPlay = getStockIcon(QLatin1String("media-playback-start"), QStyle::SP_MediaPlay);
    QIcon const iconPause = getStockIcon(QLatin1String("media-playback-pause"), QStyle::SP_MediaPause);
    QIcon const iconOpen = getStockIcon(QLatin1String("document-open"), QStyle::SP_DialogOpenButton);
    ui.action_OpenFile->setIcon(iconOpen);
    ui.action_AddURL->setIcon(addEmblem(iconOpen,
        QStringList() << QLatin1String("emblem-web") << QLatin1String("applications-internet")));
    ui.action_New->setIcon(getStockIcon(QLatin1String("document-new"), QStyle::SP_DesktopIcon));
    ui.action_Properties->setIcon(getStockIcon(QLatin1String("document-properties"), QStyle::SP_DesktopIcon));
    ui.action_OpenFolder->setIcon(getStockIcon(QLatin1String("folder-open"), QStyle::SP_DirOpenIcon));
    ui.action_Start->setIcon(iconPlay);
    ui.action_StartNow->setIcon(iconPlay);
    ui.action_Announce->setIcon(getStockIcon(QLatin1String("network-transmit-receive")));
    ui.action_Pause->setIcon(iconPause);
    ui.action_Remove->setIcon(getStockIcon(QLatin1String("list-remove"), QStyle::SP_TrashIcon));
    ui.action_Delete->setIcon(getStockIcon(QLatin1String("edit-delete"), QStyle::SP_TrashIcon));
    ui.action_StartAll->setIcon(iconPlay);
    ui.action_PauseAll->setIcon(iconPause);
    ui.action_Quit->setIcon(getStockIcon(QLatin1String("application-exit")));
    ui.action_SelectAll->setIcon(getStockIcon(QLatin1String("edit-select-all")));
    ui.action_ReverseSortOrder->setIcon(getStockIcon(QLatin1String("view-sort-ascending"), QStyle::SP_ArrowDown));
    ui.action_Preferences->setIcon(getStockIcon(QLatin1String("preferences-system")));
    ui.action_Contents->setIcon(getStockIcon(QLatin1String("help-contents"), QStyle::SP_DialogHelpButton));
    ui.action_About->setIcon(getStockIcon(QLatin1String("help-about")));
    ui.action_QueueMoveTop->setIcon(getStockIcon(QLatin1String("go-top")));
    ui.action_QueueMoveUp->setIcon(getStockIcon(QLatin1String("go-up"), QStyle::SP_ArrowUp));
    ui.action_QueueMoveDown->setIcon(getStockIcon(QLatin1String("go-down"), QStyle::SP_ArrowDown));
    ui.action_QueueMoveBottom->setIcon(getStockIcon(QLatin1String("go-bottom")));

    auto makeNetworkPixmap = [this](char const* nameIn, QSize size = QSize(16, 16))
        {
            QString const name = QLatin1String(nameIn);
            QIcon const icon = getStockIcon(name, QStyle::SP_DriveNetIcon);
            return icon.pixmap(size);
        };
    myPixmapNetworkError = makeNetworkPixmap("network-error");
    myPixmapNetworkIdle = makeNetworkPixmap("network-idle");
    myPixmapNetworkReceive = makeNetworkPixmap("network-receive");
    myPixmapNetworkTransmit = makeNetworkPixmap("network-transmit");
    myPixmapNetworkTransmitReceive = makeNetworkPixmap("network-transmit-receive");

    // ui signals
    connect(ui.action_Toolbar, SIGNAL(toggled(bool)), this, SLOT(setToolbarVisible(bool)));
    connect(ui.action_Filterbar, SIGNAL(toggled(bool)), this, SLOT(setFilterbarVisible(bool)));
    connect(ui.action_Statusbar, SIGNAL(toggled(bool)), this, SLOT(setStatusbarVisible(bool)));
    connect(ui.action_CompactView, SIGNAL(toggled(bool)), this, SLOT(setCompactView(bool)));
    connect(ui.action_ReverseSortOrder, SIGNAL(toggled(bool)), this, SLOT(setSortAscendingPref(bool)));
    connect(ui.action_Start, SIGNAL(triggered()), this, SLOT(startSelected()));
    connect(ui.action_QueueMoveTop, SIGNAL(triggered()), this, SLOT(queueMoveTop()));
    connect(ui.action_QueueMoveUp, SIGNAL(triggered()), this, SLOT(queueMoveUp()));
    connect(ui.action_QueueMoveDown, SIGNAL(triggered()), this, SLOT(queueMoveDown()));
    connect(ui.action_QueueMoveBottom, SIGNAL(triggered()), this, SLOT(queueMoveBottom()));
    connect(ui.action_StartNow, SIGNAL(triggered()), this, SLOT(startSelectedNow()));
    connect(ui.action_Pause, SIGNAL(triggered()), this, SLOT(pauseSelected()));
    connect(ui.action_Remove, SIGNAL(triggered()), this, SLOT(removeSelected()));
    connect(ui.action_Delete, SIGNAL(triggered()), this, SLOT(deleteSelected()));
    connect(ui.action_Verify, SIGNAL(triggered()), this, SLOT(verifySelected()));
    connect(ui.action_Announce, SIGNAL(triggered()), this, SLOT(reannounceSelected()));
    connect(ui.action_StartAll, SIGNAL(triggered()), this, SLOT(startAll()));
    connect(ui.action_PauseAll, SIGNAL(triggered()), this, SLOT(pauseAll()));
    connect(ui.action_OpenFile, SIGNAL(triggered()), this, SLOT(openTorrent()));
    connect(ui.action_AddURL, SIGNAL(triggered()), this, SLOT(openURL()));
    connect(ui.action_New, SIGNAL(triggered()), this, SLOT(newTorrent()));
    connect(ui.action_Preferences, SIGNAL(triggered()), this, SLOT(openPreferences()));
    connect(ui.action_Statistics, SIGNAL(triggered()), this, SLOT(openStats()));
    connect(ui.action_Donate, SIGNAL(triggered()), this, SLOT(openDonate()));
    connect(ui.action_About, SIGNAL(triggered()), this, SLOT(openAbout()));
    connect(ui.action_Contents, SIGNAL(triggered()), this, SLOT(openHelp()));
    connect(ui.action_OpenFolder, SIGNAL(triggered()), this, SLOT(openFolder()));
    connect(ui.action_CopyMagnetToClipboard, SIGNAL(triggered()), this, SLOT(copyMagnetLinkToClipboard()));
    connect(ui.action_SetLocation, SIGNAL(triggered()), this, SLOT(setLocation()));
    connect(ui.action_Properties, SIGNAL(triggered()), this, SLOT(openProperties()));
    connect(ui.action_SessionDialog, SIGNAL(triggered()), this, SLOT(openSession()));
    connect(ui.listView, SIGNAL(activated(QModelIndex)), ui.action_Properties, SLOT(trigger()));
    connect(ui.action_SelectAll, SIGNAL(triggered()), ui.listView, SLOT(selectAll()));
    connect(ui.action_DeselectAll, SIGNAL(triggered()), ui.listView, SLOT(clearSelection()));
    connect(ui.action_Quit, SIGNAL(triggered()), qApp, SLOT(quit()));

    auto refreshActionSensitivitySoon = [this]() { refreshSoon(REFRESH_ACTION_SENSITIVITY); };
    connect(&myFilterModel, &TorrentFilter::rowsInserted, this, refreshActionSensitivitySoon);
    connect(&myFilterModel, &TorrentFilter::rowsRemoved, this, refreshActionSensitivitySoon);
    connect(&myModel, &TorrentModel::torrentsChanged, this, refreshActionSensitivitySoon);

    // torrent view
    myFilterModel.setSourceModel(&myModel);
    auto refreshSoonAdapter = [this]() { refreshSoon(); };
    connect(&myModel, &TorrentModel::modelReset, this, refreshSoonAdapter);
    connect(&myModel, &TorrentModel::rowsRemoved, this, refreshSoonAdapter);
    connect(&myModel, &TorrentModel::rowsInserted, this, refreshSoonAdapter);
    connect(&myModel, &TorrentModel::dataChanged, this, refreshSoonAdapter);

    ui.listView->setModel(&myFilterModel);
    connect(ui.listView->selectionModel(), &QItemSelectionModel::selectionChanged, refreshActionSensitivitySoon);

    QPair<QAction*, int> const sortModes[] =
    {
        qMakePair(ui.action_SortByActivity, static_cast<int>(SortMode::SORT_BY_ACTIVITY)),
        qMakePair(ui.action_SortByAge, static_cast<int>(SortMode::SORT_BY_AGE)),
        qMakePair(ui.action_SortByETA, static_cast<int>(SortMode::SORT_BY_ETA)),
        qMakePair(ui.action_SortByName, static_cast<int>(SortMode::SORT_BY_NAME)),
        qMakePair(ui.action_SortByProgress, static_cast<int>(SortMode::SORT_BY_PROGRESS)),
        qMakePair(ui.action_SortByQueue, static_cast<int>(SortMode::SORT_BY_QUEUE)),
        qMakePair(ui.action_SortByRatio, static_cast<int>(SortMode::SORT_BY_RATIO)),
        qMakePair(ui.action_SortBySize, static_cast<int>(SortMode::SORT_BY_SIZE)),
        qMakePair(ui.action_SortByState, static_cast<int>(SortMode::SORT_BY_STATE))
    };

    QActionGroup* actionGroup = new QActionGroup(this);

    for (auto const& mode : sortModes)
    {
        mode.first->setProperty(SORT_MODE_KEY, mode.second);
        actionGroup->addAction(mode.first);
    }

    connect(actionGroup, SIGNAL(triggered(QAction*)), this, SLOT(onSortModeChanged(QAction*)));

    myAltSpeedAction = new QAction(tr("Speed Limits"), this);
    myAltSpeedAction->setIcon(ui.altSpeedButton->icon());
    myAltSpeedAction->setCheckable(true);
    connect(myAltSpeedAction, SIGNAL(triggered()), this, SLOT(toggleSpeedMode()));

    QMenu* menu = new QMenu(this);
    menu->addAction(ui.action_OpenFile);
    menu->addAction(ui.action_AddURL);
    menu->addSeparator();
    menu->addAction(ui.action_ShowMainWindow);
    menu->addAction(ui.action_ShowMessageLog);
    menu->addAction(ui.action_About);
    menu->addSeparator();
    menu->addAction(ui.action_StartAll);
    menu->addAction(ui.action_PauseAll);
    menu->addAction(myAltSpeedAction);
    menu->addSeparator();
    menu->addAction(ui.action_Quit);
    myTrayIcon.setContextMenu(menu);
    myTrayIcon.setIcon(QIcon::fromTheme(QLatin1String("transmission-tray-icon"), qApp->windowIcon()));

    connect(&myPrefs, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));
    connect(ui.action_ShowMainWindow, SIGNAL(triggered(bool)), this, SLOT(toggleWindows(bool)));
    connect(&myTrayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this,
        SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));

    toggleWindows(!minimized);
    ui.action_TrayIcon->setChecked(minimized || prefs.getBool(Prefs::SHOW_TRAY_ICON));

    initStatusBar();
    ui.verticalLayout->insertWidget(0, myFilterBar = new FilterBar(myPrefs, myModel, myFilterModel));

    auto refreshHeaderSoon = [this]() { refreshSoon(REFRESH_TORRENT_VIEW_HEADER); };
    connect(&myModel, &TorrentModel::rowsInserted, this, refreshHeaderSoon);
    connect(&myModel, &TorrentModel::rowsRemoved, this, refreshHeaderSoon);
    connect(&myFilterModel, &TorrentFilter::rowsInserted, this, refreshHeaderSoon);
    connect(&myFilterModel, &TorrentFilter::rowsRemoved, this, refreshHeaderSoon);
    connect(ui.listView, SIGNAL(headerDoubleClicked()), myFilterBar, SLOT(clear()));

    QList<int> initKeys;
    initKeys << Prefs::MAIN_WINDOW_X << Prefs::SHOW_TRAY_ICON << Prefs::SORT_REVERSED << Prefs::SORT_MODE << Prefs::FILTERBAR <<
        Prefs::STATUSBAR << Prefs::STATUSBAR_STATS << Prefs::TOOLBAR << Prefs::ALT_SPEED_LIMIT_ENABLED <<
        Prefs::COMPACT_VIEW << Prefs::DSPEED << Prefs::DSPEED_ENABLED << Prefs::USPEED << Prefs::USPEED_ENABLED <<
        Prefs::RATIO << Prefs::RATIO_ENABLED;

    for (int const key : initKeys)
    {
        refreshPref(key);
    }

    auto refreshStatusSoon = [this]() { refreshSoon(REFRESH_STATUS_BAR); };
    connect(&mySession, SIGNAL(sourceChanged()), this, SLOT(onSessionSourceChanged()));
    connect(&mySession, &Session::statsUpdated, this, refreshStatusSoon);
    connect(&mySession, SIGNAL(dataReadProgress()), this, SLOT(dataReadProgress()));
    connect(&mySession, SIGNAL(dataSendProgress()), this, SLOT(dataSendProgress()));
    connect(&mySession, SIGNAL(httpAuthenticationRequired()), this, SLOT(wrongAuthentication()));
    connect(&mySession, SIGNAL(networkResponse(QNetworkReply::NetworkError, QString)), this,
        SLOT(onNetworkResponse(QNetworkReply::NetworkError, QString)));

    if (mySession.isServer())
    {
        ui.networkLabel->hide();
    }
    else
    {
        connect(&myNetworkTimer, &QTimer::timeout, this, &MainWindow::onNetworkTimer);
        myNetworkTimer.start(1000);
    }

    connect(&myRefreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refreshSoon();
}

MainWindow::~MainWindow()
{
}

void MainWindow::onSessionSourceChanged()
{
    myModel.clear();
}

/****
*****
****/

void MainWindow::onSetPrefs()
{
    QVariantList const p = sender()->property(PREF_VARIANTS_KEY).toList();
    assert(p.size() % 2 == 0);

    for (int i = 0, n = p.size(); i < n; i += 2)
    {
        myPrefs.set(p[i].toInt(), p[i + 1]);
    }
}

void MainWindow::onSetPrefs(bool isChecked)
{
    if (isChecked)
    {
        onSetPrefs();
    }
}

void MainWindow::initStatusBar()
{
    ui.optionsButton->setMenu(createOptionsMenu());

    int const minimumSpeedWidth = ui.downloadSpeedLabel->fontMetrics().width(Formatter::uploadSpeedToString(Speed::fromKBps(
        999.99)));
    ui.downloadSpeedLabel->setMinimumWidth(minimumSpeedWidth);
    ui.uploadSpeedLabel->setMinimumWidth(minimumSpeedWidth);

    ui.statsModeButton->setMenu(createStatsModeMenu());

    connect(ui.altSpeedButton, SIGNAL(clicked()), this, SLOT(toggleSpeedMode()));
}

QMenu* MainWindow::createOptionsMenu()
{
    auto const initSpeedSubMenu = [this](QMenu* menu, QAction*& offAction, QAction*& onAction, int pref, int enabledPref)
        {
            int const stockSpeeds[] = { 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750 };
            int const currentValue = myPrefs.get<int>(pref);

            QActionGroup* actionGroup = new QActionGroup(this);

            offAction = menu->addAction(tr("Unlimited"));
            offAction->setCheckable(true);
            offAction->setProperty(PREF_VARIANTS_KEY, QVariantList() << enabledPref << false);
            actionGroup->addAction(offAction);
            connect(offAction, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            onAction = menu->addAction(tr("Limited at %1").arg(Formatter::speedToString(Speed::fromKBps(currentValue))));
            onAction->setCheckable(true);
            onAction->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << currentValue << enabledPref << true);
            actionGroup->addAction(onAction);
            connect(onAction, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            menu->addSeparator();

            for (int const i : stockSpeeds)
            {
                QAction* action = menu->addAction(Formatter::speedToString(Speed::fromKBps(i)));
                action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << i << enabledPref << true);
                connect(action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
            }
        };

    auto const initSeedRatioSubMenu = [this](QMenu* menu, QAction*& offAction, QAction*& onAction, int pref, int enabledPref)
        {
            double const stockRatios[] = { 0.25, 0.50, 0.75, 1, 1.5, 2, 3 };
            double const currentValue = myPrefs.get<double>(pref);

            QActionGroup* actionGroup = new QActionGroup(this);

            offAction = menu->addAction(tr("Seed Forever"));
            offAction->setCheckable(true);
            offAction->setProperty(PREF_VARIANTS_KEY, QVariantList() << enabledPref << false);
            actionGroup->addAction(offAction);
            connect(offAction, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            onAction = menu->addAction(tr("Stop at Ratio (%1)").arg(Formatter::ratioToString(currentValue)));
            onAction->setCheckable(true);
            onAction->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << currentValue << enabledPref << true);
            actionGroup->addAction(onAction);
            connect(onAction, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            menu->addSeparator();

            for (double const i : stockRatios)
            {
                QAction* action = menu->addAction(Formatter::ratioToString(i));
                action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << i << enabledPref << true);
                connect(action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
            }
        };

    QMenu* menu = new QMenu(this);

    initSpeedSubMenu(menu->addMenu(tr("Limit Download Speed")), myDlimitOffAction, myDlimitOnAction, Prefs::DSPEED,
        Prefs::DSPEED_ENABLED);
    initSpeedSubMenu(menu->addMenu(tr("Limit Upload Speed")), myUlimitOffAction, myUlimitOnAction, Prefs::USPEED,
        Prefs::USPEED_ENABLED);

    menu->addSeparator();

    initSeedRatioSubMenu(menu->addMenu(tr("Stop Seeding at Ratio")), myRatioOffAction, myRatioOnAction, Prefs::RATIO,
        Prefs::RATIO_ENABLED);

    return menu;
}

QMenu* MainWindow::createStatsModeMenu()
{
    QPair<QAction*, QLatin1String> const statsModes[] =
    {
        qMakePair(ui.action_TotalRatio, TotalRatioStatsModeName),
        qMakePair(ui.action_TotalTransfer, TotalTransferStatsModeName),
        qMakePair(ui.action_SessionRatio, SessionRatioStatsModeName),
        qMakePair(ui.action_SessionTransfer, SessionTransferStatsModeName)
    };

    QActionGroup* actionGroup = new QActionGroup(this);
    QMenu* menu = new QMenu(this);

    for (auto const& mode : statsModes)
    {
        mode.first->setProperty(STATS_MODE_KEY, QString(mode.second));
        actionGroup->addAction(mode.first);
        menu->addAction(mode.first);
    }

    connect(actionGroup, SIGNAL(triggered(QAction*)), this, SLOT(onStatsModeChanged(QAction*)));

    return menu;
}

/****
*****
****/

void MainWindow::onSortModeChanged(QAction* action)
{
    myPrefs.set(Prefs::SORT_MODE, SortMode(action->property(SORT_MODE_KEY).toInt()));
}

void MainWindow::setSortAscendingPref(bool b)
{
    myPrefs.set(Prefs::SORT_REVERSED, b);
}

/****
*****
****/

void MainWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event)

    ui.action_ShowMainWindow->setChecked(true);
}

/****
*****
****/

void MainWindow::hideEvent(QHideEvent* event)
{
    Q_UNUSED(event)

    if (!isVisible())
    {
        ui.action_ShowMainWindow->setChecked(false);
    }
}

/****
*****
****/

void MainWindow::openSession()
{
    Utils::openDialog(mySessionDialog, mySession, myPrefs, this);
}

void MainWindow::openPreferences()
{
    Utils::openDialog(myPrefsDialog, mySession, myPrefs, this);
}

void MainWindow::openProperties()
{
    Utils::openDialog(myDetailsDialog, mySession, myPrefs, myModel, this);
    myDetailsDialog->setIds(getSelectedTorrents());
}

void MainWindow::setLocation()
{
    RelocateDialog* d = new RelocateDialog(mySession, myModel, getSelectedTorrents(), this);
    d->setAttribute(Qt::WA_DeleteOnClose, true);
    d->show();
}

namespace
{

// Open Folder & select torrent's file or top folder
#undef HAVE_OPEN_SELECT

#if defined(Q_OS_WIN)

#define HAVE_OPEN_SELECT

static void openSelect(QString const& path)
{
    QString const explorer = QLatin1String("explorer");
    QString param;

    if (!QFileInfo(path).isDir())
    {
        param = QLatin1String("/select,");
    }

    param += QDir::toNativeSeparators(path);
    QProcess::startDetached(explorer, QStringList(param));
}

#elif defined(Q_OS_MAC)

#define HAVE_OPEN_SELECT

static void openSelect(QString const& path)
{
    QStringList scriptArgs;
    scriptArgs << QLatin1String("-e") <<
        QString::fromLatin1("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path);
    QProcess::execute(QLatin1String("/usr/bin/osascript"), scriptArgs);

    scriptArgs.clear();
    scriptArgs << QLatin1String("-e") << QLatin1String("tell application \"Finder\" to activate");
    QProcess::execute(QLatin1String("/usr/bin/osascript"), scriptArgs);
}

#endif

} // namespace

void MainWindow::openFolder()
{
    auto const selectedTorrents = getSelectedTorrents();

    if (selectedTorrents.size() != 1)
    {
        return;
    }

    int const torrentId(*selectedTorrents.begin());
    Torrent const* tor(myModel.getTorrentFromId(torrentId));

    if (tor == nullptr)
    {
        return;
    }

    QString path(tor->getPath());
    FileList const& files = tor->files();

    if (files.isEmpty())
    {
        return;
    }

    QString const firstfile = files.at(0).filename;
    int slashIndex = firstfile.indexOf(QLatin1Char('/'));

    if (slashIndex > -1)
    {
        path = path + QLatin1Char('/') + firstfile.left(slashIndex);
    }

#ifdef HAVE_OPEN_SELECT

    else
    {
        openSelect(path + QLatin1Char('/') + firstfile);
        return;
    }

#endif

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::copyMagnetLinkToClipboard()
{
    int const id(*getSelectedTorrents().begin());
    mySession.copyMagnetLinkToClipboard(id);
}

void MainWindow::openStats()
{
    Utils::openDialog(myStatsDialog, mySession, this);
}

void MainWindow::openDonate()
{
    QDesktopServices::openUrl(QUrl(QLatin1String("https://transmissionbt.com/donate/")));
}

void MainWindow::openAbout()
{
    Utils::openDialog(myAboutDialog, this);
}

void MainWindow::openHelp()
{
    QDesktopServices::openUrl(QUrl(QString::fromLatin1("https://transmissionbt.com/help/gtk/%1.%2x").arg(MAJOR_VERSION).
        arg(MINOR_VERSION / 10)));
}

/****
*****
****/

void MainWindow::refreshSoon(int fields)
{
    myRefreshFields |= fields;

    if (!myRefreshTimer.isActive())
    {
        myRefreshTimer.setSingleShot(true);
        myRefreshTimer.start(200);
    }
}

MainWindow::TransferStats MainWindow::getTransferStats() const
{
    TransferStats stats;

    for (auto const& tor : myModel.torrents())
    {
        stats.speedUp += tor->uploadSpeed();
        stats.speedDown += tor->downloadSpeed();
        stats.peersSending += tor->webseedsWeAreDownloadingFrom();
        stats.peersSending += tor->peersWeAreDownloadingFrom();
        stats.peersReceiving += tor->peersWeAreUploadingTo();
    }

    return stats;
}

void MainWindow::onRefreshTimer()
{
    int fields = 0;
    std::swap(fields, myRefreshFields);

    if (fields & REFRESH_TITLE)
    {
        refreshTitle();
    }

    if (fields & (REFRESH_TRAY_ICON | REFRESH_STATUS_BAR))
    {
        auto const stats = getTransferStats();

        if (fields & REFRESH_TRAY_ICON)
        {
            refreshTrayIcon(stats);
        }

        if (fields & REFRESH_STATUS_BAR)
        {
            refreshStatusBar(stats);
        }
    }

    if (fields & REFRESH_TORRENT_VIEW_HEADER)
    {
        refreshTorrentViewHeader();
    }

    if (fields & REFRESH_ACTION_SENSITIVITY)
    {
        refreshActionSensitivity();
    }
}

void MainWindow::refreshTitle()
{
    QString title(QLatin1String("Transmission"));
    QUrl const url(mySession.getRemoteUrl());

    if (!url.isEmpty())
    {
        //: Second (optional) part of main window title "Transmission - host:port" (added when connected to remote session);
        //: notice that leading space (before the dash) is included here
        title += tr(" - %1:%2").arg(url.host()).arg(url.port());
    }

    setWindowTitle(title);
}

void MainWindow::refreshTrayIcon(TransferStats const& stats)
{
    QString tip;

    if (myNetworkError)
    {
        tip = tr("Network Error");
    }
    else if (stats.peersSending == 0 && stats.peersReceiving == 0)
    {
        tip = tr("Idle");
    }
    else if (stats.peersSending != 0)
    {
        tip = Formatter::downloadSpeedToString(stats.speedDown) + QLatin1String("   ") + Formatter::uploadSpeedToString(
            stats.speedUp);
    }
    else if (stats.peersReceiving != 0)
    {
        tip = Formatter::uploadSpeedToString(stats.speedUp);
    }

    myTrayIcon.setToolTip(tip);
}

void MainWindow::refreshStatusBar(TransferStats const& stats)
{
    ui.uploadSpeedLabel->setText(Formatter::uploadSpeedToString(stats.speedUp));
    ui.uploadSpeedLabel->setVisible(stats.peersSending || stats.peersReceiving);
    ui.downloadSpeedLabel->setText(Formatter::downloadSpeedToString(stats.speedDown));
    ui.downloadSpeedLabel->setVisible(stats.peersSending);

    ui.networkLabel->setVisible(!mySession.isServer());

    QString const mode(myPrefs.getString(Prefs::STATUSBAR_STATS));
    QString str;

    if (mode == SessionRatioStatsModeName)
    {
        str = tr("Ratio: %1").arg(Formatter::ratioToString(mySession.getStats().ratio));
    }
    else if (mode == SessionTransferStatsModeName)
    {
        tr_session_stats const& stats(mySession.getStats());
        str = tr("Down: %1, Up: %2").arg(Formatter::sizeToString(stats.downloadedBytes)).
            arg(Formatter::sizeToString(stats.uploadedBytes));
    }
    else if (mode == TotalTransferStatsModeName)
    {
        tr_session_stats const& stats(mySession.getCumulativeStats());
        str = tr("Down: %1, Up: %2").arg(Formatter::sizeToString(stats.downloadedBytes)).
            arg(Formatter::sizeToString(stats.uploadedBytes));
    }
    else // default is "total-ratio"
    {
        assert(mode == TotalRatioStatsModeName);
        str = tr("Ratio: %1").arg(Formatter::ratioToString(mySession.getCumulativeStats().ratio));
    }

    ui.statsLabel->setText(str);
}

void MainWindow::refreshTorrentViewHeader()
{
    int const totalCount = myModel.rowCount();
    int const visibleCount = myFilterModel.rowCount();

    if (visibleCount == totalCount)
    {
        ui.listView->setHeaderText(QString());
    }
    else
    {
        ui.listView->setHeaderText(tr("Showing %L1 of %Ln torrent(s)", nullptr, totalCount).arg(visibleCount));
    }
}

void MainWindow::refreshActionSensitivity()
{
    int paused(0);
    int selected(0);
    int selectedAndCanAnnounce(0);
    int selectedAndPaused(0);
    int selectedAndQueued(0);
    int selectedWithMetadata(0);
    QAbstractItemModel const* model(ui.listView->model());
    QItemSelectionModel const* selectionModel(ui.listView->selectionModel());
    bool const hasSelection = selectionModel->hasSelection();
    int const rowCount(model->rowCount());

    // count how many torrents are selected, paused, etc
    auto const now = time(nullptr);
    for (int row = 0; row < rowCount; ++row)
    {
        QModelIndex const modelIndex(model->index(row, 0));
        auto const& tor = model->data(modelIndex, TorrentModel::TorrentRole).value<Torrent const*>();

        if (tor != nullptr)
        {
            bool const isSelected = hasSelection && selectionModel->isSelected(modelIndex);
            bool const isPaused = tor->isPaused();

            if (isPaused)
            {
                ++paused;
            }

            if (isSelected)
            {
                ++selected;

                if (isPaused)
                {
                    ++selectedAndPaused;
                }

                if (tor->isQueued())
                {
                    ++selectedAndQueued;
                }

                if (tor->hasMetadata())
                {
                    ++selectedWithMetadata;
                }

                if (tor->canManualAnnounceAt(now))
                {
                    ++selectedAndCanAnnounce;
                }
            }
        }
    }

    bool const haveSelection(selected > 0);
    bool const haveSelectionWithMetadata = selectedWithMetadata > 0;
    bool const oneSelection(selected == 1);

    ui.action_Verify->setEnabled(haveSelectionWithMetadata);
    ui.action_Remove->setEnabled(haveSelection);
    ui.action_Delete->setEnabled(haveSelection);
    ui.action_Properties->setEnabled(haveSelection);
    ui.action_DeselectAll->setEnabled(haveSelection);
    ui.action_SetLocation->setEnabled(haveSelection);

    ui.action_OpenFolder->setEnabled(oneSelection && haveSelectionWithMetadata && mySession.isLocal());
    ui.action_CopyMagnetToClipboard->setEnabled(oneSelection);

    ui.action_SelectAll->setEnabled(selected < rowCount);
    ui.action_StartAll->setEnabled(paused > 0);
    ui.action_PauseAll->setEnabled(paused < rowCount);
    ui.action_Start->setEnabled(selectedAndPaused > 0);
    ui.action_StartNow->setEnabled(selectedAndPaused + selectedAndQueued > 0);
    ui.action_Pause->setEnabled(selectedAndPaused < selected);
    ui.action_Announce->setEnabled(selected > 0 && (selectedAndCanAnnounce == selected));

    ui.action_QueueMoveTop->setEnabled(haveSelection);
    ui.action_QueueMoveUp->setEnabled(haveSelection);
    ui.action_QueueMoveDown->setEnabled(haveSelection);
    ui.action_QueueMoveBottom->setEnabled(haveSelection);

    if (!myDetailsDialog.isNull())
    {
        myDetailsDialog->setIds(getSelectedTorrents());
    }
}

/**
***
**/

void MainWindow::clearSelection()
{
    ui.action_DeselectAll->trigger();
}

torrent_ids_t MainWindow::getSelectedTorrents(bool withMetadataOnly) const
{
    torrent_ids_t ids;

    for (QModelIndex const& index : ui.listView->selectionModel()->selectedRows())
    {
        Torrent const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());

        if (tor != nullptr && (!withMetadataOnly || tor->hasMetadata()))
        {
            ids.insert(tor->id());
        }
    }

    return ids;
}

void MainWindow::startSelected()
{
    mySession.startTorrents(getSelectedTorrents());
}

void MainWindow::startSelectedNow()
{
    mySession.startTorrentsNow(getSelectedTorrents());
}

void MainWindow::pauseSelected()
{
    mySession.pauseTorrents(getSelectedTorrents());
}

void MainWindow::queueMoveTop()
{
    mySession.queueMoveTop(getSelectedTorrents());
}

void MainWindow::queueMoveUp()
{
    mySession.queueMoveUp(getSelectedTorrents());
}

void MainWindow::queueMoveDown()
{
    mySession.queueMoveDown(getSelectedTorrents());
}

void MainWindow::queueMoveBottom()
{
    mySession.queueMoveBottom(getSelectedTorrents());
}

void MainWindow::startAll()
{
    mySession.startTorrents();
}

void MainWindow::pauseAll()
{
    mySession.pauseTorrents();
}

void MainWindow::removeSelected()
{
    removeTorrents(false);
}

void MainWindow::deleteSelected()
{
    removeTorrents(true);
}

void MainWindow::verifySelected()
{
    mySession.verifyTorrents(getSelectedTorrents(true));
}

void MainWindow::reannounceSelected()
{
    mySession.reannounceTorrents(getSelectedTorrents());
}

/**
***
**/

void MainWindow::onStatsModeChanged(QAction* action)
{
    myPrefs.set(Prefs::STATUSBAR_STATS, action->property(STATS_MODE_KEY).toString());
}

/**
***
**/

void MainWindow::setCompactView(bool visible)
{
    myPrefs.set(Prefs::COMPACT_VIEW, visible);
}

void MainWindow::toggleSpeedMode()
{
    myPrefs.toggleBool(Prefs::ALT_SPEED_LIMIT_ENABLED);
    bool const mode = myPrefs.get<bool>(Prefs::ALT_SPEED_LIMIT_ENABLED);
    myAltSpeedAction->setChecked(mode);
}

void MainWindow::setToolbarVisible(bool visible)
{
    myPrefs.set(Prefs::TOOLBAR, visible);
}

void MainWindow::setFilterbarVisible(bool visible)
{
    myPrefs.set(Prefs::FILTERBAR, visible);
}

void MainWindow::setStatusbarVisible(bool visible)
{
    myPrefs.set(Prefs::STATUSBAR, visible);
}

/**
***
**/

void MainWindow::toggleWindows(bool doShow)
{
    if (!doShow)
    {
        hide();
    }
    else
    {
        if (!isVisible())
        {
            show();
        }

        if (isMinimized())
        {
            showNormal();
        }

        // activateWindow ();
        raise();
        qApp->setActiveWindow(this);
    }
}

void MainWindow::trayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
    {
        if (isMinimized())
        {
            toggleWindows(true);
        }
        else
        {
            toggleWindows(!isVisible());
        }
    }
}

void MainWindow::refreshPref(int key)
{
    bool b;
    int i;
    QString str;
    QActionGroup* actionGroup;

    switch (key)
    {
    case Prefs::STATUSBAR_STATS:
        str = myPrefs.getString(key);
        actionGroup = ui.action_TotalRatio->actionGroup();
        assert(actionGroup != nullptr);

        for (QAction* action : actionGroup->actions())
        {
            action->setChecked(str == action->property(STATS_MODE_KEY).toString());
        }

        refreshSoon(REFRESH_STATUS_BAR);
        break;

    case Prefs::SORT_REVERSED:
        ui.action_ReverseSortOrder->setChecked(myPrefs.getBool(key));
        break;

    case Prefs::SORT_MODE:
        i = myPrefs.get<SortMode>(key).mode();
        actionGroup = ui.action_SortByActivity->actionGroup();
        assert(actionGroup != nullptr);

        for (QAction* action : actionGroup->actions())
        {
            action->setChecked(i == action->property(SORT_MODE_KEY).toInt());
        }

        break;

    case Prefs::DSPEED_ENABLED:
        (myPrefs.get<bool>(key) ? myDlimitOnAction : myDlimitOffAction)->setChecked(true);
        break;

    case Prefs::DSPEED:
        myDlimitOnAction->setText(tr("Limited at %1").arg(Formatter::speedToString(Speed::fromKBps(myPrefs.get<int>(key)))));
        break;

    case Prefs::USPEED_ENABLED:
        (myPrefs.get<bool>(key) ? myUlimitOnAction : myUlimitOffAction)->setChecked(true);
        break;

    case Prefs::USPEED:
        myUlimitOnAction->setText(tr("Limited at %1").arg(Formatter::speedToString(Speed::fromKBps(myPrefs.get<int>(key)))));
        break;

    case Prefs::RATIO_ENABLED:
        (myPrefs.get<bool>(key) ? myRatioOnAction : myRatioOffAction)->setChecked(true);
        break;

    case Prefs::RATIO:
        myRatioOnAction->setText(tr("Stop at Ratio (%1)").arg(Formatter::ratioToString(myPrefs.get<double>(key))));
        break;

    case Prefs::FILTERBAR:
        b = myPrefs.getBool(key);
        myFilterBar->setVisible(b);
        ui.action_Filterbar->setChecked(b);
        break;

    case Prefs::STATUSBAR:
        b = myPrefs.getBool(key);
        ui.statusBar->setVisible(b);
        ui.action_Statusbar->setChecked(b);
        break;

    case Prefs::TOOLBAR:
        b = myPrefs.getBool(key);
        ui.toolBar->setVisible(b);
        ui.action_Toolbar->setChecked(b);
        break;

    case Prefs::SHOW_TRAY_ICON:
        b = myPrefs.getBool(key);
        ui.action_TrayIcon->setChecked(b);
        myTrayIcon.setVisible(b);
        qApp->setQuitOnLastWindowClosed(!b);
        refreshSoon(REFRESH_TRAY_ICON);
        break;

    case Prefs::COMPACT_VIEW:
        {
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537

            QItemSelectionModel* selectionModel(ui.listView->selectionModel());
            QItemSelection const selection(selectionModel->selection());
            QModelIndex const currentIndex(selectionModel->currentIndex());

#endif

            b = myPrefs.getBool(key);
            ui.action_CompactView->setChecked(b);
            ui.listView->setItemDelegate(b ? myTorrentDelegateMin : myTorrentDelegate);

#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537

            selectionModel->clear();
            ui.listView->reset(); // force the rows to resize
            selectionModel->select(selection, QItemSelectionModel::Select);
            selectionModel->setCurrentIndex(currentIndex, QItemSelectionModel::NoUpdate);

#endif

            break;
        }

    case Prefs::MAIN_WINDOW_X:
    case Prefs::MAIN_WINDOW_Y:
    case Prefs::MAIN_WINDOW_WIDTH:
    case Prefs::MAIN_WINDOW_HEIGHT:
        setGeometry(myPrefs.getInt(Prefs::MAIN_WINDOW_X), myPrefs.getInt(Prefs::MAIN_WINDOW_Y),
            myPrefs.getInt(Prefs::MAIN_WINDOW_WIDTH), myPrefs.getInt(Prefs::MAIN_WINDOW_HEIGHT));
        break;

    case Prefs::ALT_SPEED_LIMIT_ENABLED:
    case Prefs::ALT_SPEED_LIMIT_UP:
    case Prefs::ALT_SPEED_LIMIT_DOWN:
        {
            b = myPrefs.getBool(Prefs::ALT_SPEED_LIMIT_ENABLED);
            myAltSpeedAction->setChecked(b);
            ui.altSpeedButton->setChecked(b);
            QString const fmt = b ? tr("Click to disable Temporary Speed Limits\n (%1 down, %2 up)") :
                tr("Click to enable Temporary Speed Limits\n (%1 down, %2 up)");
            Speed const d = Speed::fromKBps(myPrefs.getInt(Prefs::ALT_SPEED_LIMIT_DOWN));
            Speed const u = Speed::fromKBps(myPrefs.getInt(Prefs::ALT_SPEED_LIMIT_UP));
            ui.altSpeedButton->setToolTip(fmt.arg(Formatter::speedToString(d)).arg(Formatter::speedToString(u)));
            break;
        }

    default:
        break;
    }
}

/***
****
***/

namespace
{

QLatin1String const SHOW_OPTIONS_CHECKBOX_NAME("show-options-checkbox");

} // namespace

void MainWindow::newTorrent()
{
    MakeDialog* dialog = new MakeDialog(mySession, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openTorrent()
{
    QFileDialog* d;
    d = new QFileDialog(this, tr("Open Torrent"), myPrefs.getString(Prefs::OPEN_DIALOG_FOLDER),
        tr("Torrent Files (*.torrent);;All Files (*.*)"));
    d->setFileMode(QFileDialog::ExistingFiles);
    d->setAttribute(Qt::WA_DeleteOnClose);

    auto const l = qobject_cast<QGridLayout*>(d->layout());

    if (l != nullptr)
    {
        QCheckBox* b = new QCheckBox(tr("Show &options dialog"));
        b->setChecked(myPrefs.getBool(Prefs::OPTIONS_PROMPT));
        b->setObjectName(SHOW_OPTIONS_CHECKBOX_NAME);
        l->addWidget(b, l->rowCount(), 0, 1, -1, Qt::AlignLeft);
    }

    connect(d, SIGNAL(filesSelected(QStringList)), this, SLOT(addTorrents(QStringList)));

    d->open();
}

void MainWindow::openURL()
{
    QString str = qApp->clipboard()->text(QClipboard::Selection);

    if (!AddData::isSupported(str))
    {
        str = qApp->clipboard()->text(QClipboard::Clipboard);
    }

    if (!AddData::isSupported(str))
    {
        str.clear();
    }

    addTorrent(str, true);
}

void MainWindow::addTorrents(QStringList const& filenames)
{
    bool showOptions = myPrefs.getBool(Prefs::OPTIONS_PROMPT);

    QFileDialog const* const fileDialog = qobject_cast<QFileDialog const*>(sender());

    if (fileDialog != nullptr)
    {
        QCheckBox const* const b = fileDialog->findChild<QCheckBox const*>(SHOW_OPTIONS_CHECKBOX_NAME);

        if (b != nullptr)
        {
            showOptions = b->isChecked();
        }
    }

    for (QString const& filename : filenames)
    {
        addTorrent(filename, showOptions);
    }
}

void MainWindow::addTorrent(AddData const& addMe, bool showOptions)
{
    if (showOptions)
    {
        OptionsDialog* o = new OptionsDialog(mySession, myPrefs, addMe, this);
        o->show();
        qApp->alert(o);
    }
    else
    {
        mySession.addTorrent(addMe);
        qApp->alert(this);
    }
}

void MainWindow::removeTorrents(bool const deleteFiles)
{
    torrent_ids_t ids;
    QMessageBox msgBox(this);
    QString primary_text;
    QString secondary_text;
    int incomplete = 0;
    int connected = 0;
    int count;

    for (QModelIndex const& index : ui.listView->selectionModel()->selectedRows())
    {
        Torrent const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());
        ids.insert(tor->id());

        if (tor->connectedPeers())
        {
            ++connected;
        }

        if (!tor->isDone())
        {
            ++incomplete;
        }
    }

    if (ids.empty())
    {
        return;
    }

    count = ids.size();

    if (!deleteFiles)
    {
        primary_text = count == 1 ? tr("Remove torrent?") : tr("Remove %Ln torrent(s)?", nullptr, count);
    }
    else
    {
        primary_text = count == 1 ? tr("Delete this torrent's downloaded files?") :
            tr("Delete these %Ln torrent(s)' downloaded files?", nullptr, count);
    }

    if (incomplete == 0 && connected == 0)
    {
        secondary_text = count == 1 ?
            tr("Once removed, continuing the transfer will require the torrent file or magnet link.") :
            tr("Once removed, continuing the transfers will require the torrent files or magnet links.");
    }
    else if (count == incomplete)
    {
        secondary_text = count == 1 ? tr("This torrent has not finished downloading.") :
            tr("These torrents have not finished downloading.");
    }
    else if (count == connected)
    {
        secondary_text = count == 1 ? tr("This torrent is connected to peers.") :
            tr("These torrents are connected to peers.");
    }
    else
    {
        if (connected != 0)
        {
            secondary_text = connected == 1 ? tr("One of these torrents is connected to peers.") :
                tr("Some of these torrents are connected to peers.");
        }

        if (connected != 0 && incomplete != 0)
        {
            secondary_text += QLatin1Char('\n');
        }

        if (incomplete != 0)
        {
            secondary_text += incomplete == 1 ? tr("One of these torrents has not finished downloading.") :
                tr("Some of these torrents have not finished downloading.");
        }
    }

    msgBox.setWindowTitle(QLatin1String(" "));
    msgBox.setText(QString::fromLatin1("<big><b>%1</big></b>").arg(primary_text));
    msgBox.setInformativeText(secondary_text);
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    msgBox.setIcon(QMessageBox::Question);
    // hack needed to keep the dialog from being too narrow
    auto layout = qobject_cast<QGridLayout*>(msgBox.layout());

    if (layout == nullptr)
    {
        layout = new QGridLayout;
        msgBox.setLayout(layout);
    }

    QSpacerItem* spacer = new QSpacerItem(450, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());

    if (msgBox.exec() == QMessageBox::Ok)
    {
        ui.listView->selectionModel()->clear();
        mySession.removeTorrents(ids, deleteFiles);
    }
}

/***
****
***/

void MainWindow::updateNetworkIcon()
{
    time_t const now = time(nullptr);
    int const period = 3;
    time_t const secondsSinceLastSend = now - myLastSendTime;
    time_t const secondsSinceLastRead = now - myLastReadTime;
    bool const isSending = secondsSinceLastSend <= period;
    bool const isReading = secondsSinceLastRead <= period;
    QPixmap pixmap;

    if (myNetworkError)
    {
        pixmap = myPixmapNetworkError;
    }
    else if (isSending && isReading)
    {
        pixmap = myPixmapNetworkTransmitReceive;
    }
    else if (isSending)
    {
        pixmap = myPixmapNetworkTransmit;
    }
    else if (isReading)
    {
        pixmap = myPixmapNetworkReceive;
    }
    else
    {
        pixmap = myPixmapNetworkIdle;
    }

    QString tip;
    QString const url = mySession.getRemoteUrl().host();

    if (myLastReadTime == 0)
    {
        tip = tr("%1 has not responded yet").arg(url);
    }
    else if (myNetworkError)
    {
        tip = tr(myErrorMessage.toLatin1().constData());
    }
    else if (secondsSinceLastRead < 30)
    {
        tip = tr("%1 is responding").arg(url);
    }
    else if (secondsSinceLastRead < 60 * 2)
    {
        tip = tr("%1 last responded %2 ago").arg(url).arg(Formatter::timeToString(secondsSinceLastRead));
    }
    else
    {
        tip = tr("%1 is not responding").arg(url);
    }

    ui.networkLabel->setPixmap(pixmap);
    ui.networkLabel->setToolTip(tip);
}

void MainWindow::onNetworkTimer()
{
    updateNetworkIcon();
}

void MainWindow::dataReadProgress()
{
    if (!myNetworkError)
    {
        myLastReadTime = time(nullptr);
    }
}

void MainWindow::dataSendProgress()
{
    myLastSendTime = time(nullptr);
}

void MainWindow::onNetworkResponse(QNetworkReply::NetworkError code, QString const& message)
{
    bool const hadError = myNetworkError;
    bool const haveError = code != QNetworkReply::NoError && code != QNetworkReply::UnknownContentError;

    myNetworkError = haveError;
    myErrorMessage = message;
    refreshSoon(REFRESH_TRAY_ICON);
    updateNetworkIcon();

    // Refresh our model if we've just gotten a clean connection to the session.
    // That way we can rebuild after a restart of transmission-daemon
    if (hadError && !haveError)
    {
        myModel.clear();
    }
}

void MainWindow::wrongAuthentication()
{
    mySession.stop();
    openSession();
}

/***
****
***/

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    QMimeData const* mime = event->mimeData();

    if (mime->hasFormat(QLatin1String("application/x-bittorrent")) || mime->hasUrls() ||
        mime->text().trimmed().endsWith(QLatin1String(".torrent"), Qt::CaseInsensitive) ||
        mime->text().startsWith(QLatin1String("magnet:"), Qt::CaseInsensitive))
    {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList list;

    if (event->mimeData()->hasText())
    {
        list = event->mimeData()->text().trimmed().split(QLatin1Char('\n'));
    }
    else if (event->mimeData()->hasUrls())
    {
        for (QUrl const& url : event->mimeData()->urls())
        {
            list.append(url.toLocalFile());
        }
    }

    for (QString const& entry : list)
    {
        QString key = entry.trimmed();

        if (!key.isEmpty())
        {
            QUrl const url(key);

            if (url.isLocalFile())
            {
                key = url.toLocalFile();
            }

            qApp->addTorrent(key);
        }
    }
}

/***
****
***/

void MainWindow::contextMenuEvent(QContextMenuEvent* event)
{
    ui.menuTorrent->popup(event->globalPos());
}
