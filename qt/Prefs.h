// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cassert>
#include <cstdint>
#include <tuple>
#include <utility>

#include <QDateTime>
#include <QDir>
#include <QObject>
#include <QString>
#include <QStringList>
#include <libtransmission/constants.h>
#include <libtransmission/quark.h>
#include <libtransmission/transmission.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/display-modes.h>

#include "UserMetaType.h"

class Prefs : public QObject
{
    Q_OBJECT

public:
    Prefs() = default;
    explicit Prefs(tr_variant const& settings);
    explicit Prefs(QString const& dir);
    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() override = default;

    [[nodiscard]] static bool isCore(tr_quark key);

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(tr_quark key) const
    {
        return keyvalImpl(key);
    }

    void set(tr_quark key, tr_variant const& value)
    {
        auto const& pref = item(key);

        switch (pref.type)
        {
        case QMetaType::Bool:
            if (auto const tmp = tr::serializer::to_value<bool>(value))
            {
                set(key, *tmp);
            }
            break;

        case QMetaType::Int:
            if (auto const tmp = tr::serializer::to_value<int64_t>(value))
            {
                set(key, static_cast<int>(*tmp));
            }
            break;

        case UserMetaType::EncryptionModeType:
            if (auto const tmp = tr::serializer::to_value<tr_encryption_mode>(value))
            {
                set(key, *tmp);
            }
            break;

        case UserMetaType::SortModeType:
            if (auto const tmp = tr::serializer::to_value<SortMode>(value))
            {
                set(key, *tmp);
            }
            break;

        case UserMetaType::StatsModeType:
            if (auto const tmp = tr::serializer::to_value<StatsMode>(value))
            {
                set(key, *tmp);
            }
            break;

        case UserMetaType::ShowModeType:
            if (auto const tmp = tr::serializer::to_value<ShowMode>(value))
            {
                set(key, *tmp);
            }
            break;

        case QMetaType::QString:
            if (auto const tmp = tr::serializer::to_value<QString>(value))
            {
                set(key, *tmp);
            }
            break;

        case QMetaType::QStringList:
            if (auto const tmp = tr::serializer::to_value<QStringList>(value))
            {
                set(key, *tmp);
            }
            break;

        case QMetaType::Double:
            if (auto const tmp = tr::serializer::to_value<double>(value))
            {
                set(key, *tmp);
            }
            break;

        case QMetaType::QDateTime:
            if (auto const tmp = tr::serializer::to_value<QDateTime>(value))
            {
                set(key, *tmp);
            }
            break;

        default:
            assert(false && "unhandled type");
            break;
        }
    }

    template<typename T>
    void set(tr_quark key, T const& value)
    {
        if (tr::serializer::set(*this, item(key).key, value))
        {
            emit changed(key);
        }
    }

    void set(tr_quark /*key*/, char const* /*value*/) = delete;

    template<typename T>
    [[nodiscard]] T get(tr_quark key) const
    {
        auto const val = tr::serializer::get<T>(*this, item(key).key);
        assert(val.has_value());
        return val.value_or(T{});
    }

    [[nodiscard]] tr_variant::Map current_settings() const;
    void save(QString const& filename) const;

signals:
    void changed(tr_quark key);

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    bool options_prompt_ = true;
    QString open_dialog_folder_ = QDir::home().absolutePath();
    bool inhibit_hibernation_ = false;
    QString dir_watch_ = QString::fromStdString(tr_getDefaultDownloadDir());
    bool dir_watch_enabled_ = false;
    bool show_tray_icon_ = false;
    bool start_minimized_ = false;
    bool show_notification_on_add_ = true;
    bool show_notification_on_complete_ = true;
    bool askquit_ = true;
    SortMode sort_mode_ = DefaultSortMode;
    bool sort_reversed_ = false;
    bool compact_view_ = false;
    bool filterbar_ = true;
    bool statusbar_ = true;
    StatsMode statusbar_stats_ = DefaultStatsMode;
    bool show_tracker_scrapes_ = false;
    bool show_backup_trackers_ = false;
    bool toolbar_ = true;
    QDateTime blocklist_date_ = QDateTime::fromSecsSinceEpoch(0);
    bool blocklist_updates_enabled_ = true;
    QString main_window_layout_order_ = QStringLiteral("menu,toolbar,filter,list,statusbar");
    int main_window_height_ = 500;
    int main_window_width_ = 600;
    int main_window_x_ = 50;
    int main_window_y_ = 50;
    ShowMode filter_mode_ = DefaultShowMode;
    QString filter_trackers_;
    QString filter_text_;
    bool session_is_remote_ = false;
    QString session_remote_host_ = QStringLiteral("localhost");
    bool session_remote_https_ = false;
    QString session_remote_password_;
    int session_remote_port_ = static_cast<int>(TrDefaultRpcPort);
    bool session_remote_auth_ = false;
    QString session_remote_username_;
    QString session_remote_url_base_path_ = QStringLiteral("/transmission/");
    QStringList complete_sound_command_;
    bool complete_sound_enabled_ = true;
    bool read_clipboard_ = false;
    int alt_speed_limit_up_ = 0;
    int alt_speed_limit_down_ = 0;
    bool alt_speed_limit_enabled_ = false;
    int alt_speed_limit_time_begin_ = 0;
    int alt_speed_limit_time_end_ = 0;
    bool alt_speed_limit_time_enabled_ = false;
    int alt_speed_limit_time_day_ = 0;
    bool blocklist_enabled_ = false;
    QString blocklist_url_;
    QString default_trackers_;
    int dspeed_ = 0;
    bool dspeed_enabled_ = false;
    QString download_dir_ = QString::fromStdString(tr_getDefaultDownloadDir());
    bool download_queue_enabled_ = false;
    int download_queue_size_ = 0;
    tr_encryption_mode encryption_ = {};
    int idle_limit_ = 0;
    bool idle_limit_enabled_ = false;
    QString incomplete_dir_;
    bool incomplete_dir_enabled_ = false;
    int msglevel_ = 0;
    int peer_limit_global_ = 0;
    int peer_limit_torrent_ = 0;
    int peer_port_ = 0;
    bool peer_port_random_on_start_ = false;
    int peer_port_random_low_ = 0;
    int peer_port_random_high_ = 0;
    int queue_stalled_minutes_ = 0;
    bool script_torrent_done_enabled_ = false;
    QString script_torrent_done_filename_;
    bool script_torrent_done_seeding_enabled_ = false;
    QString script_torrent_done_seeding_filename_;
    QString socket_diffserv_;
    bool start_ = false;
    bool trash_original_ = false;
    bool pex_enabled_ = false;
    bool dht_enabled_ = false;
    bool utp_enabled_ = false;
    bool lpd_enabled_ = false;
    bool port_forwarding_ = false;
    int preallocation_ = 0;
    double ratio_ = 0.0;
    bool ratio_enabled_ = false;
    bool rename_partial_files_ = false;
    bool rpc_auth_required_ = false;
    bool rpc_enabled_ = false;
    QString rpc_password_;
    int rpc_port_ = 0;
    QString rpc_username_;
    bool rpc_whitelist_enabled_ = false;
    QString rpc_whitelist_;
    bool uspeed_enabled_ = false;
    int uspeed_ = 0;
    int upload_slots_per_torrent_ = 0;

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&Prefs::options_prompt_>{ TR_KEY_show_options_window },
        Field<&Prefs::open_dialog_folder_>{ TR_KEY_open_dialog_dir },
        Field<&Prefs::inhibit_hibernation_>{ TR_KEY_inhibit_desktop_hibernation },
        Field<&Prefs::dir_watch_>{ TR_KEY_watch_dir },
        Field<&Prefs::dir_watch_enabled_>{ TR_KEY_watch_dir_enabled },
        Field<&Prefs::show_tray_icon_>{ TR_KEY_show_notification_area_icon },
        Field<&Prefs::start_minimized_>{ TR_KEY_start_minimized },
        Field<&Prefs::show_notification_on_add_>{ TR_KEY_torrent_added_notification_enabled },
        Field<&Prefs::show_notification_on_complete_>{ TR_KEY_torrent_complete_notification_enabled },
        Field<&Prefs::askquit_>{ TR_KEY_prompt_before_exit },
        Field<&Prefs::sort_mode_>{ TR_KEY_sort_mode },
        Field<&Prefs::sort_reversed_>{ TR_KEY_sort_reversed },
        Field<&Prefs::compact_view_>{ TR_KEY_compact_view },
        Field<&Prefs::filterbar_>{ TR_KEY_show_filterbar },
        Field<&Prefs::statusbar_>{ TR_KEY_show_statusbar },
        Field<&Prefs::statusbar_stats_>{ TR_KEY_statusbar_stats },
        Field<&Prefs::show_tracker_scrapes_>{ TR_KEY_show_tracker_scrapes },
        Field<&Prefs::show_backup_trackers_>{ TR_KEY_show_backup_trackers },
        Field<&Prefs::toolbar_>{ TR_KEY_show_toolbar },
        Field<&Prefs::blocklist_date_>{ TR_KEY_blocklist_date },
        Field<&Prefs::blocklist_updates_enabled_>{ TR_KEY_blocklist_updates_enabled },
        Field<&Prefs::main_window_layout_order_>{ TR_KEY_main_window_layout_order },
        Field<&Prefs::main_window_height_>{ TR_KEY_main_window_height },
        Field<&Prefs::main_window_width_>{ TR_KEY_main_window_width },
        Field<&Prefs::main_window_x_>{ TR_KEY_main_window_x },
        Field<&Prefs::main_window_y_>{ TR_KEY_main_window_y },
        Field<&Prefs::filter_mode_>{ TR_KEY_filter_mode },
        Field<&Prefs::filter_trackers_>{ TR_KEY_filter_trackers },
        Field<&Prefs::filter_text_>{ TR_KEY_filter_text },
        Field<&Prefs::session_is_remote_>{ TR_KEY_remote_session_enabled },
        Field<&Prefs::session_remote_host_>{ TR_KEY_remote_session_host },
        Field<&Prefs::session_remote_https_>{ TR_KEY_remote_session_https },
        Field<&Prefs::session_remote_password_>{ TR_KEY_remote_session_password },
        Field<&Prefs::session_remote_port_>{ TR_KEY_remote_session_port },
        Field<&Prefs::session_remote_auth_>{ TR_KEY_remote_session_requires_authentication },
        Field<&Prefs::session_remote_username_>{ TR_KEY_remote_session_username },
        Field<&Prefs::session_remote_url_base_path_>{ TR_KEY_remote_session_url_base_path },
        Field<&Prefs::complete_sound_command_>{ TR_KEY_torrent_complete_sound_command },
        Field<&Prefs::complete_sound_enabled_>{ TR_KEY_torrent_complete_sound_enabled },
        Field<&Prefs::read_clipboard_>{ TR_KEY_read_clipboard },
        Field<&Prefs::alt_speed_limit_up_>{ TR_KEY_alt_speed_up },
        Field<&Prefs::alt_speed_limit_down_>{ TR_KEY_alt_speed_down },
        Field<&Prefs::alt_speed_limit_enabled_>{ TR_KEY_alt_speed_enabled },
        Field<&Prefs::alt_speed_limit_time_begin_>{ TR_KEY_alt_speed_time_begin },
        Field<&Prefs::alt_speed_limit_time_end_>{ TR_KEY_alt_speed_time_end },
        Field<&Prefs::alt_speed_limit_time_enabled_>{ TR_KEY_alt_speed_time_enabled },
        Field<&Prefs::alt_speed_limit_time_day_>{ TR_KEY_alt_speed_time_day },
        Field<&Prefs::blocklist_enabled_>{ TR_KEY_blocklist_enabled },
        Field<&Prefs::blocklist_url_>{ TR_KEY_blocklist_url },
        Field<&Prefs::default_trackers_>{ TR_KEY_default_trackers },
        Field<&Prefs::dspeed_>{ TR_KEY_speed_limit_down },
        Field<&Prefs::dspeed_enabled_>{ TR_KEY_speed_limit_down_enabled },
        Field<&Prefs::download_dir_>{ TR_KEY_download_dir },
        Field<&Prefs::download_queue_enabled_>{ TR_KEY_download_queue_enabled },
        Field<&Prefs::download_queue_size_>{ TR_KEY_download_queue_size },
        Field<&Prefs::encryption_>{ TR_KEY_encryption },
        Field<&Prefs::idle_limit_>{ TR_KEY_idle_seeding_limit },
        Field<&Prefs::idle_limit_enabled_>{ TR_KEY_idle_seeding_limit_enabled },
        Field<&Prefs::incomplete_dir_>{ TR_KEY_incomplete_dir },
        Field<&Prefs::incomplete_dir_enabled_>{ TR_KEY_incomplete_dir_enabled },
        Field<&Prefs::msglevel_>{ TR_KEY_message_level },
        Field<&Prefs::peer_limit_global_>{ TR_KEY_peer_limit_global },
        Field<&Prefs::peer_limit_torrent_>{ TR_KEY_peer_limit_per_torrent },
        Field<&Prefs::peer_port_>{ TR_KEY_peer_port },
        Field<&Prefs::peer_port_random_on_start_>{ TR_KEY_peer_port_random_on_start },
        Field<&Prefs::peer_port_random_low_>{ TR_KEY_peer_port_random_low },
        Field<&Prefs::peer_port_random_high_>{ TR_KEY_peer_port_random_high },
        Field<&Prefs::queue_stalled_minutes_>{ TR_KEY_queue_stalled_minutes },
        Field<&Prefs::script_torrent_done_enabled_>{ TR_KEY_script_torrent_done_enabled },
        Field<&Prefs::script_torrent_done_filename_>{ TR_KEY_script_torrent_done_filename },
        Field<&Prefs::script_torrent_done_seeding_enabled_>{ TR_KEY_script_torrent_done_seeding_enabled },
        Field<&Prefs::script_torrent_done_seeding_filename_>{ TR_KEY_script_torrent_done_seeding_filename },
        Field<&Prefs::socket_diffserv_>{ TR_KEY_peer_socket_diffserv },
        Field<&Prefs::start_>{ TR_KEY_start_added_torrents },
        Field<&Prefs::trash_original_>{ TR_KEY_trash_original_torrent_files },
        Field<&Prefs::pex_enabled_>{ TR_KEY_pex_enabled },
        Field<&Prefs::dht_enabled_>{ TR_KEY_dht_enabled },
        Field<&Prefs::utp_enabled_>{ TR_KEY_utp_enabled },
        Field<&Prefs::lpd_enabled_>{ TR_KEY_lpd_enabled },
        Field<&Prefs::port_forwarding_>{ TR_KEY_port_forwarding_enabled },
        Field<&Prefs::preallocation_>{ TR_KEY_preallocation },
        Field<&Prefs::ratio_>{ TR_KEY_seed_ratio_limit },
        Field<&Prefs::ratio_enabled_>{ TR_KEY_seed_ratio_limited },
        Field<&Prefs::rename_partial_files_>{ TR_KEY_rename_partial_files },
        Field<&Prefs::rpc_auth_required_>{ TR_KEY_rpc_authentication_required },
        Field<&Prefs::rpc_enabled_>{ TR_KEY_rpc_enabled },
        Field<&Prefs::rpc_password_>{ TR_KEY_rpc_password },
        Field<&Prefs::rpc_port_>{ TR_KEY_rpc_port },
        Field<&Prefs::rpc_username_>{ TR_KEY_rpc_username },
        Field<&Prefs::rpc_whitelist_enabled_>{ TR_KEY_rpc_whitelist_enabled },
        Field<&Prefs::rpc_whitelist_>{ TR_KEY_rpc_whitelist },
        Field<&Prefs::uspeed_enabled_>{ TR_KEY_speed_limit_up_enabled },
        Field<&Prefs::uspeed_>{ TR_KEY_speed_limit_up },
        Field<&Prefs::upload_slots_per_torrent_>{ TR_KEY_upload_slots_per_torrent });

private:
    struct PrefItem
    {
        tr_quark key;
        int type;
    };

    static auto constexpr Items = std::array<PrefItem, 94>{ {
        { .key = TR_KEY_show_options_window, .type = QMetaType::Bool },
        { .key = TR_KEY_open_dialog_dir, .type = QMetaType::QString },
        { .key = TR_KEY_inhibit_desktop_hibernation, .type = QMetaType::Bool },
        { .key = TR_KEY_watch_dir, .type = QMetaType::QString },
        { .key = TR_KEY_watch_dir_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_show_notification_area_icon, .type = QMetaType::Bool },
        { .key = TR_KEY_start_minimized, .type = QMetaType::Bool },
        { .key = TR_KEY_torrent_added_notification_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_torrent_complete_notification_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_prompt_before_exit, .type = QMetaType::Bool },
        { .key = TR_KEY_sort_mode, .type = UserMetaType::SortModeType },
        { .key = TR_KEY_sort_reversed, .type = QMetaType::Bool },
        { .key = TR_KEY_compact_view, .type = QMetaType::Bool },
        { .key = TR_KEY_show_filterbar, .type = QMetaType::Bool },
        { .key = TR_KEY_show_statusbar, .type = QMetaType::Bool },
        { .key = TR_KEY_statusbar_stats, .type = UserMetaType::StatsModeType },
        { .key = TR_KEY_show_tracker_scrapes, .type = QMetaType::Bool },
        { .key = TR_KEY_show_backup_trackers, .type = QMetaType::Bool },
        { .key = TR_KEY_show_toolbar, .type = QMetaType::Bool },
        { .key = TR_KEY_blocklist_date, .type = QMetaType::QDateTime },
        { .key = TR_KEY_blocklist_updates_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_main_window_layout_order, .type = QMetaType::QString },
        { .key = TR_KEY_main_window_height, .type = QMetaType::Int },
        { .key = TR_KEY_main_window_width, .type = QMetaType::Int },
        { .key = TR_KEY_main_window_x, .type = QMetaType::Int },
        { .key = TR_KEY_main_window_y, .type = QMetaType::Int },
        { .key = TR_KEY_filter_mode, .type = UserMetaType::ShowModeType },
        { .key = TR_KEY_filter_trackers, .type = QMetaType::QString },
        { .key = TR_KEY_filter_text, .type = QMetaType::QString },
        { .key = TR_KEY_remote_session_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_remote_session_host, .type = QMetaType::QString },
        { .key = TR_KEY_remote_session_https, .type = QMetaType::Bool },
        { .key = TR_KEY_remote_session_password, .type = QMetaType::QString },
        { .key = TR_KEY_remote_session_port, .type = QMetaType::Int },
        { .key = TR_KEY_remote_session_requires_authentication, .type = QMetaType::Bool },
        { .key = TR_KEY_remote_session_username, .type = QMetaType::QString },
        { .key = TR_KEY_remote_session_url_base_path, .type = QMetaType::QString },
        { .key = TR_KEY_torrent_complete_sound_command, .type = QMetaType::QStringList },
        { .key = TR_KEY_torrent_complete_sound_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_read_clipboard, .type = QMetaType::Bool },
        { .key = TR_KEY_alt_speed_up, .type = QMetaType::Int },
        { .key = TR_KEY_alt_speed_down, .type = QMetaType::Int },
        { .key = TR_KEY_alt_speed_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_alt_speed_time_begin, .type = QMetaType::Int },
        { .key = TR_KEY_alt_speed_time_end, .type = QMetaType::Int },
        { .key = TR_KEY_alt_speed_time_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_alt_speed_time_day, .type = QMetaType::Int },
        { .key = TR_KEY_blocklist_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_blocklist_url, .type = QMetaType::QString },
        { .key = TR_KEY_default_trackers, .type = QMetaType::QString },
        { .key = TR_KEY_speed_limit_down, .type = QMetaType::Int },
        { .key = TR_KEY_speed_limit_down_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_download_dir, .type = QMetaType::QString },
        { .key = TR_KEY_download_queue_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_download_queue_size, .type = QMetaType::Int },
        { .key = TR_KEY_encryption, .type = UserMetaType::EncryptionModeType },
        { .key = TR_KEY_idle_seeding_limit, .type = QMetaType::Int },
        { .key = TR_KEY_idle_seeding_limit_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_incomplete_dir, .type = QMetaType::QString },
        { .key = TR_KEY_incomplete_dir_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_message_level, .type = QMetaType::Int },
        { .key = TR_KEY_peer_limit_global, .type = QMetaType::Int },
        { .key = TR_KEY_peer_limit_per_torrent, .type = QMetaType::Int },
        { .key = TR_KEY_peer_port, .type = QMetaType::Int },
        { .key = TR_KEY_peer_port_random_on_start, .type = QMetaType::Bool },
        { .key = TR_KEY_peer_port_random_low, .type = QMetaType::Int },
        { .key = TR_KEY_peer_port_random_high, .type = QMetaType::Int },
        { .key = TR_KEY_queue_stalled_minutes, .type = QMetaType::Int },
        { .key = TR_KEY_script_torrent_done_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_script_torrent_done_filename, .type = QMetaType::QString },
        { .key = TR_KEY_script_torrent_done_seeding_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_script_torrent_done_seeding_filename, .type = QMetaType::QString },
        { .key = TR_KEY_peer_socket_diffserv, .type = QMetaType::QString },
        { .key = TR_KEY_start_added_torrents, .type = QMetaType::Bool },
        { .key = TR_KEY_trash_original_torrent_files, .type = QMetaType::Bool },
        { .key = TR_KEY_pex_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_dht_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_utp_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_lpd_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_port_forwarding_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_preallocation, .type = QMetaType::Int },
        { .key = TR_KEY_seed_ratio_limit, .type = QMetaType::Double },
        { .key = TR_KEY_seed_ratio_limited, .type = QMetaType::Bool },
        { .key = TR_KEY_rename_partial_files, .type = QMetaType::Bool },
        { .key = TR_KEY_rpc_authentication_required, .type = QMetaType::Bool },
        { .key = TR_KEY_rpc_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_rpc_password, .type = QMetaType::QString },
        { .key = TR_KEY_rpc_port, .type = QMetaType::Int },
        { .key = TR_KEY_rpc_username, .type = QMetaType::QString },
        { .key = TR_KEY_rpc_whitelist_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_rpc_whitelist, .type = QMetaType::QString },
        { .key = TR_KEY_speed_limit_up_enabled, .type = QMetaType::Bool },
        { .key = TR_KEY_speed_limit_up, .type = QMetaType::Int },
        { .key = TR_KEY_upload_slots_per_torrent, .type = QMetaType::Int },
    } };

    [[nodiscard]] static PrefItem const& item(tr_quark key);
    [[nodiscard]] std::pair<tr_quark, tr_variant> keyvalImpl(tr_quark key) const;
};
