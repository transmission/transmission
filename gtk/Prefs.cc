// This file copyright (C) Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <algorithm>

#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>

#include <libtransmission/utils.h>
#include <libtransmission/variant.h>

#include "Prefs.h"
#include "Utils.h"

namespace
{

auto constexpr MaxRecentDirs = size_t{ 4 };

std::string gl_confdir;

} // namespace

namespace ClientPrefs
{

void bind_window_state(Gtk::Window& window, Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix)
{
    auto const width_key = fmt::format("{}{}", key_prefix, WindowKeySuffix::Width);
    auto const height_key = fmt::format("{}{}", key_prefix, WindowKeySuffix::Height);
    auto const maximized_key = fmt::format("{}{}", key_prefix, WindowKeySuffix::Maximized);

#if GTKMM_CHECK_VERSION(4, 0, 0)

    settings->bind(width_key, window.property_default_width());
    settings->bind(height_key, window.property_default_height());
    settings->bind(maximized_key, window.property_maximized());

#else

    auto on_main_window_size_allocated = [&window, settings, width_key, height_key]()
    {
        if (window.is_maximized())
        {
            return;
        }

        int width = 0;
        int height = 0;
        window.get_size(width, height);

        if (settings->get_int(width_key) != width)
        {
            settings->set_int(width_key, width);
        }

        if (settings->get_int(height_key) != height)
        {
            settings->set_int(height_key, height);
        }
    };

    window.set_default_size(settings->get_int(width_key), settings->get_int(height_key));

    if (settings->get_boolean(maximized_key))
    {
        window.maximize();
    }

    settings->bind(maximized_key, window.property_is_maximized(), TR_GIO_SETTINGS_BIND_FLAGS(SET));
    window.signal_size_allocate().connect(sigc::hide<0>(on_main_window_size_allocated));

#endif
}

std::list<std::string> get_recent_dirs(Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix)
{
    auto const items = settings->get_string_array(fmt::format("{}{}", key_prefix, DirKeySuffix::Recents));
    auto result = std::list<std::string>();
    std::transform(items.begin(), items.end(), std::back_inserter(result), &Glib::locale_from_utf8);
    return result;
}

void save_recent_dir(Glib::RefPtr<Gio::Settings> const& settings, std::string_view key_prefix, std::string_view dir)
{
    if (dir.empty())
    {
        return;
    }

    auto const key = fmt::format("{}{}", key_prefix, DirKeySuffix::Recents);
    auto const dir_utf8 = Glib::locale_to_utf8(std::string(dir));

    auto items = std::vector<Glib::ustring>(settings->get_string_array(key));
    if (!items.empty() && items.front() == dir_utf8)
    {
        return;
    }

    items.erase(std::remove(items.begin(), items.end(), dir_utf8), items.end());
    items.emplace(items.begin(), dir_utf8);
    items.resize(std::min(items.size(), MaxRecentDirs));

    settings->set_string_array(key, items);
}

} // namespace ClientPrefs

void gtr_pref_init(std::string_view config_dir)
{
    gl_confdir = config_dir;
}

/***
****
****  Preferences
****
***/

namespace
{

std::string get_default_download_dir()
{
    auto dir = Glib::get_user_special_dir(TR_GLIB_USER_DIRECTORY(DOWNLOAD));

    if (dir.empty())
    {
        dir = Glib::get_user_special_dir(TR_GLIB_USER_DIRECTORY(DESKTOP));
    }

    if (dir.empty())
    {
        dir = tr_getDefaultDownloadDir();
    }

    return dir;
}

tr_variant* getPrefs()
{
    static tr_variant settings;
    static bool loaded = false;

    if (!loaded)
    {
        tr_variantInitDict(&settings, 1);
        tr_variantDictAddStr(&settings, TR_KEY_download_dir, get_default_download_dir());
        tr_sessionLoadSettings(&settings, gl_confdir.c_str(), nullptr);
        loaded = true;
    }

    return &settings;
}

} // namespace

/***
****
***/

tr_variant* gtr_pref_get_all()
{
    return getPrefs();
}

int64_t gtr_pref_int_get(tr_quark const key)
{
    int64_t i = 0;

    return tr_variantDictFindInt(getPrefs(), key, &i) ? i : 0;
}

void gtr_pref_int_set(tr_quark const key, int64_t value)
{
    tr_variantDictAddInt(getPrefs(), key, value);
}

double gtr_pref_double_get(tr_quark const key)
{
    double d = 0;

    return tr_variantDictFindReal(getPrefs(), key, &d) ? d : 0.0;
}

void gtr_pref_double_set(tr_quark const key, double value)
{
    tr_variantDictAddReal(getPrefs(), key, value);
}

/***
****
***/

bool gtr_pref_flag_get(tr_quark const key)
{
    bool boolVal = false;

    return tr_variantDictFindBool(getPrefs(), key, &boolVal) ? boolVal : false;
}

void gtr_pref_flag_set(tr_quark const key, bool value)
{
    tr_variantDictAddBool(getPrefs(), key, value);
}

/***
****
***/

std::string gtr_pref_string_get(tr_quark const key)
{
    auto sv = std::string_view{};
    (void)tr_variantDictFindStrView(getPrefs(), key, &sv);
    return std::string{ sv };
}

void gtr_pref_string_set(tr_quark const key, std::string_view value)
{
    tr_variantDictAddStr(getPrefs(), key, value);
}

/***
****
***/

void gtr_pref_save(tr_session* session)
{
    tr_sessionSaveSettings(session, gl_confdir.c_str(), getPrefs());
}
