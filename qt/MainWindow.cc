/*
 * This file Copyright (C) 2009-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>
#include <cassert>
#include <functional>
#include <utility>

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

/**
 * This is a proxy-style for that forces it to be always disabled.
 * We use this to make our torrent list view behave consistently on
 * both GTK and Qt implementations.
 */
class ListViewProxyStyle : public QProxyStyle
{
public:
    int styleHint(StyleHint hint, QStyleOption const* option = nullptr, QWidget const* widget = nullptr,
        QStyleHintReturn* return_data = nullptr) const override
    {
        if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        {
            return 0;
        }

        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }
};

QIcon MainWindow::getStockIcon(QString const& name, int fallback)
{
    QIcon icon = QIcon::fromTheme(name);

    if (icon.isNull() && fallback >= 0)
    {
        icon = style()->standardIcon(QStyle::StandardPixmap(fallback), nullptr, this);
    }

    return icon;
}

QIcon MainWindow::addEmblem(QIcon base_icon, QStringList const& emblem_names)
{
    if (base_icon.isNull())
    {
        return base_icon;
    }

    QIcon emblem_icon;

    for (QString const& emblem_name : emblem_names)
    {
        emblem_icon = QIcon::fromTheme(emblem_name);

        if (!emblem_icon.isNull())
        {
            break;
        }
    }

    if (emblem_icon.isNull())
    {
        return base_icon;
    }

    QIcon icon;

    for (QSize const& size : base_icon.availableSizes())
    {
        QSize const emblem_size = size / 2;
        QRect const emblem_rect = QStyle::alignedRect(layoutDirection(), Qt::AlignBottom | Qt::AlignRight, emblem_size,
            QRect(QPoint(0, 0), size));

        QPixmap pixmap = base_icon.pixmap(size);
        QPixmap emblem_pixmap = emblem_icon.pixmap(emblem_size);

        {
            QPainter painter(&pixmap);
            painter.drawPixmap(emblem_rect, emblem_pixmap, emblem_pixmap.rect());
        }

        icon.addPixmap(pixmap);
    }

    return icon;
}

MainWindow::MainWindow(Session& session, Prefs& prefs, TorrentModel& model, bool minimized) :
    session_(session),
    prefs_(prefs),
    model_(model),
    lvp_style_(new ListViewProxyStyle{}),
    filter_model_(prefs),
    torrent_delegate_(new TorrentDelegate(this)),
    torrent_delegate_min_(new TorrentDelegateMin(this)),
    network_timer_(this),
    total_ratio_stats_mode_name_(QStringLiteral("total-ratio")),
    total_transfer_stats_mode_name_(QStringLiteral("total-transfer")),
    session_ratio_stats_mode_name_(QStringLiteral("session-ratio")),
    session_transfer_stats_mode_name_(QStringLiteral("session-transfer")),
    show_options_checkbox_name_(QStringLiteral("show-options-checkbox")),
    refresh_timer_(this)
{
    setAcceptDrops(true);

    auto* sep = new QAction(this);
    sep->setSeparator(true);

    ui_.setupUi(this);

    ui_.listView->setStyle(lvp_style_.get());
    ui_.listView->setAttribute(Qt::WA_MacShowFocusRect, false);

    // icons
    QIcon const icon_play = getStockIcon(QStringLiteral("media-playback-start"), QStyle::SP_MediaPlay);
    QIcon const icon_pause = getStockIcon(QStringLiteral("media-playback-pause"), QStyle::SP_MediaPause);
    QIcon const icon_open = getStockIcon(QStringLiteral("document-open"), QStyle::SP_DialogOpenButton);
    ui_.action_OpenFile->setIcon(icon_open);
    ui_.action_AddURL->setIcon(addEmblem(icon_open,
        QStringList() << QStringLiteral("emblem-web") << QStringLiteral("applications-internet")));
    ui_.action_New->setIcon(getStockIcon(QStringLiteral("document-new"), QStyle::SP_DesktopIcon));
    ui_.action_Properties->setIcon(getStockIcon(QStringLiteral("document-properties"), QStyle::SP_DesktopIcon));
    ui_.action_OpenFolder->setIcon(getStockIcon(QStringLiteral("folder-open"), QStyle::SP_DirOpenIcon));
    ui_.action_Start->setIcon(icon_play);
    ui_.action_StartNow->setIcon(icon_play);
    ui_.action_Announce->setIcon(getStockIcon(QStringLiteral("network-transmit-receive")));
    ui_.action_Pause->setIcon(icon_pause);
    ui_.action_Remove->setIcon(getStockIcon(QStringLiteral("list-remove"), QStyle::SP_TrashIcon));
    ui_.action_Delete->setIcon(getStockIcon(QStringLiteral("edit-delete"), QStyle::SP_TrashIcon));
    ui_.action_StartAll->setIcon(icon_play);
    ui_.action_PauseAll->setIcon(icon_pause);
    ui_.action_Quit->setIcon(getStockIcon(QStringLiteral("application-exit")));
    ui_.action_SelectAll->setIcon(getStockIcon(QStringLiteral("edit-select-all")));
    ui_.action_ReverseSortOrder->setIcon(getStockIcon(QStringLiteral("view-sort-ascending"), QStyle::SP_ArrowDown));
    ui_.action_Preferences->setIcon(getStockIcon(QStringLiteral("preferences-system")));
    ui_.action_Contents->setIcon(getStockIcon(QStringLiteral("help-contents"), QStyle::SP_DialogHelpButton));
    ui_.action_About->setIcon(getStockIcon(QStringLiteral("help-about")));
    ui_.action_QueueMoveTop->setIcon(getStockIcon(QStringLiteral("go-top")));
    ui_.action_QueueMoveUp->setIcon(getStockIcon(QStringLiteral("go-up"), QStyle::SP_ArrowUp));
    ui_.action_QueueMoveDown->setIcon(getStockIcon(QStringLiteral("go-down"), QStyle::SP_ArrowDown));
    ui_.action_QueueMoveBottom->setIcon(getStockIcon(QStringLiteral("go-bottom")));

    auto make_network_pixmap = [this](QString name, QSize size = { 16, 16 })
        {
            return getStockIcon(name, QStyle::SP_DriveNetIcon).pixmap(size);
        };
    pixmap_network_error_ = make_network_pixmap(QStringLiteral("network-error"));
    pixmap_network_idle_ = make_network_pixmap(QStringLiteral("network-idle"));
    pixmap_network_receive_ = make_network_pixmap(QStringLiteral("network-receive"));
    pixmap_network_transmit_ = make_network_pixmap(QStringLiteral("network-transmit"));
    pixmap_network_transmit_receive_ = make_network_pixmap(QStringLiteral("network-transmit-receive"));

    // ui signals
    connect(ui_.action_Toolbar, SIGNAL(toggled(bool)), this, SLOT(setToolbarVisible(bool)));
    connect(ui_.action_Filterbar, SIGNAL(toggled(bool)), this, SLOT(setFilterbarVisible(bool)));
    connect(ui_.action_Statusbar, SIGNAL(toggled(bool)), this, SLOT(setStatusbarVisible(bool)));
    connect(ui_.action_CompactView, SIGNAL(toggled(bool)), this, SLOT(setCompactView(bool)));
    connect(ui_.action_ReverseSortOrder, SIGNAL(toggled(bool)), this, SLOT(setSortAscendingPref(bool)));
    connect(ui_.action_Start, SIGNAL(triggered()), this, SLOT(startSelected()));
    connect(ui_.action_QueueMoveTop, SIGNAL(triggered()), this, SLOT(queueMoveTop()));
    connect(ui_.action_QueueMoveUp, SIGNAL(triggered()), this, SLOT(queueMoveUp()));
    connect(ui_.action_QueueMoveDown, SIGNAL(triggered()), this, SLOT(queueMoveDown()));
    connect(ui_.action_QueueMoveBottom, SIGNAL(triggered()), this, SLOT(queueMoveBottom()));
    connect(ui_.action_StartNow, SIGNAL(triggered()), this, SLOT(startSelectedNow()));
    connect(ui_.action_Pause, SIGNAL(triggered()), this, SLOT(pauseSelected()));
    connect(ui_.action_Remove, SIGNAL(triggered()), this, SLOT(removeSelected()));
    connect(ui_.action_Delete, SIGNAL(triggered()), this, SLOT(deleteSelected()));
    connect(ui_.action_Verify, SIGNAL(triggered()), this, SLOT(verifySelected()));
    connect(ui_.action_Announce, SIGNAL(triggered()), this, SLOT(reannounceSelected()));
    connect(ui_.action_StartAll, SIGNAL(triggered()), this, SLOT(startAll()));
    connect(ui_.action_PauseAll, SIGNAL(triggered()), this, SLOT(pauseAll()));
    connect(ui_.action_OpenFile, SIGNAL(triggered()), this, SLOT(openTorrent()));
    connect(ui_.action_AddURL, SIGNAL(triggered()), this, SLOT(openURL()));
    connect(ui_.action_New, SIGNAL(triggered()), this, SLOT(newTorrent()));
    connect(ui_.action_Preferences, SIGNAL(triggered()), this, SLOT(openPreferences()));
    connect(ui_.action_Statistics, SIGNAL(triggered()), this, SLOT(openStats()));
    connect(ui_.action_Donate, SIGNAL(triggered()), this, SLOT(openDonate()));
    connect(ui_.action_About, SIGNAL(triggered()), this, SLOT(openAbout()));
    connect(ui_.action_Contents, SIGNAL(triggered()), this, SLOT(openHelp()));
    connect(ui_.action_OpenFolder, SIGNAL(triggered()), this, SLOT(openFolder()));
    connect(ui_.action_CopyMagnetToClipboard, SIGNAL(triggered()), this, SLOT(copyMagnetLinkToClipboard()));
    connect(ui_.action_SetLocation, SIGNAL(triggered()), this, SLOT(setLocation()));
    connect(ui_.action_Properties, SIGNAL(triggered()), this, SLOT(openProperties()));
    connect(ui_.action_SessionDialog, SIGNAL(triggered()), this, SLOT(openSession()));
    connect(ui_.listView, SIGNAL(activated(QModelIndex)), ui_.action_Properties, SLOT(trigger()));
    connect(ui_.action_SelectAll, SIGNAL(triggered()), ui_.listView, SLOT(selectAll()));
    connect(ui_.action_DeselectAll, SIGNAL(triggered()), ui_.listView, SLOT(clearSelection()));
    connect(ui_.action_Quit, SIGNAL(triggered()), qApp, SLOT(quit()));

    auto refresh_action_sensitivity_soon = [this]() { refreshSoon(REFRESH_ACTION_SENSITIVITY); };
    connect(&filter_model_, &TorrentFilter::rowsInserted, this, refresh_action_sensitivity_soon);
    connect(&filter_model_, &TorrentFilter::rowsRemoved, this, refresh_action_sensitivity_soon);
    connect(&model_, &TorrentModel::torrentsChanged, this, refresh_action_sensitivity_soon);

    // torrent view
    filter_model_.setSourceModel(&model_);
    auto refresh_soon_adapter = [this]() { refreshSoon(); };
    connect(&model_, &TorrentModel::modelReset, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::rowsRemoved, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::rowsInserted, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::torrentsChanged, this, refresh_soon_adapter);

    ui_.listView->setModel(&filter_model_);
    connect(ui_.listView->selectionModel(), &QItemSelectionModel::selectionChanged, refresh_action_sensitivity_soon);

    std::array<std::pair<QAction*, int>, 9> const sort_modes =
    {{
        { ui_.action_SortByActivity, SortMode::SORT_BY_ACTIVITY },
        { ui_.action_SortByAge, SortMode::SORT_BY_AGE },
        { ui_.action_SortByETA, SortMode::SORT_BY_ETA },
        { ui_.action_SortByName, SortMode::SORT_BY_NAME },
        { ui_.action_SortByProgress, SortMode::SORT_BY_PROGRESS },
        { ui_.action_SortByQueue, SortMode::SORT_BY_QUEUE },
        { ui_.action_SortByRatio, SortMode::SORT_BY_RATIO },
        { ui_.action_SortBySize, SortMode::SORT_BY_SIZE },
        { ui_.action_SortByState, SortMode::SORT_BY_STATE }
    }};

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    auto* action_group = new QActionGroup(this);

    for (auto const& mode : sort_modes)
    {
        mode.first->setProperty(SORT_MODE_KEY, mode.second);
        action_group->addAction(mode.first);
    }

    connect(action_group, SIGNAL(triggered(QAction*)), this, SLOT(onSortModeChanged(QAction*)));

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    alt_speed_action_ = new QAction(tr("Speed Limits"), this);
    alt_speed_action_->setIcon(ui_.altSpeedButton->icon());
    alt_speed_action_->setCheckable(true);
    connect(alt_speed_action_, SIGNAL(triggered()), this, SLOT(toggleSpeedMode()));

    auto* menu = new QMenu(this);
    menu->addAction(ui_.action_OpenFile);
    menu->addAction(ui_.action_AddURL);
    menu->addSeparator();
    menu->addAction(ui_.action_ShowMainWindow);
    menu->addAction(ui_.action_ShowMessageLog);
    menu->addAction(ui_.action_About);
    menu->addSeparator();
    menu->addAction(ui_.action_StartAll);
    menu->addAction(ui_.action_PauseAll);
    menu->addAction(alt_speed_action_);
    menu->addSeparator();
    menu->addAction(ui_.action_Quit);
    tray_icon_.setContextMenu(menu);
    tray_icon_.setIcon(QIcon::fromTheme(QStringLiteral("transmission-tray-icon"), qApp->windowIcon()));

    connect(&prefs_, SIGNAL(changed(int)), this, SLOT(refreshPref(int)));
    connect(ui_.action_ShowMainWindow, SIGNAL(triggered(bool)), this, SLOT(toggleWindows(bool)));
    connect(&tray_icon_, SIGNAL(activated(QSystemTrayIcon::ActivationReason)), this,
        SLOT(trayActivated(QSystemTrayIcon::ActivationReason)));

    toggleWindows(!minimized);
    ui_.action_TrayIcon->setChecked(minimized || prefs.getBool(Prefs::SHOW_TRAY_ICON));

    initStatusBar();
    ui_.verticalLayout->insertWidget(0, filter_bar_ = new FilterBar(prefs_, model_, filter_model_));

    auto refresh_header_soon = [this]() { refreshSoon(REFRESH_TORRENT_VIEW_HEADER); };
    connect(&model_, &TorrentModel::rowsInserted, this, refresh_header_soon);
    connect(&model_, &TorrentModel::rowsRemoved, this, refresh_header_soon);
    connect(&filter_model_, &TorrentFilter::rowsInserted, this, refresh_header_soon);
    connect(&filter_model_, &TorrentFilter::rowsRemoved, this, refresh_header_soon);
    connect(ui_.listView, SIGNAL(headerDoubleClicked()), filter_bar_, SLOT(clear()));

    static std::array<int, 16> constexpr InitKeys =
    {
        Prefs::ALT_SPEED_LIMIT_ENABLED,
        Prefs::COMPACT_VIEW,
        Prefs::DSPEED,
        Prefs::DSPEED_ENABLED,
        Prefs::FILTERBAR,
        Prefs::MAIN_WINDOW_X,
        Prefs::RATIO,
        Prefs::RATIO_ENABLED,
        Prefs::SHOW_TRAY_ICON,
        Prefs::SORT_MODE,
        Prefs::SORT_REVERSED,
        Prefs::STATUSBAR,
        Prefs::STATUSBAR_STATS,
        Prefs::TOOLBAR,
        Prefs::USPEED,
        Prefs::USPEED_ENABLED,
    };
    for (auto const key : InitKeys)
    {
        refreshPref(key);
    }

    auto refresh_status_soon = [this]() { refreshSoon(REFRESH_STATUS_BAR); };
    connect(&session_, SIGNAL(sourceChanged()), this, SLOT(onSessionSourceChanged()));
    connect(&session_, &Session::statsUpdated, this, refresh_status_soon);
    connect(&session_, SIGNAL(dataReadProgress()), this, SLOT(dataReadProgress()));
    connect(&session_, SIGNAL(dataSendProgress()), this, SLOT(dataSendProgress()));
    connect(&session_, SIGNAL(httpAuthenticationRequired()), this, SLOT(wrongAuthentication()));
    connect(&session_, SIGNAL(networkResponse(QNetworkReply::NetworkError, QString)), this,
        SLOT(onNetworkResponse(QNetworkReply::NetworkError, QString)));

    if (session_.isServer())
    {
        ui_.networkLabel->hide();
    }
    else
    {
        connect(&network_timer_, &QTimer::timeout, this, &MainWindow::onNetworkTimer);
        network_timer_.start(1000);
    }

    connect(&refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);
    refreshSoon();
}

void MainWindow::onSessionSourceChanged()
{
    model_.clear();
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
        prefs_.set(p[i].toInt(), p[i + 1]);
    }
}

void MainWindow::onSetPrefs(bool is_checked)
{
    if (is_checked)
    {
        onSetPrefs();
    }
}

void MainWindow::initStatusBar()
{
    ui_.optionsButton->setMenu(createOptionsMenu());

    int const minimum_speed_width =
        ui_.downloadSpeedLabel->fontMetrics().boundingRect(Formatter::get().uploadSpeedToString(Speed::fromKBps(999.99))).width();
    ui_.downloadSpeedLabel->setMinimumWidth(minimum_speed_width);
    ui_.uploadSpeedLabel->setMinimumWidth(minimum_speed_width);

    ui_.statsModeButton->setMenu(createStatsModeMenu());

    connect(ui_.altSpeedButton, SIGNAL(clicked()), this, SLOT(toggleSpeedMode()));
}

QMenu* MainWindow::createOptionsMenu()
{
    auto const init_speed_sub_menu = [this](QMenu* menu, QAction*& off_action, QAction*& on_action, int pref, int enabled_pref)
        {
            std::array<int, 13> stock_speeds = { 5, 10, 20, 30, 40, 50, 75, 100, 150, 200, 250, 500, 750 };
            int const current_value = prefs_.get<int>(pref);

            // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
            auto* action_group = new QActionGroup(this);

            off_action = menu->addAction(tr("Unlimited"));
            off_action->setCheckable(true);
            off_action->setProperty(PREF_VARIANTS_KEY, QVariantList() << enabled_pref << false);
            action_group->addAction(off_action);
            connect(off_action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            on_action =
                menu->addAction(tr("Limited at %1").arg(Formatter::get().speedToString(Speed::fromKBps(current_value))));
            on_action->setCheckable(true);
            on_action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << current_value << enabled_pref << true);
            action_group->addAction(on_action);
            connect(on_action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            menu->addSeparator();

            for (int const i : stock_speeds)
            {
                QAction* action = menu->addAction(Formatter::get().speedToString(Speed::fromKBps(i)));
                action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << i << enabled_pref << true);
                connect(action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
            }
        };

    auto const init_seed_ratio_sub_menu =
        [this](QMenu* menu, QAction*& off_action, QAction*& on_action, int pref, int enabled_pref)
        {
            std::array<double, 7> stock_ratios = { 0.25, 0.50, 0.75, 1, 1.5, 2, 3 };
            auto const current_value = prefs_.get<double>(pref);

            auto* action_group = new QActionGroup(this);

            off_action = menu->addAction(tr("Seed Forever"));
            off_action->setCheckable(true);
            off_action->setProperty(PREF_VARIANTS_KEY, QVariantList() << enabled_pref << false);
            action_group->addAction(off_action);
            connect(off_action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            on_action = menu->addAction(tr("Stop at Ratio (%1)").arg(Formatter::get().ratioToString(current_value)));
            on_action->setCheckable(true);
            on_action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << current_value << enabled_pref << true);
            action_group->addAction(on_action);
            connect(on_action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs(bool)));

            menu->addSeparator();

            for (double const i : stock_ratios)
            {
                QAction* action = menu->addAction(Formatter::get().ratioToString(i));
                action->setProperty(PREF_VARIANTS_KEY, QVariantList() << pref << i << enabled_pref << true);
                connect(action, SIGNAL(triggered(bool)), this, SLOT(onSetPrefs()));
            }
        };

    auto* menu = new QMenu(this);

    init_speed_sub_menu(menu->addMenu(tr("Limit Download Speed")), dlimit_off_action_, dlimit_on_action_, Prefs::DSPEED,
        Prefs::DSPEED_ENABLED);
    init_speed_sub_menu(menu->addMenu(tr("Limit Upload Speed")), ulimit_off_action_, ulimit_on_action_, Prefs::USPEED,
        Prefs::USPEED_ENABLED);

    menu->addSeparator();

    init_seed_ratio_sub_menu(menu->addMenu(tr("Stop Seeding at Ratio")), ratio_off_action_, ratio_on_action_, Prefs::RATIO,
        Prefs::RATIO_ENABLED);

    return menu;
}

QMenu* MainWindow::createStatsModeMenu()
{
    std::array<QPair<QAction*, QString>, 4> stats_modes =
    {
        qMakePair(ui_.action_TotalRatio, total_ratio_stats_mode_name_),
        qMakePair(ui_.action_TotalTransfer, total_transfer_stats_mode_name_),
        qMakePair(ui_.action_SessionRatio, session_ratio_stats_mode_name_),
        qMakePair(ui_.action_SessionTransfer, session_transfer_stats_mode_name_)
    };

    auto* action_group = new QActionGroup(this);
    auto* menu = new QMenu(this);

    for (auto const& mode : stats_modes)
    {
        mode.first->setProperty(STATS_MODE_KEY, QString(mode.second));
        action_group->addAction(mode.first);
        menu->addAction(mode.first);
    }

    connect(action_group, SIGNAL(triggered(QAction*)), this, SLOT(onStatsModeChanged(QAction*)));

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    return menu;
}

/****
*****
****/

void MainWindow::onSortModeChanged(QAction* action)
{
    prefs_.set(Prefs::SORT_MODE, SortMode(action->property(SORT_MODE_KEY).toInt()));
}

void MainWindow::setSortAscendingPref(bool b)
{
    prefs_.set(Prefs::SORT_REVERSED, b);
}

/****
*****
****/

void MainWindow::showEvent(QShowEvent* event)
{
    Q_UNUSED(event)

    ui_.action_ShowMainWindow->setChecked(true);
}

/****
*****
****/

void MainWindow::hideEvent(QHideEvent* event)
{
    Q_UNUSED(event)

    if (!isVisible())
    {
        ui_.action_ShowMainWindow->setChecked(false);
    }
}

/****
*****
****/

void MainWindow::openSession()
{
    Utils::openDialog(session_dialog_, session_, prefs_, this);
}

void MainWindow::openPreferences()
{
    Utils::openDialog(prefs_dialog_, session_, prefs_, this);
}

void MainWindow::openProperties()
{
    Utils::openDialog(details_dialog_, session_, prefs_, model_, this);
    details_dialog_->setIds(getSelectedTorrents());
}

void MainWindow::setLocation()
{
    auto* d = new RelocateDialog(session_, model_, getSelectedTorrents(), this);
    d->setAttribute(Qt::WA_DeleteOnClose, true);
    d->show();
}

namespace
{

// Open Folder & select torrent's file or top folder
#undef HAVE_OPEN_SELECT

#if defined(Q_OS_WIN)

#define HAVE_OPEN_SELECT

void openSelect(QString const& path)
{
    auto const explorer = QStringLiteral("explorer");
    QString param;

    if (!QFileInfo(path).isDir())
    {
        param = QStringLiteral("/select,");
    }

    param += QDir::toNativeSeparators(path);
    QProcess::startDetached(explorer, QStringList(param));
}

#elif defined(Q_OS_MAC)

#define HAVE_OPEN_SELECT

void openSelect(QString const& path)
{
    QStringList script_args;
    script_args << QStringLiteral("-e") <<
        QStringLiteral("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path);
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), script_args);

    script_args.clear();
    script_args << QStringLiteral("-e") << QStringLiteral("tell application \"Finder\" to activate");
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), script_args);
}

#endif

} // namespace

void MainWindow::openFolder()
{
    auto const selected_torrents = getSelectedTorrents();

    if (selected_torrents.size() != 1)
    {
        return;
    }

    int const torrent_id(*selected_torrents.begin());
    Torrent const* tor(model_.getTorrentFromId(torrent_id));

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

    QString const first_file = files.at(0).filename;
    int slash_index = first_file.indexOf(QLatin1Char('/'));

    if (slash_index > -1)
    {
        path = path + QLatin1Char('/') + first_file.left(slash_index);
    }

#ifdef HAVE_OPEN_SELECT

    else
    {
        openSelect(path + QLatin1Char('/') + first_file);
        return;
    }

#endif

    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::copyMagnetLinkToClipboard()
{
    int const id(*getSelectedTorrents().begin());
    session_.copyMagnetLinkToClipboard(id);
}

void MainWindow::openStats()
{
    Utils::openDialog(stats_dialog_, session_, this);
}

void MainWindow::openDonate()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://transmissionbt.com/donate/")));
}

void MainWindow::openAbout()
{
    Utils::openDialog(about_dialog_, this);
}

void MainWindow::openHelp()
{
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://transmissionbt.com/help/gtk/%1.%2x").arg(MAJOR_VERSION).
        arg(MINOR_VERSION / 10)));
}

/****
*****
****/

void MainWindow::refreshSoon(int fields)
{
    refresh_fields_ |= fields;

    if (!refresh_timer_.isActive())
    {
        refresh_timer_.setSingleShot(true);
        refresh_timer_.start(200);
    }
}

MainWindow::TransferStats MainWindow::getTransferStats() const
{
    TransferStats stats;

    for (auto const& tor : model_.torrents())
    {
        stats.speed_up += tor->uploadSpeed();
        stats.speed_down += tor->downloadSpeed();
        stats.peers_sending += tor->webseedsWeAreDownloadingFrom();
        stats.peers_sending += tor->peersWeAreDownloadingFrom();
        stats.peers_receiving += tor->peersWeAreUploadingTo();
    }

    return stats;
}

void MainWindow::onRefreshTimer()
{
    int fields = 0;
    std::swap(fields, refresh_fields_);

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
    QString title(QStringLiteral("Transmission"));
    QUrl const url(session_.getRemoteUrl());

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

    if (network_error_)
    {
        tip = tr("Network Error");
    }
    else if (stats.peers_sending == 0 && stats.peers_receiving == 0)
    {
        tip = tr("Idle");
    }
    else if (stats.peers_sending != 0)
    {
        tip = Formatter::get().downloadSpeedToString(stats.speed_down) + QStringLiteral("   ") +
            Formatter::get().uploadSpeedToString(
            stats.speed_up);
    }
    else if (stats.peers_receiving != 0)
    {
        tip = Formatter::get().uploadSpeedToString(stats.speed_up);
    }

    tray_icon_.setToolTip(tip);
}

void MainWindow::refreshStatusBar(TransferStats const& stats)
{
    ui_.uploadSpeedLabel->setText(Formatter::get().uploadSpeedToString(stats.speed_up));
    ui_.uploadSpeedLabel->setVisible(stats.peers_sending || stats.peers_receiving);
    ui_.downloadSpeedLabel->setText(Formatter::get().downloadSpeedToString(stats.speed_down));
    ui_.downloadSpeedLabel->setVisible(stats.peers_sending);

    ui_.networkLabel->setVisible(!session_.isServer());

    auto const mode = prefs_.getString(Prefs::STATUSBAR_STATS);
    auto str = QString {};

    if (mode == session_ratio_stats_mode_name_)
    {
        str = tr("Ratio: %1")
            .arg(Formatter::get().ratioToString(session_.getStats().ratio));
    }
    else if (mode == session_transfer_stats_mode_name_)
    {
        auto const& st = session_.getStats();
        str = tr("Down: %1, Up: %2")
            .arg(Formatter::get().sizeToString(st.downloadedBytes))
            .arg(Formatter::get().sizeToString(st.uploadedBytes));
    }
    else if (mode == total_transfer_stats_mode_name_)
    {
        auto const& st = session_.getCumulativeStats();
        str = tr("Down: %1, Up: %2")
            .arg(Formatter::get().sizeToString(st.downloadedBytes))
            .arg(Formatter::get().sizeToString(st.uploadedBytes));
    }
    else // default is "total-ratio"
    {
        assert(mode == total_ratio_stats_mode_name_);
        str = tr("Ratio: %1")
            .arg(Formatter::get().ratioToString(session_.getCumulativeStats().ratio));
    }

    ui_.statsLabel->setText(str);
}

void MainWindow::refreshTorrentViewHeader()
{
    int const total_count = model_.rowCount();
    int const visible_count = filter_model_.rowCount();

    if (visible_count == total_count)
    {
        ui_.listView->setHeaderText(QString());
    }
    else
    {
        ui_.listView->setHeaderText(tr("Showing %L1 of %Ln torrent(s)", nullptr, total_count).arg(visible_count));
    }
}

void MainWindow::refreshActionSensitivity()
{
    auto const* model = ui_.listView->model();
    auto const* selection_model = ui_.listView->selectionModel();
    auto const row_count = model->rowCount();

    // count how many torrents are selected, paused, etc
    auto selected = int{};
    auto selected_and_can_announce = int{};
    auto selected_and_paused = int{};
    auto selected_and_queued = int{};
    auto selected_with_metadata = int{};
    auto const now = time(nullptr);
    for (auto const& row : selection_model->selectedRows())
    {
        auto const& tor = model->data(row, TorrentModel::TorrentRole).value<Torrent const*>();

        ++selected;

        if (tor->isPaused())
        {
            ++selected_and_paused;
        }

        if (tor->isQueued())
        {
            ++selected_and_queued;
        }

        if (tor->hasMetadata())
        {
            ++selected_with_metadata;
        }

        if (tor->canManualAnnounceAt(now))
        {
            ++selected_and_can_announce;
        }
    }

    auto const& torrents = model_.torrents();
    auto const is_paused = [](auto const* tor) { return tor->isPaused(); };
    auto const any_paused = std::any_of(std::begin(torrents), std::end(torrents), is_paused);
    auto const any_not_paused = std::any_of(std::begin(torrents), std::end(torrents), std::not_fn(is_paused));

    auto const have_selection = selected > 0;
    auto const have_selection_with_metadata = selected_with_metadata > 0;
    auto const one_selection = selected == 1;

    ui_.action_Verify->setEnabled(have_selection_with_metadata);
    ui_.action_Remove->setEnabled(have_selection);
    ui_.action_Delete->setEnabled(have_selection);
    ui_.action_Properties->setEnabled(have_selection);
    ui_.action_DeselectAll->setEnabled(have_selection);
    ui_.action_SetLocation->setEnabled(have_selection);

    ui_.action_OpenFolder->setEnabled(one_selection && have_selection_with_metadata && session_.isLocal());
    ui_.action_CopyMagnetToClipboard->setEnabled(one_selection);

    ui_.action_SelectAll->setEnabled(selected < row_count);
    ui_.action_StartAll->setEnabled(any_paused);
    ui_.action_PauseAll->setEnabled(any_not_paused);
    ui_.action_Start->setEnabled(selected_and_paused > 0);
    ui_.action_StartNow->setEnabled(selected_and_paused + selected_and_queued > 0);
    ui_.action_Pause->setEnabled(selected_and_paused < selected);
    ui_.action_Announce->setEnabled(selected > 0 && (selected_and_can_announce == selected));

    ui_.action_QueueMoveTop->setEnabled(have_selection);
    ui_.action_QueueMoveUp->setEnabled(have_selection);
    ui_.action_QueueMoveDown->setEnabled(have_selection);
    ui_.action_QueueMoveBottom->setEnabled(have_selection);

    if (!details_dialog_.isNull())
    {
        details_dialog_->setIds(getSelectedTorrents());
    }
}

/**
***
**/

// NOLINTNEXTLINE(readability-make-member-function-const)
void MainWindow::clearSelection()
{
    ui_.action_DeselectAll->trigger();
}

torrent_ids_t MainWindow::getSelectedTorrents(bool with_metadata_only) const
{
    torrent_ids_t ids;

    for (QModelIndex const& index : ui_.listView->selectionModel()->selectedRows())
    {
        auto const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());

        if (tor != nullptr && (!with_metadata_only || tor->hasMetadata()))
        {
            ids.insert(tor->id());
        }
    }

    return ids;
}

void MainWindow::startSelected()
{
    session_.startTorrents(getSelectedTorrents());
}

void MainWindow::startSelectedNow()
{
    session_.startTorrentsNow(getSelectedTorrents());
}

void MainWindow::pauseSelected()
{
    session_.pauseTorrents(getSelectedTorrents());
}

void MainWindow::queueMoveTop()
{
    session_.queueMoveTop(getSelectedTorrents());
}

void MainWindow::queueMoveUp()
{
    session_.queueMoveUp(getSelectedTorrents());
}

void MainWindow::queueMoveDown()
{
    session_.queueMoveDown(getSelectedTorrents());
}

void MainWindow::queueMoveBottom()
{
    session_.queueMoveBottom(getSelectedTorrents());
}

void MainWindow::startAll()
{
    session_.startTorrents();
}

void MainWindow::pauseAll()
{
    session_.pauseTorrents();
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
    session_.verifyTorrents(getSelectedTorrents(true));
}

void MainWindow::reannounceSelected()
{
    session_.reannounceTorrents(getSelectedTorrents());
}

/**
***
**/

void MainWindow::onStatsModeChanged(QAction* action)
{
    prefs_.set(Prefs::STATUSBAR_STATS, action->property(STATS_MODE_KEY).toString());
}

/**
***
**/

void MainWindow::setCompactView(bool visible)
{
    prefs_.set(Prefs::COMPACT_VIEW, visible);
}

void MainWindow::toggleSpeedMode()
{
    prefs_.toggleBool(Prefs::ALT_SPEED_LIMIT_ENABLED);
    bool const mode = prefs_.get<bool>(Prefs::ALT_SPEED_LIMIT_ENABLED);
    alt_speed_action_->setChecked(mode);
}

void MainWindow::setToolbarVisible(bool visible)
{
    prefs_.set(Prefs::TOOLBAR, visible);
}

void MainWindow::setFilterbarVisible(bool visible)
{
    prefs_.set(Prefs::FILTERBAR, visible);
}

void MainWindow::setStatusbarVisible(bool visible)
{
    prefs_.set(Prefs::STATUSBAR, visible);
}

/**
***
**/

void MainWindow::toggleWindows(bool do_show)
{
    if (!do_show)
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
    QActionGroup* action_group;

    switch (key)
    {
    case Prefs::STATUSBAR_STATS:
        str = prefs_.getString(key);
        action_group = ui_.action_TotalRatio->actionGroup();
        assert(action_group != nullptr);

        for (QAction* action : action_group->actions())
        {
            action->setChecked(str == action->property(STATS_MODE_KEY).toString());
        }

        refreshSoon(REFRESH_STATUS_BAR);
        break;

    case Prefs::SORT_REVERSED:
        ui_.action_ReverseSortOrder->setChecked(prefs_.getBool(key));
        break;

    case Prefs::SORT_MODE:
        i = prefs_.get<SortMode>(key).mode();
        action_group = ui_.action_SortByActivity->actionGroup();
        assert(action_group != nullptr);

        for (QAction* action : action_group->actions())
        {
            action->setChecked(i == action->property(SORT_MODE_KEY).toInt());
        }

        break;

    case Prefs::DSPEED_ENABLED:
        (prefs_.get<bool>(key) ? dlimit_on_action_ : dlimit_off_action_)->setChecked(true);
        break;

    case Prefs::DSPEED:
        dlimit_on_action_->setText(tr("Limited at %1").arg(Formatter::get().speedToString(Speed::fromKBps(prefs_.get<int>(
            key)))));
        break;

    case Prefs::USPEED_ENABLED:
        (prefs_.get<bool>(key) ? ulimit_on_action_ : ulimit_off_action_)->setChecked(true);
        break;

    case Prefs::USPEED:
        ulimit_on_action_->setText(tr("Limited at %1").arg(Formatter::get().speedToString(Speed::fromKBps(prefs_.get<int>(
            key)))));
        break;

    case Prefs::RATIO_ENABLED:
        (prefs_.get<bool>(key) ? ratio_on_action_ : ratio_off_action_)->setChecked(true);
        break;

    case Prefs::RATIO:
        ratio_on_action_->setText(tr("Stop at Ratio (%1)").arg(Formatter::get().ratioToString(prefs_.get<double>(key))));
        break;

    case Prefs::FILTERBAR:
        b = prefs_.getBool(key);
        filter_bar_->setVisible(b);
        ui_.action_Filterbar->setChecked(b);
        break;

    case Prefs::STATUSBAR:
        b = prefs_.getBool(key);
        ui_.statusBar->setVisible(b);
        ui_.action_Statusbar->setChecked(b);
        break;

    case Prefs::TOOLBAR:
        b = prefs_.getBool(key);
        ui_.toolBar->setVisible(b);
        ui_.action_Toolbar->setChecked(b);
        break;

    case Prefs::SHOW_TRAY_ICON:
        b = prefs_.getBool(key);
        ui_.action_TrayIcon->setChecked(b);
        tray_icon_.setVisible(b);
        qApp->setQuitOnLastWindowClosed(!b);
        refreshSoon(REFRESH_TRAY_ICON);
        break;

    case Prefs::COMPACT_VIEW:
        {
#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537

            QItemSelectionModel* selection_model(ui_.listView->selectionModel());
            QItemSelection const selection(selection_model->selection());
            QModelIndex const current_index(selection_model->currentIndex());

#endif

            b = prefs_.getBool(key);
            ui_.action_CompactView->setChecked(b);
            ui_.listView->setItemDelegate(b ? torrent_delegate_min_ : torrent_delegate_);

#if QT_VERSION < QT_VERSION_CHECK(5, 4, 0) // QTBUG-33537

            selectionModel->clear();
            ui_.listView->reset(); // force the rows to resize
            selectionModel->select(selection, QItemSelectionModel::Select);
            selectionModel->setCurrentIndex(current_index, QItemSelectionModel::NoUpdate);

#endif

            break;
        }

    case Prefs::MAIN_WINDOW_X:
    case Prefs::MAIN_WINDOW_Y:
    case Prefs::MAIN_WINDOW_WIDTH:
    case Prefs::MAIN_WINDOW_HEIGHT:
        setGeometry(prefs_.getInt(Prefs::MAIN_WINDOW_X), prefs_.getInt(Prefs::MAIN_WINDOW_Y),
            prefs_.getInt(Prefs::MAIN_WINDOW_WIDTH), prefs_.getInt(Prefs::MAIN_WINDOW_HEIGHT));
        break;

    case Prefs::ALT_SPEED_LIMIT_ENABLED:
    case Prefs::ALT_SPEED_LIMIT_UP:
    case Prefs::ALT_SPEED_LIMIT_DOWN:
        {
            b = prefs_.getBool(Prefs::ALT_SPEED_LIMIT_ENABLED);
            alt_speed_action_->setChecked(b);
            ui_.altSpeedButton->setChecked(b);
            QString const fmt = b ? tr("Click to disable Temporary Speed Limits\n (%1 down, %2 up)") :
                tr("Click to enable Temporary Speed Limits\n (%1 down, %2 up)");
            Speed const d = Speed::fromKBps(prefs_.getInt(Prefs::ALT_SPEED_LIMIT_DOWN));
            Speed const u = Speed::fromKBps(prefs_.getInt(Prefs::ALT_SPEED_LIMIT_UP));
            ui_.altSpeedButton->setToolTip(fmt.arg(Formatter::get().speedToString(d)).arg(Formatter::get().speedToString(u)));
            break;
        }

    default:
        break;
    }
}

/***
****
***/

void MainWindow::newTorrent()
{
    auto* dialog = new MakeDialog(session_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openTorrent()
{
    QFileDialog* d;
    d = new QFileDialog(this, tr("Open Torrent"), prefs_.getString(Prefs::OPEN_DIALOG_FOLDER),
        tr("Torrent Files (*.torrent);;All Files (*.*)"));
    d->setFileMode(QFileDialog::ExistingFiles);
    d->setAttribute(Qt::WA_DeleteOnClose);

    auto* const l = qobject_cast<QGridLayout*>(d->layout());

    if (l != nullptr)
    {
        auto* b = new QCheckBox(tr("Show &options dialog"));
        b->setChecked(prefs_.getBool(Prefs::OPTIONS_PROMPT));
        b->setObjectName(show_options_checkbox_name_);
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
    bool show_options = prefs_.getBool(Prefs::OPTIONS_PROMPT);

    auto const* const file_dialog = qobject_cast<QFileDialog const*>(sender());

    if (file_dialog != nullptr)
    {
        auto const* const b = file_dialog->findChild<QCheckBox const*>(show_options_checkbox_name_);

        if (b != nullptr)
        {
            show_options = b->isChecked();
        }
    }

    for (QString const& filename : filenames)
    {
        addTorrent(filename, show_options);
    }
}

void MainWindow::addTorrent(AddData const& addMe, bool show_options)
{
    if (show_options)
    {
        auto* o = new OptionsDialog(session_, prefs_, addMe, this);
        o->show();
        qApp->alert(o);
    }
    else
    {
        session_.addTorrent(addMe);
        qApp->alert(this);
    }
}

void MainWindow::removeTorrents(bool const delete_files)
{
    torrent_ids_t ids;
    QMessageBox msg_box(this);
    QString primary_text;
    QString secondary_text;
    int incomplete = 0;
    int connected = 0;
    int count;

    for (QModelIndex const& index : ui_.listView->selectionModel()->selectedRows())
    {
        auto const* tor(index.data(TorrentModel::TorrentRole).value<Torrent const*>());
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

    if (!delete_files)
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

    msg_box.setWindowTitle(QStringLiteral(" "));
    msg_box.setText(QStringLiteral("<big><b>%1</big></b>").arg(primary_text));
    msg_box.setInformativeText(secondary_text);
    msg_box.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msg_box.setDefaultButton(QMessageBox::Cancel);
    msg_box.setIcon(QMessageBox::Question);
    // hack needed to keep the dialog from being too narrow
    auto* layout = qobject_cast<QGridLayout*>(msg_box.layout());

    if (layout == nullptr)
    {
        layout = new QGridLayout;
        msg_box.setLayout(layout);
    }

    auto* spacer = new QSpacerItem(450, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
    layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());

    if (msg_box.exec() == QMessageBox::Ok)
    {
        ui_.listView->selectionModel()->clear();
        session_.removeTorrents(ids, delete_files);
    }
}

/***
****
***/

void MainWindow::updateNetworkIcon()
{
    static constexpr int const Period = 3;
    time_t const now = time(nullptr);
    time_t const seconds_since_last_send = now - last_send_time_;
    time_t const seconds_since_last_read = now - last_read_time_;
    bool const is_sending = seconds_since_last_send <= Period;
    bool const is_reading = seconds_since_last_read <= Period;
    QPixmap pixmap;

    if (network_error_)
    {
        pixmap = pixmap_network_error_;
    }
    else if (is_sending && is_reading)
    {
        pixmap = pixmap_network_transmit_receive_;
    }
    else if (is_sending)
    {
        pixmap = pixmap_network_transmit_;
    }
    else if (is_reading)
    {
        pixmap = pixmap_network_receive_;
    }
    else
    {
        pixmap = pixmap_network_idle_;
    }

    QString tip;
    QString const url = session_.getRemoteUrl().host();

    if (last_read_time_ == 0)
    {
        tip = tr("%1 has not responded yet").arg(url);
    }
    else if (network_error_)
    {
        tip = tr(error_message_.toLatin1().constData());
    }
    else if (seconds_since_last_read < 30)
    {
        tip = tr("%1 is responding").arg(url);
    }
    else if (seconds_since_last_read < 60 * 2)
    {
        tip = tr("%1 last responded %2 ago").arg(url).arg(Formatter::get().timeToString(seconds_since_last_read));
    }
    else
    {
        tip = tr("%1 is not responding").arg(url);
    }

    ui_.networkLabel->setPixmap(pixmap);
    ui_.networkLabel->setToolTip(tip);
}

void MainWindow::onNetworkTimer()
{
    updateNetworkIcon();
}

void MainWindow::dataReadProgress()
{
    if (!network_error_)
    {
        last_read_time_ = time(nullptr);
    }
}

void MainWindow::dataSendProgress()
{
    last_send_time_ = time(nullptr);
}

void MainWindow::onNetworkResponse(QNetworkReply::NetworkError code, QString const& message)
{
    bool const had_error = network_error_;
    bool const have_error = code != QNetworkReply::NoError && code != QNetworkReply::UnknownContentError;

    network_error_ = have_error;
    error_message_ = message;
    refreshSoon(REFRESH_TRAY_ICON);
    updateNetworkIcon();

    // Refresh our model if we've just gotten a clean connection to the session.
    // That way we can rebuild after a restart of transmission-daemon
    if (had_error && !have_error)
    {
        model_.clear();
    }
}

void MainWindow::wrongAuthentication()
{
    session_.stop();
    openSession();
}

/***
****
***/

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    QMimeData const* mime = event->mimeData();

    if (mime->hasFormat(QStringLiteral("application/x-bittorrent")) || mime->hasUrls() ||
        mime->text().trimmed().endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) ||
        mime->text().startsWith(QStringLiteral("magnet:"), Qt::CaseInsensitive))
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
    ui_.menuTorrent->popup(event->globalPos());
}
