/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <cassert>
#include <cstdlib>

#include <QDateTime>
#include <QDir>
#include <QFile>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "CustomVariantType.h"
#include "Prefs.h"
#include "Utils.h"
#include "VariantHelpers.h"

using ::trqt::variant_helpers::dictAdd;
using ::trqt::variant_helpers::getValue;

/***
****
***/

std::array<Prefs::PrefItem, Prefs::PREFS_COUNT> const Prefs::Items
{
    /* gui settings */
    PrefItem{ OPTIONS_PROMPT, TR_KEY_show_options_window, QVariant::Bool },
    { OPEN_DIALOG_FOLDER, TR_KEY_open_dialog_dir, QVariant::String },
    { INHIBIT_HIBERNATION, TR_KEY_inhibit_desktop_hibernation, QVariant::Bool },
    { DIR_WATCH, TR_KEY_watch_dir, QVariant::String },
    { DIR_WATCH_ENABLED, TR_KEY_watch_dir_enabled, QVariant::Bool },
    { SHOW_TRAY_ICON, TR_KEY_show_notification_area_icon, QVariant::Bool },
    { START_MINIMIZED, TR_KEY_start_minimized, QVariant::Bool },
    { SHOW_NOTIFICATION_ON_ADD, TR_KEY_torrent_added_notification_enabled, QVariant::Bool },
    { SHOW_NOTIFICATION_ON_COMPLETE, TR_KEY_torrent_complete_notification_enabled, QVariant::Bool },
    { ASKQUIT, TR_KEY_prompt_before_exit, QVariant::Bool },
    { SORT_MODE, TR_KEY_sort_mode, CustomVariantType::SortModeType },
    { SORT_REVERSED, TR_KEY_sort_reversed, QVariant::Bool },
    { COMPACT_VIEW, TR_KEY_compact_view, QVariant::Bool },
    { FILTERBAR, TR_KEY_show_filterbar, QVariant::Bool },
    { STATUSBAR, TR_KEY_show_statusbar, QVariant::Bool },
    { STATUSBAR_STATS, TR_KEY_statusbar_stats, QVariant::String },
    { SHOW_TRACKER_SCRAPES, TR_KEY_show_extra_peer_details, QVariant::Bool },
    { SHOW_BACKUP_TRACKERS, TR_KEY_show_backup_trackers, QVariant::Bool },
    { TOOLBAR, TR_KEY_show_toolbar, QVariant::Bool },
    { BLOCKLIST_DATE, TR_KEY_blocklist_date, QVariant::DateTime },
    { BLOCKLIST_UPDATES_ENABLED, TR_KEY_blocklist_updates_enabled, QVariant::Bool },
    { MAIN_WINDOW_LAYOUT_ORDER, TR_KEY_main_window_layout_order, QVariant::String },
    { MAIN_WINDOW_HEIGHT, TR_KEY_main_window_height, QVariant::Int },
    { MAIN_WINDOW_WIDTH, TR_KEY_main_window_width, QVariant::Int },
    { MAIN_WINDOW_X, TR_KEY_main_window_x, QVariant::Int },
    { MAIN_WINDOW_Y, TR_KEY_main_window_y, QVariant::Int },
    { FILTER_MODE, TR_KEY_filter_mode, CustomVariantType::FilterModeType },
    { FILTER_TRACKERS, TR_KEY_filter_trackers, QVariant::String },
    { FILTER_TEXT, TR_KEY_filter_text, QVariant::String },
    { SESSION_IS_REMOTE, TR_KEY_remote_session_enabled, QVariant::Bool },
    { SESSION_REMOTE_HOST, TR_KEY_remote_session_host, QVariant::String },
    { SESSION_REMOTE_PORT, TR_KEY_remote_session_port, QVariant::Int },
    { SESSION_REMOTE_AUTH, TR_KEY_remote_session_requres_authentication, QVariant::Bool },
    { SESSION_REMOTE_USERNAME, TR_KEY_remote_session_username, QVariant::String },
    { SESSION_REMOTE_PASSWORD, TR_KEY_remote_session_password, QVariant::String },
    { COMPLETE_SOUND_COMMAND, TR_KEY_torrent_complete_sound_command, QVariant::String },
    { COMPLETE_SOUND_ENABLED, TR_KEY_torrent_complete_sound_enabled, QVariant::Bool },
    { USER_HAS_GIVEN_INFORMED_CONSENT, TR_KEY_user_has_given_informed_consent, QVariant::Bool },

    /* libtransmission settings */
    { ALT_SPEED_LIMIT_UP, TR_KEY_alt_speed_up, QVariant::Int },
    { ALT_SPEED_LIMIT_DOWN, TR_KEY_alt_speed_down, QVariant::Int },
    { ALT_SPEED_LIMIT_ENABLED, TR_KEY_alt_speed_enabled, QVariant::Bool },
    { ALT_SPEED_LIMIT_TIME_BEGIN, TR_KEY_alt_speed_time_begin, QVariant::Int },
    { ALT_SPEED_LIMIT_TIME_END, TR_KEY_alt_speed_time_end, QVariant::Int },
    { ALT_SPEED_LIMIT_TIME_ENABLED, TR_KEY_alt_speed_time_enabled, QVariant::Bool },
    { ALT_SPEED_LIMIT_TIME_DAY, TR_KEY_alt_speed_time_day, QVariant::Int },
    { BLOCKLIST_ENABLED, TR_KEY_blocklist_enabled, QVariant::Bool },
    { BLOCKLIST_URL, TR_KEY_blocklist_url, QVariant::String },
    { DSPEED, TR_KEY_speed_limit_down, QVariant::Int },
    { DSPEED_ENABLED, TR_KEY_speed_limit_down_enabled, QVariant::Bool },
    { DOWNLOAD_DIR, TR_KEY_download_dir, QVariant::String },
    { DOWNLOAD_QUEUE_ENABLED, TR_KEY_download_queue_enabled, QVariant::Bool },
    { DOWNLOAD_QUEUE_SIZE, TR_KEY_download_queue_size, QVariant::Int },
    { ENCRYPTION, TR_KEY_encryption, QVariant::Int },
    { IDLE_LIMIT, TR_KEY_idle_seeding_limit, QVariant::Int },
    { IDLE_LIMIT_ENABLED, TR_KEY_idle_seeding_limit_enabled, QVariant::Bool },
    { INCOMPLETE_DIR, TR_KEY_incomplete_dir, QVariant::String },
    { INCOMPLETE_DIR_ENABLED, TR_KEY_incomplete_dir_enabled, QVariant::Bool },
    { MSGLEVEL, TR_KEY_message_level, QVariant::Int },
    { PEER_LIMIT_GLOBAL, TR_KEY_peer_limit_global, QVariant::Int },
    { PEER_LIMIT_TORRENT, TR_KEY_peer_limit_per_torrent, QVariant::Int },
    { PEER_PORT, TR_KEY_peer_port, QVariant::Int },
    { PEER_PORT_RANDOM_ON_START, TR_KEY_peer_port_random_on_start, QVariant::Bool },
    { PEER_PORT_RANDOM_LOW, TR_KEY_peer_port_random_low, QVariant::Int },
    { PEER_PORT_RANDOM_HIGH, TR_KEY_peer_port_random_high, QVariant::Int },
    { QUEUE_STALLED_MINUTES, TR_KEY_queue_stalled_minutes, QVariant::Int },
    { SCRIPT_TORRENT_DONE_ENABLED, TR_KEY_script_torrent_done_enabled, QVariant::Bool },
    { SCRIPT_TORRENT_DONE_FILENAME, TR_KEY_script_torrent_done_filename, QVariant::String },
    { SOCKET_TOS, TR_KEY_peer_socket_tos, QVariant::Int },
    { START, TR_KEY_start_added_torrents, QVariant::Bool },
    { TRASH_ORIGINAL, TR_KEY_trash_original_torrent_files, QVariant::Bool },
    { PEX_ENABLED, TR_KEY_pex_enabled, QVariant::Bool },
    { DHT_ENABLED, TR_KEY_dht_enabled, QVariant::Bool },
    { UTP_ENABLED, TR_KEY_utp_enabled, QVariant::Bool },
    { LPD_ENABLED, TR_KEY_lpd_enabled, QVariant::Bool },
    { PORT_FORWARDING, TR_KEY_port_forwarding_enabled, QVariant::Bool },
    { PREALLOCATION, TR_KEY_preallocation, QVariant::Int },
    { RATIO, TR_KEY_ratio_limit, QVariant::Double },
    { RATIO_ENABLED, TR_KEY_ratio_limit_enabled, QVariant::Bool },
    { RENAME_PARTIAL_FILES, TR_KEY_rename_partial_files, QVariant::Bool },
    { RPC_AUTH_REQUIRED, TR_KEY_rpc_authentication_required, QVariant::Bool },
    { RPC_ENABLED, TR_KEY_rpc_enabled, QVariant::Bool },
    { RPC_PASSWORD, TR_KEY_rpc_password, QVariant::String },
    { RPC_PORT, TR_KEY_rpc_port, QVariant::Int },
    { RPC_USERNAME, TR_KEY_rpc_username, QVariant::String },
    { RPC_WHITELIST_ENABLED, TR_KEY_rpc_whitelist_enabled, QVariant::Bool },
    { RPC_WHITELIST, TR_KEY_rpc_whitelist, QVariant::String },
    { USPEED_ENABLED, TR_KEY_speed_limit_up_enabled, QVariant::Bool },
    { USPEED, TR_KEY_speed_limit_up, QVariant::Int },
    { UPLOAD_SLOTS_PER_TORRENT, TR_KEY_upload_slots_per_torrent, QVariant::Int }
};

/***
****
***/

Prefs::Prefs(QString config_dir) :
    config_dir_(std::move(config_dir)),
    FilterModes{
        std::make_pair(FilterMode::SHOW_ALL, QStringLiteral("show-all")),
        std::make_pair(FilterMode::SHOW_ACTIVE, QStringLiteral("show-active")),
        std::make_pair(FilterMode::SHOW_DOWNLOADING, QStringLiteral("show-downloading")),
        std::make_pair(FilterMode::SHOW_SEEDING, QStringLiteral("show-seeding")),
        std::make_pair(FilterMode::SHOW_PAUSED, QStringLiteral("show-paused")),
        std::make_pair(FilterMode::SHOW_FINISHED, QStringLiteral("show-finished")),
        std::make_pair(FilterMode::SHOW_VERIFYING, QStringLiteral("show-verifying")),
        std::make_pair(FilterMode::SHOW_ERROR, QStringLiteral("show-error"))
        },
    SortModes{
        std::make_pair(SortMode::SORT_BY_NAME, QStringLiteral("sort-by-name")),
        std::make_pair(SortMode::SORT_BY_ACTIVITY, QStringLiteral("sort-by-activity")),
        std::make_pair(SortMode::SORT_BY_AGE, QStringLiteral("sort-by-age")),
        std::make_pair(SortMode::SORT_BY_ETA, QStringLiteral("sort-by-eta")),
        std::make_pair(SortMode::SORT_BY_PROGRESS, QStringLiteral("sort-by-progress")),
        std::make_pair(SortMode::SORT_BY_QUEUE, QStringLiteral("sort-by-queue")),
        std::make_pair(SortMode::SORT_BY_RATIO, QStringLiteral("sort-by-ratio")),
        std::make_pair(SortMode::SORT_BY_SIZE, QStringLiteral("sort-by-size")),
        std::make_pair(SortMode::SORT_BY_STATE, QStringLiteral("sort-by-state")),
        std::make_pair(SortMode::SORT_BY_ID, QStringLiteral("sort-by-id"))
        }
{
    static_assert(sizeof(Items) / sizeof(Items[0]) == PREFS_COUNT);

#ifndef NDEBUG
    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        assert(Items[i].id == i);
    }

#endif

    // these are the prefs that don't get saved to settings.json
    // when the application exits.
    temporary_prefs_ << FILTER_TEXT;

    tr_variant top;
    tr_variantInitDict(&top, 0);
    initDefaults(&top);
    tr_sessionLoadSettings(&top, config_dir_.toUtf8().constData(), nullptr);

    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        tr_variant* b(tr_variantDictFind(&top, Items[i].key));

        switch (Items[i].type)
        {
        case QVariant::Int:
            {
                auto const value = getValue<int64_t>(b);
                if (value)
                {
                    values_[i].setValue(*value);
                }
            }
            break;

        case CustomVariantType::SortModeType:
            {
                auto const value = getValue<QString>(b);
                if (value)
                {
                    auto test = [&value](auto const& item) { return item.second == *value; };
                    auto it = std::find_if(std::cbegin(SortModes), std::cend(SortModes), test);
                    auto const& pair = it == std::end(SortModes) ? SortModes[0] : *it;
                    values_[i] = QVariant::fromValue(SortMode(pair.first));
                }
            }
            break;

        case CustomVariantType::FilterModeType:
            {
                auto const value = getValue<QString>(b);
                if (value)
                {
                    auto test = [&value](auto const& item) { return item.second == *value; };
                    auto it = std::find_if(std::cbegin(FilterModes), std::cend(FilterModes), test);
                    auto const& pair = it == std::end(FilterModes) ? FilterModes[0] : *it;
                    values_[i] = QVariant::fromValue(FilterMode(pair.first));
                }
            }
            break;

        case QVariant::String:
            {
                auto const value = getValue<QString>(b);
                if (value)
                {
                    values_[i].setValue(*value);
                }
            }
            break;

        case QVariant::Bool:
            {
                auto const value = getValue<bool>(b);
                if (value)
                {
                    values_[i].setValue(*value);
                }
            }
            break;

        case QVariant::Double:
            {
                auto const value = getValue<double>(b);
                if (value)
                {
                    values_[i].setValue(*value);
                }
            }
            break;

        case QVariant::DateTime:
            {
                auto const value = getValue<time_t>(b);
                if (value)
                {
                    values_[i].setValue(QDateTime::fromTime_t(*value));
                }
            }
            break;

        default:
            assert(false && "unhandled type");
            break;
        }
    }

    tr_variantFree(&top);
}

Prefs::~Prefs()
{
    // make a dict from settings.json
    tr_variant current_settings;
    tr_variantInitDict(&current_settings, PREFS_COUNT);

    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        if (temporary_prefs_.contains(i))
        {
            continue;
        }

        tr_quark const key = Items[i].key;
        QVariant const& val = values_[i];

        switch (Items[i].type)
        {
        case QVariant::Int:
            dictAdd(&current_settings, key, val.toInt());
            break;

        case CustomVariantType::SortModeType:
            {
                auto const mode = val.value<SortMode>().mode();
                auto test = [&mode](auto const& item) { return item.first == mode; };
                auto it = std::find_if(std::cbegin(SortModes), std::cend(SortModes), test);
                auto const& pair = it == std::end(SortModes) ? SortModes[0] : *it;
                dictAdd(&current_settings, key, pair.second);
                break;
            }

        case CustomVariantType::FilterModeType:
            {
                auto const mode = val.value<FilterMode>().mode();
                auto test = [&mode](auto const& item) { return item.first == mode; };
                auto it = std::find_if(std::cbegin(FilterModes), std::cend(FilterModes), test);
                auto const& pair = it == std::end(FilterModes) ? FilterModes[0] : *it;
                dictAdd(&current_settings, key, pair.second);
                break;
            }

        case QVariant::String:
            dictAdd(&current_settings, key, val.toString());
            break;

        case QVariant::Bool:
            dictAdd(&current_settings, key, val.toBool());
            break;

        case QVariant::Double:
            dictAdd(&current_settings, key, val.toDouble());
            break;

        case QVariant::DateTime:
            dictAdd(&current_settings, key, val.toDateTime().toTime_t());
            break;

        default:
            assert(false && "unhandled type");
            break;
        }
    }

    // update settings.json with our settings
    tr_variant file_settings;
    QFile const file(QDir(config_dir_).absoluteFilePath(QStringLiteral("settings.json")));

    if (!tr_variantFromFile(&file_settings, TR_VARIANT_FMT_JSON, file.fileName().toUtf8().constData(), nullptr))
    {
        tr_variantInitDict(&file_settings, PREFS_COUNT);
    }

    tr_variantMergeDicts(&file_settings, &current_settings);
    tr_variantToFile(&file_settings, TR_VARIANT_FMT_JSON, file.fileName().toUtf8().constData());
    tr_variantFree(&file_settings);

    // cleanup
    tr_variantFree(&current_settings);
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
void Prefs::initDefaults(tr_variant* d)
{
    auto constexpr FilterMode = std::string_view { "all" };
    auto constexpr SessionHost = std::string_view { "localhost" };
    auto constexpr SessionPassword = std::string_view { "" };
    auto constexpr SessionUsername = std::string_view { "" };
    auto constexpr SortMode = std::string_view { "sort-by-name" };
    auto constexpr SoundCommand =
        std::string_view { "canberra-gtk-play -i complete-download -d 'transmission torrent downloaded'" };
    auto constexpr StatsMode = std::string_view { "total-ratio" };
    auto constexpr WindowLayout = std::string_view { "menu,toolbar,filter,list,statusbar" };

    auto const download_dir = std::string_view { tr_getDefaultDownloadDir() };

    tr_variantDictReserve(d, 38);
    dictAdd(d, TR_KEY_blocklist_updates_enabled, true);
    dictAdd(d, TR_KEY_compact_view, false);
    dictAdd(d, TR_KEY_inhibit_desktop_hibernation, false);
    dictAdd(d, TR_KEY_prompt_before_exit, true);
    dictAdd(d, TR_KEY_remote_session_enabled, false);
    dictAdd(d, TR_KEY_remote_session_requres_authentication, false);
    dictAdd(d, TR_KEY_show_backup_trackers, false);
    dictAdd(d, TR_KEY_show_extra_peer_details, false);
    dictAdd(d, TR_KEY_show_filterbar, true);
    dictAdd(d, TR_KEY_show_notification_area_icon, false);
    dictAdd(d, TR_KEY_start_minimized, false);
    dictAdd(d, TR_KEY_show_options_window, true);
    dictAdd(d, TR_KEY_show_statusbar, true);
    dictAdd(d, TR_KEY_show_toolbar, true);
    dictAdd(d, TR_KEY_show_tracker_scrapes, false);
    dictAdd(d, TR_KEY_sort_reversed, false);
    dictAdd(d, TR_KEY_torrent_added_notification_enabled, true);
    dictAdd(d, TR_KEY_torrent_complete_notification_enabled, true);
    dictAdd(d, TR_KEY_torrent_complete_sound_command, SoundCommand);
    dictAdd(d, TR_KEY_torrent_complete_sound_enabled, true);
    dictAdd(d, TR_KEY_user_has_given_informed_consent, false);
    dictAdd(d, TR_KEY_watch_dir_enabled, false);
    dictAdd(d, TR_KEY_blocklist_date, 0);
    dictAdd(d, TR_KEY_main_window_height, 500);
    dictAdd(d, TR_KEY_main_window_width, 300);
    dictAdd(d, TR_KEY_main_window_x, 50);
    dictAdd(d, TR_KEY_main_window_y, 50);
    dictAdd(d, TR_KEY_remote_session_port, TR_DEFAULT_RPC_PORT);
    dictAdd(d, TR_KEY_download_dir, download_dir);
    dictAdd(d, TR_KEY_filter_mode, FilterMode);
    dictAdd(d, TR_KEY_main_window_layout_order, WindowLayout);
    dictAdd(d, TR_KEY_open_dialog_dir, QDir::home().absolutePath());
    dictAdd(d, TR_KEY_remote_session_host, SessionHost);
    dictAdd(d, TR_KEY_remote_session_password, SessionPassword);
    dictAdd(d, TR_KEY_remote_session_username, SessionUsername);
    dictAdd(d, TR_KEY_sort_mode, SortMode);
    dictAdd(d, TR_KEY_statusbar_stats, StatsMode);
    dictAdd(d, TR_KEY_watch_dir, download_dir);
}

/***
****
***/

bool Prefs::getBool(int key) const
{
    assert(Items[key].type == QVariant::Bool);
    return values_[key].toBool();
}

QString Prefs::getString(int key) const
{
    assert(Items[key].type == QVariant::String);
    QByteArray const b = values_[key].toByteArray();

    if (Utils::isValidUtf8(b.constData()))
    {
        values_[key].setValue(QString::fromUtf8(b.constData()));
    }

    return values_[key].toString();
}

int Prefs::getInt(int key) const
{
    assert(Items[key].type == QVariant::Int);
    return values_[key].toInt();
}

double Prefs::getDouble(int key) const
{
    assert(Items[key].type == QVariant::Double);
    return values_[key].toDouble();
}

QDateTime Prefs::getDateTime(int key) const
{
    assert(Items[key].type == QVariant::DateTime);
    return values_[key].toDateTime();
}

/***
****
***/

void Prefs::toggleBool(int key)
{
    set(key, !getBool(key));
}
