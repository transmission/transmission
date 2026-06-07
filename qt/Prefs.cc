// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cmath>
#include <cassert>
#include <limits>
#include <string_view>
#include <utility>

#include <QDateTime>

#include <libtransmission/transmission.h>

#include <libtransmission/api-compat.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include "Filters.h"
#include "Prefs.h"
#include "UserMetaType.h"

namespace api_compat = tr::api_compat;
namespace ser = tr::serializer;
using namespace std::string_view_literals;

// ---

Prefs::Prefs(tr_variant const& settings)
{
    tr::serializer::load(*this, Fields, settings);
}

Prefs::Prefs(QString const& dir)
{
    auto settings = tr_sessionLoadSettings(dir.toStdString());
    if (settings.holds_alternative<tr_variant::Map>())
    {
        tr::serializer::load(*this, Fields, settings);
    }
}

Prefs::PrefItem const& Prefs::item(tr_quark const key)
{
    for (auto const& pref : Items)
    {
        if (pref.key == key)
        {
            return pref;
        }
    }

    assert(false && "unknown pref key");
    return Items[0];
}

bool Prefs::isCore(tr_quark const key)
{
    switch (key)
    {
    case TR_KEY_alt_speed_up:
    case TR_KEY_alt_speed_down:
    case TR_KEY_alt_speed_enabled:
    case TR_KEY_alt_speed_time_begin:
    case TR_KEY_alt_speed_time_end:
    case TR_KEY_alt_speed_time_enabled:
    case TR_KEY_alt_speed_time_day:
    case TR_KEY_blocklist_enabled:
    case TR_KEY_blocklist_url:
    case TR_KEY_default_trackers:
    case TR_KEY_speed_limit_down:
    case TR_KEY_speed_limit_down_enabled:
    case TR_KEY_download_dir:
    case TR_KEY_download_queue_enabled:
    case TR_KEY_download_queue_size:
    case TR_KEY_encryption:
    case TR_KEY_idle_seeding_limit:
    case TR_KEY_idle_seeding_limit_enabled:
    case TR_KEY_incomplete_dir:
    case TR_KEY_incomplete_dir_enabled:
    case TR_KEY_message_level:
    case TR_KEY_peer_limit_global:
    case TR_KEY_peer_limit_per_torrent:
    case TR_KEY_peer_port:
    case TR_KEY_peer_port_random_on_start:
    case TR_KEY_peer_port_random_low:
    case TR_KEY_peer_port_random_high:
    case TR_KEY_queue_stalled_minutes:
    case TR_KEY_script_torrent_done_enabled:
    case TR_KEY_script_torrent_done_filename:
    case TR_KEY_script_torrent_done_seeding_enabled:
    case TR_KEY_script_torrent_done_seeding_filename:
    case TR_KEY_peer_socket_diffserv:
    case TR_KEY_start_added_torrents:
    case TR_KEY_trash_original_torrent_files:
    case TR_KEY_pex_enabled:
    case TR_KEY_dht_enabled:
    case TR_KEY_utp_enabled:
    case TR_KEY_lpd_enabled:
    case TR_KEY_port_forwarding_enabled:
    case TR_KEY_preallocation:
    case TR_KEY_seed_ratio_limit:
    case TR_KEY_seed_ratio_limited:
    case TR_KEY_rename_partial_files:
    case TR_KEY_rpc_authentication_required:
    case TR_KEY_rpc_enabled:
    case TR_KEY_rpc_password:
    case TR_KEY_rpc_port:
    case TR_KEY_rpc_username:
    case TR_KEY_rpc_whitelist_enabled:
    case TR_KEY_rpc_whitelist:
    case TR_KEY_speed_limit_up_enabled:
    case TR_KEY_speed_limit_up:
    case TR_KEY_upload_slots_per_torrent:
        return true;

    default:
        return false;
    }
}

tr_variant::Map Prefs::current_settings() const
{
    auto map = tr::serializer::save(*this, Fields);
    map.erase(TR_KEY_filter_text);

    return map;
}

std::pair<tr_quark, tr_variant> Prefs::keyvalImpl(tr_quark const key) const
{
    auto const& pref = item(key);
    auto const map = tr::serializer::save(*this, Fields);
    auto const iter = map.find(pref.key);
    assert(iter != std::end(map));
    return std::pair<tr_quark, tr_variant>{ pref.key, iter->second.clone() };
}

void Prefs::save(QString const& filename) const
{
    auto const filename_str = filename.toStdString();
    auto serde = tr_variant_serde::json();

    auto settings = tr_variant::make_map(Items.size());
    if (auto var = serde.parse_file(filename_str))
    {
        api_compat::convert_incoming_data(*var);
        settings.merge(*var);
    }
    settings.merge(tr_variant{ current_settings() });
    settings.get_if<tr_variant::Map>()->erase(TR_KEY_filter_text);
    api_compat::convert_outgoing_data(settings);
    serde.to_file(settings, filename_str);
}
