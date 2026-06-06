// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint> // int64_t, uint32_t, uint64_t
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include <small/set.hpp>
#include <small/vector.hpp>

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/peer-mgr.h" // tr_pex
#include "libtransmission/serializer.h"
#include "libtransmission/string-utils.h"
#include "libtransmission/utils.h" // for tr_strv_strip(), tr_strlower()
#include "libtransmission/variant.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/types.h"

using namespace std::literals;

namespace tr::serializer
{
namespace
{
template<typename T, size_t N>
using LookupTable = std::array<std::pair<std::u8string_view, T>, N>;

template<typename T, size_t N>
[[nodiscard]] tr_variant from_enum_or_integral_with_lookup(LookupTable<T, N> const& rows, T const src)
{
    static_assert(std::is_enum_v<T> || std::is_integral_v<T>);

    for (auto const& [key, value] : rows)
    {
        if (value == src)
        {
            return tr_variant::unmanaged_string(key);
        }
    }

    return static_cast<int64_t>(src);
}

template<typename T, size_t N>
[[nodiscard]] bool to_enum_or_integral_with_lookup(LookupTable<T, N> const& rows, tr_variant const& src, T* tgt)
{
    static_assert(std::is_enum_v<T> || std::is_integral_v<T>);

    if (tgt == nullptr)
    {
        return false;
    }

    if (auto const val = src.value_if<std::string_view>())
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [key, value] : rows)
        {
            if (auto const key_sv = std::string_view{ reinterpret_cast<char const*>(key.data()), key.size()}; key_sv == needle)
            {
                *tgt = value;
                return true;
            }
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        for (auto const& [key, value] : rows)
        {
            if (static_cast<int64_t>(value) == *val)
            {
                *tgt = value;
                return true;
            }
        }
    }

    return false;
}

// ---

bool to_bool(tr_variant const& src, bool* tgt)
{
    if (auto val = src.value_if<bool>())
    {
        *tgt = *val;
        return true;
    }

    return false;
}

tr_variant from_bool(bool const& val)
{
    return val;
}

// ---

bool to_double(tr_variant const& src, double* tgt)
{
    if (auto val = src.value_if<double>())
    {
        *tgt = *val;
        return true;
    }

    return false;
}

tr_variant from_double(double const& val)
{
    return val;
}

// ---

template<std::integral T>
bool to_int(tr_variant const& src, T* tgt)
{
    static_assert(!std::is_same_v<T, bool>);
    if (auto const val = src.value_if<T>())
    {
        *tgt = *val;
        return true;
    }

    return false;
}

template<std::integral T>
tr_variant from_int(T const& val)
{
    static_assert(!std::is_same_v<T, bool>);
    return val;
}

// ---

auto constexpr EncryptionKeys = LookupTable<tr_encryption_mode, 3U>{ {
    { u8"required", TR_ENCRYPTION_REQUIRED },
    { u8"preferred", TR_ENCRYPTION_PREFERRED },
    { u8"allowed", TR_CLEAR_PREFERRED },
} };

bool to_encryption_mode(tr_variant const& src, tr_encryption_mode* tgt)
{
    return to_enum_or_integral_with_lookup(EncryptionKeys, src, tgt);
}

tr_variant from_encryption_mode(tr_encryption_mode const& val)
{
    return from_enum_or_integral_with_lookup(EncryptionKeys, val);
}

// ---

auto constexpr LogKeys = LookupTable<tr_log_level, 7U>{ {
    { u8"critical", TR_LOG_CRITICAL },
    { u8"debug", TR_LOG_DEBUG },
    { u8"error", TR_LOG_ERROR },
    { u8"info", TR_LOG_INFO },
    { u8"off", TR_LOG_OFF },
    { u8"trace", TR_LOG_TRACE },
    { u8"warn", TR_LOG_WARN },
} };

bool to_log_level(tr_variant const& src, tr_log_level* tgt)
{
    return to_enum_or_integral_with_lookup(LogKeys, src, tgt);
}

tr_variant from_log_level(tr_log_level const& val)
{
    return static_cast<int64_t>(val);
}

// ---

bool to_mode_t(tr_variant const& src, tr_mode_t* tgt)
{
    if (auto const val = src.value_if<std::string_view>())
    {
        if (auto const mode = tr_num_parse<uint32_t>(*val, nullptr, 8); mode)
        {
            *tgt = static_cast<tr_mode_t>(*mode);
            return true;
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = static_cast<tr_mode_t>(*val);
        return true;
    }

    return false;
}

tr_variant from_mode_t(tr_mode_t const& val)
{
    return fmt::format("{:#03o}", val);
}

// ---

bool to_sched_day(tr_variant const& src, tr_sched_day* tgt)
{
    if (auto const val = src.value_if<int64_t>())
    {
        switch (*val)
        {
        case TR_SCHED_SUN:
            *tgt = TR_SCHED_SUN;
            return true;

        case TR_SCHED_MON:
            *tgt = TR_SCHED_MON;
            return true;

        case TR_SCHED_TUES:
            *tgt = TR_SCHED_TUES;
            return true;

        case TR_SCHED_WED:
            *tgt = TR_SCHED_WED;
            return true;

        case TR_SCHED_THURS:
            *tgt = TR_SCHED_THURS;
            return true;

        case TR_SCHED_FRI:
            *tgt = TR_SCHED_FRI;
            return true;

        case TR_SCHED_SAT:
            *tgt = TR_SCHED_SAT;
            return true;

        case TR_SCHED_WEEKDAY:
            *tgt = TR_SCHED_WEEKDAY;
            return true;

        case TR_SCHED_WEEKEND:
            *tgt = TR_SCHED_WEEKEND;
            return true;

        case TR_SCHED_ALL:
            *tgt = TR_SCHED_ALL;
            return true;

        default:
            tr_logAddWarn(fmt::format(fmt::runtime(_("Invalid tr_sched_days value {val}")), fmt::arg("val", *val)));
            break;
        }
    }

    return false;
}

tr_variant from_sched_day(tr_sched_day const& val)
{
    return val;
}

// ---

bool to_msec(tr_variant const& src, std::chrono::milliseconds* tgt)
{
    if (auto val = src.value_if<int64_t>())
    {
        *tgt = std::chrono::milliseconds(*val);
        return true;
    }

    return false;
}

tr_variant from_msec(std::chrono::milliseconds const& src)
{
    return src.count();
}

// ---

bool to_port(tr_variant const& src, tr_port* tgt)
{
    if (auto const val = src.value_if<uint16_t>())
    {
        *tgt = tr_port::from_host(*val);
        return true;
    }

    return false;
}

tr_variant from_port(tr_port const& val)
{
    return val.host();
}

// ---

auto constexpr PreallocationKeys = LookupTable<tr_file_preallocation, 5U>{ {
    { u8"off", tr_file_preallocation::None },
    { u8"none", tr_file_preallocation::None },
    { u8"fast", tr_file_preallocation::Sparse },
    { u8"sparse", tr_file_preallocation::Sparse },
    { u8"full", tr_file_preallocation::Full },
} };

bool to_preallocation_mode(tr_variant const& src, tr_file_preallocation* tgt)
{
    return to_enum_or_integral_with_lookup(PreallocationKeys, src, tgt);
}

tr_variant from_preallocation_mode(tr_file_preallocation const& val)
{
    return static_cast<int64_t>(val);
}

// ---

auto constexpr PreferredTransportKeys = LookupTable<tr_preferred_transport, PreferredTransportCount>{ {
    { u8"utp", tr_preferred_transport::UTP },
    { u8"tcp", tr_preferred_transport::TCP },
} };

bool to_preferred_transport(tr_variant const& src, small::max_size_vector<tr_preferred_transport, PreferredTransportCount>* tgt)
{
    static auto constexpr LoadSingle = [](tr_variant const& var) -> std::optional<tr_preferred_transport>
    {
        auto tmp = tr_preferred_transport{};
        return to_enum_or_integral_with_lookup(PreferredTransportKeys, var, &tmp) ? std::make_optional(tmp) : std::nullopt;
    };

    if (auto* const l = src.get_if<tr_variant::Vector>(); l != nullptr)
    {
        auto tmp = small::max_size_unordered_set<tr_preferred_transport, PreferredTransportCount>{};
        tmp.reserve(tmp.max_size());

        for (size_t i = 0, n = std::min(std::size(*l), tmp.max_size()); i < n; ++i)
        {
            auto const value = LoadSingle((*l)[i]);
            if (!value || !tmp.insert(*value).second)
            {
                return false;
            }
        }

        // N.B. As of small 0.2.2, small::max_size_unordered_set preserves insertion order,
        // so we can directly copy the elements
        tgt->assign(std::begin(tmp), std::end(tmp));
        return true;
    }

    auto const preferred = LoadSingle(src);
    if (!preferred)
    {
        return false;
    }

    tgt->assign(1U, *preferred);
    return true;
}

tr_variant from_preferred_transport(small::max_size_vector<tr_preferred_transport, PreferredTransportCount> const& val)
{
    static auto constexpr SaveSingle = [](tr_preferred_transport const ele) -> tr_variant
    {
        return static_cast<int64_t>(ele);
    };

    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(val));
    for (auto const ele : val)
    {
        ret.emplace_back(SaveSingle(ele));
    }

    return ret;
}

// ---

bool to_string(tr_variant const& src, std::string* tgt)
{
    if (auto const val = src.value_if<std::string_view>())
    {
        *tgt = std::string{ *val };
        return true;
    }

    return false;
}

tr_variant from_string(std::string const& val)
{
    return val;
}

// ---

// RFCs 2474, 3246, 4594 & 8622
// Service class names are defined in RFC 4594, RFC 5865, and RFC 8622.
// Not all platforms have these IPTOS_ definitions, so hardcode them here
auto constexpr DiffServKeys = LookupTable<int, 28U>{ {
    { u8"cs0", 0x00 }, // IPTOS_CLASS_CS0
    { u8"le", 0x04 },
    { u8"cs1", 0x20 }, // IPTOS_CLASS_CS1
    { u8"af11", 0x28 }, // IPTOS_DSCP_AF11
    { u8"af12", 0x30 }, // IPTOS_DSCP_AF12
    { u8"af13", 0x38 }, // IPTOS_DSCP_AF13
    { u8"cs2", 0x40 }, // IPTOS_CLASS_CS2
    { u8"af21", 0x48 }, // IPTOS_DSCP_AF21
    { u8"af22", 0x50 }, // IPTOS_DSCP_AF22
    { u8"af23", 0x58 }, // IPTOS_DSCP_AF23
    { u8"cs3", 0x60 }, // IPTOS_CLASS_CS3
    { u8"af31", 0x68 }, // IPTOS_DSCP_AF31
    { u8"af32", 0x70 }, // IPTOS_DSCP_AF32
    { u8"af33", 0x78 }, // IPTOS_DSCP_AF33
    { u8"cs4", 0x80 }, // IPTOS_CLASS_CS4
    { u8"af41", 0x88 }, // IPTOS_DSCP_AF41
    { u8"af42", 0x90 }, // IPTOS_DSCP_AF42
    { u8"af43", 0x98 }, // IPTOS_DSCP_AF43
    { u8"cs5", 0xa0 }, // IPTOS_CLASS_CS5
    { u8"ef", 0xb8 }, // IPTOS_DSCP_EF
    { u8"cs6", 0xc0 }, // IPTOS_CLASS_CS6
    { u8"cs7", 0xe0 }, // IPTOS_CLASS_CS7

    // <netinet/ip.h> lists these TOS names as deprecated,
    // but keep them defined here for backward compatibility
    { u8"routine", 0x00 }, // IPTOS_PREC_ROUTINE
    { u8"lowcost", 0x02 }, // IPTOS_LOWCOST
    { u8"mincost", 0x02 }, // IPTOS_MINCOST
    { u8"reliable", 0x04 }, // IPTOS_RELIABILITY
    { u8"throughput", 0x08 }, // IPTOS_THROUGHPUT
    { u8"lowdelay", 0x10 }, // IPTOS_LOWDELAY
} };

bool to_diffserv_t(tr_variant const& src, tr_diffserv_t* tgt)
{
    auto tmp = int{};
    if (!to_enum_or_integral_with_lookup(DiffServKeys, src, &tmp))
    {
        return false;
    }

    *tgt = tr_diffserv_t{ tmp };
    return true;
}

tr_variant from_diffserv_t(tr_diffserv_t const& val)
{
    return from_enum_or_integral_with_lookup(DiffServKeys, static_cast<int>(val));
}

// ---

auto constexpr VerifyModeKeys = LookupTable<tr_verify_added_mode, 2U>{ {
    { u8"fast", TR_VERIFY_ADDED_FAST },
    { u8"full", TR_VERIFY_ADDED_FULL },
} };

bool to_verify_added_mode(tr_variant const& src, tr_verify_added_mode* tgt)
{
    return to_enum_or_integral_with_lookup(VerifyModeKeys, src, tgt);
}

tr_variant from_verify_added_mode(tr_verify_added_mode const& val)
{
    return from_enum_or_integral_with_lookup(VerifyModeKeys, val);
}

// ---

bool to_u8string(tr_variant const& src, std::u8string* tgt)
{
    if (auto const val = src.value_if<std::u8string_view>())
    {
        TR_ASSERT(tr_strv_find_invalid_utf8(*val) == std::u8string_view::npos);
        *tgt = *val;
        return true;
    }

    if (auto const val = src.value_if<std::string_view>())
    {
        if (tr_strv_find_invalid_utf8(*val) != std::string_view::npos)
        {
            tr_logAddWarn(fmt::format(fmt::runtime(_("String '{string}' contains invalid UTF-8")), fmt::arg("string", *val)));
        }

        *tgt = tr_strv_to_u8string(tr_strv_replace_invalid(*val));
        return true;
    }

    return false;
}

tr_variant from_u8string(std::u8string const& val)
{
    return val;
}

// ---

bool to_fs_path(tr_variant const& src, std::filesystem::path* tgt)
{
    if (auto u8str = std::u8string{}; to_u8string(src, &u8str))
    {
        *tgt = std::filesystem::path{ u8str };
        return true;
    }

    return false;
}

tr_variant from_fs_path(std::filesystem::path const& path)
{
    return from_u8string(path.u8string());
}

// ---

bool to_pex(tr_variant const& src, tr_pex* tgt)
{
    auto* const map = src.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return false;
    }

    auto const sockaddr = map->value_if<std::string_view>(TR_KEY_socket_address);
    if (!sockaddr)
    {
        return false;
    }

    auto pex = tr_pex{};
    auto* const compact = reinterpret_cast<std::byte const*>(std::data(*sockaddr));
    switch (std::size(*sockaddr))
    {
    case tr_socket_address::CompactSockAddrBytes[TR_AF_INET]:
        pex.socket_address = tr_socket_address::from_compact_ipv4(compact).first;
        break;

    case tr_socket_address::CompactSockAddrBytes[TR_AF_INET6]:
        pex.socket_address = tr_socket_address::from_compact_ipv6(compact).first;
        break;

    default:
        return false;
    }

    pex.flags = static_cast<uint8_t>(map->value_if<int64_t>(TR_KEY_flags).value_or(0));

    *tgt = std::move(pex);
    return true;
}

tr_variant from_pex(tr_pex const& val)
{
    auto pex = tr_variant::Map{ 2U };

    auto buf = std::array<char, tr_socket_address::CompactSockAddrMaxBytes>{};
    auto* const buf_data = std::data(buf);
    auto* const begin = reinterpret_cast<std::byte*>(buf_data);
    auto const* const end = val.to_compact(begin);

    pex.try_emplace(TR_KEY_socket_address, std::string_view{ buf_data, static_cast<size_t>(end - begin) });
    pex.try_emplace(TR_KEY_flags, val.flags);

    return pex;
}
} // unnamed namespace

void Converters::ensure_default_converters()
{
    static auto once = std::once_flag{};
    std::call_once(
        once,
        []
        {
            Converters::add(to_bool, from_bool);
            Converters::add(to_diffserv_t, from_diffserv_t);
            Converters::add(to_double, from_double);
            Converters::add(to_encryption_mode, from_encryption_mode);
            Converters::add(to_fs_path, from_fs_path);
            Converters::add(to_int<int64_t>, from_int<int64_t>);
            Converters::add(to_int<size_t>, from_int<size_t>);
            Converters::add(to_int<time_t>, from_int<time_t>);
            Converters::add(to_int<uint16_t>, from_int<uint16_t>);
            Converters::add(to_int<uint32_t>, from_int<uint32_t>);
            Converters::add(to_int<uint64_t>, from_int<uint64_t>);
            Converters::add(to_log_level, from_log_level);
            Converters::add(to_mode_t, from_mode_t);
            Converters::add(to_msec, from_msec);
            Converters::add(to_pex, from_pex);
            Converters::add(to_port, from_port);
            Converters::add(to_preallocation_mode, from_preallocation_mode);
            Converters::add(to_preferred_transport, from_preferred_transport);
            Converters::add(to_sched_day, from_sched_day);
            Converters::add(to_string, from_string);
            Converters::add(to_u8string, from_u8string);
            Converters::add(to_verify_added_mode, from_verify_added_mode);
        });
}

} // namespace tr::serializer
