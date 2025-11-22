// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cassert>
#include <memory>
#include <string_view>
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
#include "NativeIcon.h"
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

using namespace std::literals;

namespace
{

char const* const PrefVariantsKey = "submenu";
char const* const StatsModeKey = "stats-mode";
char const* const SortModeKey = "sort-mode";

namespace icons
{

using MenuMode = std::bitset<4U>;
constexpr auto Standard = MenuMode{ 1 << 0U };
constexpr auto Noun = MenuMode{ 1 << 1U };
constexpr auto Verb = MenuMode{ 1 << 2U };
constexpr auto Other = MenuMode{ 1 << 3U };

[[nodiscard]] MenuMode get_menu_mode()
{
    static std::optional<MenuMode> value;

    if (!value)
    {
        auto const override = qgetenv("TR_ICON_MODE").toLower();
        if (override.contains("all"))
            value = Noun | Standard | Verb | Other;
        if (override.contains("noun"))
            value = Noun;
        if (override.contains("standard"))
            value = Standard;
        if (override.contains("verb"))
            value = Verb;
    }

#if defined(Q_OS_WIN)

    if (!value)
    {
        // https://learn.microsoft.com/en-us/windows/apps/design/controls/menus
        // Consider providing menu item icons for:
        // The most commonly used items.
        // Menu items whose icon is standard or well known.
        // Menu items whose icon well illustrates what the command does.
        // Don't feel obligated to provide icons for commands that don't have
        // a standard visualization. Cryptic icons aren't helpful, create visual
        // clutter, and prevent users from focusing on the important menu items.
        value = Standard;
    }

#elif defined(Q_OS_MAC)

    if (!value)
    {
        // https://developer.apple.com/design/human-interface-guidelines/menus
        // Represent menu item actions with familiar icons. Icons help people
        // recognize common actions throughout your app. Use the same icons as
        // the system to represent actions such as Copy, Share, and Delete,
        // wherever they appear. For a list of icons that represent common
        // actions, see Standard icons.
        // Don’t display an icon if you can’t find one that clearly represents
        // the menu item. Not all menu items need an icon. Be careful when adding
        // icons for custom menu items to avoid confusion with other existing
        // actions, and don’t add icons just for the sake of ornamentation.
        value = Standard;

        // Note: Qt 6.7.3 turned off menu icons by default on macOS.
        // https://github.com/qt/qtbase/commit/d671e1af3b736ee7d866323246fc2190fc5e076a
        // This seems too restrictive based on the Apple HIG guidance at
        // https://developer.apple.com/design/human-interface-guidelines/menus
        // Based on the HIG text above, this is probably too restrictive?
    }

#else

    if (!value)
    {
        auto const desktop = qgetenv("XDG_CURRENT_DESKTOP");

        // https://discourse.gnome.org/t/principle-of-icons-in-menus/4803
        // We do not “block” icons in menus; icons are generally reserved
        // for “nouns”, or “objects”—for instance: website favicons in
        // bookmark menus, or file-type icons—instead of having them for
        // “verbs”, or “actions”—for instance: save, copy, print, etc.
        if (desktop.contains("GNOME"))
        {
            value = Noun;
        }

        // https://develop.kde.org/hig/icons/#icons-for-menu-items-and-buttons-with-text
        // Set an icon on every button and menu item, making sure not to
        // use the same icon for multiple visible buttons or menu items.
        // Choose different icons, or use more specific ones to disambiguate.
        else if (desktop.contains("KDE"))
        {
            value = Standard | Noun | Verb | Other;
        }

        // Unknown DE -- not GNOME or KDE, so probably no HIG.
        // Use best guess.
        else
        {
            value = Standard | Noun;
        }
    }

#endif

    return *value;
}

[[nodiscard]] bool visible(MenuMode const type)
{
    return (get_menu_mode() & type).any();
}

[[nodiscard]] auto addEmblem(QIcon base_icon, QIcon emblem_icon, Qt::LayoutDirection layout_direction)
{
    auto icon = QIcon{};

    for (QSize const& size : base_icon.availableSizes())
    {
        auto const emblem_size = size / 2;
        auto const emblem_rect = QStyle::alignedRect(
            layout_direction,
            Qt::AlignBottom | Qt::AlignRight,
            emblem_size,
            QRect{ QPoint{ 0, 0 }, size });

        auto pixmap = base_icon.pixmap(size);
        auto const emblem_pixmap = emblem_icon.pixmap(emblem_size);
        QPainter{ &pixmap }.drawPixmap(emblem_rect, emblem_pixmap, emblem_pixmap.rect());

        icon.addPixmap(pixmap);
    }

    return icon;
}

} // namespace icons
} // namespace

/**
 * This is a proxy-style for that forces it to be always disabled.
 * We use this to make our torrent list view behave consistently on
 * both GTK and Qt implementations.
 */
class ListViewProxyStyle : public QProxyStyle
{
public:
    int styleHint(
        StyleHint hint,
        QStyleOption const* option = nullptr,
        QWidget const* widget = nullptr,
        QStyleHintReturn* return_data = nullptr) const override
    {
        if (hint == QStyle::SH_ItemView_ActivateItemOnSingleClick)
        {
            return 0;
        }

        return QProxyStyle::styleHint(hint, option, widget, return_data);
    }
};

MainWindow::MainWindow(Session& session, Prefs& prefs, TorrentModel& model, bool minimized)
    : session_{ session }
    , prefs_{ prefs }
    , model_{ model }
    , lvp_style_{ std::make_shared<ListViewProxyStyle>() }
    , filter_model_{ prefs }
    , torrent_delegate_{ new TorrentDelegate{ this } }
    , torrent_delegate_min_{ new TorrentDelegateMin{ this } }
    , network_timer_{ this }
    , refresh_timer_{ this }
{
    setAcceptDrops(true);

    auto* sep = new QAction{ this };
    sep->setSeparator(true);

    ui_.setupUi(this);

    ui_.listView->setStyle(lvp_style_.get());
    ui_.listView->setAttribute(Qt::WA_MacShowFocusRect, false);

    // icons

    auto set = [](QAction* action, QIcon icon, icons::MenuMode const type)
    {
        action->setIcon(icon);
        action->setIconVisibleInMenu(icons::visible(type));
    };

    auto* action = ui_.action_OpenFile;
    auto type = icons::Standard | icons::Verb;
    auto icon = NativeIcon::get("folder.open"sv, segoe::FolderOpen, "folder"sv, QStyle::SP_DirOpenIcon);
    set(action, icon, type);

    action = ui_.action_AddURL;
    type = icons::Verb;
    icon = icons::addEmblem(icon, NativeIcon::get("globe"sv, segoe::Globe, "emblem-symbolic-link"sv), layoutDirection());
    set(action, icon, type);

    action = ui_.action_New;
    type = icons::Standard | icons::Verb;
    icon = NativeIcon::get("plus"sv, segoe::Add, "document-new", QStyle::SP_FileIcon);
    set(action, icon, type);

    action = ui_.action_Properties;
    type = icons::Standard | icons::Noun;
    icon = NativeIcon::get("doc.text.magnifyingglass"sv, segoe::Info, "document-properties"sv, QStyle::SP_FileIcon);
    set(action, icon, type);

    action = ui_.action_OpenFolder;
    type = icons::Standard | icons::Verb;
    icon = NativeIcon::get("folder"sv, segoe::OpenFolder, "folder-open"sv, QStyle::SP_DirOpenIcon);
    set(action, icon, type);

    action = ui_.action_Start;
    type = icons::Standard | icons::Verb;
    icon = NativeIcon::get("play.fill"sv, segoe::Play, "media-playback-start"sv, QStyle::SP_MediaPlay);
    set(action, icon, type);

    action = ui_.action_Pause;
    type = icons::Standard | icons::Verb;
    icon = NativeIcon::get("pause.fill"sv, segoe::Pause, "media-playback-pause"sv, QStyle::SP_MediaPause);
    set(action, icon, type);

    action = ui_.action_Remove;
    type = icons::Verb;
    icon = NativeIcon::get("minus"sv, segoe::Remove, "list-remove"sv, QStyle::SP_DialogCancelButton);
    set(action, icon, type);

    action = ui_.action_Delete;
    type = icons::Verb;
    icon = NativeIcon::get("trash"sv, segoe::Delete, "edit-delete"sv, QStyle::SP_TrashIcon);
    set(action, icon, type);

    action = ui_.action_SetLocation;
    type = icons::Verb;
    icon = NativeIcon::get("doc.on.clipboard"sv, segoe::Copy, "edit-copy"sv);
    set(action, icon, type);

    action = ui_.action_Quit;
    icon = NativeIcon::get("power"sv, segoe::PowerButton, "application-exit"sv);
    type = icons::Standard | icons::Verb;
    set(action, icon, type);

    action = ui_.action_SelectAll;
    type = icons::Verb;
    icon = NativeIcon::get("checkmark.square"sv, segoe::SelectAll, "edit-select-all"sv);
    set(action, icon, type);

    action = ui_.action_DeselectAll;
    type = icons::Verb;
    icon = NativeIcon::get("checkmark"sv, segoe::Checkbox, "edit-select-none"sv);
    set(action, icon, type);

    action = ui_.action_Preferences;
    icon = NativeIcon::get("gearshape"sv, segoe::Settings, "preferences-system"sv);
    type = icons::Standard | icons::Verb;
    set(action, icon, type);

    action = ui_.action_Statistics;
    type = icons::Verb;
    icon = NativeIcon::get("chart.bar"sv, segoe::ReportDocument, "info"sv);
    set(action, icon, type);

    action = ui_.action_Donate;
    type = icons::Verb;
    icon = NativeIcon::get("heart"sv, segoe::Heart, "donate"sv);
    set(action, icon, type);

    action = ui_.action_About;
    type = icons::Standard;
    icon = NativeIcon::get("info.circle"sv, segoe::Info, "help-about"sv, QStyle::SP_MessageBoxInformation);
    set(action, icon, type);

    action = ui_.action_CopyMagnetToClipboard;
    type = icons::Verb;
    icon = NativeIcon::get("doc.on.clipboard"sv, segoe::Copy, "edit-copy"sv);
    set(action, icon, type);

    action = ui_.action_Verify;
    type = icons::Verb;
    icon = NativeIcon::get("arrow.clockwise"sv, segoe::Refresh, "view-refresh"sv, QStyle::SP_BrowserReload);
    set(action, icon, type);

    action = ui_.action_Contents;
    type = icons::Standard;
    icon = NativeIcon::get("questionmark.circle"sv, segoe::Help, "help-faq"sv, QStyle::SP_DialogHelpButton);
    set(action, icon, type);

    action = ui_.action_QueueMoveTop;
    type = icons::Verb;
    icon = NativeIcon::get("arrow.up.to.line"sv, segoe::CaretUpSolid8, "go-top"sv);
    set(action, icon, type);

    action = ui_.action_QueueMoveUp;
    type = icons::Verb;
    icon = NativeIcon::get("arrow.up"sv, segoe::CaretUp8, "go-up"sv);
    set(action, icon, type);

    action = ui_.action_QueueMoveDown;
    type = icons::Verb;
    icon = NativeIcon::get("arrow.down"sv, segoe::CaretDown8, "go-down"sv);
    set(action, icon, type);

    action = ui_.action_QueueMoveBottom;
    type = icons::Verb;
    icon = NativeIcon::get("arrow.down.to.line"sv, segoe::CaretDownSolid8, "go-bottom"sv);
    set(action, icon, type);

    // network icons

    auto constexpr NetworkIconSize = QSize{ 16, 16 };
    icon = NativeIcon::get(""sv, QChar{}, "network-idle"sv);
    pixmap_network_idle_ = icon.pixmap(NetworkIconSize);

    icon = NativeIcon::get("wifi.exclamationmark"sv, segoe::Error, "network-error"sv, QStyle::SP_MessageBoxCritical);
    pixmap_network_error_ = icon.pixmap(NetworkIconSize);

    icon = NativeIcon::get("arrow.down.circle"sv, segoe::Download, "network-receive"sv);
    pixmap_network_receive_ = icon.pixmap(NetworkIconSize);

    icon = NativeIcon::get("arrow.up.circle"sv, segoe::Upload, "network-transmit"sv);
    pixmap_network_transmit_ = icon.pixmap(NetworkIconSize);

    icon = NativeIcon::get("arrow.up.arrow.down.circle"sv, segoe::UploadDownload, "network-transmit-receive"sv);
    pixmap_network_transmit_receive_ = icon.pixmap(NetworkIconSize);

    // ui signals
    connect(ui_.action_Toolbar, &QAction::toggled, this, &MainWindow::setToolbarVisible);
    connect(ui_.action_Filterbar, &QAction::toggled, this, &MainWindow::setFilterbarVisible);
    connect(ui_.action_Statusbar, &QAction::toggled, this, &MainWindow::setStatusbarVisible);
    connect(ui_.action_CompactView, &QAction::toggled, this, &MainWindow::setCompactView);
    connect(ui_.action_ReverseSortOrder, &QAction::toggled, this, &MainWindow::setSortAscendingPref);
    connect(ui_.action_Start, &QAction::triggered, this, &MainWindow::startSelected);
    connect(ui_.action_QueueMoveTop, &QAction::triggered, this, &MainWindow::queueMoveTop);
    connect(ui_.action_QueueMoveUp, &QAction::triggered, this, &MainWindow::queueMoveUp);
    connect(ui_.action_QueueMoveDown, &QAction::triggered, this, &MainWindow::queueMoveDown);
    connect(ui_.action_QueueMoveBottom, &QAction::triggered, this, &MainWindow::queueMoveBottom);
    connect(ui_.action_StartNow, &QAction::triggered, this, &MainWindow::startSelectedNow);
    connect(ui_.action_Pause, &QAction::triggered, this, &MainWindow::pauseSelected);
    connect(ui_.action_Remove, &QAction::triggered, this, &MainWindow::removeSelected);
    connect(ui_.action_Delete, &QAction::triggered, this, &MainWindow::deleteSelected);
    connect(ui_.action_Verify, &QAction::triggered, this, &MainWindow::verifySelected);
    connect(ui_.action_Announce, &QAction::triggered, this, &MainWindow::reannounceSelected);
    connect(ui_.action_StartAll, &QAction::triggered, this, &MainWindow::startAll);
    connect(ui_.action_PauseAll, &QAction::triggered, this, &MainWindow::pauseAll);
    connect(ui_.action_OpenFile, &QAction::triggered, this, &MainWindow::openTorrent);
    connect(ui_.action_AddURL, &QAction::triggered, this, &MainWindow::openURL);
    connect(ui_.action_New, &QAction::triggered, this, &MainWindow::newTorrent);
    connect(ui_.action_Preferences, &QAction::triggered, this, &MainWindow::openPreferences);
    connect(ui_.action_Statistics, &QAction::triggered, this, &MainWindow::openStats);
    connect(ui_.action_Donate, &QAction::triggered, this, &MainWindow::openDonate);
    connect(ui_.action_About, &QAction::triggered, this, &MainWindow::openAbout);
    connect(ui_.action_Contents, &QAction::triggered, this, &MainWindow::openHelp);
    connect(ui_.action_OpenFolder, &QAction::triggered, this, &MainWindow::openFolder);
    connect(ui_.action_CopyMagnetToClipboard, &QAction::triggered, this, &MainWindow::copyMagnetLinkToClipboard);
    connect(ui_.action_SetLocation, &QAction::triggered, this, &MainWindow::setLocation);
    connect(ui_.action_Properties, &QAction::triggered, this, &MainWindow::openProperties);
    connect(ui_.action_SessionDialog, &QAction::triggered, this, &MainWindow::openSession);
    connect(ui_.listView, &QAbstractItemView::activated, ui_.action_Properties, &QAction::trigger);
    connect(ui_.action_SelectAll, &QAction::triggered, ui_.listView, &QAbstractItemView::selectAll);
    connect(ui_.action_DeselectAll, &QAction::triggered, ui_.listView, &QAbstractItemView::clearSelection);
    connect(ui_.action_Quit, &QAction::triggered, qApp, &QCoreApplication::quit);

    auto refresh_action_sensitivity_soon = [this]()
    {
        refreshSoon(REFRESH_ACTION_SENSITIVITY);
    };
    connect(&filter_model_, &TorrentFilter::rowsInserted, this, refresh_action_sensitivity_soon);
    connect(&filter_model_, &TorrentFilter::rowsRemoved, this, refresh_action_sensitivity_soon);
    connect(&model_, &TorrentModel::torrentsChanged, this, refresh_action_sensitivity_soon);

    // torrent view
    filter_model_.setSourceModel(&model_);
    auto refresh_soon_adapter = [this]()
    {
        refreshSoon();
    };
    connect(&model_, &TorrentModel::modelReset, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::rowsRemoved, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::rowsInserted, this, refresh_soon_adapter);
    connect(&model_, &TorrentModel::torrentsChanged, this, refresh_soon_adapter);

    ui_.listView->setModel(&filter_model_);
    connect(ui_.listView->selectionModel(), &QItemSelectionModel::selectionChanged, refresh_action_sensitivity_soon);

    std::array<std::pair<QAction*, int>, 9> const sort_modes = { {
        { ui_.action_SortByActivity, SortMode::SORT_BY_ACTIVITY },
        { ui_.action_SortByAge, SortMode::SORT_BY_AGE },
        { ui_.action_SortByETA, SortMode::SORT_BY_ETA },
        { ui_.action_SortByName, SortMode::SORT_BY_NAME },
        { ui_.action_SortByProgress, SortMode::SORT_BY_PROGRESS },
        { ui_.action_SortByQueue, SortMode::SORT_BY_QUEUE },
        { ui_.action_SortByRatio, SortMode::SORT_BY_RATIO },
        { ui_.action_SortBySize, SortMode::SORT_BY_SIZE },
        { ui_.action_SortByState, SortMode::SORT_BY_STATE },
    } };

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    auto* action_group = new QActionGroup{ this };

    for (auto const& [qaction, mode] : sort_modes)
    {
        qaction->setProperty(SortModeKey, mode);
        action_group->addAction(qaction);
    }

    connect(action_group, &QActionGroup::triggered, this, &MainWindow::onSortModeChanged);

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    alt_speed_action_ = new QAction{ tr("Speed Limits"), this };
    alt_speed_action_->setIcon(ui_.altSpeedButton->icon());
    alt_speed_action_->setCheckable(true);
    connect(alt_speed_action_, &QAction::triggered, this, &MainWindow::toggleSpeedMode);

    auto* menu = new QMenu{ this };
    menu->addAction(ui_.action_OpenFile);
    menu->addAction(ui_.action_AddURL);
    menu->addSeparator();
    menu->addAction(ui_.action_ShowMainWindow);
    menu->addAction(ui_.action_About);
    menu->addSeparator();
    menu->addAction(ui_.action_StartAll);
    menu->addAction(ui_.action_PauseAll);
    menu->addAction(alt_speed_action_);
    menu->addSeparator();
    menu->addAction(ui_.action_Quit);
    tray_icon_.setContextMenu(menu);
    tray_icon_.setIcon(QIcon::fromTheme(QStringLiteral("transmission-tray-icon"), QApplication::windowIcon()));

    connect(&prefs_, &Prefs::changed, this, &MainWindow::refreshPref);
    connect(ui_.action_ShowMainWindow, &QAction::triggered, this, &MainWindow::toggleWindows);
    connect(&tray_icon_, &QSystemTrayIcon::activated, this, &MainWindow::trayActivated);

    toggleWindows(!minimized);
    ui_.action_TrayIcon->setChecked(minimized || prefs.getBool(Prefs::SHOW_TRAY_ICON));

    initStatusBar();
    auto* filter_bar = new FilterBar{ prefs_, model_, filter_model_ };
    ui_.verticalLayout->insertWidget(0, filter_bar);
    filter_bar_ = filter_bar;

    auto refresh_header_soon = [this]()
    {
        refreshSoon(REFRESH_TORRENT_VIEW_HEADER);
    };
    connect(&model_, &TorrentModel::rowsInserted, this, refresh_header_soon);
    connect(&model_, &TorrentModel::rowsRemoved, this, refresh_header_soon);
    connect(&filter_model_, &TorrentFilter::rowsInserted, this, refresh_header_soon);
    connect(&filter_model_, &TorrentFilter::rowsRemoved, this, refresh_header_soon);
    connect(ui_.listView, &TorrentView::headerDoubleClicked, filter_bar, &FilterBar::clear);

    static std::array<int, 17> constexpr InitKeys = {
        Prefs::ALT_SPEED_LIMIT_ENABLED, //
        Prefs::COMPACT_VIEW, //
        Prefs::DSPEED, //
        Prefs::DSPEED_ENABLED, //
        Prefs::FILTERBAR, //
        Prefs::MAIN_WINDOW_X, //
        Prefs::RATIO, //
        Prefs::RATIO_ENABLED, //
        Prefs::READ_CLIPBOARD, //
        Prefs::SHOW_TRAY_ICON, //
        Prefs::SORT_MODE, //
        Prefs::SORT_REVERSED, //
        Prefs::STATUSBAR, //
        Prefs::STATUSBAR_STATS, //
        Prefs::TOOLBAR, //
        Prefs::USPEED, //
        Prefs::USPEED_ENABLED, //
    };
    for (auto const key : InitKeys)
    {
        refreshPref(key);
    }

    auto refresh_status_soon = [this]()
    {
        refreshSoon(REFRESH_STATUS_BAR);
    };
    connect(&session_, &Session::sourceChanged, this, &MainWindow::onSessionSourceChanged);
    connect(&session_, &Session::statsUpdated, this, refresh_status_soon);
    connect(&session_, &Session::dataReadProgress, this, &MainWindow::dataReadProgress);
    connect(&session_, &Session::dataSendProgress, this, &MainWindow::dataSendProgress);
    connect(&session_, &Session::httpAuthenticationRequired, this, &MainWindow::wrongAuthentication);
    connect(&session_, &Session::networkResponse, this, &MainWindow::onNetworkResponse);

    connect(&network_timer_, &QTimer::timeout, this, &MainWindow::updateNetworkLabel);
    connect(&refresh_timer_, &QTimer::timeout, this, &MainWindow::onRefreshTimer);

    onSessionSourceChanged();
    refreshSoon();
}

void MainWindow::onSessionSourceChanged()
{
    model_.clear();

    if (session_.isServer())
    {
        updateNetworkLabel();
        ui_.networkLabel->show();
        network_timer_.start(1000);
    }
    else
    {
        ui_.networkLabel->hide();
        network_timer_.stop();
    }
}

/****
*****
****/

void MainWindow::onSetPrefs()
{
    QVariantList const p = sender()->property(PrefVariantsKey).toList();
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

    int const minimum_speed_width = ui_.downloadSpeedLabel->fontMetrics()
                                        .size(0, Speed{ 999.99, Speed::Units::KByps }.to_qstring())
                                        .width();
    ui_.downloadSpeedLabel->setMinimumWidth(minimum_speed_width);
    ui_.uploadSpeedLabel->setMinimumWidth(minimum_speed_width);

    ui_.statsModeButton->setMenu(createStatsModeMenu());

    connect(ui_.altSpeedButton, &QAbstractButton::clicked, this, &MainWindow::toggleSpeedMode);
}

QMenu* MainWindow::createOptionsMenu()
{
    auto const init_speed_sub_menu = [this](QMenu* menu, QAction*& off_action, QAction*& on_action, int pref, int enabled_pref)
    {
        int const current_value = prefs_.get<int>(pref);

        // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
        auto* action_group = new QActionGroup{ this };

        off_action = menu->addAction(tr("Unlimited"));
        off_action->setCheckable(true);
        off_action->setProperty(PrefVariantsKey, QVariantList{ enabled_pref, false });
        action_group->addAction(off_action);
        connect(off_action, &QAction::triggered, this, qOverload<bool>(&MainWindow::onSetPrefs));

        on_action = menu->addAction(tr("Limited at %1").arg(Speed{ current_value, Speed::Units::KByps }.to_qstring()));
        on_action->setCheckable(true);
        on_action->setProperty(PrefVariantsKey, QVariantList{ pref, current_value, enabled_pref, true });
        action_group->addAction(on_action);
        connect(on_action, &QAction::triggered, this, qOverload<bool>(&MainWindow::onSetPrefs));

        menu->addSeparator();

        for (auto const kbyps : { 50, 100, 250, 500, 1000, 2500, 5000, 10000 })
        {
            auto* const action = menu->addAction(Speed{ kbyps, Speed::Units::KByps }.to_qstring());
            action->setProperty(PrefVariantsKey, QVariantList{ pref, kbyps, enabled_pref, true });
            connect(action, &QAction::triggered, this, qOverload<>(&MainWindow::onSetPrefs));
        }
    };

    auto const init_seed_ratio_sub_menu =
        [this](QMenu* menu, QAction*& off_action, QAction*& on_action, int pref, int enabled_pref)
    {
        static constexpr std::array<double, 7> StockRatios = { 0.25, 0.50, 0.75, 1, 1.5, 2, 3 };
        auto const current_value = prefs_.get<double>(pref);

        auto* action_group = new QActionGroup{ this };

        off_action = menu->addAction(tr("Seed Forever"));
        off_action->setCheckable(true);
        off_action->setProperty(PrefVariantsKey, QVariantList{ enabled_pref, false });
        action_group->addAction(off_action);
        connect(off_action, &QAction::triggered, this, qOverload<bool>(&MainWindow::onSetPrefs));

        on_action = menu->addAction(tr("Stop at Ratio (%1)").arg(Formatter::ratio_to_string(current_value)));
        on_action->setCheckable(true);
        on_action->setProperty(PrefVariantsKey, QVariantList{ pref, current_value, enabled_pref, true });
        action_group->addAction(on_action);
        connect(on_action, &QAction::triggered, this, qOverload<bool>(&MainWindow::onSetPrefs));

        menu->addSeparator();

        for (double const i : StockRatios)
        {
            QAction* action = menu->addAction(Formatter::ratio_to_string(i));
            action->setProperty(PrefVariantsKey, QVariantList{ pref, i, enabled_pref, true });
            connect(action, &QAction::triggered, this, qOverload<>(&MainWindow::onSetPrefs));
        }
    };

    auto* menu = new QMenu{ this };

    init_speed_sub_menu(
        menu->addMenu(tr("Limit Download Speed")),
        dlimit_off_action_,
        dlimit_on_action_,
        Prefs::DSPEED,
        Prefs::DSPEED_ENABLED);
    init_speed_sub_menu(
        menu->addMenu(tr("Limit Upload Speed")),
        ulimit_off_action_,
        ulimit_on_action_,
        Prefs::USPEED,
        Prefs::USPEED_ENABLED);

    menu->addSeparator();

    init_seed_ratio_sub_menu(
        menu->addMenu(tr("Stop Seeding at Ratio")),
        ratio_off_action_,
        ratio_on_action_,
        Prefs::RATIO,
        Prefs::RATIO_ENABLED);

    return menu;
}

QMenu* MainWindow::createStatsModeMenu()
{
    auto const stats_modes = std::array<std::pair<QAction*, QString>, 4>{ {
        { ui_.action_TotalRatio, total_ratio_stats_mode_name_ },
        { ui_.action_TotalTransfer, total_transfer_stats_mode_name_ },
        { ui_.action_SessionRatio, session_ratio_stats_mode_name_ },
        { ui_.action_SessionTransfer, session_transfer_stats_mode_name_ },
    } };

    auto* action_group = new QActionGroup{ this };
    auto* menu = new QMenu{ this };

    for (auto const& mode : stats_modes)
    {
        mode.first->setProperty(StatsModeKey, QString{ mode.second });
        action_group->addAction(mode.first);
        menu->addAction(mode.first);
    }

    connect(action_group, &QActionGroup::triggered, this, &MainWindow::onStatsModeChanged);

    // NOLINTNEXTLINE(clang-analyzer-cplusplus.NewDeleteLeaks)
    return menu;
}

/****
*****
****/

void MainWindow::onSortModeChanged(QAction const* action)
{
    prefs_.set(Prefs::SORT_MODE, SortMode(action->property(SortModeKey).toInt()));
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
    auto* d = new RelocateDialog{ session_, model_, getSelectedTorrents(), this };
    d->setAttribute(Qt::WA_DeleteOnClose, true);
    d->show();
}

namespace
{
namespace open_folder_helpers
{
#if defined(Q_OS_WIN)
void openSelect(QString const& path)
{
    auto const explorer = QStringLiteral("explorer");
    QString param;

    if (!QFileInfo{ path }.isDir())
    {
        param = QStringLiteral("/select,");
    }

    param += QDir::toNativeSeparators(path);
    QProcess::startDetached(explorer, QStringList{ param });
}
#elif defined(Q_OS_MAC)
void openSelect(QString const& path)
{
    QStringList script_args;
    script_args << QStringLiteral("-e") << QStringLiteral("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path);
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), script_args);

    script_args.clear();
    script_args << QStringLiteral("-e") << QStringLiteral("tell application \"Finder\" to activate");
    QProcess::execute(QStringLiteral("/usr/bin/osascript"), script_args);
}
#else
void openSelect(QString const& path)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
#endif

// if all torrents in a list have the same top folder, return it
[[nodiscard]] QString getTopFolder(FileList const& files)
{
    if (std::empty(files))
    {
        return {};
    }

    auto const& first_filename = files.at(0).filename;
    auto const slash_index = first_filename.indexOf(QLatin1Char{ '/' });
    if (slash_index == -1)
    {
        return {};
    }

    auto top = first_filename.left(slash_index);
    if (!std::all_of(std::begin(files), std::end(files), [&top](auto const& file) { return file.filename.startsWith(top); }))
    {
        return {};
    }

    return top;
}

[[nodiscard]] bool isTopFolder(QDir const& parent, QString const& child)
{
    if (child.isEmpty())
    {
        return false;
    }

    auto const info = QFileInfo{ parent, child };
    return info.exists() && info.isDir();
}

[[nodiscard]] QString getTopFolder(QDir const& parent, Torrent const* const tor)
{
    if (auto top = getTopFolder(tor->files()); isTopFolder(parent, top))
    {
        return top;
    }

    if (auto const& top = tor->name(); isTopFolder(parent, top))
    {
        return top;
    }

    return {};
}
} // namespace open_folder_helpers
} // namespace

void MainWindow::openFolder()
{
    using namespace open_folder_helpers;

    auto const selected_torrents = getSelectedTorrents();
    if (std::size(selected_torrents) != 1U)
    {
        return;
    }

    auto const torrent_id = *selected_torrents.begin();
    auto const* const tor = model_.getTorrentFromId(torrent_id);
    if (tor == nullptr)
    {
        return;
    }

    auto const parent = QDir{ tor->getPath() };
    auto const child = getTopFolder(parent, tor);
    openSelect(parent.filePath(child));
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

void MainWindow::openDonate() const
{
    QDesktopServices::openUrl(QUrl{ QStringLiteral("https://transmissionbt.com/donate/") });
}

void MainWindow::openAbout()
{
    Utils::openDialog(about_dialog_, session_, this);
}

void MainWindow::openHelp() const
{
    QDesktopServices::openUrl(
        QUrl{ QStringLiteral("https://transmissionbt.com/help/gtk/%1.%2x").arg(MAJOR_VERSION).arg(MINOR_VERSION / 10) });
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

    if (auto const url = QUrl{ session_.getRemoteUrl() }; !url.isEmpty())
    {
        //: Second (optional) part of main window title "Transmission - host:port" (added when connected to remote session)
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
        tip = stats.speed_down.to_download_qstring() + QStringLiteral("   ") + stats.speed_up.to_upload_qstring();
    }
    else if (stats.peers_receiving != 0)
    {
        tip = stats.speed_up.to_upload_qstring();
    }

    tray_icon_.setToolTip(tip);
}

void MainWindow::refreshStatusBar(TransferStats const& stats)
{
    ui_.uploadSpeedLabel->setText(stats.speed_up.to_upload_qstring());
    ui_.uploadSpeedLabel->setVisible(stats.peers_sending || stats.peers_receiving);
    ui_.downloadSpeedLabel->setText(stats.speed_down.to_download_qstring());
    ui_.downloadSpeedLabel->setVisible(stats.peers_sending);

    auto const mode = prefs_.getString(Prefs::STATUSBAR_STATS);
    auto str = QString{};

    if (mode == session_ratio_stats_mode_name_)
    {
        str = tr("Ratio: %1").arg(Formatter::ratio_to_string(session_.getStats().ratio));
    }
    else if (mode == session_transfer_stats_mode_name_)
    {
        auto const& st = session_.getStats();
        str = tr("Down: %1, Up: %2")
                  .arg(Formatter::storage_to_string(st.downloadedBytes))
                  .arg(Formatter::storage_to_string(st.uploadedBytes));
    }
    else if (mode == total_transfer_stats_mode_name_)
    {
        auto const& st = session_.getCumulativeStats();
        str = tr("Down: %1, Up: %2")
                  .arg(Formatter::storage_to_string(st.downloadedBytes))
                  .arg(Formatter::storage_to_string(st.uploadedBytes));
    }
    else // default is "total-ratio"
    {
        assert(mode == total_ratio_stats_mode_name_);
        str = tr("Ratio: %1").arg(Formatter::ratio_to_string(session_.getCumulativeStats().ratio));
    }

    ui_.statsLabel->setText(str);
}

void MainWindow::refreshTorrentViewHeader()
{
    int const total_count = model_.rowCount();
    int const visible_count = filter_model_.rowCount();

    if (visible_count == total_count)
    {
        ui_.listView->setHeaderText(QString{});
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
        auto const* const tor = model->data(row, TorrentModel::TorrentRole).value<Torrent const*>();
        if (tor == nullptr)
        {
            continue;
        }

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
    auto const is_paused = [](auto const* tor)
    {
        return tor->isPaused();
    };
    auto const any_paused = std::any_of(std::begin(torrents), std::end(torrents), is_paused);
    auto const any_not_paused = !std::all_of(std::begin(torrents), std::end(torrents), is_paused);

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

void MainWindow::onStatsModeChanged(QAction const* action)
{
    prefs_.set(Prefs::STATUSBAR_STATS, action->property(StatsModeKey).toString());
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

        raise();
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
        this->activateWindow();
#else
        QApplication::setActiveWindow(this);
#endif
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
    auto b = bool{};
    auto i = int{};
    auto str = QString{};

    switch (key)
    {
    case Prefs::STATUSBAR_STATS:
        str = prefs_.getString(key);

        for (auto* action : ui_.action_TotalRatio->actionGroup()->actions())
        {
            action->setChecked(str == action->property(StatsModeKey).toString());
        }

        refreshSoon(REFRESH_STATUS_BAR);
        break;

    case Prefs::SORT_REVERSED:
        ui_.action_ReverseSortOrder->setChecked(prefs_.getBool(key));
        break;

    case Prefs::SORT_MODE:
        i = prefs_.get<SortMode>(key).mode();

        for (auto* action : ui_.action_SortByActivity->actionGroup()->actions())
        {
            action->setChecked(i == action->property(SortModeKey).toInt());
        }

        break;

    case Prefs::DSPEED_ENABLED:
        (prefs_.get<bool>(key) ? dlimit_on_action_ : dlimit_off_action_)->setChecked(true);
        break;

    case Prefs::DSPEED:
        dlimit_on_action_->setText(
            tr("Limited at %1").arg(Speed{ prefs_.get<unsigned int>(key), Speed::Units::KByps }.to_qstring()));
        break;

    case Prefs::USPEED_ENABLED:
        (prefs_.get<bool>(key) ? ulimit_on_action_ : ulimit_off_action_)->setChecked(true);
        break;

    case Prefs::USPEED:
        ulimit_on_action_->setText(
            tr("Limited at %1").arg(Speed{ prefs_.get<unsigned int>(key), Speed::Units::KByps }.to_qstring()));
        break;

    case Prefs::RATIO_ENABLED:
        (prefs_.get<bool>(key) ? ratio_on_action_ : ratio_off_action_)->setChecked(true);
        break;

    case Prefs::RATIO:
        ratio_on_action_->setText(tr("Stop at Ratio (%1)").arg(Formatter::ratio_to_string(prefs_.get<double>(key))));
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
        QApplication::setQuitOnLastWindowClosed(!b);
        refreshSoon(REFRESH_TRAY_ICON);
        break;

    case Prefs::COMPACT_VIEW:
        b = prefs_.getBool(key);
        ui_.action_CompactView->setChecked(b);
        ui_.listView->setItemDelegate(b ? torrent_delegate_min_ : torrent_delegate_);
        break;

    case Prefs::MAIN_WINDOW_X:
    case Prefs::MAIN_WINDOW_Y:
    case Prefs::MAIN_WINDOW_WIDTH:
    case Prefs::MAIN_WINDOW_HEIGHT:
        setGeometry(
            prefs_.getInt(Prefs::MAIN_WINDOW_X),
            prefs_.getInt(Prefs::MAIN_WINDOW_Y),
            prefs_.getInt(Prefs::MAIN_WINDOW_WIDTH),
            prefs_.getInt(Prefs::MAIN_WINDOW_HEIGHT));
        break;

    case Prefs::ALT_SPEED_LIMIT_ENABLED:
    case Prefs::ALT_SPEED_LIMIT_UP:
    case Prefs::ALT_SPEED_LIMIT_DOWN:
        {
            b = prefs_.getBool(Prefs::ALT_SPEED_LIMIT_ENABLED);
            alt_speed_action_->setChecked(b);
            ui_.altSpeedButton->setChecked(b);
            auto const fmt = b ? tr("Click to disable Temporary Speed Limits\n (%1 down, %2 up)") :
                                 tr("Click to enable Temporary Speed Limits\n (%1 down, %2 up)");
            auto const d = Speed{ prefs_.get<unsigned int>(Prefs::ALT_SPEED_LIMIT_DOWN), Speed::Units::KByps };
            auto const u = Speed{ prefs_.get<unsigned int>(Prefs::ALT_SPEED_LIMIT_UP), Speed::Units::KByps };
            ui_.altSpeedButton->setToolTip(fmt.arg(d.to_qstring()).arg(u.to_qstring()));
            break;
        }

    case Prefs::READ_CLIPBOARD:
        auto_add_clipboard_links_ = prefs_.getBool(Prefs::READ_CLIPBOARD);
        break;

    default:
        break;
    }
}

/***
****
***/

void MainWindow::newTorrent()
{
    auto* dialog = new MakeDialog{ session_, this };
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void MainWindow::openTorrent()
{
    auto* const d = new QFileDialog{ this,
                                     tr("Open Torrent"),
                                     prefs_.getString(Prefs::OPEN_DIALOG_FOLDER),
                                     tr("Torrent Files (*.torrent);;All Files (*.*)") };
    d->setFileMode(QFileDialog::ExistingFiles);
    d->setAttribute(Qt::WA_DeleteOnClose);

    if (auto* const l = qobject_cast<QGridLayout*>(d->layout()); l != nullptr)
    {
        auto* b = new QCheckBox{ tr("Show &options dialog") };
        b->setChecked(prefs_.getBool(Prefs::OPTIONS_PROMPT));
        b->setObjectName(show_options_checkbox_name_);
        l->addWidget(b, l->rowCount(), 0, 1, -1, Qt::AlignLeft);
    }

    connect(d, &QFileDialog::filesSelected, this, &MainWindow::addTorrents);

    d->open();
}

void MainWindow::openURL()
{
    auto add = AddData::create(QApplication::clipboard()->text(QClipboard::Selection));

    if (!add)
    {
        add = AddData::create(QApplication::clipboard()->text(QClipboard::Clipboard));
    }

    if (!add)
    {
        add = AddData{};
    }

    addTorrent(std::move(*add), true);
}

void MainWindow::addTorrentFromClipboard()
{
    if (auto const text = QGuiApplication::clipboard()->text().trimmed();
        text.endsWith(QStringLiteral(".torrent"), Qt::CaseInsensitive) || tr_magnet_metainfo{}.parseMagnet(text.toStdString()))
    {
        for (auto const& entry : text.split(QLatin1Char('\n')))
        {
            auto key = entry.trimmed();
            if (key.isEmpty())
            {
                continue;
            }

            if (auto const url = QUrl{ key }; url.isLocalFile())
            {
                key = url.toLocalFile();
            }

            if (!clipboard_processed_keys_.contains(key))
            {
                clipboard_processed_keys_.append(key);
                trApp->addTorrent(AddData{ key });
            }
        }
    }
}

void MainWindow::addTorrents(QStringList const& filenames)
{
    bool show_options = prefs_.getBool(Prefs::OPTIONS_PROMPT);

    if (auto const* const file_dialog = qobject_cast<QFileDialog const*>(sender()); file_dialog != nullptr)
    {
        if (auto const* const b = file_dialog->findChild<QCheckBox const*>(show_options_checkbox_name_); b != nullptr)
        {
            show_options = b->isChecked();
        }
    }

    for (QString const& filename : filenames)
    {
        addTorrent(AddData{ filename }, show_options);
    }
}

void MainWindow::addTorrent(AddData add_me, bool show_options)
{
    if (show_options)
    {
        auto* o = new OptionsDialog{ session_, prefs_, std::move(add_me), this };
        o->show();
        QApplication::alert(o);
    }
    else
    {
        session_.addTorrent(std::move(add_me));
        QApplication::alert(this);
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

    int const count = ids.size();

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
        secondary_text = count == 1 ? tr("This torrent is connected to peers.") : tr("These torrents are connected to peers.");
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
        layout = new QGridLayout{};
        msg_box.setLayout(layout);
    }

    auto* spacer = new QSpacerItem{ 450, 0, QSizePolicy::Minimum, QSizePolicy::Expanding };
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

void MainWindow::updateNetworkLabel()
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
    else if (seconds_since_last_read < 120)
    {
        tip = tr("%1 last responded %2 ago").arg(url).arg(Formatter::time_to_string(seconds_since_last_read));
    }
    else
    {
        tip = tr("%1 is not responding").arg(url);
    }

    ui_.networkLabel->setPixmap(pixmap);
    ui_.networkLabel->setToolTip(tip);
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
    updateNetworkLabel();

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
        tr_magnet_metainfo{}.parseMagnet(mime->text().toStdString()))
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
            if (auto const url = QUrl{ key }; url.isLocalFile())
            {
                key = url.toLocalFile();
            }

            trApp->addTorrent(AddData{ key });
        }
    }
}

bool MainWindow::event(QEvent* e)
{
    switch (e->type())
    {
    case QEvent::WindowActivate:
        addTorrentFromClipboard();
        break;

    case QEvent::Clipboard:
        if (auto_add_clipboard_links_)
            addTorrentFromClipboard();
        break;

    default:
        break;
    }

    return QMainWindow::event(e);
}

/***
****
***/

void MainWindow::contextMenuEvent(QContextMenuEvent* event)
{
    ui_.menuTorrent->popup(event->globalPos());
}
