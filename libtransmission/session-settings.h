// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef> // size_t
#include <cstdint> // uint8_t
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include <small/vector.hpp>

#include "libtransmission/constants.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/serializer.h"
#include "libtransmission/types.h"
#include "libtransmission/variant.h"

namespace tr
{

namespace detail
{

template<typename Settings>
[[nodiscard]] bool settings_fields_contains(tr_quark key)
{
    auto found = false;
    std::apply([&found, key](auto const&... field) { ((found = found || field.key == key), ...); }, Settings::fields());
    return found;
}

} // namespace detail

struct SessionSettings final
{
public:
    SessionSettings() = default;

    explicit SessionSettings(tr_variant const& src)
    {
        load(src);
    }

    void fixup_from_preferred_transports();
    void fixup_to_preferred_transports();

    void load(tr_variant const& src)
    {
        tr::serializer::load(*this, Fields, src);

        if (auto const* map = src.get_if<tr_variant::Map>())
        {
            if (map->contains(TR_KEY_preferred_transports))
            {
                fixup_from_preferred_transports();
            }
            else
            {
                fixup_to_preferred_transports();
            }
        }
    }

    [[nodiscard]] tr_variant::Map save() const
    {
        return tr::serializer::save(*this, Fields);
    }

    [[nodiscard]] static constexpr auto const& fields() noexcept
    {
        return Fields;
    }

    bool announce_ip_enabled = false;
    bool blocklist_enabled = false;
    bool dht_enabled = true;
    bool download_queue_enabled = true;
    bool idle_seeding_limit_enabled = false;
    bool incomplete_dir_enabled = false;
    bool is_incomplete_file_naming_enabled = true;
    bool lpd_enabled = true;
    bool peer_port_random_on_start = false;
    bool pex_enabled = true;
    bool port_forwarding_enabled = true;
    bool queue_stalled_enabled = true;
    bool ratio_limit_enabled = false;
    bool script_torrent_added_enabled = false;
    bool script_torrent_done_enabled = false;
    bool script_torrent_done_seeding_enabled = false;
    bool seed_queue_enabled = false;
    bool sequential_download = false;
    bool should_delete_source_torrents = false;
    bool should_scrape_paused_torrents = true;
    bool should_start_added_torrents = true;
    bool speed_limit_down_enabled = false;
    bool speed_limit_up_enabled = false;
    bool tcp_enabled = true;
    bool torrent_complete_verify_enabled = false;
    bool utp_enabled = true;
    double ratio_limit = 2.0;
    size_t unused_cache_size_mbytes = 4U; // TODO(TR5): remove
    size_t download_queue_size = 5U;
    size_t peer_limit_global = TrDefaultPeerLimitGlobal;
    size_t peer_limit_per_torrent = TrDefaultPeerLimitTorrent;
    size_t queue_stalled_minutes = 30U;
    size_t reqq = 2000U;
    size_t seed_queue_size = 10U;
    size_t speed_limit_down = 100U;
    size_t speed_limit_up = 100U;
    size_t upload_slots_per_torrent = 8U;
    small::max_size_vector<tr_preferred_transport, PreferredTransportCount> preferred_transports = {
        tr_preferred_transport::UTP,
        tr_preferred_transport::TCP,
    };
    std::vector<std::string> ip_endpoint_ipv4 = { "https://ip4.transmissionbt.com/" };
    std::vector<std::string> ip_endpoint_ipv6 = { "https://ip6.transmissionbt.com/" };
    std::chrono::milliseconds sleep_per_seconds_during_verify = std::chrono::milliseconds{ 100 };
    std::optional<std::string> proxy_url;
    std::string announce_ip;
    std::string bind_address_ipv4;
    std::string bind_address_ipv6;
    std::string bind_interface;
    std::string blocklist_url = "http://www.example.com/blocklist";
    std::string default_trackers_str;
    std::string download_dir = get_default_download_dir();
    std::string incomplete_dir = get_default_download_dir();
    std::string peer_congestion_algorithm;
    std::string script_torrent_added_filename;
    std::string script_torrent_done_filename;
    std::string script_torrent_done_seeding_filename;
    tr_encryption_mode encryption_mode = TR_ENCRYPTION_PREFERRED;
    tr_log_level log_level = TR_LOG_INFO;
    tr_mode_t umask = 022;
    tr_file_preallocation preallocation_mode = tr_file_preallocation::Sparse;
    tr_port peer_port_random_high = tr_port::from_host(65535);
    tr_port peer_port_random_low = tr_port::from_host(49152);
    tr_port peer_port = tr_port::from_host(TrDefaultPeerPort);
    tr_diffserv_t peer_socket_diffserv{ 0x04 };
    tr_verify_added_mode torrent_added_verify_mode = TR_VERIFY_ADDED_FAST;
    uint16_t idle_seeding_limit_minutes = 30U;

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

    [[nodiscard]] static std::string get_default_download_dir();

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&SessionSettings::announce_ip>{ TR_KEY_announce_ip },
        Field<&SessionSettings::announce_ip_enabled>{ TR_KEY_announce_ip_enabled },
        Field<&SessionSettings::bind_address_ipv4>{ TR_KEY_bind_address_ipv4 },
        Field<&SessionSettings::bind_address_ipv6>{ TR_KEY_bind_address_ipv6 },
        Field<&SessionSettings::bind_interface>{ TR_KEY_bind_interface },
        Field<&SessionSettings::blocklist_enabled>{ TR_KEY_blocklist_enabled },
        Field<&SessionSettings::blocklist_url>{ TR_KEY_blocklist_url },
        Field<&SessionSettings::unused_cache_size_mbytes>{ TR_KEY_cache_size_mib },
        Field<&SessionSettings::default_trackers_str>{ TR_KEY_default_trackers },
        Field<&SessionSettings::dht_enabled>{ TR_KEY_dht_enabled },
        Field<&SessionSettings::download_dir>{ TR_KEY_download_dir },
        Field<&SessionSettings::download_queue_enabled>{ TR_KEY_download_queue_enabled },
        Field<&SessionSettings::download_queue_size>{ TR_KEY_download_queue_size },
        Field<&SessionSettings::encryption_mode>{ TR_KEY_encryption },
        Field<&SessionSettings::idle_seeding_limit_minutes>{ TR_KEY_idle_seeding_limit },
        Field<&SessionSettings::idle_seeding_limit_enabled>{ TR_KEY_idle_seeding_limit_enabled },
        Field<&SessionSettings::incomplete_dir>{ TR_KEY_incomplete_dir },
        Field<&SessionSettings::incomplete_dir_enabled>{ TR_KEY_incomplete_dir_enabled },
        Field<&SessionSettings::ip_endpoint_ipv4>{ TR_KEY_ip_endpoints_ipv4 },
        Field<&SessionSettings::ip_endpoint_ipv6>{ TR_KEY_ip_endpoints_ipv6 },
        Field<&SessionSettings::lpd_enabled>{ TR_KEY_lpd_enabled },
        Field<&SessionSettings::log_level>{ TR_KEY_message_level },
        Field<&SessionSettings::peer_congestion_algorithm>{ TR_KEY_peer_congestion_algorithm },
        Field<&SessionSettings::peer_limit_global>{ TR_KEY_peer_limit_global },
        Field<&SessionSettings::peer_limit_per_torrent>{ TR_KEY_peer_limit_per_torrent },
        Field<&SessionSettings::peer_port>{ TR_KEY_peer_port },
        Field<&SessionSettings::peer_port_random_high>{ TR_KEY_peer_port_random_high },
        Field<&SessionSettings::peer_port_random_low>{ TR_KEY_peer_port_random_low },
        Field<&SessionSettings::peer_port_random_on_start>{ TR_KEY_peer_port_random_on_start },
        Field<&SessionSettings::peer_socket_diffserv>{ TR_KEY_peer_socket_diffserv },
        Field<&SessionSettings::pex_enabled>{ TR_KEY_pex_enabled },
        Field<&SessionSettings::port_forwarding_enabled>{ TR_KEY_port_forwarding_enabled },
        Field<&SessionSettings::preallocation_mode>{ TR_KEY_preallocation },
        Field<&SessionSettings::preferred_transports>{ TR_KEY_preferred_transports },
        Field<&SessionSettings::proxy_url>{ TR_KEY_proxy_url },
        Field<&SessionSettings::queue_stalled_enabled>{ TR_KEY_queue_stalled_enabled },
        Field<&SessionSettings::queue_stalled_minutes>{ TR_KEY_queue_stalled_minutes },
        Field<&SessionSettings::ratio_limit>{ TR_KEY_seed_ratio_limit },
        Field<&SessionSettings::ratio_limit_enabled>{ TR_KEY_seed_ratio_limited },
        Field<&SessionSettings::is_incomplete_file_naming_enabled>{ TR_KEY_rename_partial_files },
        Field<&SessionSettings::reqq>{ TR_KEY_reqq },
        Field<&SessionSettings::should_scrape_paused_torrents>{ TR_KEY_scrape_paused_torrents_enabled },
        Field<&SessionSettings::script_torrent_added_enabled>{ TR_KEY_script_torrent_added_enabled },
        Field<&SessionSettings::script_torrent_added_filename>{ TR_KEY_script_torrent_added_filename },
        Field<&SessionSettings::script_torrent_done_enabled>{ TR_KEY_script_torrent_done_enabled },
        Field<&SessionSettings::script_torrent_done_filename>{ TR_KEY_script_torrent_done_filename },
        Field<&SessionSettings::script_torrent_done_seeding_enabled>{ TR_KEY_script_torrent_done_seeding_enabled },
        Field<&SessionSettings::script_torrent_done_seeding_filename>{ TR_KEY_script_torrent_done_seeding_filename },
        Field<&SessionSettings::seed_queue_enabled>{ TR_KEY_seed_queue_enabled },
        Field<&SessionSettings::seed_queue_size>{ TR_KEY_seed_queue_size },
        Field<&SessionSettings::sequential_download>{ TR_KEY_sequential_download },
        Field<&SessionSettings::sleep_per_seconds_during_verify>{ TR_KEY_sleep_per_seconds_during_verify },
        Field<&SessionSettings::speed_limit_down>{ TR_KEY_speed_limit_down },
        Field<&SessionSettings::speed_limit_down_enabled>{ TR_KEY_speed_limit_down_enabled },
        Field<&SessionSettings::speed_limit_up>{ TR_KEY_speed_limit_up },
        Field<&SessionSettings::speed_limit_up_enabled>{ TR_KEY_speed_limit_up_enabled },
        Field<&SessionSettings::should_start_added_torrents>{ TR_KEY_start_added_torrents },
        Field<&SessionSettings::tcp_enabled>{ TR_KEY_tcp_enabled },
        Field<&SessionSettings::torrent_added_verify_mode>{ TR_KEY_torrent_added_verify_mode },
        Field<&SessionSettings::torrent_complete_verify_enabled>{ TR_KEY_torrent_complete_verify_enabled },
        Field<&SessionSettings::should_delete_source_torrents>{ TR_KEY_trash_original_torrent_files },
        Field<&SessionSettings::umask>{ TR_KEY_umask },
        Field<&SessionSettings::upload_slots_per_torrent>{ TR_KEY_upload_slots_per_torrent },
        Field<&SessionSettings::utp_enabled>{ TR_KEY_utp_enabled });
};

struct SessionAltSpeedSettings final
{
public:
    SessionAltSpeedSettings() = default;

    explicit SessionAltSpeedSettings(tr_variant const& src)
    {
        load(src);
    }

    void load(tr_variant const& src)
    {
        tr::serializer::load(*this, Fields, src);
    }

    [[nodiscard]] tr_variant::Map save() const
    {
        return tr::serializer::save(*this, Fields);
    }

    [[nodiscard]] static constexpr auto const& fields() noexcept
    {
        return Fields;
    }

    bool is_active = false;
    bool scheduler_enabled = false; // whether alt speeds toggle on and off on schedule
    size_t minute_begin = 540U; // minutes past midnight; 9AM
    size_t minute_end = 1020U; // minutes past midnight; 5PM
    size_t speed_down_kbyps = 50U;
    size_t speed_up_kbyps = 50U;
    tr_sched_day use_on_these_weekdays = TR_SCHED_ALL;

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&SessionAltSpeedSettings::is_active>{ TR_KEY_alt_speed_enabled },
        Field<&SessionAltSpeedSettings::speed_up_kbyps>{ TR_KEY_alt_speed_up },
        Field<&SessionAltSpeedSettings::speed_down_kbyps>{ TR_KEY_alt_speed_down },
        Field<&SessionAltSpeedSettings::scheduler_enabled>{ TR_KEY_alt_speed_time_enabled },
        Field<&SessionAltSpeedSettings::use_on_these_weekdays>{ TR_KEY_alt_speed_time_day },
        Field<&SessionAltSpeedSettings::minute_begin>{ TR_KEY_alt_speed_time_begin },
        Field<&SessionAltSpeedSettings::minute_end>{ TR_KEY_alt_speed_time_end });
};

struct RpcServerSettings final
{
public:
    RpcServerSettings() = default;

    explicit RpcServerSettings(tr_variant const& src)
    {
        load(src);
    }

    void load(tr_variant const& src)
    {
        tr::serializer::load(*this, Fields, src);
    }

    [[nodiscard]] tr_variant::Map save() const
    {
        return tr::serializer::save(*this, Fields);
    }

    [[nodiscard]] static constexpr auto const& fields() noexcept
    {
        return Fields;
    }

    bool authentication_required = false;
    bool is_anti_brute_force_enabled = false;
    bool is_enabled = false;
    bool is_host_whitelist_enabled = true;
    bool is_whitelist_enabled = true;
    size_t anti_brute_force_limit = 100U;
    std::string bind_address_str = "0.0.0.0";
    std::string host_whitelist_str;
    std::string salted_password;
    std::string url = std::string{ TrDefaultHttpServerBasePath };
    std::string username;
    std::string whitelist_str = std::string{ TrDefaultRpcWhitelist };
    tr_mode_t socket_mode = 0750;
    tr_port port = tr_port::from_host(TrDefaultRpcPort);

private:
    template<auto MemberPtr>
    using Field = tr::serializer::Field<MemberPtr>;

public:
    static constexpr auto Fields = std::make_tuple(
        Field<&RpcServerSettings::is_anti_brute_force_enabled>{ TR_KEY_anti_brute_force_enabled },
        Field<&RpcServerSettings::anti_brute_force_limit>{ TR_KEY_anti_brute_force_threshold },
        Field<&RpcServerSettings::authentication_required>{ TR_KEY_rpc_authentication_required },
        Field<&RpcServerSettings::bind_address_str>{ TR_KEY_rpc_bind_address },
        Field<&RpcServerSettings::is_enabled>{ TR_KEY_rpc_enabled },
        Field<&RpcServerSettings::host_whitelist_str>{ TR_KEY_rpc_host_whitelist },
        Field<&RpcServerSettings::is_host_whitelist_enabled>{ TR_KEY_rpc_host_whitelist_enabled },
        Field<&RpcServerSettings::port>{ TR_KEY_rpc_port },
        Field<&RpcServerSettings::salted_password>{ TR_KEY_rpc_password },
        Field<&RpcServerSettings::socket_mode>{ TR_KEY_rpc_socket_mode },
        Field<&RpcServerSettings::url>{ TR_KEY_rpc_url },
        Field<&RpcServerSettings::username>{ TR_KEY_rpc_username },
        Field<&RpcServerSettings::whitelist_str>{ TR_KEY_rpc_whitelist },
        Field<&RpcServerSettings::is_whitelist_enabled>{ TR_KEY_rpc_whitelist_enabled });
};

struct SessionSettingsSnapshot final
{
    enum class Group : uint8_t
    {
        Session,
        AltSpeeds,
        RpcServer
    };

    SessionSettings session;
    SessionAltSpeedSettings alt_speeds;
    RpcServerSettings rpc_server;

    SessionSettingsSnapshot() = default;

    explicit SessionSettingsSnapshot(tr_variant const& src)
    {
        load(src);
    }

    void load(tr_variant const& src)
    {
        session.load(src);
        alt_speeds.load(src);
        rpc_server.load(src);
    }

    [[nodiscard]] tr_variant::Map save() const
    {
        auto map = session.save();

        for (auto& [key, value] : alt_speeds.save())
        {
            map.insert_or_assign(key, std::move(value));
        }

        for (auto& [key, value] : rpc_server.save())
        {
            map.insert_or_assign(key, std::move(value));
        }

        return map;
    }

    [[nodiscard]] static std::optional<Group> classify(tr_quark key)
    {
        if (detail::settings_fields_contains<SessionSettings>(key))
        {
            return Group::Session;
        }

        if (detail::settings_fields_contains<SessionAltSpeedSettings>(key))
        {
            return Group::AltSpeeds;
        }

        if (detail::settings_fields_contains<RpcServerSettings>(key))
        {
            return Group::RpcServer;
        }

        return {};
    }

    [[nodiscard]] static bool has_key(tr_quark key)
    {
        return classify(key).has_value();
    }

    template<typename T>
    [[nodiscard]] std::optional<T> get(tr_quark key) const
    {
        if (auto value = tr::serializer::get<T>(session, key))
        {
            return value;
        }

        if (auto value = tr::serializer::get<T>(alt_speeds, key))
        {
            return value;
        }

        return tr::serializer::get<T>(rpc_server, key);
    }

    template<typename T>
    bool set(tr_quark key, T value)
    {
        return tr::serializer::set(session, key, value) || tr::serializer::set(alt_speeds, key, value) ||
            tr::serializer::set(rpc_server, key, std::move(value));
    }

    bool set(tr_quark key, tr_variant const& value)
    {
        return tr::serializer::set_from_variant(session, key, value) ||
            tr::serializer::set_from_variant(alt_speeds, key, value) ||
            tr::serializer::set_from_variant(rpc_server, key, value);
    }

    [[nodiscard]] std::optional<std::pair<tr_quark, tr_variant>> keyval(tr_quark key) const
    {
        if (auto value = tr::serializer::to_variant(session, key))
        {
            return { { key, std::move(*value) } };
        }

        if (auto value = tr::serializer::to_variant(alt_speeds, key))
        {
            return { { key, std::move(*value) } };
        }

        if (auto value = tr::serializer::to_variant(rpc_server, key))
        {
            return { { key, std::move(*value) } };
        }

        return {};
    }
};

} // namespace tr
