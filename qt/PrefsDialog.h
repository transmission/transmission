// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <map>
#include <optional>

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
    PrefsDialog(Session&, Prefs&, QWidget* parent = nullptr);
    PrefsDialog& operator=(PrefsDialog&&) = delete;
    PrefsDialog& operator=(PrefsDialog const&) = delete;
    PrefsDialog(PrefsDialog&&) = delete;
    PrefsDialog(PrefsDialog const&) = delete;

private slots:
    void focusChanged(QWidget* old, QWidget* cur);
    void checkBoxToggled(bool checked);
    void spinBoxEditingFinished();
    void timeEditingFinished();
    void lineEditingFinished();
    void pathChanged(QString const& path);
    void refreshPref(int key);
    void encryptionEdited(int);
    void altSpeedDaysEdited(int);
    void sessionUpdated();
    void onPortTested(std::optional<bool>, Session::PortTestIpProtocol);
    void onPortTest();
    void onIdleLimitChanged();
    void onQueueStalledMinutesChanged();

    void onUpdateBlocklistClicked();
    void onUpdateBlocklistCancelled();
    void onBlocklistDialogDestroyed(QObject*);
    void onBlocklistUpdated(int n);

private:
    using key2widget_t = std::map<int, QWidget*>;

    enum PortTestStatus : uint8_t
    {
        PORT_TEST_UNKNOWN = 0U,
        PORT_TEST_CHECKING,
        PORT_TEST_OPEN,
        PORT_TEST_CLOSED,
        PORT_TEST_ERROR
    };

    bool updateWidgetValue(QWidget* widget, int pref_key) const;
    void portTestSetEnabled();
    void linkWidgetToPref(QWidget* widget, int pref_key);
    void updateBlocklistLabel();
    void updateDownloadingWidgetsLocality();
    void updatePortStatusLabel();
    void updateSeedingWidgetsLocality();
    static QString getPortStatusText(PortTestStatus status) noexcept;

    void setPref(int key, QVariant const& v);

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

    key2widget_t widgets_;
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
