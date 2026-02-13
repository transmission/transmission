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

#include "tr/base/quark.h"
#include "tr/base/variant.h"

#include "tr/app/display-modes.h"

#include "UserMetaType.h"

class Prefs : public QObject
{
    Q_OBJECT

public:
    enum
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

    [[nodiscard]] static auto constexpr isCore(int const idx)
    {
        return FIRST_CORE_PREF <= idx && idx <= LAST_CORE_PREF;
    }

    [[nodiscard]] std::pair<tr_quark, tr_variant> keyval(int idx) const;

    void loadFromConfigDir(QString dir);

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

    static inline auto constexpr Items = std::array<PrefItem, PREFS_COUNT>{ {
        // gui settings
        { OPTIONS_PROMPT, TR_KEY_show_options_window, QMetaType::Bool },
        { OPEN_DIALOG_FOLDER, TR_KEY_open_dialog_dir, QMetaType::QString },
        { INHIBIT_HIBERNATION, TR_KEY_inhibit_desktop_hibernation, QMetaType::Bool },
        { DIR_WATCH, TR_KEY_watch_dir, QMetaType::QString },
        { DIR_WATCH_ENABLED, TR_KEY_watch_dir_enabled, QMetaType::Bool },
        { SHOW_TRAY_ICON, TR_KEY_show_notification_area_icon, QMetaType::Bool },
        { START_MINIMIZED, TR_KEY_start_minimized, QMetaType::Bool },
        { SHOW_NOTIFICATION_ON_ADD, TR_KEY_torrent_added_notification_enabled, QMetaType::Bool },
        { SHOW_NOTIFICATION_ON_COMPLETE, TR_KEY_torrent_complete_notification_enabled, QMetaType::Bool },
        { ASKQUIT, TR_KEY_prompt_before_exit, QMetaType::Bool },
        { SORT_MODE, TR_KEY_sort_mode, UserMetaType::SortModeType },
        { SORT_REVERSED, TR_KEY_sort_reversed, QMetaType::Bool },
        { COMPACT_VIEW, TR_KEY_compact_view, QMetaType::Bool },
        { FILTERBAR, TR_KEY_show_filterbar, QMetaType::Bool },
        { STATUSBAR, TR_KEY_show_statusbar, QMetaType::Bool },
        { STATUSBAR_STATS, TR_KEY_statusbar_stats, UserMetaType::StatsModeType },
        { SHOW_TRACKER_SCRAPES, TR_KEY_show_tracker_scrapes, QMetaType::Bool },
        { SHOW_BACKUP_TRACKERS, TR_KEY_show_backup_trackers, QMetaType::Bool },
        { TOOLBAR, TR_KEY_show_toolbar, QMetaType::Bool },
        { BLOCKLIST_DATE, TR_KEY_blocklist_date, QMetaType::QDateTime },
        { BLOCKLIST_UPDATES_ENABLED, TR_KEY_blocklist_updates_enabled, QMetaType::Bool },
        { MAIN_WINDOW_LAYOUT_ORDER, TR_KEY_main_window_layout_order, QMetaType::QString },
        { MAIN_WINDOW_HEIGHT, TR_KEY_main_window_height, QMetaType::Int },
        { MAIN_WINDOW_WIDTH, TR_KEY_main_window_width, QMetaType::Int },
        { MAIN_WINDOW_X, TR_KEY_main_window_x, QMetaType::Int },
        { MAIN_WINDOW_Y, TR_KEY_main_window_y, QMetaType::Int },
        { FILTER_MODE, TR_KEY_filter_mode, UserMetaType::ShowModeType },
        { FILTER_TRACKERS, TR_KEY_filter_trackers, QMetaType::QString },
        { FILTER_TEXT, TR_KEY_filter_text, QMetaType::QString },
        { SESSION_IS_REMOTE, TR_KEY_remote_session_enabled, QMetaType::Bool },
        { SESSION_REMOTE_HOST, TR_KEY_remote_session_host, QMetaType::QString },
        { SESSION_REMOTE_HTTPS, TR_KEY_remote_session_https, QMetaType::Bool },
        { SESSION_REMOTE_PASSWORD, TR_KEY_remote_session_password, QMetaType::QString },
        { SESSION_REMOTE_PORT, TR_KEY_remote_session_port, QMetaType::Int },
        { SESSION_REMOTE_AUTH, TR_KEY_remote_session_requires_authentication, QMetaType::Bool },
        { SESSION_REMOTE_USERNAME, TR_KEY_remote_session_username, QMetaType::QString },
        { SESSION_REMOTE_URL_BASE_PATH, TR_KEY_remote_session_url_base_path, QMetaType::QString },
        { COMPLETE_SOUND_COMMAND, TR_KEY_torrent_complete_sound_command, QMetaType::QStringList },
        { COMPLETE_SOUND_ENABLED, TR_KEY_torrent_complete_sound_enabled, QMetaType::Bool },
        { READ_CLIPBOARD, TR_KEY_read_clipboard, QMetaType::Bool },

        /* libtransmission settings */
        { ALT_SPEED_LIMIT_UP, TR_KEY_alt_speed_up, QMetaType::Int },
        { ALT_SPEED_LIMIT_DOWN, TR_KEY_alt_speed_down, QMetaType::Int },
        { ALT_SPEED_LIMIT_ENABLED, TR_KEY_alt_speed_enabled, QMetaType::Bool },
        { ALT_SPEED_LIMIT_TIME_BEGIN, TR_KEY_alt_speed_time_begin, QMetaType::Int },
        { ALT_SPEED_LIMIT_TIME_END, TR_KEY_alt_speed_time_end, QMetaType::Int },
        { ALT_SPEED_LIMIT_TIME_ENABLED, TR_KEY_alt_speed_time_enabled, QMetaType::Bool },
        { ALT_SPEED_LIMIT_TIME_DAY, TR_KEY_alt_speed_time_day, QMetaType::Int },
        { BLOCKLIST_ENABLED, TR_KEY_blocklist_enabled, QMetaType::Bool },
        { BLOCKLIST_URL, TR_KEY_blocklist_url, QMetaType::QString },
        { DEFAULT_TRACKERS, TR_KEY_default_trackers, QMetaType::QString },
        { DSPEED, TR_KEY_speed_limit_down, QMetaType::Int },
        { DSPEED_ENABLED, TR_KEY_speed_limit_down_enabled, QMetaType::Bool },
        { DOWNLOAD_DIR, TR_KEY_download_dir, QMetaType::QString },
        { DOWNLOAD_QUEUE_ENABLED, TR_KEY_download_queue_enabled, QMetaType::Bool },
        { DOWNLOAD_QUEUE_SIZE, TR_KEY_download_queue_size, QMetaType::Int },
        { ENCRYPTION, TR_KEY_encryption, UserMetaType::EncryptionModeType },
        { IDLE_LIMIT, TR_KEY_idle_seeding_limit, QMetaType::Int },
        { IDLE_LIMIT_ENABLED, TR_KEY_idle_seeding_limit_enabled, QMetaType::Bool },
        { INCOMPLETE_DIR, TR_KEY_incomplete_dir, QMetaType::QString },
        { INCOMPLETE_DIR_ENABLED, TR_KEY_incomplete_dir_enabled, QMetaType::Bool },
        { MSGLEVEL, TR_KEY_message_level, QMetaType::Int },
        { PEER_LIMIT_GLOBAL, TR_KEY_peer_limit_global, QMetaType::Int },
        { PEER_LIMIT_TORRENT, TR_KEY_peer_limit_per_torrent, QMetaType::Int },
        { PEER_PORT, TR_KEY_peer_port, QMetaType::Int },
        { PEER_PORT_RANDOM_ON_START, TR_KEY_peer_port_random_on_start, QMetaType::Bool },
        { PEER_PORT_RANDOM_LOW, TR_KEY_peer_port_random_low, QMetaType::Int },
        { PEER_PORT_RANDOM_HIGH, TR_KEY_peer_port_random_high, QMetaType::Int },
        { QUEUE_STALLED_MINUTES, TR_KEY_queue_stalled_minutes, QMetaType::Int },
        { SCRIPT_TORRENT_DONE_ENABLED, TR_KEY_script_torrent_done_enabled, QMetaType::Bool },
        { SCRIPT_TORRENT_DONE_FILENAME, TR_KEY_script_torrent_done_filename, QMetaType::QString },
        { SCRIPT_TORRENT_DONE_SEEDING_ENABLED, TR_KEY_script_torrent_done_seeding_enabled, QMetaType::Bool },
        { SCRIPT_TORRENT_DONE_SEEDING_FILENAME, TR_KEY_script_torrent_done_seeding_filename, QMetaType::QString },
        { SOCKET_DIFFSERV, TR_KEY_peer_socket_diffserv, QMetaType::QString },
        { START, TR_KEY_start_added_torrents, QMetaType::Bool },
        { TRASH_ORIGINAL, TR_KEY_trash_original_torrent_files, QMetaType::Bool },
        { PEX_ENABLED, TR_KEY_pex_enabled, QMetaType::Bool },
        { DHT_ENABLED, TR_KEY_dht_enabled, QMetaType::Bool },
        { UTP_ENABLED, TR_KEY_utp_enabled, QMetaType::Bool },
        { LPD_ENABLED, TR_KEY_lpd_enabled, QMetaType::Bool },
        { PORT_FORWARDING, TR_KEY_port_forwarding_enabled, QMetaType::Bool },
        { PREALLOCATION, TR_KEY_preallocation, QMetaType::Int },
        { RATIO, TR_KEY_ratio_limit, QMetaType::Double },
        { RATIO_ENABLED, TR_KEY_ratio_limit_enabled, QMetaType::Bool },
        { RENAME_PARTIAL_FILES, TR_KEY_rename_partial_files, QMetaType::Bool },
        { RPC_AUTH_REQUIRED, TR_KEY_rpc_authentication_required, QMetaType::Bool },
        { RPC_ENABLED, TR_KEY_rpc_enabled, QMetaType::Bool },
        { RPC_PASSWORD, TR_KEY_rpc_password, QMetaType::QString },
        { RPC_PORT, TR_KEY_rpc_port, QMetaType::Int },
        { RPC_USERNAME, TR_KEY_rpc_username, QMetaType::QString },
        { RPC_WHITELIST_ENABLED, TR_KEY_rpc_whitelist_enabled, QMetaType::Bool },
        { RPC_WHITELIST, TR_KEY_rpc_whitelist, QMetaType::QString },
        { USPEED_ENABLED, TR_KEY_speed_limit_up_enabled, QMetaType::Bool },
        { USPEED, TR_KEY_speed_limit_up, QMetaType::Int },
        { UPLOAD_SLOTS_PER_TORRENT, TR_KEY_upload_slots_per_torrent, QMetaType::Int },
    } };

    [[nodiscard]] static tr_variant::Map defaults();

    void set(int key, char const* value) = delete;

    std::array<QVariant, PREFS_COUNT> mutable values_;
};
