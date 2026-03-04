// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <utility>

#include "BaseDialog.h"
#include "Prefs.h"
#include "Session.h"
#include "ui_PrefsDialog.h"

class QHttp;
class QMessageBox;
class QString;

class PrefsDialog : public BaseDialog
{
    Q_OBJECT

public:
    PrefsDialog(Session& session, Prefs& prefs, QWidget* parent = nullptr);
    ~PrefsDialog() override = default;
    PrefsDialog& operator=(PrefsDialog&&) = delete;
    PrefsDialog& operator=(PrefsDialog const&) = delete;
    PrefsDialog(PrefsDialog&&) = delete;
    PrefsDialog(PrefsDialog const&) = delete;

private slots:
    void refreshPref(int key);
    void sessionUpdated();
    void onPortTested(std::optional<bool> result, Session::PortTestIpProtocol ip_protocol);
    void onPortTest();
    void onIdleLimitChanged();
    void onQueueStalledMinutesChanged();

    void onUpdateBlocklistClicked();
    void onUpdateBlocklistCancelled();
    void onBlocklistDialogDestroyed(QObject* o);
    void onBlocklistUpdated(int n);

private:
    enum class PortTestStatus : uint8_t
    {
        Unknown = 0U,
        Checking,
        Open,
        Closed,
        Error
    };

    template<typename T>
    void set(int const key, T const& val)
    {
        prefs_.set(key, val);
        refreshPref(key);
    }

    void portTestSetEnabled();
    void updateBlocklistLabel();
    void updateDownloadingWidgetsLocality();
    void updatePortStatusLabel();
    void updateSeedingWidgetsLocality();
    static QString getPortStatusText(PortTestStatus status) noexcept;

    template<typename T, size_t N>
    void initComboFromItems(std::array<std::pair<QString, T>, N> const& items, QComboBox* w, int key);

    void initAltSpeedDaysCombo(QComboBox* w, int key);
    void initEncryptionCombo(QComboBox* w, int key);
    void initWidget(FreeSpaceLabel* w, int key);
    void initWidget(PathButton* w, int key);
    void initWidget(QCheckBox* w, int key);
    void initWidget(QDoubleSpinBox* w, int key);
    void initWidget(QLineEdit* w, int key);
    void initWidget(QPlainTextEdit* w, int key);
    void initWidget(QSpinBox* w, int key);
    void initWidget(QTimeEdit* w, int key);

    void initDownloadingTab();
    void initSeedingTab();
    void initSpeedTab();
    void initPrivacyTab();
    void initNetworkTab();
    void initDesktopTab();
    void initRemoteTab();

    Session& session_;
    Prefs& prefs_;

    Ui::PrefsDialog ui_ = {};

    bool const is_server_;
    bool is_local_ = {};
    std::array<PortTestStatus, Session::NUM_PORT_TEST_IP_PROTOCOL> port_test_status_ = {};

    std::multimap<int, std::function<void()>> updaters_;

    QWidgetList web_widgets_;
    QWidgetList web_auth_widgets_;
    QWidgetList web_whitelist_widgets_;
    QWidgetList proxy_widgets_;
    QWidgetList proxy_auth_widgets_;
    QWidgetList sched_widgets_;
    QWidgetList block_widgets_;
    QWidgetList unsupported_when_remote_;

    int blocklist_http_tag_ = {};
    QHttp* blocklist_http_ = {};
    QMessageBox* blocklist_dialog_ = {};
};
