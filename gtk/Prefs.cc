// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include "Prefs.h"

#include "GtkCompat.h"
#include "PrefsDialog.h"

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include <glibmm/miscutils.h>

#include <string>
#include <string_view>

using namespace std::literals;

std::string gl_confdir;

void gtr_pref_init(std::string_view config_dir)
{
    gl_confdir = config_dir;
}

namespace
{
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
    map.try_emplace(TR_KEY_sort_mode, "sort-by-name"sv);
    map.try_emplace(TR_KEY_sort_reversed, false);
    map.try_emplace(TR_KEY_statusbar_stats, "total-ratio"sv);
    map.try_emplace(TR_KEY_torrent_added_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_notification_enabled, true);
    map.try_emplace(TR_KEY_torrent_complete_sound_enabled, true);
    map.try_emplace(TR_KEY_trash_can_enabled, true);
    map.try_emplace(TR_KEY_user_has_given_informed_consent, false);
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
        settings = get_default_app_settings();
        settings.merge(tr_sessionLoadSettings(gl_confdir.c_str(), nullptr));
        ensure_sound_cmd_is_a_list(&settings);
    }

    return settings;
}
} // namespace

tr_variant& gtr_pref_get_all()
{
    return getPrefs();
}

int64_t gtr_pref_int_get(tr_quark const key)
{
    int64_t i = 0;

    return tr_variantDictFindInt(&getPrefs(), key, &i) ? i : 0;
}

void gtr_pref_int_set(tr_quark const key, int64_t value)
{
    tr_variantDictAddInt(&getPrefs(), key, value);
}

double gtr_pref_double_get(tr_quark const key)
{
    double d = 0;

    return tr_variantDictFindReal(&getPrefs(), key, &d) ? d : 0.0;
}

void gtr_pref_double_set(tr_quark const key, double value)
{
    tr_variantDictAddReal(&getPrefs(), key, value);
}

// ---

bool gtr_pref_flag_get(tr_quark const key)
{
    bool boolVal = false;

    return tr_variantDictFindBool(&getPrefs(), key, &boolVal) ? boolVal : false;
}

void gtr_pref_flag_set(tr_quark const key, bool value)
{
    tr_variantDictAddBool(&getPrefs(), key, value);
}

// ---

std::vector<std::string> gtr_pref_strv_get(tr_quark const key)
{
    std::vector<std::string> ret;

    if (tr_variant* list = nullptr; tr_variantDictFindList(&getPrefs(), key, &list))
    {
        size_t const n = tr_variantListSize(list);
        ret.reserve(n);

        for (size_t i = 0; i < n; ++i)
        {
            auto sv = std::string_view{};
            if (tr_variantGetStrView(tr_variantListChild(list, i), &sv))
            {
                ret.emplace_back(sv);
            }
        }
    }

    return ret;
}

std::string gtr_pref_string_get(tr_quark const key)
{
    auto sv = std::string_view{};
    (void)tr_variantDictFindStrView(&getPrefs(), key, &sv);
    return std::string{ sv };
}

void gtr_pref_string_set(tr_quark const key, std::string_view value)
{
    tr_variantDictAddStr(&getPrefs(), key, value);
}

// ---

void gtr_pref_save(tr_session* session)
{
    tr_sessionSaveSettings(session, gl_confdir.c_str(), getPrefs());
}
