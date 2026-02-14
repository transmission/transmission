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

#include "tr/torrent/transmission.h"

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
void PrefsDialog::initComboFromItems(std::array<std::pair<QString, T>, N> const& items, QComboBox* const w, int const key)
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

void PrefsDialog::initAltSpeedDaysCombo(QComboBox* const w, int const key)
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

void PrefsDialog::initEncryptionCombo(QComboBox* const w, int const key)
{
    static auto const Items = std::array<std::pair<QString, tr_encryption_mode>, 3U>{ {
        { tr("Allow encryption"), TR_CLEAR_PREFERRED },
        { tr("Prefer encryption"), TR_ENCRYPTION_PREFERRED },
        { tr("Require encryption"), TR_ENCRYPTION_REQUIRED },
    } };

    initComboFromItems(Items, w, key);
}

void PrefsDialog::initWidget(QPlainTextEdit* const w, int const key)
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

void PrefsDialog::initWidget(FreeSpaceLabel* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        w->setPath(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));
}

void PrefsDialog::initWidget(PathButton* const w, int const key)
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

void PrefsDialog::initWidget(QCheckBox* const w, int const key)
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

void PrefsDialog::initWidget(QDoubleSpinBox* const w, int const key)
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

void PrefsDialog::initWidget(QLineEdit* const w, int const key)
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

void PrefsDialog::initWidget(QSpinBox* const w, int const key)
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

void PrefsDialog::initWidget(QTimeEdit* const w, int const key)
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
    initWidget(ui_.enableRpcCheck, Prefs::RPC_ENABLED);
    initWidget(ui_.rpcPortSpin, Prefs::RPC_PORT);
    initWidget(ui_.requireRpcAuthCheck, Prefs::RPC_AUTH_REQUIRED);
    initWidget(ui_.rpcUsernameEdit, Prefs::RPC_USERNAME);
    initWidget(ui_.rpcPasswordEdit, Prefs::RPC_PASSWORD);
    initWidget(ui_.enableRpcWhitelistCheck, Prefs::RPC_WHITELIST_ENABLED);
    initWidget(ui_.rpcWhitelistEdit, Prefs::RPC_WHITELIST);

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

    initWidget(ui_.uploadSpeedLimitCheck, Prefs::USPEED_ENABLED);
    initWidget(ui_.uploadSpeedLimitSpin, Prefs::USPEED);
    initWidget(ui_.downloadSpeedLimitCheck, Prefs::DSPEED_ENABLED);
    initWidget(ui_.downloadSpeedLimitSpin, Prefs::DSPEED);
    initWidget(ui_.altUploadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_UP);
    initWidget(ui_.altDownloadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_DOWN);
    initWidget(ui_.altSpeedLimitScheduleCheck, Prefs::ALT_SPEED_LIMIT_TIME_ENABLED);
    initWidget(ui_.altSpeedLimitStartTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_BEGIN);
    initWidget(ui_.altSpeedLimitEndTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_END);
    initAltSpeedDaysCombo(ui_.altSpeedLimitDaysCombo, Prefs::ALT_SPEED_LIMIT_TIME_DAY);

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
    initWidget(ui_.showTrayIconCheck, Prefs::SHOW_TRAY_ICON);
    initWidget(ui_.startMinimizedCheck, Prefs::START_MINIMIZED);
    initWidget(ui_.notifyOnTorrentAddedCheck, Prefs::SHOW_NOTIFICATION_ON_ADD);
    initWidget(ui_.notifyOnTorrentCompletedCheck, Prefs::SHOW_NOTIFICATION_ON_COMPLETE);
    initWidget(ui_.playSoundOnTorrentCompletedCheck, Prefs::COMPLETE_SOUND_ENABLED);
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

    initWidget(ui_.peerPortSpin, Prefs::PEER_PORT);
    initWidget(ui_.randomPeerPortCheck, Prefs::PEER_PORT_RANDOM_ON_START);
    initWidget(ui_.enablePortForwardingCheck, Prefs::PORT_FORWARDING);
    initWidget(ui_.torrentPeerLimitSpin, Prefs::PEER_LIMIT_TORRENT);
    initWidget(ui_.globalPeerLimitSpin, Prefs::PEER_LIMIT_GLOBAL);
    initWidget(ui_.enableUtpCheck, Prefs::UTP_ENABLED);
    initWidget(ui_.enablePexCheck, Prefs::PEX_ENABLED);
    initWidget(ui_.enableDhtCheck, Prefs::DHT_ENABLED);
    initWidget(ui_.enableLpdCheck, Prefs::LPD_ENABLED);
    initWidget(ui_.defaultTrackersPlainTextEdit, Prefs::DEFAULT_TRACKERS);

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

void PrefsDialog::onBlocklistUpdated(int n)
{
    blocklist_dialog_->setText(tr("<b>Update succeeded!</b><p>Blocklist now has %Ln rule(s).</p>", nullptr, n));
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
    initEncryptionCombo(ui_.encryptionModeCombo, Prefs::ENCRYPTION);
    initWidget(ui_.blocklistCheck, Prefs::BLOCKLIST_ENABLED);
    initWidget(ui_.blocklistEdit, Prefs::BLOCKLIST_URL);
    initWidget(ui_.autoUpdateBlocklistCheck, Prefs::BLOCKLIST_UPDATES_ENABLED);

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

    initWidget(ui_.ratioLimitCheck, Prefs::RATIO_ENABLED);
    initWidget(ui_.ratioLimitSpin, Prefs::RATIO);
    initWidget(ui_.idleLimitCheck, Prefs::IDLE_LIMIT_ENABLED);
    initWidget(ui_.idleLimitSpin, Prefs::IDLE_LIMIT);
    initWidget(ui_.doneSeedingScriptCheck, Prefs::SCRIPT_TORRENT_DONE_SEEDING_ENABLED);
    initWidget(ui_.doneSeedingScriptButton, Prefs::SCRIPT_TORRENT_DONE_SEEDING_FILENAME);
    initWidget(ui_.doneSeedingScriptEdit, Prefs::SCRIPT_TORRENT_DONE_SEEDING_FILENAME);

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
    ui_.downloadDirFreeSpaceLabel->setPath(prefs_.get<QString>(Prefs::DOWNLOAD_DIR));

    initWidget(ui_.watchDirCheck, Prefs::DIR_WATCH_ENABLED);
    initWidget(ui_.watchDirButton, Prefs::DIR_WATCH);
    initWidget(ui_.watchDirEdit, Prefs::DIR_WATCH);
    initWidget(ui_.showTorrentOptionsDialogCheck, Prefs::OPTIONS_PROMPT);
    initWidget(ui_.startAddedTorrentsCheck, Prefs::START);
    initWidget(ui_.detectTorrentFromClipboard, Prefs::READ_CLIPBOARD);
    initWidget(ui_.trashTorrentFileCheck, Prefs::TRASH_ORIGINAL);
    initWidget(ui_.downloadDirButton, Prefs::DOWNLOAD_DIR);
    initWidget(ui_.downloadDirEdit, Prefs::DOWNLOAD_DIR);
    initWidget(ui_.downloadDirFreeSpaceLabel, Prefs::DOWNLOAD_DIR);
    initWidget(ui_.downloadQueueSizeSpin, Prefs::DOWNLOAD_QUEUE_SIZE);
    initWidget(ui_.queueStalledMinutesSpin, Prefs::QUEUE_STALLED_MINUTES);
    initWidget(ui_.renamePartialFilesCheck, Prefs::RENAME_PARTIAL_FILES);
    initWidget(ui_.incompleteDirCheck, Prefs::INCOMPLETE_DIR_ENABLED);
    initWidget(ui_.incompleteDirButton, Prefs::INCOMPLETE_DIR);
    initWidget(ui_.incompleteDirEdit, Prefs::INCOMPLETE_DIR);
    initWidget(ui_.doneDownloadingScriptCheck, Prefs::SCRIPT_TORRENT_DONE_ENABLED);
    initWidget(ui_.doneDownloadingScriptButton, Prefs::SCRIPT_TORRENT_DONE_FILENAME);
    initWidget(ui_.doneDownloadingScriptEdit, Prefs::SCRIPT_TORRENT_DONE_FILENAME);

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

    static std::array<int, 10> constexpr InitKeys = {
        Prefs::ALT_SPEED_LIMIT_ENABLED,
        Prefs::ALT_SPEED_LIMIT_TIME_ENABLED,
        Prefs::BLOCKLIST_ENABLED,
        Prefs::DIR_WATCH,
        Prefs::DOWNLOAD_DIR,
        Prefs::ENCRYPTION,
        Prefs::INCOMPLETE_DIR,
        Prefs::INCOMPLETE_DIR_ENABLED,
        Prefs::RPC_ENABLED,
        Prefs::SCRIPT_TORRENT_DONE_FILENAME,
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
    int const n = session_.blocklistSize();
    ui_.blocklistStatusLabel->setText(tr("<i>Blocklist contains %Ln rule(s)</i>", nullptr, n));
}

void PrefsDialog::refreshPref(int key)
{
    switch (key)
    {
    case Prefs::RPC_ENABLED:
    case Prefs::RPC_WHITELIST_ENABLED:
    case Prefs::RPC_AUTH_REQUIRED:
        {
            bool const enabled(prefs_.get<bool>(Prefs::RPC_ENABLED));
            bool const whitelist(prefs_.get<bool>(Prefs::RPC_WHITELIST_ENABLED));
            bool const auth(prefs_.get<bool>(Prefs::RPC_AUTH_REQUIRED));

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

    case Prefs::ALT_SPEED_LIMIT_TIME_ENABLED:
        {
            bool const enabled = prefs_.get<bool>(key);

            for (auto* const w : sched_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case Prefs::BLOCKLIST_ENABLED:
        {
            bool const enabled = prefs_.get<bool>(key);

            for (auto* const w : block_widgets_)
            {
                w->setEnabled(enabled);
            }

            break;
        }

    case Prefs::PEER_PORT:
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
