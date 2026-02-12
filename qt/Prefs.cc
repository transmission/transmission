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

#include "lib/base/variant.h"

#include "lib/base/serializer.h"

#include "libtransmission/transmission.h"
#include "libtransmission/api-compat.h"

#include "Filters.h"
#include "Prefs.h"
#include "UserMetaType.h"

namespace api_compat = tr::api_compat;
namespace ser = tr::serializer;
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
        return qvarFromOptional(ser::to_value<int64_t>(var));

    case UserMetaType::EncryptionModeType:
        return qvarFromOptional(ser::to_value<tr_encryption_mode>(var));

    case UserMetaType::SortModeType:
        return qvarFromOptional(ser::to_value<SortMode>(var));

    case UserMetaType::StatsModeType:
        return qvarFromOptional(ser::to_value<StatsMode>(var));

    case UserMetaType::ShowModeType:
        return qvarFromOptional(ser::to_value<ShowMode>(var));

    case QMetaType::QString:
        return qvarFromOptional(ser::to_value<QString>(var));

    case QMetaType::QStringList:
        return qvarFromOptional(ser::to_value<QStringList>(var));

    case QMetaType::Bool:
        return qvarFromOptional(ser::to_value<bool>(var));

    case QMetaType::Double:
        return qvarFromOptional(ser::to_value<double>(var));

    case QMetaType::QDateTime:
        return qvarFromOptional(ser::to_value<QDateTime>(var));

    default:
        assert(false && "unhandled type");
        return {};
    }
}

[[nodiscard]] tr_variant trvarFromQVar(QVariant const& var, int const qt_metatype)
{
    switch (qt_metatype)
    {
    case QMetaType::Int:
        return ser::to_variant(var.value<int>());

    case UserMetaType::EncryptionModeType:
        return ser::to_variant(var.value<tr_encryption_mode>());

    case UserMetaType::SortModeType:
        return ser::to_variant(var.value<SortMode>());

    case UserMetaType::ShowModeType:
        return ser::to_variant(var.value<ShowMode>());

    case UserMetaType::StatsModeType:
        return ser::to_variant(var.value<StatsMode>());

    case QMetaType::QString:
        return ser::to_variant(var.value<QString>());

    case QMetaType::QStringList:
        return ser::to_variant(var.value<QStringList>());

    case QMetaType::Bool:
        return ser::to_variant(var.value<bool>());

    case QMetaType::Double:
        return ser::to_variant(var.value<double>());

    case QMetaType::QDateTime:
        return ser::to_variant(var.value<QDateTime>());

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
        map.insert_or_assign(Key, ser::to_variant(DefaultVal));
    }
}
} // namespace

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

// ---

Prefs::Prefs()
{
    static_assert(sizeof(Items) / sizeof(Items[0]) == PREFS_COUNT);
#ifndef NDEBUG
    for (int i = 0; i < PREFS_COUNT; ++i)
    {
        assert(Items[i].id == i);
    }
#endif

    load(defaults());
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
        if (auto const iter = settings.find(Items[idx].key); iter != settings.end())
        {
            values_[idx] = qvarFromTVar(iter->second, Items[idx].type);
        }
    }
}

void Prefs::set(int const key, tr_variant const& value)
{
    auto const tmp = qvarFromTVar(value, Items[key].type);
    if (tmp.isNull())
    {
        return;
    }

    auto& v = values_[key];
    if (v.isNull() || v != tmp)
    {
        v = tmp;
        emit changed(key);
    }
}

tr_variant::Map Prefs::current_settings() const
{
    auto map = tr_variant::Map{ PREFS_COUNT };

    for (int idx = 0; idx < PREFS_COUNT; ++idx)
    {
        if (prefIsSavable(idx))
        {
            auto [key, val] = keyval(idx);
            map.try_emplace(key, std::move(val));
        }
    }

    return map;
}

std::pair<tr_quark, tr_variant> Prefs::keyval(int const idx) const
{
    return { Items[idx].key, trvarFromQVar(values_[idx], Items[idx].type) };
}

void Prefs::save(QString const& filename) const
{
    auto const filename_str = filename.toStdString();
    auto serde = tr_variant_serde::json();

    auto settings = tr_variant::make_map(PREFS_COUNT);
    if (auto const var = serde.parse_file(filename_str))
    {
        settings.merge(*var);
    }
    settings.merge(tr_variant{ current_settings() });
    api_compat::convert_outgoing_data(settings);
    serde.to_file(settings, filename_str);
}

/**
 * This is where we initialize the preferences file with the default values.
 * If you add a new preferences key, you /must/ add a default value here.
 */
// static
tr_variant::Map Prefs::defaults()
{
    auto const download_dir = tr_getDefaultDownloadDir();

    auto map = tr_variant::Map{ 64U };
    map.try_emplace(TR_KEY_blocklist_date, 0);
    map.try_emplace(TR_KEY_blocklist_updates_enabled, true);
    map.try_emplace(TR_KEY_compact_view, false);
    map.try_emplace(TR_KEY_download_dir, download_dir);
    map.try_emplace(TR_KEY_filter_mode, ser::to_variant(DefaultShowMode));
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
    map.try_emplace(TR_KEY_remote_session_url_base_path, tr_variant::unmanaged_string(TrDefaultHttpServerBasePath));
    map.try_emplace(TR_KEY_remote_session_username, tr_variant::unmanaged_string(""sv));
    map.try_emplace(TR_KEY_show_backup_trackers, false);
    map.try_emplace(TR_KEY_show_filterbar, true);
    map.try_emplace(TR_KEY_show_notification_area_icon, false);
    map.try_emplace(TR_KEY_show_options_window, true);
    map.try_emplace(TR_KEY_show_statusbar, true);
    map.try_emplace(TR_KEY_show_toolbar, true);
    map.try_emplace(TR_KEY_show_tracker_scrapes, false);
    map.try_emplace(TR_KEY_sort_mode, ser::to_variant(DefaultSortMode));
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
