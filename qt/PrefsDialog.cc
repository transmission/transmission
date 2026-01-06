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
#include "CustomVariantType.h"
#include "Formatter.h"
#include "FreeSpaceLabel.h"
#include "Prefs.h"
#include "Session.h"
#include "Utils.h"

using namespace libtransmission;

// ---

void PrefsDialog::linkAltSpeedDaysComboToPref(QComboBox* const w, int const key)
{
    static auto items = []
    {
        auto ret = std::array<std::pair<int, QString>, 10U>{};
        auto idx = 0;
        ret[idx++] = { TR_SCHED_ALL, tr("Every Day") };
        ret[idx++] = { TR_SCHED_WEEKDAY, tr("Weekdays") };
        ret[idx++] = { TR_SCHED_WEEKEND, tr("Weekends") };

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
            ret[idx++] = { qt_day_to_tr_day.at(i), locale.dayName(i) };
        }
        for (int i = Qt::Monday; i < first_day_of_week; ++i)
        {
            ret[idx++] = { qt_day_to_tr_day.at(i), locale.dayName(i) };
        }
        return ret;
    }();

    for (auto const& [day, label] : items)
    {
        w->addItem(label);
    }

    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        auto const val = prefs_.get<int>(key);
        for (size_t i = 0; i < std::size(items); ++i)
        {
            if (items[i].first == val)
            {
                w->setCurrentIndex(i);
            }
        }
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    auto on_activated = [this, key](int const idx)
    {
        if (0 <= idx && idx < static_cast<int>(std::size(items)))
        {
            set(key, items[idx].first);
        }
    };
    connect(w, &QComboBox::activated, std::move(on_activated));
}

void PrefsDialog::linkEncryptionComboToPref(QComboBox* const w, int const key)
{
    static auto const Items = std::array<std::pair<int, QString>, 3U>{ {
        { TR_CLEAR_PREFERRED, tr("Allow encryption") },
        { TR_ENCRYPTION_PREFERRED, tr("Prefer encryption") },
        { TR_ENCRYPTION_REQUIRED, tr("Require encryption") },
    } };

    for (auto const& [mode, label] : Items)
    {
        w->addItem(label);
    }

    auto updater = [this, key, w]()
    {
        auto const blocker = QSignalBlocker{ w };
        auto const val = prefs_.get<int>(key);
        for (size_t i = 0; i < std::size(Items); ++i)
        {
            if (Items[i].first == val)
            {
                w->setCurrentIndex(i);
            }
        }
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    auto const on_activated = [this, key](int const idx)
    {
        if (0 <= idx && idx < static_cast<int>(std::size(Items)))
        {
            set(key, Items[idx].first);
        }
    };

    connect(w, &QComboBox::activated, std::move(on_activated));
}

void PrefsDialog::linkWidgetToPref(QPlainTextEdit* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
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

void PrefsDialog::linkWidgetToPref(FreeSpaceLabel* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
        w->setPath(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));
}

void PrefsDialog::linkWidgetToPref(PathButton* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
        w->setPath(prefs_.get<QString>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &PathButton::pathChanged, [this, key](QString const& val) { set(key, val); });
}

void PrefsDialog::linkWidgetToPref(QCheckBox* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
        w->setChecked(prefs_.get<bool>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &QAbstractButton::toggled, [this, key](bool const val) { set(key, val); });
}

void PrefsDialog::linkWidgetToPref(QDoubleSpinBox* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
        w->setValue(prefs_.get<double>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &QDoubleSpinBox::valueChanged, [this, key](double const val) { set(key, val); });
}

void PrefsDialog::linkWidgetToPref(QLineEdit* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
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

void PrefsDialog::linkWidgetToPref(QSpinBox* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
        w->setValue(prefs_.get<int>(key));
    };
    updater();
    updaters_.emplace(key, std::move(updater));

    connect(w, &QSpinBox::valueChanged, [this, key](int const val) { set(key, val); });
}

void PrefsDialog::linkWidgetToPref(QTimeEdit* const w, int const key)
{
    auto updater = [this, key, w]()
    {
        auto blocker = QSignalBlocker{ w };
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
    linkWidgetToPref(ui_.enableRpcCheck, Prefs::RPC_ENABLED);
    linkWidgetToPref(ui_.rpcPortSpin, Prefs::RPC_PORT);
    linkWidgetToPref(ui_.requireRpcAuthCheck, Prefs::RPC_AUTH_REQUIRED);
    linkWidgetToPref(ui_.rpcUsernameEdit, Prefs::RPC_USERNAME);
    linkWidgetToPref(ui_.rpcPasswordEdit, Prefs::RPC_PASSWORD);
    linkWidgetToPref(ui_.enableRpcWhitelistCheck, Prefs::RPC_WHITELIST_ENABLED);
    linkWidgetToPref(ui_.rpcWhitelistEdit, Prefs::RPC_WHITELIST);

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

    linkWidgetToPref(ui_.uploadSpeedLimitCheck, Prefs::USPEED_ENABLED);
    linkWidgetToPref(ui_.uploadSpeedLimitSpin, Prefs::USPEED);
    linkWidgetToPref(ui_.downloadSpeedLimitCheck, Prefs::DSPEED_ENABLED);
    linkWidgetToPref(ui_.downloadSpeedLimitSpin, Prefs::DSPEED);
    linkWidgetToPref(ui_.altUploadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_UP);
    linkWidgetToPref(ui_.altDownloadSpeedLimitSpin, Prefs::ALT_SPEED_LIMIT_DOWN);
    linkWidgetToPref(ui_.altSpeedLimitScheduleCheck, Prefs::ALT_SPEED_LIMIT_TIME_ENABLED);
    linkWidgetToPref(ui_.altSpeedLimitStartTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_BEGIN);
    linkWidgetToPref(ui_.altSpeedLimitEndTimeEdit, Prefs::ALT_SPEED_LIMIT_TIME_END);
    linkAltSpeedDaysComboToPref(ui_.altSpeedLimitDaysCombo, Prefs::ALT_SPEED_LIMIT_TIME_DAY);

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
    linkWidgetToPref(ui_.showTrayIconCheck, Prefs::SHOW_TRAY_ICON);
    linkWidgetToPref(ui_.startMinimizedCheck, Prefs::START_MINIMIZED);
    linkWidgetToPref(ui_.notifyOnTorrentAddedCheck, Prefs::SHOW_NOTIFICATION_ON_ADD);
    linkWidgetToPref(ui_.notifyOnTorrentCompletedCheck, Prefs::SHOW_NOTIFICATION_ON_COMPLETE);
    linkWidgetToPref(ui_.playSoundOnTorrentCompletedCheck, Prefs::COMPLETE_SOUND_ENABLED);
}

// ---

QString PrefsDialog::getPortStatusText(PrefsDialog::PortTestStatus status) noexcept
{
    switch (status)
    {
    case PORT_TEST_UNKNOWN:
        return tr("unknown");
    case PORT_TEST_CHECKING:
        return tr("checking…");
    case PORT_TEST_OPEN:
        return tr("open");
    case PORT_TEST_CLOSED:
        return tr("closed");
    case PORT_TEST_ERROR:
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
            return PORT_TEST_ERROR;
        }
        if (!*res)
        {
            return PORT_TEST_CLOSED;
        }
        return PORT_TEST_OPEN;
    };

    // Only update the UI if the current status is "checking", so that
    // we won't show the port test results for the old peer port if it
    // changed while we have port_test RPC call(s) in-flight.
    if (port_test_status_[ip_protocol] == PORT_TEST_CHECKING)
    {
        port_test_status_[ip_protocol] = StatusFromResult(result);
        updatePortStatusLabel();
    }
    portTestSetEnabled();
}

void PrefsDialog::onPortTest()
{
    port_test_status_[Session::PORT_TEST_IPV4] = PORT_TEST_CHECKING;
    port_test_status_[Session::PORT_TEST_IPV6] = PORT_TEST_CHECKING;
    updatePortStatusLabel();

    session_.portTest(Session::PORT_TEST_IPV4);
    session_.portTest(Session::PORT_TEST_IPV6);

    portTestSetEnabled();
}

void PrefsDialog::initNetworkTab()
{
    ui_.torrentPeerLimitSpin->setRange(1, INT_MAX);
    ui_.globalPeerLimitSpin->setRange(1, INT_MAX);

    linkWidgetToPref(ui_.peerPortSpin, Prefs::PEER_PORT);
    linkWidgetToPref(ui_.randomPeerPortCheck, Prefs::PEER_PORT_RANDOM_ON_START);
    linkWidgetToPref(ui_.enablePortForwardingCheck, Prefs::PORT_FORWARDING);
    linkWidgetToPref(ui_.torrentPeerLimitSpin, Prefs::PEER_LIMIT_TORRENT);
    linkWidgetToPref(ui_.globalPeerLimitSpin, Prefs::PEER_LIMIT_GLOBAL);
    linkWidgetToPref(ui_.enableUtpCheck, Prefs::UTP_ENABLED);
    linkWidgetToPref(ui_.enablePexCheck, Prefs::PEX_ENABLED);
    linkWidgetToPref(ui_.enableDhtCheck, Prefs::DHT_ENABLED);
    linkWidgetToPref(ui_.enableLpdCheck, Prefs::LPD_ENABLED);
    linkWidgetToPref(ui_.defaultTrackersPlainTextEdit, Prefs::DEFAULT_TRACKERS);

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
    linkEncryptionComboToPref(ui_.encryptionModeCombo, Prefs::ENCRYPTION);
    linkWidgetToPref(ui_.blocklistCheck, Prefs::BLOCKLIST_ENABLED);
    linkWidgetToPref(ui_.blocklistEdit, Prefs::BLOCKLIST_URL);
    linkWidgetToPref(ui_.autoUpdateBlocklistCheck, Prefs::BLOCKLIST_UPDATES_ENABLED);

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

    linkWidgetToPref(ui_.ratioLimitCheck, Prefs::RATIO_ENABLED);
    linkWidgetToPref(ui_.ratioLimitSpin, Prefs::RATIO);
    linkWidgetToPref(ui_.idleLimitCheck, Prefs::IDLE_LIMIT_ENABLED);
    linkWidgetToPref(ui_.idleLimitSpin, Prefs::IDLE_LIMIT);
    linkWidgetToPref(ui_.doneSeedingScriptCheck, Prefs::SCRIPT_TORRENT_DONE_SEEDING_ENABLED);
    linkWidgetToPref(ui_.doneSeedingScriptButton, Prefs::SCRIPT_TORRENT_DONE_SEEDING_FILENAME);
    linkWidgetToPref(ui_.doneSeedingScriptEdit, Prefs::SCRIPT_TORRENT_DONE_SEEDING_FILENAME);

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

    linkWidgetToPref(ui_.watchDirCheck, Prefs::DIR_WATCH_ENABLED);
    linkWidgetToPref(ui_.watchDirButton, Prefs::DIR_WATCH);
    linkWidgetToPref(ui_.watchDirEdit, Prefs::DIR_WATCH);
    linkWidgetToPref(ui_.showTorrentOptionsDialogCheck, Prefs::OPTIONS_PROMPT);
    linkWidgetToPref(ui_.startAddedTorrentsCheck, Prefs::START);
    linkWidgetToPref(ui_.detectTorrentFromClipboard, Prefs::READ_CLIPBOARD);
    linkWidgetToPref(ui_.trashTorrentFileCheck, Prefs::TRASH_ORIGINAL);
    linkWidgetToPref(ui_.downloadDirButton, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadDirEdit, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadDirFreeSpaceLabel, Prefs::DOWNLOAD_DIR);
    linkWidgetToPref(ui_.downloadQueueSizeSpin, Prefs::DOWNLOAD_QUEUE_SIZE);
    linkWidgetToPref(ui_.queueStalledMinutesSpin, Prefs::QUEUE_STALLED_MINUTES);
    linkWidgetToPref(ui_.renamePartialFilesCheck, Prefs::RENAME_PARTIAL_FILES);
    linkWidgetToPref(ui_.incompleteDirCheck, Prefs::INCOMPLETE_DIR_ENABLED);
    linkWidgetToPref(ui_.incompleteDirButton, Prefs::INCOMPLETE_DIR);
    linkWidgetToPref(ui_.incompleteDirEdit, Prefs::INCOMPLETE_DIR);
    linkWidgetToPref(ui_.doneDownloadingScriptCheck, Prefs::SCRIPT_TORRENT_DONE_ENABLED);
    linkWidgetToPref(ui_.doneDownloadingScriptButton, Prefs::SCRIPT_TORRENT_DONE_FILENAME);
    linkWidgetToPref(ui_.doneDownloadingScriptEdit, Prefs::SCRIPT_TORRENT_DONE_FILENAME);

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
        port_test_status_[Session::PORT_TEST_IPV4] = PORT_TEST_UNKNOWN;
        port_test_status_[Session::PORT_TEST_IPV6] = PORT_TEST_UNKNOWN;
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
