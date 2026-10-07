// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "PrefsDialog.h"

#include <cassert>
#include <optional>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTime>
#include <QTimeEdit>

#include <libtransmission/transmission.h>

#include "ColumnResizer.h"
#include "Formatter.h"
#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "Session.h"
#include "UserMetaType.h"
#include "Utils.h"

using namespace tr;

// ---

template<typename T, size_t N>
void PrefsDialog::initComboFromItems(std::array<std::pair<QString, T>, N> const& items, QComboBox* const w, tr_quark const key)
{
    for (auto const& [label, value] : items)
    {
        w->addItem(label);
    }

    auto updater = [this, key, w, items]()
    {
        auto const blocker = QSignalBlocker{ w };
        auto const val = prefs_.get<T>(key);
        for (size_t i = 0; i < std::size(items); ++i)
        {
            if (items[i].second == val)
            {
                w->setCurrentIndex(static_cast<int>(i));
            }
        }
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    auto on_activated = [this, key, items](int const idx)
    {
        if (0 <= idx && idx < static_cast<int>(std::size(items)))
        {
            set(key, items[static_cast<size_t>(idx)].second);
        }
    };
    connect(w, qOverload<int>(&QComboBox::activated), std::move(on_activated));
}

void PrefsDialog::initAltSpeedDaysCombo(QComboBox* const w, tr_quark const key)
{
    static auto items = []
    {
        auto ret = std::array<std::pair<QString, int>, 10U>{};
        auto idx = 0;
        ret[idx++] = { tr("Every Day"), TR_SCHED_ALL };
        ret[idx++] = { tr("Weekdays"), TR_SCHED_WEEKDAY };
        ret[idx++] = { tr("Weekends"), TR_SCHED_WEEKEND };

        auto const locale = QLocale{};
        auto const qt_day_to_tr_day = std::map<int, int>{ {
            { Qt::Monday, TR_SCHED_MON },
            { Qt::Tuesday, TR_SCHED_TUES },
            { Qt::Wednesday, TR_SCHED_WED },
            { Qt::Thursday, TR_SCHED_THURS },
            { Qt::Friday, TR_SCHED_FRI },
            { Qt::Saturday, TR_SCHED_SAT },
            { Qt::Sunday, TR_SCHED_SUN },
        } };
        auto const first_day_of_week = locale.firstDayOfWeek();
        for (int i = first_day_of_week; i <= Qt::Sunday; ++i)
        {
            ret[idx++] = { locale.dayName(i), qt_day_to_tr_day.at(i) };
        }
        for (int i = Qt::Monday; i < first_day_of_week; ++i)
        {
            ret[idx++] = { locale.dayName(i), qt_day_to_tr_day.at(i) };
        }
        return ret;
    }();

    initComboFromItems(items, w, key);
}

void PrefsDialog::initEncryptionCombo(QComboBox* const w, tr_quark const key)
{
    static auto const Items = std::array<std::pair<QString, tr_encryption_mode>, 3U>{ {
        { tr("Allow encryption"), TR_CLEAR_PREFERRED },
        { tr("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
        { tr("Require encryption"), TR_ENCRYPTION_REQUIRED },
    } };

    initComboFromItems(Items, w, key);
}

void PrefsDialog::initWidget(QPlainTextEdit* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setPlainText(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    // We don't want to change the preference every time there's a keystroke
    // in a QPlainTextEdit, so instead of connecting to the textChanged signal,
    // only update the pref when the text changed AND focus was lost.
    auto on_focus_changed = [this, key, w](QWidget* old, QWidget* /*cur*/)
    {
        if (w == old && w->document()->isModified())
        {
            set(key, w->toPlainText());
        }
    };
    connect(qApp, &QApplication::focusChanged, std::move(on_focus_changed));
}

void PrefsDialog::initWidget(FreeSpaceLabel* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setPath(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));
}

void PrefsDialog::initWidget(PathButton* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setPath(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &PathButton::pathChanged, [this, key](QString const& val) { set(key, val); });
}

void PrefsDialog::initWidget(QCheckBox* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setChecked(prefs_.get<bool>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &QAbstractButton::toggled, [this, key](bool const val) { set(key, val); });
}

void PrefsDialog::initWidget(QDoubleSpinBox* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setValue(prefs_.get<double>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, qOverload<double>(&QDoubleSpinBox::valueChanged), [this, key](double const val) { set(key, val); });
}

void PrefsDialog::initWidget(QLineEdit* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setText(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    auto on_editing_finished = [this, key, w]()
    {
        if (w->isModified())
        {
            set(key, w->text());
        }
    };
    connect(w, &QLineEdit::editingFinished, std::move(on_editing_finished));
}

void PrefsDialog::initWidget(QSpinBox* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setValue(prefs_.get<int>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, qOverload<int>(&QSpinBox::valueChanged), [this, key](int const val) { set(key, val); });
}

void PrefsDialog::initWidget(QTimeEdit* const w, tr_quark const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        auto const minutes = prefs_.get<int>(key);
        w->setTime(QTime{ 0, 0 }.addSecs(minutes * 60));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    auto on_editing_finished = [this, key, w]()
    {
        auto const minutes = w->time().msecsSinceStartOfDay() / (1000 * 60);
        set(key, minutes);
    };
    connect(w, &QAbstractSpinBox::editingFinished, std::move(on_editing_finished));
}

// ---

void PrefsDialog::initRemoteTab()
{
    initWidget(ui_.enableRpcCheck, TR_KEY_rpc_enabled);
    initWidget(ui_.rpcPortSpin, TR_KEY_rpc_port);
    initWidget(ui_.requireRpcAuthCheck, TR_KEY_rpc_authentication_required);
    initWidget(ui_.rpcUsernameEdit, TR_KEY_rpc_username);
    initWidget(ui_.rpcPasswordEdit, TR_KEY_rpc_password);
    initWidget(ui_.enableRpcWhitelistCheck, TR_KEY_rpc_whitelist_enabled);
    initWidget(ui_.rpcWhitelistEdit, TR_KEY_rpc_whitelist);

    web_widgets_ << ui_.rpcPortLabel << ui_.rpcPortSpin << ui_.requireRpcAuthCheck << ui_.enableRpcWhitelistCheck;
    web_auth_widgets_ << ui_.rpcUsernameLabel << ui_.rpcUsernameEdit << ui_.rpcPasswordLabel << ui_.rpcPasswordEdit;
    web_whitelist_widgets_ << ui_.rpcWhitelistLabel << ui_.rpcWhitelistEdit;
    unsupported_when_remote_ << ui_.enableRpcCheck << web_widgets_ << web_auth_widgets_ << web_whitelist_widgets_;

    connect(ui_.openWebClientButton, &QAbstractButton::clicked, &session_, &Session::launchWebInterface);
}

// ---

void PrefsDialog::initSpeedTab()
{
    auto const suffix = QStringLiteral(" %1").arg(Speed::display_name(Speed::Units::KByps));

    ui_.uploadSpeedLimitSpin->setSuffix(suffix);
    ui_.downloadSpeedLimitSpin->setSuffix(suffix);
    ui_.altUploadSpeedLimitSpin->setSuffix(suffix);
    ui_.altDownloadSpeedLimitSpin->setSuffix(suffix);

    initWidget(ui_.uploadSpeedLimitCheck, TR_KEY_speed_limit_up_enabled);
    initWidget(ui_.uploadSpeedLimitSpin, TR_KEY_speed_limit_up);
    initWidget(ui_.downloadSpeedLimitCheck, TR_KEY_speed_limit_down_enabled);
    initWidget(ui_.downloadSpeedLimitSpin, TR_KEY_speed_limit_down);
    initWidget(ui_.altUploadSpeedLimitSpin, TR_KEY_alt_speed_up);
    initWidget(ui_.altDownloadSpeedLimitSpin, TR_KEY_alt_speed_down);
    initWidget(ui_.altSpeedLimitScheduleCheck, TR_KEY_alt_speed_time_enabled);
    initWidget(ui_.altSpeedLimitStartTimeEdit, TR_KEY_alt_speed_time_begin);
    initWidget(ui_.altSpeedLimitEndTimeEdit, TR_KEY_alt_speed_time_end);
    initAltSpeedDaysCombo(ui_.altSpeedLimitDaysCombo, TR_KEY_alt_speed_time_day);

    sched_widgets_ << ui_.altSpeedLimitStartTimeEdit << ui_.altSpeedLimitToLabel << ui_.altSpeedLimitEndTimeEdit
                   << ui_.altSpeedLimitDaysLabel << ui_.altSpeedLimitDaysCombo;

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.speedLimitsSectionLayout);
    cr->addLayout(ui_.altSpeedLimitsSectionLayout);
    cr->update();
}

// ---

void PrefsDialog::initDesktopTab()
{
    initWidget(ui_.showTrayIconCheck, TR_KEY_show_notification_area_icon);
    initWidget(ui_.startMinimizedCheck, TR_KEY_start_minimized);
    initWidget(ui_.notifyOnTorrentAddedCheck, TR_KEY_torrent_added_notification_enabled);
    initWidget(ui_.notifyOnTorrentCompletedCheck, TR_KEY_torrent_complete_notification_enabled);
    initWidget(ui_.playSoundOnTorrentCompletedCheck, TR_KEY_torrent_complete_sound_enabled);
}

// ---

QString PrefsDialog::getPortStatusText(PrefsDialog::PortTestStatus status) noexcept
{
    switch (status)
    {
    case PortTestStatus::Unknown:
        return tr("unknown");
    case PortTestStatus::Checking:
        return tr("checking…");
    case PortTestStatus::Open:
        return tr("open");
    case PortTestStatus::Closed:
        return tr("closed");
    case PortTestStatus::Error:
        return tr("error");
    default:
        return {};
    }
}

void PrefsDialog::updatePortStatusLabel()
{
    auto const status_ipv4 = getPortStatusText(port_test_status_[Session::PORT_TEST_IPV4]);
    auto const status_ipv6 = getPortStatusText(port_test_status_[Session::PORT_TEST_IPV6]);

    ui_.peerPortStatusLabel->setText(
        port_test_status_[Session::PORT_TEST_IPV4] == port_test_status_[Session::PORT_TEST_IPV6] ?
            tr("Status: <b>%1</b>").arg(status_ipv4) :
            tr("Status: <b>%1</b> (IPv4), <b>%2</b> (IPv6)").arg(status_ipv4).arg(status_ipv6));
}

void PrefsDialog::portTestSetEnabled()
{
    // Depend on the RPC call status instead of the UI status, so that the widgets
    // won't be enabled even if the port peer port changed while we have port_test
    // RPC call(s) in-flight.
    auto const sensitive = !session_.portTestPending(Session::PORT_TEST_IPV4) &&
        !session_.portTestPending(Session::PORT_TEST_IPV6);
    ui_.testPeerPortButton->setEnabled(sensitive);
    ui_.peerPortSpin->setEnabled(sensitive);
}

void PrefsDialog::onPortTested(std::optional<bool> result, Session::PortTestIpProtocol ip_protocol)
{
    constexpr auto StatusFromResult = [](std::optional<bool> const res)
    {
        if (!res)
        {
            return PortTestStatus::Error;
        }
        if (!*res)
        {
            return PortTestStatus::Closed;
        }
        return PortTestStatus::Open;
    };

    // Only update the UI if the current status is "checking", so that
    // we won't show the port test results for the old peer port if it
    // changed while we have port_test RPC call(s) in-flight.
    if (port_test_status_[ip_protocol] == PortTestStatus::Checking)
    {
        port_test_status_[ip_protocol] = StatusFromResult(result);
        updatePortStatusLabel();
    }
    portTestSetEnabled();
}

void PrefsDialog::onPortTest()
{
    port_test_status_[Session::PORT_TEST_IPV4] = PortTestStatus::Checking;
    port_test_status_[Session::PORT_TEST_IPV6] = PortTestStatus::Checking;
    updatePortStatusLabel();

    session_.portTest(Session::PORT_TEST_IPV4);
    session_.portTest(Session::PORT_TEST_IPV6);

    portTestSetEnabled();
}

void PrefsDialog::initNetworkTab()
{
    ui_.torrentPeerLimitSpin->setRange(1, INT_MAX);
    ui_.globalPeerLimitSpin->setRange(1, INT_MAX);

    initWidget(ui_.peerPortSpin, TR_KEY_peer_port);
    initWidget(ui_.randomPeerPortCheck, TR_KEY_peer_port_random_on_start);
    initWidget(ui_.enablePortForwardingCheck, TR_KEY_port_forwarding_enabled);
    initWidget(ui_.torrentPeerLimitSpin, TR_KEY_peer_limit_per_torrent);
    initWidget(ui_.globalPeerLimitSpin, TR_KEY_peer_limit_global);
    initWidget(ui_.enableUtpCheck, TR_KEY_utp_enabled);
    initWidget(ui_.enablePexCheck, TR_KEY_pex_enabled);
    initWidget(ui_.enableDhtCheck, TR_KEY_dht_enabled);
    initWidget(ui_.enableLpdCheck, TR_KEY_lpd_enabled);
    initWidget(ui_.defaultTrackersPlainTextEdit, TR_KEY_default_trackers);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.incomingPeersSectionLayout);
    cr->addLayout(ui_.peerLimitsSectionLayout);
    cr->update();

    connect(ui_.testPeerPortButton, &QAbstractButton::clicked, this, &PrefsDialog::onPortTest);
    connect(&session_, &Session::portTested, this, &PrefsDialog::onPortTested);

    updatePortStatusLabel();
}

// ---

void PrefsDialog::onBlocklistDialogDestroyed(QObject* o)
{
    Q_UNUSED(o)

    blocklist_dialog_ = nullptr;
}

void PrefsDialog::onUpdateBlocklistCancelled()
{
    disconnect(&session_, &Session::blocklistUpdated, this, &PrefsDialog::onBlocklistUpdated);
    blocklist_dialog_->deleteLater();
}

void PrefsDialog::onBlocklistUpdated(int64_t n)
{
    blocklist_dialog_->setText(
        tr("<b>Update succeeded!</b><p>Blocklist now has %Ln rule(s).</p>", nullptr, static_cast<int>(n)));
    blocklist_dialog_->setTextFormat(Qt::RichText);
}

void PrefsDialog::onUpdateBlocklistClicked()
{
    blocklist_dialog_ = new QMessageBox{ QMessageBox::Information,
                                         QString{},
                                         tr("<b>Update Blocklist</b><p>Getting new blocklist…</p>"),
                                         QMessageBox::Close,
                                         this };
    connect(blocklist_dialog_, &QDialog::rejected, this, &PrefsDialog::onUpdateBlocklistCancelled);
    connect(&session_, &Session::blocklistUpdated, this, &PrefsDialog::onBlocklistUpdated);
    blocklist_dialog_->show();
    session_.updateBlocklist();
}

void PrefsDialog::initPrivacyTab()
{
    initEncryptionCombo(ui_.encryptionModeCombo, TR_KEY_encryption);
    initWidget(ui_.blocklistCheck, TR_KEY_blocklist_enabled);
    initWidget(ui_.blocklistEdit, TR_KEY_blocklist_url);
    initWidget(ui_.autoUpdateBlocklistCheck, TR_KEY_blocklist_updates_enabled);

    block_widgets_ << ui_.blocklistEdit << ui_.blocklistStatusLabel << ui_.updateBlocklistButton
                   << ui_.autoUpdateBlocklistCheck;

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.encryptionSectionLayout);
    cr->addLayout(ui_.blocklistSectionLayout);
    cr->update();

    connect(ui_.updateBlocklistButton, &QAbstractButton::clicked, this, &PrefsDialog::onUpdateBlocklistClicked);

    updateBlocklistLabel();
}

// ---

void PrefsDialog::onIdleLimitChanged()
{
    //: Spin box format, "Stop seeding if idle for: [ 5 minutes ]"
    auto const* const units_format = QT_TRANSLATE_N_NOOP("PrefsDialog", "%1 minute(s)");
    auto const placeholder = QStringLiteral("%1");
    Utils::updateSpinBoxFormat(ui_.idleLimitSpin, "PrefsDialog", units_format, placeholder);
}

void PrefsDialog::initSeedingTab()
{
    ui_.doneSeedingScriptButton->setTitle(tr("Select \"Torrent Done Seeding\" Script"));

    initWidget(ui_.ratioLimitCheck, TR_KEY_seed_ratio_limited);
    initWidget(ui_.ratioLimitSpin, TR_KEY_seed_ratio_limit);
    initWidget(ui_.idleLimitCheck, TR_KEY_idle_seeding_limit_enabled);
    initWidget(ui_.idleLimitSpin, TR_KEY_idle_seeding_limit);
    initWidget(ui_.doneSeedingScriptCheck, TR_KEY_script_torrent_done_seeding_enabled);
    initWidget(ui_.doneSeedingScriptButton, TR_KEY_script_torrent_done_seeding_filename);
    initWidget(ui_.doneSeedingScriptEdit, TR_KEY_script_torrent_done_seeding_filename);

    connect(ui_.idleLimitSpin, qOverload<int>(&QSpinBox::valueChanged), this, &PrefsDialog::onIdleLimitChanged);

    updateSeedingWidgetsLocality();
    onIdleLimitChanged();
}

void PrefsDialog::onQueueStalledMinutesChanged()
{
    //: Spin box format, "Download is inactive if data sharing stopped: [ 5 minutes ago ]"
    auto const* const units_format = QT_TRANSLATE_N_NOOP("PrefsDialog", "%1 minute(s) ago");
    auto const placeholder = QStringLiteral("%1");
    Utils::updateSpinBoxFormat(ui_.queueStalledMinutesSpin, "PrefsDialog", units_format, placeholder);
}

void PrefsDialog::initDownloadingTab()
{
    ui_.watchDirButton->setMode(PathButton::DirectoryMode);
    ui_.downloadDirButton->setMode(PathButton::DirectoryMode);
    ui_.incompleteDirButton->setMode(PathButton::DirectoryMode);
    ui_.doneDownloadingScriptButton->setMode(PathButton::FileMode);
    ui_.doneSeedingScriptButton->setMode(PathButton::FileMode);

    ui_.watchDirButton->setTitle(tr("Select Watch Directory"));
    ui_.downloadDirButton->setTitle(tr("Select Destination"));
    ui_.incompleteDirButton->setTitle(tr("Select Incomplete Directory"));
    ui_.doneDownloadingScriptButton->setTitle(tr("Select \"Torrent Done Downloading\" Script"));

    ui_.watchDirStack->setMinimumWidth(200);

    ui_.downloadDirFreeSpaceLabel->setSession(session_);
    ui_.downloadDirFreeSpaceLabel->setPath(prefs_.get<QString>(TR_KEY_download_dir));

    initWidget(ui_.watchDirCheck, TR_KEY_watch_dir_enabled);
    initWidget(ui_.watchDirButton, TR_KEY_watch_dir);
    initWidget(ui_.watchDirEdit, TR_KEY_watch_dir);
    initWidget(ui_.showTorrentOptionsDialogCheck, TR_KEY_show_options_window);
    initWidget(ui_.startAddedTorrentsCheck, TR_KEY_start_added_torrents);
    initWidget(ui_.detectTorrentFromClipboard, TR_KEY_read_clipboard);
    initWidget(ui_.trashTorrentFileCheck, TR_KEY_trash_original_torrent_files);
    initWidget(ui_.downloadDirButton, TR_KEY_download_dir);
    initWidget(ui_.downloadDirEdit, TR_KEY_download_dir);
    initWidget(ui_.downloadDirFreeSpaceLabel, TR_KEY_download_dir);
    initWidget(ui_.downloadQueueSizeSpin, TR_KEY_download_queue_size);
    initWidget(ui_.queueStalledMinutesSpin, TR_KEY_queue_stalled_minutes);
    initWidget(ui_.renamePartialFilesCheck, TR_KEY_rename_partial_files);
    initWidget(ui_.incompleteDirCheck, TR_KEY_incomplete_dir_enabled);
    initWidget(ui_.incompleteDirButton, TR_KEY_incomplete_dir);
    initWidget(ui_.incompleteDirEdit, TR_KEY_incomplete_dir);
    initWidget(ui_.doneDownloadingScriptCheck, TR_KEY_script_torrent_done_enabled);
    initWidget(ui_.doneDownloadingScriptButton, TR_KEY_script_torrent_done_filename);
    initWidget(ui_.doneDownloadingScriptEdit, TR_KEY_script_torrent_done_filename);

    auto* cr = new ColumnResizer{ this };
    cr->addLayout(ui_.addingSectionLayout);
    cr->addLayout(ui_.downloadQueueSectionLayout);
    cr->addLayout(ui_.incompleteSectionLayout);
    cr->update();

    connect(
        ui_.queueStalledMinutesSpin,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        &PrefsDialog::onQueueStalledMinutesChanged);

    updateDownloadingWidgetsLocality();
    onQueueStalledMinutesChanged();
}

void PrefsDialog::updateDownloadingWidgetsLocality()
{
    ui_.watchDirStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.watchDirButton) : ui_.watchDirEdit);
    ui_.downloadDirStack->setCurrentWidget(is_local_ ? static_cast<QWidget*>(ui_.downloadDirButton) : ui_.downloadDirEdit);
    ui_.incompleteDirStack->setCurrentWidget(
        is_local_ ? static_cast<QWidget*>(ui_.incompleteDirButton) : ui_.incompleteDirEdit);
    ui_.doneDownloadingScriptStack->setCurrentWidget(
        is_local_ ? static_cast<QWidget*>(ui_.doneDownloadingScriptButton) : ui_.doneDownloadingScriptEdit);

    ui_.watchDirStack->setFixedHeight(ui_.watchDirStack->currentWidget()->sizeHint().height());
    ui_.downloadDirStack->setFixedHeight(ui_.downloadDirStack->currentWidget()->sizeHint().height());
    ui_.incompleteDirStack->setFixedHeight(ui_.incompleteDirStack->currentWidget()->sizeHint().height());
    ui_.doneDownloadingScriptStack->setFixedHeight(ui_.doneDownloadingScriptStack->currentWidget()->sizeHint().height());

    ui_.downloadDirLabel->setBuddy(ui_.downloadDirStack->currentWidget());
}

void PrefsDialog::updateSeedingWidgetsLocality()
{
    ui_.doneSeedingScriptStack->setCurrentWidget(
        is_local_ ? static_cast<QWidget*>(ui_.doneSeedingScriptButton) : ui_.doneSeedingScriptEdit);
    ui_.doneSeedingScriptStack->setFixedHeight(ui_.doneSeedingScriptStack->currentWidget()->sizeHint().height());
}

// ---

PrefsDialog::PrefsDialog(Session& session, Prefs& prefs, QWidget* parent)
    : BaseDialog{ parent }
    , session_{ session }
    , prefs_{ prefs }
    , is_server_{ session.isServer() }
    , is_local_{ session_.isLocal() }
{
    ui_.setupUi(this);

    initSpeedTab();
    initDownloadingTab();
    initSeedingTab();
    initPrivacyTab();
    initNetworkTab();
    initDesktopTab();
    initRemoteTab();

    connect(&session_, &Session::sessionUpdated, this, &PrefsDialog::sessionUpdated);

    static std::array<tr_quark, 10> constexpr InitKeys = {
        TR_KEY_alt_speed_enabled, TR_KEY_alt_speed_time_enabled,
        TR_KEY_blocklist_enabled, TR_KEY_watch_dir,
        TR_KEY_download_dir,      TR_KEY_encryption,
        TR_KEY_incomplete_dir,    TR_KEY_incomplete_dir_enabled,
        TR_KEY_rpc_enabled,       TR_KEY_script_torrent_done_filename,
    };

    for (auto const key : InitKeys)
    {
        refreshPref(key);
    }

    // if it's a remote session, disable the preferences
    // that don't work in remote sessions
    if (!is_server_)
    {
        for (QWidget* const w : unsupported_when_remote_)
        {
            w->setToolTip(tr("Not supported by remote sessions"));
            w->setEnabled(false);
        }
    }

    adjustSize();
}

// ---

void PrefsDialog::sessionUpdated()
{
    if (bool const is_local = session_.isLocal(); is_local_ != is_local)
    {
        is_local_ = is_local;
        updateDownloadingWidgetsLocality();
        updateSeedingWidgetsLocality();
    }

    updateBlocklistLabel();
}

void PrefsDialog::updateBlocklistLabel()
{
    ui_.blocklistStatusLabel->setText(
        tr("<i>Blocklist contains %Ln rule(s)</i>", nullptr, static_cast<int>(session_.blocklistSize())));
}

void PrefsDialog::refreshPref(tr_quark key)
{
    switch (key)
    {
    case TR_KEY_rpc_enabled:
    case TR_KEY_rpc_whitelist_enabled:
    case TR_KEY_rpc_authentication_required:
        {
            bool const enabled(prefs_.get<bool>(TR_KEY_rpc_enabled));
            bool const whitelist(prefs_.get<bool>(TR_KEY_rpc_whitelist_enabled));
            bool const auth(prefs_.get<bool>(TR_KEY_rpc_authentication_required));

            for (auto* const w : web_whitelist_widgets_)
            {
                w->setEnabled(enabled && whitelist);
            }

            for (auto* const w : web_auth_widgets_)
            {
                w->setEnabled(enabled && auth);
            }

            for (auto* const w : web_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case TR_KEY_alt_speed_time_enabled:
        {
            bool const enabled = prefs_.get<bool>(key);

            for (auto* const w : sched_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case TR_KEY_blocklist_enabled:
        {
            bool const enabled = prefs_.get<bool>(key);

            for (auto* const w : block_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case TR_KEY_peer_port:
        port_test_status_[Session::PORT_TEST_IPV4] = PortTestStatus::Unknown;
        port_test_status_[Session::PORT_TEST_IPV6] = PortTestStatus::Unknown;
        updatePortStatusLabel();
        portTestSetEnabled();
        break;

    default:
        break;
    }

    for (auto [iter, end] = updaters_.equal_range(key); iter != end; ++iter)
    {
        iter->second();
    }
}
