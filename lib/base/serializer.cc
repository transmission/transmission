// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include <fmt/format.h>

#include "lib/base/i18n.h"
#include "lib/base/log.h" // for tr_log_level
#include "lib/base/serializer.h"
#include "lib/base/string-utils.h"

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

} // namespace

void Converters::ensure_default_converters()
{
    static auto once = std::once_flag{};
    std::call_once(
        once,
        []
        {
            Converters::add(to_bool, from_bool);
            Converters::add(to_double, from_double);
            Converters::add(to_fs_path, from_fs_path);
            Converters::add(to_int64, from_int64);
            Converters::add(to_log_level, from_log_level);
            Converters::add(to_msec, from_msec);
            Converters::add(to_size_t, from_size_t);
            Converters::add(to_string, from_string);
            Converters::add(to_u8string, from_u8string);
            Converters::add(to_uint64, from_uint64);
        });
}

} // namespace tr::serializer
