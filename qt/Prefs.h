// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <utility>

#include <QObject>
#include <QString>
#include <QVariant>

#include <libtransmission/quark.h>
#include <libtransmission/variant.h>

#include <libtransmission-app/display-modes.h>

#include "UserMetaType.h"

class Prefs : public QObject
{
    Q_OBJECT

public:
    enum : uint8_t
    {
        /* client prefs */
        OPTIONS_PROMPT,
        OPEN_DIALOG_FOLDER,
        INHIBIT_HIBERNATION,
        DIR_WATCH,
        DIR_WATCH_ENABLED,
        SHOW_TRAY_ICON,
        START_MINIMIZED,
        SHOW_NOTIFICATION_ON_ADD,
        SHOW_NOTIFICATION_ON_COMPLETE,
        ASKQUIT,
        SORT_MODE,
        SORT_REVERSED,
        COMPACT_VIEW,
        FILTERBAR,
        STATUSBAR,
        STATUSBAR_STATS,
        SHOW_TRACKER_SCRAPES,
        SHOW_BACKUP_TRACKERS,
        TOOLBAR,
        BLOCKLIST_DATE,
        BLOCKLIST_UPDATES_ENABLED,
        MAIN_WINDOW_LAYOUT_ORDER,
        MAIN_WINDOW_HEIGHT,
        MAIN_WINDOW_WIDTH,
        MAIN_WINDOW_X,
        MAIN_WINDOW_Y,
        FILTER_MODE,
        FILTER_TRACKERS,
        FILTER_TEXT,
        SESSION_IS_REMOTE,
        SESSION_REMOTE_HOST,
        SESSION_REMOTE_HTTPS,
        SESSION_REMOTE_PASSWORD,
        SESSION_REMOTE_PORT,
        SESSION_REMOTE_AUTH,
        SESSION_REMOTE_USERNAME,
        SESSION_REMOTE_URL_BASE_PATH,
        COMPLETE_SOUND_COMMAND,
        COMPLETE_SOUND_ENABLED,
        READ_CLIPBOARD,
        /* core prefs */
        FIRST_CORE_PREF,
        ALT_SPEED_LIMIT_UP = FIRST_CORE_PREF,
        ALT_SPEED_LIMIT_DOWN,
        ALT_SPEED_LIMIT_ENABLED,
        ALT_SPEED_LIMIT_TIME_BEGIN,
        ALT_SPEED_LIMIT_TIME_END,
        ALT_SPEED_LIMIT_TIME_ENABLED,
        ALT_SPEED_LIMIT_TIME_DAY,
        BLOCKLIST_ENABLED,
        BLOCKLIST_URL,
        DEFAULT_TRACKERS,
        DSPEED,
        DSPEED_ENABLED,
        DOWNLOAD_DIR,
        DOWNLOAD_QUEUE_ENABLED,
        DOWNLOAD_QUEUE_SIZE,
        ENCRYPTION,
        IDLE_LIMIT,
        IDLE_LIMIT_ENABLED,
        INCOMPLETE_DIR,
        INCOMPLETE_DIR_ENABLED,
        MSGLEVEL,
        PEER_LIMIT_GLOBAL,
        PEER_LIMIT_TORRENT,
        PEER_PORT,
        PEER_PORT_RANDOM_ON_START,
        PEER_PORT_RANDOM_LOW,
        PEER_PORT_RANDOM_HIGH,
        QUEUE_STALLED_MINUTES,
        SCRIPT_TORRENT_DONE_ENABLED,
        SCRIPT_TORRENT_DONE_FILENAME,
        SCRIPT_TORRENT_DONE_SEEDING_ENABLED,
        SCRIPT_TORRENT_DONE_SEEDING_FILENAME,
        SOCKET_DIFFSERV,
        START,
        TRASH_ORIGINAL,
        PEX_ENABLED,
        DHT_ENABLED,
        UTP_ENABLED,
        LPD_ENABLED,
        PORT_FORWARDING,
        PREALLOCATION,
        RATIO,
        RATIO_ENABLED,
        RENAME_PARTIAL_FILES,
        RPC_AUTH_REQUIRED,
        RPC_ENABLED,
        RPC_PASSWORD,
        RPC_PORT,
        RPC_USERNAME,
        RPC_WHITELIST_ENABLED,
        RPC_WHITELIST,
        USPEED_ENABLED,
        USPEED,
        UPLOAD_SLOTS_PER_TORRENT,
        LAST_CORE_PREF = UPLOAD_SLOTS_PER_TORRENT,
        //
        PREFS_COUNT
    };

    Prefs();
    Prefs(Prefs&&) = delete;
    Prefs(Prefs const&) = delete;
    Prefs& operator=(Prefs&&) = delete;
    Prefs& operator=(Prefs const&) = delete;
    ~Prefs() override = default;

    [[nodiscard]] static auto constexpr is_core(int const idx)
    {
        return FIRST_CORE_PREF <= idx && idx <= LAST_CORE_PREF;
    }

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(int idx) const;

    void load_from_config_dir(QString const& dir);

    void load(tr_variant::Map const& settings);

    void set(int key, tr_variant const& value);

    template<typename T>
    void set(int key, T const& value)
    {
        QVariant& v(values_[key]);
        QVariant const tmp = QVariant::fromValue(value);

        if (v.isNull() || v != tmp)
        {
            v = tmp;
            emit changed(key);
        }
    }

    void set(int key, char const* value) = delete;

    template<typename T>
    [[nodiscard]] T get(int const idx) const
    {
        return values_[idx].value<T>();
    }

    [[nodiscard]] tr_variant::Map current_settings() const;

    void save(QString const& filename) const;

signals:
    void changed(int idx);

private:
    struct PrefItem
    {
        int id;
        tr_quark key;
        int type;
    };

    static auto constexpr Items = std::array<PrefItem, PREFS_COUNT>{ {
        // gui settings
        { .id = OPTIONS_PROMPT, .key = TR_KEY_show_options_window, .type = QMetaType::Bool },
        { .id = OPEN_DIALOG_FOLDER, .key = TR_KEY_open_dialog_dir, .type = QMetaType::QString },
        { .id = INHIBIT_HIBERNATION, .key = TR_KEY_inhibit_desktop_hibernation, .type = QMetaType::Bool },
        { .id = DIR_WATCH, .key = TR_KEY_watch_dir, .type = QMetaType::QString },
        { .id = DIR_WATCH_ENABLED, .key = TR_KEY_watch_dir_enabled, .type = QMetaType::Bool },
        { .id = SHOW_TRAY_ICON, .key = TR_KEY_show_notification_area_icon, .type = QMetaType::Bool },
        { .id = START_MINIMIZED, .key = TR_KEY_start_minimized, .type = QMetaType::Bool },
        { .id = SHOW_NOTIFICATION_ON_ADD, .key = TR_KEY_torrent_added_notification_enabled, .type = QMetaType::Bool },
        { .id = SHOW_NOTIFICATION_ON_COMPLETE, .key = TR_KEY_torrent_complete_notification_enabled, .type = QMetaType::Bool },
        { .id = ASKQUIT, .key = TR_KEY_prompt_before_exit, .type = QMetaType::Bool },
        { .id = SORT_MODE, .key = TR_KEY_sort_mode, .type = UserMetaType::SortModeType },
        { .id = SORT_REVERSED, .key = TR_KEY_sort_reversed, .type = QMetaType::Bool },
        { .id = COMPACT_VIEW, .key = TR_KEY_compact_view, .type = QMetaType::Bool },
        { .id = FILTERBAR, .key = TR_KEY_show_filterbar, .type = QMetaType::Bool },
        { .id = STATUSBAR, .key = TR_KEY_show_statusbar, .type = QMetaType::Bool },
        { .id = STATUSBAR_STATS, .key = TR_KEY_statusbar_stats, .type = UserMetaType::StatsModeType },
        { .id = SHOW_TRACKER_SCRAPES, .key = TR_KEY_show_tracker_scrapes, .type = QMetaType::Bool },
        { .id = SHOW_BACKUP_TRACKERS, .key = TR_KEY_show_backup_trackers, .type = QMetaType::Bool },
        { .id = TOOLBAR, .key = TR_KEY_show_toolbar, .type = QMetaType::Bool },
        { .id = BLOCKLIST_DATE, .key = TR_KEY_blocklist_date, .type = QMetaType::QDateTime },
        { .id = BLOCKLIST_UPDATES_ENABLED, .key = TR_KEY_blocklist_updates_enabled, .type = QMetaType::Bool },
        { .id = MAIN_WINDOW_LAYOUT_ORDER, .key = TR_KEY_main_window_layout_order, .type = QMetaType::QString },
        { .id = MAIN_WINDOW_HEIGHT, .key = TR_KEY_main_window_height, .type = QMetaType::Int },
        { .id = MAIN_WINDOW_WIDTH, .key = TR_KEY_main_window_width, .type = QMetaType::Int },
        { .id = MAIN_WINDOW_X, .key = TR_KEY_main_window_x, .type = QMetaType::Int },
        { .id = MAIN_WINDOW_Y, .key = TR_KEY_main_window_y, .type = QMetaType::Int },
        { .id = FILTER_MODE, .key = TR_KEY_filter_mode, .type = UserMetaType::ShowModeType },
        { .id = FILTER_TRACKERS, .key = TR_KEY_filter_trackers, .type = QMetaType::QString },
        { .id = FILTER_TEXT, .key = TR_KEY_filter_text, .type = QMetaType::QString },
        { .id = SESSION_IS_REMOTE, .key = TR_KEY_remote_session_enabled, .type = QMetaType::Bool },
        { .id = SESSION_REMOTE_HOST, .key = TR_KEY_remote_session_host, .type = QMetaType::QString },
        { .id = SESSION_REMOTE_HTTPS, .key = TR_KEY_remote_session_https, .type = QMetaType::Bool },
        { .id = SESSION_REMOTE_PASSWORD, .key = TR_KEY_remote_session_password, .type = QMetaType::QString },
        { .id = SESSION_REMOTE_PORT, .key = TR_KEY_remote_session_port, .type = QMetaType::Int },
        { .id = SESSION_REMOTE_AUTH, .key = TR_KEY_remote_session_requires_authentication, .type = QMetaType::Bool },
        { .id = SESSION_REMOTE_USERNAME, .key = TR_KEY_remote_session_username, .type = QMetaType::QString },
        { .id = SESSION_REMOTE_URL_BASE_PATH, .key = TR_KEY_remote_session_url_base_path, .type = QMetaType::QString },
        { .id = COMPLETE_SOUND_COMMAND, .key = TR_KEY_torrent_complete_sound_command, .type = QMetaType::QStringList },
        { .id = COMPLETE_SOUND_ENABLED, .key = TR_KEY_torrent_complete_sound_enabled, .type = QMetaType::Bool },
        { .id = READ_CLIPBOARD, .key = TR_KEY_read_clipboard, .type = QMetaType::Bool },

        /* libtransmission settings */
        { .id = ALT_SPEED_LIMIT_UP, .key = TR_KEY_alt_speed_up, .type = QMetaType::Int },
        { .id = ALT_SPEED_LIMIT_DOWN, .key = TR_KEY_alt_speed_down, .type = QMetaType::Int },
        { .id = ALT_SPEED_LIMIT_ENABLED, .key = TR_KEY_alt_speed_enabled, .type = QMetaType::Bool },
        { .id = ALT_SPEED_LIMIT_TIME_BEGIN, .key = TR_KEY_alt_speed_time_begin, .type = QMetaType::Int },
        { .id = ALT_SPEED_LIMIT_TIME_END, .key = TR_KEY_alt_speed_time_end, .type = QMetaType::Int },
        { .id = ALT_SPEED_LIMIT_TIME_ENABLED, .key = TR_KEY_alt_speed_time_enabled, .type = QMetaType::Bool },
        { .id = ALT_SPEED_LIMIT_TIME_DAY, .key = TR_KEY_alt_speed_time_day, .type = QMetaType::Int },
        { .id = BLOCKLIST_ENABLED, .key = TR_KEY_blocklist_enabled, .type = QMetaType::Bool },
        { .id = BLOCKLIST_URL, .key = TR_KEY_blocklist_url, .type = QMetaType::QString },
        { .id = DEFAULT_TRACKERS, .key = TR_KEY_default_trackers, .type = QMetaType::QString },
        { .id = DSPEED, .key = TR_KEY_speed_limit_down, .type = QMetaType::Int },
        { .id = DSPEED_ENABLED, .key = TR_KEY_speed_limit_down_enabled, .type = QMetaType::Bool },
        { .id = DOWNLOAD_DIR, .key = TR_KEY_download_dir, .type = QMetaType::QString },
        { .id = DOWNLOAD_QUEUE_ENABLED, .key = TR_KEY_download_queue_enabled, .type = QMetaType::Bool },
        { .id = DOWNLOAD_QUEUE_SIZE, .key = TR_KEY_download_queue_size, .type = QMetaType::Int },
        { .id = ENCRYPTION, .key = TR_KEY_encryption, .type = UserMetaType::EncryptionModeType },
        { .id = IDLE_LIMIT, .key = TR_KEY_idle_seeding_limit, .type = QMetaType::Int },
        { .id = IDLE_LIMIT_ENABLED, .key = TR_KEY_idle_seeding_limit_enabled, .type = QMetaType::Bool },
        { .id = INCOMPLETE_DIR, .key = TR_KEY_incomplete_dir, .type = QMetaType::QString },
        { .id = INCOMPLETE_DIR_ENABLED, .key = TR_KEY_incomplete_dir_enabled, .type = QMetaType::Bool },
        { .id = MSGLEVEL, .key = TR_KEY_message_level, .type = QMetaType::Int },
        { .id = PEER_LIMIT_GLOBAL, .key = TR_KEY_peer_limit_global, .type = QMetaType::Int },
        { .id = PEER_LIMIT_TORRENT, .key = TR_KEY_peer_limit_per_torrent, .type = QMetaType::Int },
        { .id = PEER_PORT, .key = TR_KEY_peer_port, .type = QMetaType::Int },
        { .id = PEER_PORT_RANDOM_ON_START, .key = TR_KEY_peer_port_random_on_start, .type = QMetaType::Bool },
        { .id = PEER_PORT_RANDOM_LOW, .key = TR_KEY_peer_port_random_low, .type = QMetaType::Int },
        { .id = PEER_PORT_RANDOM_HIGH, .key = TR_KEY_peer_port_random_high, .type = QMetaType::Int },
        { .id = QUEUE_STALLED_MINUTES, .key = TR_KEY_queue_stalled_minutes, .type = QMetaType::Int },
        { .id = SCRIPT_TORRENT_DONE_ENABLED, .key = TR_KEY_script_torrent_done_enabled, .type = QMetaType::Bool },
        { .id = SCRIPT_TORRENT_DONE_FILENAME, .key = TR_KEY_script_torrent_done_filename, .type = QMetaType::QString },
        { .id = SCRIPT_TORRENT_DONE_SEEDING_ENABLED,
          .key = TR_KEY_script_torrent_done_seeding_enabled,
          .type = QMetaType::Bool },
        { .id = SCRIPT_TORRENT_DONE_SEEDING_FILENAME,
          .key = TR_KEY_script_torrent_done_seeding_filename,
          .type = QMetaType::QString },
        { .id = SOCKET_DIFFSERV, .key = TR_KEY_peer_socket_diffserv, .type = QMetaType::QString },
        { .id = START, .key = TR_KEY_start_added_torrents, .type = QMetaType::Bool },
        { .id = TRASH_ORIGINAL, .key = TR_KEY_trash_original_torrent_files, .type = QMetaType::Bool },
        { .id = PEX_ENABLED, .key = TR_KEY_pex_enabled, .type = QMetaType::Bool },
        { .id = DHT_ENABLED, .key = TR_KEY_dht_enabled, .type = QMetaType::Bool },
        { .id = UTP_ENABLED, .key = TR_KEY_utp_enabled, .type = QMetaType::Bool },
        { .id = LPD_ENABLED, .key = TR_KEY_lpd_enabled, .type = QMetaType::Bool },
        { .id = PORT_FORWARDING, .key = TR_KEY_port_forwarding_enabled, .type = QMetaType::Bool },
        { .id = PREALLOCATION, .key = TR_KEY_preallocation, .type = QMetaType::Int },
        { .id = RATIO, .key = TR_KEY_ratio_limit, .type = QMetaType::Double },
        { .id = RATIO_ENABLED, .key = TR_KEY_ratio_limit_enabled, .type = QMetaType::Bool },
        { .id = RENAME_PARTIAL_FILES, .key = TR_KEY_rename_partial_files, .type = QMetaType::Bool },
        { .id = RPC_AUTH_REQUIRED, .key = TR_KEY_rpc_authentication_required, .type = QMetaType::Bool },
        { .id = RPC_ENABLED, .key = TR_KEY_rpc_enabled, .type = QMetaType::Bool },
        { .id = RPC_PASSWORD, .key = TR_KEY_rpc_password, .type = QMetaType::QString },
        { .id = RPC_PORT, .key = TR_KEY_rpc_port, .type = QMetaType::Int },
        { .id = RPC_USERNAME, .key = TR_KEY_rpc_username, .type = QMetaType::QString },
        { .id = RPC_WHITELIST_ENABLED, .key = TR_KEY_rpc_whitelist_enabled, .type = QMetaType::Bool },
        { .id = RPC_WHITELIST, .key = TR_KEY_rpc_whitelist, .type = QMetaType::QString },
        { .id = USPEED_ENABLED, .key = TR_KEY_speed_limit_up_enabled, .type = QMetaType::Bool },
        { .id = USPEED, .key = TR_KEY_speed_limit_up, .type = QMetaType::Int },
        { .id = UPLOAD_SLOTS_PER_TORRENT, .key = TR_KEY_upload_slots_per_torrent, .type = QMetaType::Int },
    } };

    [[nodiscard]] static tr_variant::Map defaults();

    std::array<QVariant, PREFS_COUNT> mutable values_;
};
