// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Prefs.h"

#include "GtkCompat.h"
#include "PrefsDialog.h"

#include <libtransmission-app/display-modes.h>

#include <libtransmission/transmission.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <glibmm/miscutils.h>

#include <string>
#include <string_view>

using namespace std::literals;
using namespace tr::app;
using tr::serializer::to_variant;

namespace
{
std::string gl_confdir;

[[nodiscard]] std::string get_default_download_dir()
{
    if (auto dir = Glib::get_user_special_dir(TR_GLIB_USER_DIRECTORY(DOWNLOAD)); !std::empty(dir))
    {
        return dir;
    }

    if (auto dir = Glib::get_user_special_dir(TR_GLIB_USER_DIRECTORY(DESKTOP)); !std::empty(dir))
    {
        return dir;
    }

    return tr_getDefaultDownloadDir();
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
[[nodiscard]] tr_variant get_default_app_settings()
{
    auto const dir = get_default_download_dir();

    auto map = tr_variant::Map{};
    map.try_emplace(TR_KEY_blocklist_updates_enabled, true);
    map.try_emplace(TR_KEY_compact_view, false);
    map.try_emplace(TR_KEY_details_window_height, 500);
    map.try_emplace(TR_KEY_details_window_width, 700);
    map.try_emplace(TR_KEY_download_dir, dir);
    map.try_emplace(TR_KEY_inhibit_desktop_hibernation, false);
    map.try_emplace(TR_KEY_main_window_height, 500);
    map.try_emplace(TR_KEY_main_window_is_maximized, false);
    map.try_emplace(TR_KEY_main_window_width, 300);
    map.try_emplace(TR_KEY_main_window_x, 50);
    map.try_emplace(TR_KEY_main_window_y, 50);
    map.try_emplace(TR_KEY_open_dialog_dir, Glib::get_home_dir());
    map.try_emplace(TR_KEY_show_backup_trackers, false);
    map.try_emplace(TR_KEY_show_extra_peer_details, false);
    map.try_emplace(TR_KEY_show_filterbar, true);
    map.try_emplace(TR_KEY_show_notification_area_icon, false);
    map.try_emplace(TR_KEY_show_options_window, true);
    map.try_emplace(TR_KEY_show_statusbar, true);
    map.try_emplace(TR_KEY_show_toolbar, true);
    map.try_emplace(TR_KEY_show_tracker_scrapes, false);
    map.try_emplace(TR_KEY_sort_mode, to_variant(DefaultSortMode));
    map.try_emplace(TR_KEY_sort_reversed, false);
    map.try_emplace(TR_KEY_statusbar_stats, to_variant(DefaultStatsMode));
    map.try_emplace(TR_KEY_torrent_added_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_sound_enabled, true);
    map.try_emplace(TR_KEY_trash_can_enabled, true);
    map.try_emplace(TR_KEY_watch_dir, dir);
    map.try_emplace(TR_KEY_watch_dir_enabled, false);
    return tr_variant{ std::move(map) };
}

void ensure_sound_cmd_is_a_list(tr_variant* dict)
{
    tr_quark const key = TR_KEY_torrent_complete_sound_command;
    tr_variant* list = nullptr;
    if (tr_variantDictFindList(dict, key, &list))
    {
        return;
    }

    tr_variantDictRemove(dict, key);
    list = tr_variantDictAddList(dict, key, 5);
    tr_variantListAddStr(list, "canberra-gtk-play"sv);
    tr_variantListAddStr(list, "-i"sv);
    tr_variantListAddStr(list, "complete-download"sv);
    tr_variantListAddStr(list, "-d"sv);
    tr_variantListAddStr(list, "transmission torrent downloaded"sv);
}

tr_variant& getPrefs()
{
    static auto settings = tr_variant{};

    if (!settings.has_value())
    {
        auto const app_defaults = get_default_app_settings();
        settings.merge(tr_sessionLoadSettings(gl_confdir, &app_defaults));
        ensure_sound_cmd_is_a_list(&settings);
    }

    return settings;
}
} // namespace

void gtr_pref_init(std::string_view config_dir)
{
    gl_confdir = config_dir;
}

tr_variant& gtr_pref_get_all()
{
    return getPrefs();
}

tr_variant::Map& gtr_pref_get_map()
{
    return *getPrefs().get_if<tr_variant::Map>();
}

bool gtr_pref_has_key(tr_quark const key)
{
    return gtr_pref_get_map().contains(key);
}

double gtr_pref_double_get(tr_quark const key)
{
    return gtr_pref_lookup<double>(key).value_or(0.0);
}

void gtr_pref_double_set(tr_quark const key, double const value)
{
    gtr_pref_set<double>(key, value);
}

bool gtr_pref_flag_get(tr_quark const key)
{
    return gtr_pref_lookup<bool>(key).value_or(false);
}

void gtr_pref_flag_set(tr_quark const key, bool const value)
{
    gtr_pref_set<bool>(key, value);
}

std::vector<std::string> gtr_pref_strv_get(tr_quark const key)
{
    if (auto val = gtr_pref_lookup<std::vector<std::string>>(key))
    {
        return std::move(*val);
    }

    return {};
}

std::string gtr_pref_string_get(tr_quark const key)
{
    return gtr_pref_lookup<std::string>(key).value_or(std::string{});
}

void gtr_pref_string_set(tr_quark const key, std::string_view const value)
{
    gtr_pref_set<std::string>(key, std::string{ value });
}

void gtr_pref_save(tr_session* session)
{
    tr_sessionSaveSettings(session, gl_confdir, getPrefs());
}
