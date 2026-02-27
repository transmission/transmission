// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstddef>
#include <ctime>
#include <memory>

#include <QMainWindow>
#include <QNetworkReply>
#include <QPointer>
#include <QStringList>
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

class AboutDialog;
class AddData;
class DetailsDialog;
class ListViewProxyStyle;
class Prefs;
class PrefsDialog;
class Session;
class SessionDialog;
class StatsDialog;
class TorrentDelegate;
class TorrentDelegateMin;
class TorrentModel;
struct tr_variant;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(Session& session, Prefs& prefs, TorrentModel& model, bool minimized);
    ~MainWindow() override = default;
    MainWindow(MainWindow&&) = delete;
    MainWindow(MainWindow const&) = delete;
    MainWindow& operator=(MainWindow&&) = delete;
    MainWindow& operator=(MainWindow const&) = delete;

    [[nodiscard]] constexpr QSystemTrayIcon& tray_icon() noexcept
    {
        return tray_icon_;
    }

public slots:
    void start_all();
    void start_selected();
    void start_selected_now();
    void pause_all();
    void pause_selected();
    void remove_selected();
    void delete_selected();
    void verify_selected();
    void queue_move_top();
    void queue_move_up();
    void queue_move_down();
    void queue_move_bottom();
    void reannounce_selected();

    void set_toolbar_visible(bool visible);
    void set_filterbar_visible(bool visible);
    void set_statusbar_visible(bool visible);
    void set_compact_view(bool visible);
    void wrong_authentication();

    void open_session();

protected:
    // QWidget
    void contextMenuEvent(QContextMenuEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool event(QEvent* e) override;

private slots:
    void add_torrents(QStringList const& filenames);
    void copy_magnet_link_to_clipboard();
    void data_read_progress();
    void data_send_progress();
    void new_torrent();
    void on_network_response(QNetworkReply::NetworkError code, QString const& message);
    void on_refresh_timer();
    void on_session_source_changed();
    void on_set_prefs();
    void on_set_prefs(bool is_checked);
    void on_sort_mode_changed(QAction const* action);
    void on_stats_mode_changed(QAction const* action);
    void open_about();
    void open_donate() const;
    void open_folder();
    void open_help() const;
    void open_preferences();
    void open_properties();
    void open_stats();
    void open_torrent();
    void open_url();
    void refresh_pref(int idx);
    void remove_torrents(bool delete_files);
    void set_location();
    void set_sort_ascending_pref(bool b);
    void toggle_speed_mode();
    void toggle_windows(bool do_show);
    void tray_activated(QSystemTrayIcon::ActivationReason reason);

private:
    [[nodiscard]] torrent_ids_t get_selected_torrents(bool with_metadata_only = false) const;
    void update_network_label();
    void refresh_soon(int fields);

    QMenu* create_options_menu();
    QMenu* create_stats_mode_menu();
    void init_status_bar();

    void clear_selection();
    void add_torrent_from_clipboard();
    void add_torrent(AddData const& add_me, bool show_options);

    // QWidget
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

    Session& session_;
    Prefs& prefs_;
    TorrentModel& model_;

    std::shared_ptr<ListViewProxyStyle> lvp_style_;

    QPixmap pixmap_network_error_;
    QPixmap pixmap_network_idle_;
    QPixmap pixmap_network_receive_;
    QPixmap pixmap_network_transmit_;
    QPixmap pixmap_network_transmit_receive_;

    Ui_MainWindow ui_ = {};

    time_t last_full_update_time_ = {};
    QPointer<SessionDialog> session_dialog_;
    QPointer<PrefsDialog> prefs_dialog_;
    QPointer<AboutDialog> about_dialog_;
    QPointer<StatsDialog> stats_dialog_;
    QPointer<DetailsDialog> details_dialog_;
    QSystemTrayIcon tray_icon_;
    TorrentFilter filter_model_;
    TorrentDelegate* torrent_delegate_ = {};
    TorrentDelegateMin* torrent_delegate_min_ = {};
    time_t last_send_time_ = {};
    time_t last_read_time_ = {};
    QTimer network_timer_;
    bool network_error_ = {};
    QAction* dlimit_off_action_ = {};
    QAction* dlimit_on_action_ = {};
    QAction* ulimit_off_action_ = {};
    QAction* ulimit_on_action_ = {};
    QAction* ratio_off_action_ = {};
    QAction* ratio_on_action_ = {};
    QWidgetList hidden_;
    QWidget* filter_bar_ = {};
    QAction* alt_speed_action_ = {};
    QString error_message_;
    bool auto_add_clipboard_links_ = {};
    QStringList clipboard_processed_keys_;

    QString const show_options_checkbox_name_ = QStringLiteral("show-options-checkbox");

    struct TransferStats
    {
        Speed speed_up;
        Speed speed_down;
        size_t peers_sending = 0;
        size_t peers_receiving = 0;
    };
    [[nodiscard]] TransferStats get_transfer_stats() const;

    static constexpr auto RefreshTitle = 1 << 0;
    static constexpr auto RefreshStatusBar = 1 << 1;
    static constexpr auto RefreshTrayIcon = 1 << 2;
    static constexpr auto RefreshTorrentViewHeader = 1 << 3;
    static constexpr auto RefreshActionSensitivity = 1 << 4;
    static constexpr auto RefreshIcon = 1 << 5;
    int refresh_fields_ = {};
    QTimer refresh_timer_;

    void refresh_action_sensitivity();
    void refresh_icons();
    void refresh_status_bar(TransferStats const& stats);
    void refresh_title();
    void refresh_torrent_view_header();
    void refresh_tray_icon(TransferStats const& stats);
};
