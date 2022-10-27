// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint> // for int64_t
#include <string>
#include <string_view>
#include <utility> // for std::in_place_index
#include <variant>

#include "transmission.h"

#include "log.h" // for tr_log_level
#include "net.h" // for tr_port
#include "quark.h"
#include "rpc-server.h"
#include "session-settings.h"
#include "tr-assert.h"
#include "utils.h"
#include "variant.h"

using namespace std::literals;

namespace libtransmission
{

Setting::Setting(tr_quark key, Type type, Value const& default_value)
    : key_{ key }
    , type_{ type }
    , default_value_{ default_value }
    , value_{ default_value }
{
}

void Setting::fromDict(tr_variant* dict)
{
    if (auto* const child = tr_variantDictFind(dict, key_); child != nullptr)
    {
        if (auto const value = import(child, type_); value)
        {
            value_ = *value;
        }
    }
}

[[nodiscard]] std::optional<Setting::Value> Setting::import(tr_variant* var, Type type)
{
    switch (type)
    {
    case Type::Bool:
        if (auto val = bool{}; tr_variantGetBool(var, &val))
        {
            return Value{ std::in_place_type<bool>, val };
        }
        break;

    case Type::Double:
        if (auto val = double{}; tr_variantGetReal(var, &val))
        {
            return Value{ std::in_place_type<double>, val };
        }
        break;

    case Type::Encryption:
        {
            static auto constexpr Keys = std::array<std::pair<std::string_view, tr_encryption_mode>, 3>{
                { { "required", TR_ENCRYPTION_REQUIRED },
                  { "preferred", TR_ENCRYPTION_PREFERRED },
                  { "allowed", TR_CLEAR_PREFERRED } }
            };

            if (auto val = std::string_view{}; tr_variantGetStrView(var, &val))
            {
                auto const needle = tr_strlower(tr_strvStrip(val));

                for (auto const& [key, encryption] : Keys)
                {
                    if (key == needle)
                    {
                        return Value{ std::in_place_type<tr_encryption_mode>, static_cast<tr_encryption_mode>(encryption) };
                    }
                }
            }
            if (auto val = int64_t{}; tr_variantGetInt(var, &val))
            {
                for (auto const& [key, encryption] : Keys)
                {
                    if (encryption == val)
                    {
                        return Value{ std::in_place_type<tr_encryption_mode>, static_cast<tr_encryption_mode>(encryption) };
                    }
                }
            }
        }
        break;

    case Type::Int:
        if (auto val = int64_t{}; tr_variantGetInt(var, &val))
        {
            return Value{ std::in_place_type<int>, static_cast<int>(val) };
        }
        break;

    case Type::Int64:
        if (auto val = int64_t{}; tr_variantGetInt(var, &val))
        {
            return Value{ std::in_place_type<int64_t>, val };
        }
        break;

    case Type::Log:
        {
            static auto constexpr Keys = std::array<std::pair<std::string_view, tr_log_level>, 7>{ {
                { "critical"sv, TR_LOG_CRITICAL },
                { "debug"sv, TR_LOG_DEBUG },
                { "error"sv, TR_LOG_ERROR },
                { "info"sv, TR_LOG_INFO },
                { "off"sv, TR_LOG_OFF },
                { "trace"sv, TR_LOG_TRACE },
                { "warn"sv, TR_LOG_WARN },
            } };
            if (auto val = std::string_view{}; tr_variantGetStrView(var, &val))
            {

                auto const needle = tr_strlower(tr_strvStrip(val));
                for (auto const& [name, log_level] : Keys)
                {
                    if (needle == name)
                    {
                        return Value{ std::in_place_type<tr_log_level>, log_level };
                    }
                }
            }
            if (auto val = int64_t{}; tr_variantGetInt(var, &val))
            {
                for (auto const& [name, log_level] : Keys)
                {
                    if (log_level == val)
                    {
                        return log_level;
                    }
                }
            }
        }
        break;

    case Type::ModeT:
        if (auto val = std::string_view{}; tr_variantGetStrView(var, &val))
        {
            if (auto const mode = tr_parseNum<uint32_t>(val, nullptr, 8); mode)
            {
                return Value{ std::in_place_type<mode_t>, *mode };
            }
        }
        if (auto val = int64_t{}; tr_variantGetInt(var, &val))
        {
            return Value{ std::in_place_type<mode_t>, static_cast<mode_t>(val) };
        }
        break;

    case Type::Port:
        if (auto val = int64_t{}; tr_variantGetInt(var, &val))
        {
            return Value{ std::in_place_type<tr_port>, tr_port::fromHost(val) };
        }
        break;

    case Type::Preallocation:
        {
            static auto constexpr Keys = std::array<std::pair<std::string_view, tr_preallocation_mode>, 4>{ {
                { "none"sv, TR_PREALLOCATE_NONE },
                { "fast"sv, TR_PREALLOCATE_SPARSE },
                { "sparse"sv, TR_PREALLOCATE_SPARSE },
                { "full"sv, TR_PREALLOCATE_FULL },
            } };
            if (auto val = std::string_view{}; tr_variantGetStrView(var, &val))
            {

                auto const needle = tr_strlower(tr_strvStrip(val));
                for (auto const& [key, value] : Keys)
                {
                    if (key == needle)
                    {
                        return Value{ std::in_place_type<tr_preallocation_mode>, value };
                    }
                }
            }
            if (auto val = int64_t{}; tr_variantGetInt(var, &val))
            {
                for (auto const& [name, value] : Keys)
                {
                    if (value == val)
                    {
                        return Value{ std::in_place_type<tr_preallocation_mode>, value };
                    }
                }
            }
        }
        break;

    case Type::SizeT:
        if (auto val = int64_t{}; tr_variantGetInt(var, &val))
        {
            return Value{ std::in_place_type<size_t>, static_cast<size_t>(val) };
        }
        break;

    case Type::String:
        if (auto val = std::string_view{}; tr_variantGetStrView(var, &val))
        {
            return Value{ std::in_place_type<std::string>, val };
        }
        break;

    default:
        TR_ASSERT(false);
        break;
    }

    return {};
}

SessionSettings::SessionSettings()
{
    using Type = Setting::Type;
    using Value = Setting::Value;

    // clang-format off
    settings_ = std::array<Setting, FieldCount>{{
        { TR_KEY_alt_speed_down, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 50U } } }, // half the regular
        { TR_KEY_alt_speed_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_alt_speed_time_begin, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 540U } } }, // 9am
        { TR_KEY_alt_speed_time_day, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ TR_SCHED_ALL } } },
        { TR_KEY_alt_speed_time_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_alt_speed_time_end, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 1020U } } }, // 5pm
        { TR_KEY_alt_speed_up, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 50U } } }, // half the regular
        { TR_KEY_announce_ip, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_announce_ip_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_anti_brute_force_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_anti_brute_force_threshold, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 100 } } },
        { TR_KEY_bind_address_ipv4, Type::String, Value{ std::in_place_type<std::string>, "0.0.0.0"sv } },
        { TR_KEY_bind_address_ipv6, Type::String, Value{ std::in_place_type<std::string>, "::"sv } },
        { TR_KEY_blocklist_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_blocklist_url, Type::String, Value{ std::in_place_type<std::string>, "http://www.example.com/blocklist"sv } },
        { TR_KEY_cache_size_mb, Type::SizeT, Value{ std::in_place_type<std::size_t>, 4U } },
        { TR_KEY_default_trackers, Type::String, Value{ std::in_place_type<std::string>, "" } },
        { TR_KEY_dht_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_download_dir, Type::String, Value{ std::in_place_type<std::string>, tr_getDefaultDownloadDir() } },
        { TR_KEY_download_queue_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_download_queue_size, Type::SizeT, Value{ std::in_place_type<size_t>, 5U } },
        { TR_KEY_encryption, Type::Encryption, Value{ std::in_place_type<tr_encryption_mode>, TR_ENCRYPTION_PREFERRED } },
        { TR_KEY_idle_seeding_limit, Type::SizeT, Value{ std::in_place_type<size_t>, 30U } },
        { TR_KEY_idle_seeding_limit_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_incomplete_dir, Type::String, Value{ std::in_place_type<std::string>, tr_getDefaultDownloadDir() } },
        { TR_KEY_incomplete_dir_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_lpd_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_message_level, Type::Log, Value{ std::in_place_type<tr_log_level>, TR_LOG_INFO } },
        { TR_KEY_peer_congestion_algorithm, Type::String, Value{ std::in_place_type<std::string>, "" } },
        { TR_KEY_peer_id_ttl_hours, Type::SizeT, Value{ std::in_place_type<size_t>, 6U } },
        { TR_KEY_peer_limit_global, Type::SizeT, Value{ std::in_place_type<size_t>, *tr_parseNum<int64_t>(TR_DEFAULT_PEER_LIMIT_GLOBAL_STR) } },
        { TR_KEY_peer_limit_per_torrent, Type::SizeT, Value{ std::in_place_type<size_t>, *tr_parseNum<int64_t>(TR_DEFAULT_PEER_LIMIT_TORRENT_STR) } },
        { TR_KEY_peer_port, Type::Port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(*tr_parseNum<unsigned short>(TR_DEFAULT_PEER_PORT_STR)) } },
        { TR_KEY_peer_port_random_high, Type::Port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(65535) } },
        { TR_KEY_peer_port_random_low, Type::Port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(49152) } },
        { TR_KEY_peer_port_random_on_start, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_peer_socket_tos, Type::Int, Value{ std::in_place_type<int>, int{ 0x04 } } },
        { TR_KEY_pex_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_port_forwarding_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_preallocation, Type::Preallocation, Value{ std::in_place_type<tr_preallocation_mode>, TR_PREALLOCATE_SPARSE } },
        { TR_KEY_prefetch_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_queue_stalled_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_queue_stalled_minutes, Type::SizeT, Value{ std::in_place_type<size_t>, 30U } },
        { TR_KEY_ratio_limit, Type::Double, Value{ std::in_place_type<size_t>, 2.0 } },
        { TR_KEY_ratio_limit_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rename_partial_files, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_rpc_authentication_required, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rpc_bind_address, Type::String, Value{ std::in_place_type<std::string>, "0.0.0.0"sv } },
        { TR_KEY_rpc_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rpc_host_whitelist, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_host_whitelist_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_rpc_password, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_port, Type::Port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(TR_DEFAULT_RPC_PORT) } },
        { TR_KEY_rpc_socket_mode, Type::ModeT, Value{ std::in_place_type<mode_t>, tr_rpc_server::DefaultRpcSocketMode } },
        { TR_KEY_rpc_url, Type::String, Value{ std::in_place_type<std::string>, TR_DEFAULT_RPC_URL_STR } },
        { TR_KEY_rpc_username, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_whitelist, Type::String, Value{ std::in_place_type<std::string>, TR_DEFAULT_RPC_WHITELIST } },
        { TR_KEY_rpc_whitelist_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_scrape_paused_torrents_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_script_torrent_added_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_added_filename, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_script_torrent_done_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_done_filename, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_script_torrent_done_seeding_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_done_seeding_filename, Type::String, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_seed_queue_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_seed_queue_size, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 10 } } },
        { TR_KEY_speed_limit_down, Type::SizeT, Value{ std::in_place_type<std::size_t>, 100U } },
        { TR_KEY_speed_limit_down_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_speed_limit_up, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 100 } } },
        { TR_KEY_speed_limit_up_enabled, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_start_added_torrents, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_tcp_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_trash_original_torrent_files, Type::Bool, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_umask, Type::ModeT, Value{ std::in_place_type<mode_t>, mode_t{ 022 } } },
        { TR_KEY_upload_slots_per_torrent, Type::SizeT, Value{ std::in_place_type<size_t>, size_t{ 8 } } },
        { TR_KEY_utp_enabled, Type::Bool, Value{ std::in_place_type<bool>, true } },
    }};
    // clang-format on
}

} // namespace libtransmission
