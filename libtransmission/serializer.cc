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

#include "libtransmission/transmission.h"

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/net.h" // for tr_port
#include "libtransmission/open-files.h" // for tr_open_files::Preallocation
#include "libtransmission/peer-io.h" // tr_preferred_transport
#include "libtransmission/serializer.h"
#include "libtransmission/utils.h" // for tr_strv_strip(), tr_strlower()
#include "libtransmission/variant.h"
#include "libtransmission/tr-assert.h"

using namespace std::literals;

namespace tr::serializer
{
namespace
{
template<typename T, size_t N>
using LookupTable = std::array<std::pair<std::string_view, T>, N>;

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
            if (key == needle)
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

bool to_int64(tr_variant const& src, int64_t* tgt)
{
    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = *val;
        return true;
    }

    return false;
}

tr_variant from_int64(int64_t const& val)
{
    return val;
}

// ---

auto constexpr EncryptionKeys = LookupTable<tr_encryption_mode, 3U>{ {
    { "required", TR_ENCRYPTION_REQUIRED },
    { "preferred", TR_ENCRYPTION_PREFERRED },
    { "allowed", TR_CLEAR_PREFERRED },
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
    { "critical", TR_LOG_CRITICAL },
    { "debug", TR_LOG_DEBUG },
    { "error", TR_LOG_ERROR },
    { "info", TR_LOG_INFO },
    { "off", TR_LOG_OFF },
    { "trace", TR_LOG_TRACE },
    { "warn", TR_LOG_WARN },
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
    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = tr_port::from_host(*val);
        return true;
    }

    return false;
}

tr_variant from_port(tr_port const& val)
{
    return int64_t{ val.host() };
}

// ---

auto constexpr PreallocationKeys = LookupTable<tr_open_files::Preallocation, 5U>{ {
    { "off", tr_open_files::Preallocation::None },
    { "none", tr_open_files::Preallocation::None },
    { "fast", tr_open_files::Preallocation::Sparse },
    { "sparse", tr_open_files::Preallocation::Sparse },
    { "full", tr_open_files::Preallocation::Full },
} };

bool to_preallocation_mode(tr_variant const& src, tr_open_files::Preallocation* tgt)
{
    return to_enum_or_integral_with_lookup(PreallocationKeys, src, tgt);
}

tr_variant from_preallocation_mode(tr_open_files::Preallocation const& val)
{
    return static_cast<int64_t>(val);
}

// ---

auto constexpr PreferredTransportKeys = LookupTable<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT>{ {
    { "utp", TR_PREFER_UTP },
    { "tcp", TR_PREFER_TCP },
} };

bool to_preferred_transport(
    tr_variant const& src,
    small::max_size_vector<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT>* tgt)
{
    static auto constexpr LoadSingle = [](tr_variant const& var)
    {
        auto tmp = tr_preferred_transport{};
        return to_enum_or_integral_with_lookup(PreferredTransportKeys, var, &tmp) ? tmp : TR_NUM_PREFERRED_TRANSPORT;
    };

    if (auto* const l = src.get_if<tr_variant::Vector>(); l != nullptr)
    {
        auto tmp = small::max_size_unordered_set<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT>{};
        tmp.reserve(tmp.max_size());

        for (size_t i = 0, n = std::min(std::size(*l), tmp.max_size()); i < n; ++i)
        {
            auto const value = LoadSingle((*l)[i]);
            if (value >= TR_NUM_PREFERRED_TRANSPORT || !tmp.insert(value).second)
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
    if (preferred >= TR_NUM_PREFERRED_TRANSPORT)
    {
        return false;
    }

    tgt->assign(1U, preferred);
    return true;
}

tr_variant from_preferred_transport(small::max_size_vector<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT> const& val)
{
    static auto constexpr SaveSingle = [](tr_preferred_transport const ele) -> tr_variant
    {
        return from_enum_or_integral_with_lookup(PreferredTransportKeys, ele);
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

bool to_size_t(tr_variant const& src, size_t* tgt)
{
    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = static_cast<size_t>(*val);
        return true;
    }

    return false;
}

tr_variant from_size_t(size_t const& val)
{
    return uint64_t{ val };
}

// ---

bool to_uint64(tr_variant const& src, uint64_t* tgt)
{
    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = static_cast<uint64_t>(*val);
        return true;
    }

    return false;
}

tr_variant from_uint64(uint64_t const& val)
{
    return val;
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
    { "cs0", 0x00 }, // IPTOS_CLASS_CS0
    { "le", 0x04 },
    { "cs1", 0x20 }, // IPTOS_CLASS_CS1
    { "af11", 0x28 }, // IPTOS_DSCP_AF11
    { "af12", 0x30 }, // IPTOS_DSCP_AF12
    { "af13", 0x38 }, // IPTOS_DSCP_AF13
    { "cs2", 0x40 }, // IPTOS_CLASS_CS2
    { "af21", 0x48 }, // IPTOS_DSCP_AF21
    { "af22", 0x50 }, // IPTOS_DSCP_AF22
    { "af23", 0x58 }, // IPTOS_DSCP_AF23
    { "cs3", 0x60 }, // IPTOS_CLASS_CS3
    { "af31", 0x68 }, // IPTOS_DSCP_AF31
    { "af32", 0x70 }, // IPTOS_DSCP_AF32
    { "af33", 0x78 }, // IPTOS_DSCP_AF33
    { "cs4", 0x80 }, // IPTOS_CLASS_CS4
    { "af41", 0x88 }, // IPTOS_DSCP_AF41
    { "af42", 0x90 }, // IPTOS_DSCP_AF42
    { "af43", 0x98 }, // IPTOS_DSCP_AF43
    { "cs5", 0xa0 }, // IPTOS_CLASS_CS5
    { "ef", 0xb8 }, // IPTOS_DSCP_EF
    { "cs6", 0xc0 }, // IPTOS_CLASS_CS6
    { "cs7", 0xe0 }, // IPTOS_CLASS_CS7

    // <netinet/ip.h> lists these TOS names as deprecated,
    // but keep them defined here for backward compatibility
    { "routine", 0x00 }, // IPTOS_PREC_ROUTINE
    { "lowcost", 0x02 }, // IPTOS_LOWCOST
    { "mincost", 0x02 }, // IPTOS_MINCOST
    { "reliable", 0x04 }, // IPTOS_RELIABILITY
    { "throughput", 0x08 }, // IPTOS_THROUGHPUT
    { "lowdelay", 0x10 }, // IPTOS_LOWDELAY
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
    { "fast", TR_VERIFY_ADDED_FAST },
    { "full", TR_VERIFY_ADDED_FULL },
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
    return std::string{ reinterpret_cast<char const*>(std::data(val)), std::size(val) };
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
            Converters::add(to_int64, from_int64);
            Converters::add(to_log_level, from_log_level);
            Converters::add(to_mode_t, from_mode_t);
            Converters::add(to_msec, from_msec);
            Converters::add(to_port, from_port);
            Converters::add(to_preallocation_mode, from_preallocation_mode);
            Converters::add(to_preferred_transport, from_preferred_transport);
            Converters::add(to_size_t, from_size_t);
            Converters::add(to_string, from_string);
            Converters::add(to_u8string, from_u8string);
            Converters::add(to_uint64, from_uint64);
            Converters::add(to_verify_added_mode, from_verify_added_mode);
        });
}

} // namespace tr::serializer
