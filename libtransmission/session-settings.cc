// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstdint> // for int64_t
#include <string>
#include <string_view>
#include <utility> // for std::in_place_index
#include <variant>

#include <fmt/format.h>

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

namespace
{

template<typename T>
struct Converter
{
public:
    static std::optional<T> load(tr_variant* src);
    static void save(tr_variant* tgt, T const& val);
};

template<>
struct Converter<bool>
{
    static std::optional<bool> load(tr_variant* src)
    {
        if (auto val = bool{}; tr_variantGetBool(src, &val))
        {
            return val;
        }

        return {};
    }

    static void save(tr_variant* tgt, bool const& val)
    {
        tr_variantInitBool(tgt, val);
    }
};

template<>
struct Converter<double>
{
    static std::optional<double> load(tr_variant* src)
    {
        if (auto val = double{}; tr_variantGetReal(src, &val))
        {
            return val;
        }

        return {};
    }

    static void save(tr_variant* tgt, double const& val)
    {
        tr_variantInitReal(tgt, val);
    }
};

template<>
struct Converter<tr_encryption_mode>
{
    static std::optional<tr_encryption_mode> load(tr_variant* src)
    {
        if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
        {
            auto const needle = tr_strlower(tr_strvStrip(val));

            for (auto const& [key, encryption] : Keys)
            {
                if (key == needle)
                {
                    return encryption;
                }
            }
        }

        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            for (auto const& [key, encryption] : Keys)
            {
                if (encryption == val)
                {
                    return encryption;
                }
            }
        }

        return {};
    }

    static void save(tr_variant* tgt, tr_encryption_mode const& val)
    {
        for (auto const& [key, value] : Keys)
        {
            if (value == val)
            {
                tr_variantInitStrView(tgt, key);
                return;
            }
        }
    }

private:
    static auto constexpr Keys = std::array<std::pair<std::string_view, tr_encryption_mode>, 3>{
        { { "required", TR_ENCRYPTION_REQUIRED }, { "preferred", TR_ENCRYPTION_PREFERRED }, { "allowed", TR_CLEAR_PREFERRED } }
    };
};

template<>
struct Converter<int>
{
    static std::optional<int> load(tr_variant* src)
    {
        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            return static_cast<int>(val);
        }

        return {};
    }

    static void save(tr_variant* tgt, int val)
    {
        tr_variantInitInt(tgt, val);
    }
};

template<>
struct Converter<tr_log_level>
{
    static std::optional<tr_log_level> load(tr_variant* src)
    {
        if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
        {
            auto const needle = tr_strlower(tr_strvStrip(val));

            for (auto const& [name, log_level] : Keys)
            {
                if (needle == name)
                {
                    return log_level;
                }
            }
        }

        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            for (auto const& [name, log_level] : Keys)
            {
                if (log_level == val)
                {
                    return log_level;
                }
            }
        }

        return {};
    }

    static void save(tr_variant* tgt, tr_log_level val)
    {
        for (auto const& [key, value] : Keys)
        {
            if (value == val)
            {
                tr_variantInitStrView(tgt, key);
                return;
            }
        }
    }

private:
    static auto constexpr Keys = std::array<std::pair<std::string_view, tr_log_level>, 7>{ {
        { "critical"sv, TR_LOG_CRITICAL },
        { "debug"sv, TR_LOG_DEBUG },
        { "error"sv, TR_LOG_ERROR },
        { "info"sv, TR_LOG_INFO },
        { "off"sv, TR_LOG_OFF },
        { "trace"sv, TR_LOG_TRACE },
        { "warn"sv, TR_LOG_WARN },
    } };
};

template<>
struct Converter<mode_t>
{
    static std::optional<mode_t> load(tr_variant* src)
    {
        if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
        {
            if (auto const mode = tr_parseNum<uint32_t>(val, nullptr, 8); mode)
            {
                return static_cast<mode_t>(*mode);
            }
        }

        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            return static_cast<mode_t>(val);
        }

        return {};
    }

    static void save(tr_variant* tgt, mode_t const& val)
    {
        tr_variantInitStr(tgt, fmt::format("{:03o}", val));
    }
};

template<>
struct Converter<tr_port>
{
    static std::optional<tr_port> load(tr_variant* src)
    {
        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            return tr_port::fromHost(val);
        }

        return {};
    }

    static void save(tr_variant* tgt, tr_port const& val)
    {
        tr_variantInitInt(tgt, val.host());
    }
};

template<>
struct Converter<tr_preallocation_mode>
{
    static std::optional<tr_preallocation_mode> load(tr_variant* src)
    {
        if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
        {
            auto const needle = tr_strlower(tr_strvStrip(val));

            for (auto const& [name, value] : Keys)
            {
                if (name == needle)
                {
                    return value;
                }
            }
        }

        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            for (auto const& [name, value] : Keys)
            {
                if (value == val)
                {
                    return value;
                }
            }
        }

        return {};
    }

    static void save(tr_variant* tgt, tr_preallocation_mode val)
    {
        for (auto const& [key, value] : Keys)
        {
            if (value == val)
            {
                tr_variantInitStrView(tgt, key);
                return;
            }
        }
    }

private:
    static auto constexpr Keys = std::array<std::pair<std::string_view, tr_preallocation_mode>, 4>{ {
        { "none"sv, TR_PREALLOCATE_NONE },
        { "fast"sv, TR_PREALLOCATE_SPARSE },
        { "sparse"sv, TR_PREALLOCATE_SPARSE },
        { "full"sv, TR_PREALLOCATE_FULL },
    } };
};

template<>
struct Converter<size_t>
{
    static std::optional<size_t> load(tr_variant* src)
    {
        if (auto val = int64_t{}; tr_variantGetInt(src, &val))
        {
            return static_cast<size_t>(val);
        }

        return {};
    }

    static void save(tr_variant* tgt, size_t const& val)
    {
        tr_variantInitInt(tgt, val);
    }
};

template<>
struct Converter<std::string>
{
    static std::optional<std::string> load(tr_variant* src)
    {
        if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
        {
            return std::string{ val };
        }

        return {};
    }

    static void save(tr_variant* tgt, std::string const& val)
    {
        tr_variantInitStr(tgt, val);
    }
};

} // namespace

namespace libtransmission
{
namespace
{
template<typename T>
void vsave(tr_variant* tgt, Setting const& src)
{
    Converter<T>::save(tgt, src.get<T>());
}

template<typename T>
std::optional<Setting::Value> vload(tr_variant* src)
{
    if (auto val = Converter<T>::load(src); val)
    {
        return Setting::Value{ *val };
    }

    return {};
}
} // namespace

void Setting::save(tr_variant* tgt) const
{
    // clang-format off
    switch(value_.index())
    {
        case 0: vsave<bool>(tgt, *this); break;
        case 1: vsave<double>(tgt, *this); break;
        case 2: vsave<tr_encryption_mode>(tgt, *this); break;
        case 3: vsave<int>(tgt, *this); break;
        case 4: vsave<tr_log_level>(tgt, *this); break;
        case 5: vsave<mode_t>(tgt, *this); break;
        case 6: vsave<tr_port>(tgt, *this); break;
        case 7: vsave<tr_preallocation_mode>(tgt, *this); break;
        case 8: vsave<size_t>(tgt, *this); break;
        case 9: vsave<std::string>(tgt, *this); break;
        break;
    }
    // clang-format on
}

bool Setting::load(tr_variant* src)
{
    auto value = std::optional<Value>{};

    // clang-format off
    switch(value_.index())
    {
        case 0: value = vload<bool>(src); break;
        case 1: value = vload<double>(src); break;
        case 2: value = vload<tr_encryption_mode>(src); break;
        case 3: value = vload<int>(src); break;
        case 4: value = vload<tr_log_level>(src); break;
        case 5: value = vload<mode_t>(src); break;
        case 6: value = vload<tr_port>(src); break;
        case 7: value = vload<tr_preallocation_mode>(src); break;
        case 8: value = vload<size_t>(src); break;
        case 9: value = vload<std::string>(src); break;
        break;
    }
    // clang-format on

    if (!value || value_ == *value)
    {
        return false;
    }

    value_ = std::move(*value);
    return true;
}

SessionSettings::SessionSettings()
{
    using Value = Setting::Value;

    // clang-format off
    settings_ = std::array<Setting, FieldCount>{{
        { TR_KEY_alt_speed_down, Value{ std::in_place_type<size_t>, size_t{ 50U } } }, // half the regular
        { TR_KEY_alt_speed_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_alt_speed_time_begin, Value{ std::in_place_type<size_t>, size_t{ 540U } } }, // 9am
        { TR_KEY_alt_speed_time_day, Value{ std::in_place_type<size_t>, size_t{ TR_SCHED_ALL } } },
        { TR_KEY_alt_speed_time_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_alt_speed_time_end, Value{ std::in_place_type<size_t>, size_t{ 1020U } } }, // 5pm
        { TR_KEY_alt_speed_up, Value{ std::in_place_type<size_t>, size_t{ 50U } } }, // half the regular
        { TR_KEY_announce_ip, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_announce_ip_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_anti_brute_force_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_anti_brute_force_threshold, Value{ std::in_place_type<size_t>, size_t{ 100 } } },
        { TR_KEY_bind_address_ipv4, Value{ std::in_place_type<std::string>, "0.0.0.0"sv } },
        { TR_KEY_bind_address_ipv6, Value{ std::in_place_type<std::string>, "::"sv } },
        { TR_KEY_blocklist_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_blocklist_url, Value{ std::in_place_type<std::string>, "http://www.example.com/blocklist"sv } },
        { TR_KEY_cache_size_mb, Value{ std::in_place_type<std::size_t>, 4U } },
        { TR_KEY_default_trackers, Value{ std::in_place_type<std::string>, "" } },
        { TR_KEY_dht_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_download_dir, Value{ std::in_place_type<std::string>, tr_getDefaultDownloadDir() } },
        { TR_KEY_download_queue_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_download_queue_size, Value{ std::in_place_type<size_t>, 5U } },
        { TR_KEY_encryption, Value{ std::in_place_type<tr_encryption_mode>, TR_ENCRYPTION_PREFERRED } },
        { TR_KEY_idle_seeding_limit, Value{ std::in_place_type<size_t>, 30U } },
        { TR_KEY_idle_seeding_limit_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_incomplete_dir, Value{ std::in_place_type<std::string>, tr_getDefaultDownloadDir() } },
        { TR_KEY_incomplete_dir_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_lpd_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_message_level, Value{ std::in_place_type<tr_log_level>, TR_LOG_INFO } },
        { TR_KEY_peer_congestion_algorithm, Value{ std::in_place_type<std::string>, "" } },
        { TR_KEY_peer_id_ttl_hours, Value{ std::in_place_type<size_t>, 6U } },
        { TR_KEY_peer_limit_global, Value{ std::in_place_type<size_t>, *tr_parseNum<int64_t>(TR_DEFAULT_PEER_LIMIT_GLOBAL_STR) } },
        { TR_KEY_peer_limit_per_torrent, Value{ std::in_place_type<size_t>, *tr_parseNum<int64_t>(TR_DEFAULT_PEER_LIMIT_TORRENT_STR) } },
        { TR_KEY_peer_port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(*tr_parseNum<unsigned short>(TR_DEFAULT_PEER_PORT_STR)) } },
        { TR_KEY_peer_port_random_high, Value{ std::in_place_type<tr_port>, tr_port::fromHost(65535) } },
        { TR_KEY_peer_port_random_low, Value{ std::in_place_type<tr_port>, tr_port::fromHost(49152) } },
        { TR_KEY_peer_port_random_on_start, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_peer_socket_tos, Value{ std::in_place_type<int>, int{ 0x04 } } },
        { TR_KEY_pex_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_port_forwarding_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_preallocation, Value{ std::in_place_type<tr_preallocation_mode>, TR_PREALLOCATE_SPARSE } },
        { TR_KEY_prefetch_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_queue_stalled_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_queue_stalled_minutes, Value{ std::in_place_type<size_t>, 30U } },
        { TR_KEY_ratio_limit, Value{ std::in_place_type<double>, 2.0 } },
        { TR_KEY_ratio_limit_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rename_partial_files, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_rpc_authentication_required, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rpc_bind_address, Value{ std::in_place_type<std::string>, "0.0.0.0"sv } },
        { TR_KEY_rpc_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_rpc_host_whitelist, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_host_whitelist_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_rpc_password, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_port, Value{ std::in_place_type<tr_port>, tr_port::fromHost(TR_DEFAULT_RPC_PORT) } },
        { TR_KEY_rpc_socket_mode, Value{ std::in_place_type<mode_t>, tr_rpc_server::DefaultRpcSocketMode } },
        { TR_KEY_rpc_url, Value{ std::in_place_type<std::string>, TR_DEFAULT_RPC_URL_STR } },
        { TR_KEY_rpc_username, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_rpc_whitelist, Value{ std::in_place_type<std::string>, TR_DEFAULT_RPC_WHITELIST } },
        { TR_KEY_rpc_whitelist_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_scrape_paused_torrents_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_script_torrent_added_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_added_filename, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_script_torrent_done_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_done_filename, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_script_torrent_done_seeding_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_script_torrent_done_seeding_filename, Value{ std::in_place_type<std::string>, ""sv } },
        { TR_KEY_seed_queue_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_seed_queue_size, Value{ std::in_place_type<size_t>, size_t{ 10 } } },
        { TR_KEY_speed_limit_down, Value{ std::in_place_type<std::size_t>, 100U } },
        { TR_KEY_speed_limit_down_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_speed_limit_up, Value{ std::in_place_type<size_t>, size_t{ 100 } } },
        { TR_KEY_speed_limit_up_enabled, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_start_added_torrents, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_tcp_enabled, Value{ std::in_place_type<bool>, true } },
        { TR_KEY_trash_original_torrent_files, Value{ std::in_place_type<bool>, false } },
        { TR_KEY_umask, Value{ std::in_place_type<mode_t>, mode_t{ 022 } } },
        { TR_KEY_upload_slots_per_torrent, Value{ std::in_place_type<size_t>, size_t{ 8 } } },
        { TR_KEY_utp_enabled, Value{ std::in_place_type<bool>, true } },
    }};
    // clang-format on
}

void SessionSettings::save(tr_variant* dict) const
{
    for (size_t i = 0; i < FieldCount; ++i)
    {
        auto& setting = settings_[i];
        auto const& key = setting.key();
        tr_variantDictRemove(dict, key);
        setting.save(tr_variantDictAdd(dict, key));
    }
}

SessionSettings::Changed SessionSettings::load(tr_variant* dict)
{
    auto changed = Changed{};

    for (size_t i = 0; i < FieldCount; ++i)
    {
        auto& setting = settings_[i];

        if (auto* const child = tr_variantDictFind(dict, setting.key()); child != nullptr)
        {
            if (setting.load(child))
            {
                changed.set(i);
            }
        }
    }

    return changed;
}

} // namespace libtransmission
