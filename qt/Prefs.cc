// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cassert>
#include <optional>
#include <string_view>
#include <utility>

#include <QDateTime>
#include <QDir>

#include <libtransmission/transmission.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include "CustomVariantType.h"
#include "Filters.h"
#include "Prefs.h"
#include "VariantHelpers.h"

namespace api_compat = libtransmission::api_compat;
using libtransmission::serializer::to_value;
using libtransmission::serializer::to_variant;
using ::trqt::variant_helpers::dictAdd;
using namespace std::string_view_literals;

// ---

namespace
{
template<typename T>
[[nodiscard]] QVariant qvarFromOptional(std::optional<T> const& val)
{
    return val ? QVariant::fromValue(*val) : QVariant{};
}

[[nodiscard]] QVariant qvarFromTVar(tr_variant const& var, int const qt_metatype)
{
    switch (qt_metatype)
    {
    case QMetaType::Int:
        return qvarFromOptional(to_value<int64_t>(var));

    case CustomVariantType::EncryptionModeType:
        return qvarFromOptional(to_value<tr_encryption_mode>(var));

    case CustomVariantType::SortModeType:
        return qvarFromOptional(to_value<SortMode>(var));

    case CustomVariantType::ShowModeType:
        return qvarFromOptional(to_value<ShowMode>(var));

    case QMetaType::QString:
        return qvarFromOptional(to_value<QString>(var));

    case QMetaType::QStringList:
        return qvarFromOptional(to_value<QStringList>(var));

    case QMetaType::Bool:
        return qvarFromOptional(to_value<bool>(var));

    case QMetaType::Double:
        return qvarFromOptional(to_value<double>(var));

    case QMetaType::QDateTime:
        return qvarFromOptional(to_value<QDateTime>(var));

    default:
        assert(false && "unhandled type");
        return {};
    }
}

void ensureSoundCommandIsAList(tr_variant::Map& map)
{
    auto constexpr Key = TR_KEY_torrent_complete_sound_command;
    auto constexpr DefaultVal = std::array<std::string_view, 5U>{ "canberra-gtk-play",
                                                                  "-i",
                                                                  "complete-download",
                                                                  "-d",
                                                                  "transmission torrent downloaded" };
    if (map.find_if<tr_variant::Vector>(Key) == nullptr)
    {
        map.insert_or_assign(Key, to_variant(DefaultVal));
    }
}
} // namespace

std::array<Prefs::PrefItem, Prefs::PREFS_COUNT> const Prefs::Items{
    /* gui settings */
    PrefItem{ OPTIONS_PROMPT, TR_KEY_show_options_window, QMetaType::Bool },
    { OPEN_DIALOG_FOLDER, TR_KEY_open_dialog_dir, QMetaType::QString },
    { INHIBIT_HIBERNATION, TR_KEY_inhibit_desktop_hibernation, QMetaType::Bool },
    { DIR_WATCH, TR_KEY_watch_dir, QMetaType::QString },
    { DIR_WATCH_ENABLED, TR_KEY_watch_dir_enabled, QMetaType::Bool },
    { SHOW_TRAY_ICON, TR_KEY_show_notification_area_icon, QMetaType::Bool },
    { START_MINIMIZED, TR_KEY_start_minimized, QMetaType::Bool },
    { SHOW_NOTIFICATION_ON_ADD, TR_KEY_torrent_added_notification_enabled, QMetaType::Bool },
    { SHOW_NOTIFICATION_ON_COMPLETE, TR_KEY_torrent_complete_notification_enabled, QMetaType::Bool },
    { ASKQUIT, TR_KEY_prompt_before_exit, QMetaType::Bool },
    { SORT_MODE, TR_KEY_sort_mode, CustomVariantType::SortModeType },
    { SORT_REVERSED, TR_KEY_sort_reversed, QMetaType::Bool },
    { COMPACT_VIEW, TR_KEY_compact_view, QMetaType::Bool },
    { FILTERBAR, TR_KEY_show_filterbar, QMetaType::Bool },
    { STATUSBAR, TR_KEY_show_statusbar, QMetaType::Bool },
    { STATUSBAR_STATS, TR_KEY_statusbar_stats, QMetaType::QString },
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
    { FILTER_MODE, TR_KEY_filter_mode, CustomVariantType::ShowModeType },
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
    { ENCRYPTION, TR_KEY_encryption, CustomVariantType::EncryptionModeType },
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
};

namespace
{
[[nodiscard]] constexpr auto prefIsSavable(int pref)
{
    switch (pref)
    {
    // these are the prefs that don't get saved to settings.json
    // when the application exits.
    case Prefs::FILTER_TEXT:
        return false;

    default:
        return true;
    }
}
} // namespace

/***
****
***/

Prefs::Prefs()
{
    static_assert(sizeof(Items) / sizeof(Items[0]) == PREFS_COUNT);

#ifndef NDEBUG
    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        assert(Items[i].id == i);
    }

#endif

    load(get_default_app_settings());
}

void Prefs::loadFromConfigDir(QString const dir)
{
    auto settings = tr_sessionLoadSettings(dir.toStdString());
    if (auto* const map = settings.get_if<tr_variant::Map>())
    {
        ensureSoundCommandIsAList(*map);
        load(*map);
    }
}

void Prefs::load(tr_variant::Map const& settings)
{
    for (int idx = 0; idx < PREFS_COUNT; ++idx)
    {
        if (auto const iter = settings.find(getKey(idx)); iter != settings.end())
        {
            values_[idx] = qvarFromTVar(iter->second, Items[idx].type);
        }
    }
}

void Prefs::save(QString const& filename)
{
    // make a dict from settings.json
    tr_variant current_settings;
    tr_variantInitDict(&current_settings, PREFS_COUNT);

    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        if (!prefIsSavable(i))
        {
            continue;
        }

        auto const key = getKey(i);
        auto const& val = values_[i];

        switch (Items[i].type)
        {
        case QMetaType::Int:
            dictAdd(&current_settings, key, val.toInt());
            break;

        case CustomVariantType::EncryptionModeType:
            *tr_variantDictAdd(&current_settings, key) = to_variant(val.value<tr_encryption_mode>());
            break;

        case CustomVariantType::SortModeType:
            *tr_variantDictAdd(&current_settings, key) = to_variant(val.value<SortMode>());
            break;

        case CustomVariantType::ShowModeType:
            *tr_variantDictAdd(&current_settings, key) = to_variant(val.value<ShowMode>());
            break;

        case QMetaType::QString:
            dictAdd(&current_settings, key, val.toString());
            break;

        case QMetaType::QStringList:
            dictAdd(&current_settings, key, val.toStringList());
            break;

        case QMetaType::Bool:
            dictAdd(&current_settings, key, val.toBool());
            break;

        case QMetaType::Double:
            dictAdd(&current_settings, key, val.toDouble());
            break;

        case QMetaType::QDateTime:
            dictAdd(&current_settings, key, int64_t{ val.toDateTime().toSecsSinceEpoch() });
            break;

        default:
            assert(false && "unhandled type");
            break;
        }
    }

    // update settings.json with our settings
    auto const filename_str = filename.toStdString();
    auto serde = tr_variant_serde::json();
    auto settings = tr_variant::make_map(PREFS_COUNT);
    if (auto const file_settings = serde.parse_file(filename_str))
    {
        settings.merge(*file_settings);
    }

    settings.merge(current_settings);
    api_compat::convert_outgoing_data(settings);
    serde.to_file(settings, filename_str);
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
tr_variant::Map Prefs::get_default_app_settings()
{
    auto const download_dir = tr_getDefaultDownloadDir();

    auto map = tr_variant::Map{ 64U };
    map.try_emplace(TR_KEY_blocklist_date, 0);
    map.try_emplace(TR_KEY_blocklist_updates_enabled, true);
    map.try_emplace(TR_KEY_compact_view, false);
    map.try_emplace(TR_KEY_download_dir, download_dir);
    map.try_emplace(TR_KEY_filter_mode, to_variant(DefaultShowMode));
    map.try_emplace(TR_KEY_inhibit_desktop_hibernation, false);
    map.try_emplace(TR_KEY_main_window_height, 500);
    map.try_emplace(TR_KEY_main_window_layout_order, tr_variant::unmanaged_string("menu,toolbar,filter,list,statusbar"sv));
    map.try_emplace(TR_KEY_main_window_width, 600);
    map.try_emplace(TR_KEY_main_window_x, 50);
    map.try_emplace(TR_KEY_main_window_y, 50);
    map.try_emplace(TR_KEY_open_dialog_dir, QDir::home().absolutePath().toStdString());
    map.try_emplace(TR_KEY_prompt_before_exit, true);
    map.try_emplace(TR_KEY_read_clipboard, false);
    map.try_emplace(TR_KEY_remote_session_enabled, false);
    map.try_emplace(TR_KEY_remote_session_host, tr_variant::unmanaged_string("localhost"sv));
    map.try_emplace(TR_KEY_remote_session_https, false);
    map.try_emplace(TR_KEY_remote_session_password, tr_variant::unmanaged_string(""sv));
    map.try_emplace(TR_KEY_remote_session_port, TrDefaultRpcPort);
    map.try_emplace(TR_KEY_remote_session_requires_authentication, false);
    map.try_emplace(TR_KEY_remote_session_url_base_path, tr_variant::unmanaged_string(TrHttpServerDefaultBasePath));
    map.try_emplace(TR_KEY_remote_session_username, tr_variant::unmanaged_string(""sv));
    map.try_emplace(TR_KEY_show_backup_trackers, false);
    map.try_emplace(TR_KEY_show_filterbar, true);
    map.try_emplace(TR_KEY_show_notification_area_icon, false);
    map.try_emplace(TR_KEY_show_options_window, true);
    map.try_emplace(TR_KEY_show_statusbar, true);
    map.try_emplace(TR_KEY_show_toolbar, true);
    map.try_emplace(TR_KEY_show_tracker_scrapes, false);
    map.try_emplace(TR_KEY_sort_mode, to_variant(DefaultSortMode));
    map.try_emplace(TR_KEY_sort_reversed, false);
    map.try_emplace(TR_KEY_start_minimized, false);
    map.try_emplace(TR_KEY_statusbar_stats, tr_variant::unmanaged_string("total-ratio"));
    map.try_emplace(TR_KEY_torrent_added_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_sound_enabled, true);
    map.try_emplace(TR_KEY_watch_dir, download_dir);
    map.try_emplace(TR_KEY_watch_dir_enabled, false);
    return map;
}
