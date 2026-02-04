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
    void refresh_pref(int key);
    void session_updated();
    void on_port_tested(std::optional<bool> result, Session::PortTestIpProtocol ip_protocol);
    void on_port_test();
    void on_idle_limit_changed();
    void on_queue_stalled_minutes_changed();

    void on_update_blocklist_clicked();
    void on_update_blocklist_cancelled();
    void on_blocklist_dialog_destroyed(QObject* o);
    void on_blocklist_updated(int n);

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
        refresh_pref(key);
    }

    void port_test_set_enabled();
    void update_blocklist_label();
    void update_downloading_widgets_locality();
    void update_port_status_label();
    void update_seeding_widgets_locality();
    static QString get_port_status_text(PortTestStatus status) noexcept;

    template<typename T, size_t N>
    void init_combo_from_items(std::array<std::pair<QString, T>, N> const& items, QComboBox* w, int key);

    void init_alt_speed_days_combo(QComboBox* w, int key);
    void init_encryption_combo(QComboBox* w, int key);
    void init_widget(FreeSpaceLabel* w, int key);
    void init_widget(PathButton* w, int key);
    void init_widget(QCheckBox* w, int key);
    void init_widget(QDoubleSpinBox* w, int key);
    void init_widget(QLineEdit* w, int key);
    void init_widget(QPlainTextEdit* w, int key);
    void init_widget(QSpinBox* w, int key);
    void init_widget(QTimeEdit* w, int key);

    void init_downloading_tab();
    void init_seeding_tab();
    void init_speed_tab();
    void init_privacy_tab();
    void init_network_tab();
    void init_desktop_tab();
    void init_remote_tab();

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
