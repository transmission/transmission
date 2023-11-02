// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <cstdint> // int64_t, uint32_t
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/core.h>

#include "libtransmission/transmission.h"

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/net.h" // for tr_port
#include "libtransmission/peer-io.h" // tr_preferred_transport
#include "libtransmission/utils.h" // for tr_strv_strip(), tr_strlower()
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
auto constexpr EncryptionKeys = std::array<std::pair<std::string_view, tr_encryption_mode>, 3>{ {
    { "required", TR_ENCRYPTION_REQUIRED },
    { "preferred", TR_ENCRYPTION_PREFERRED },
    { "allowed", TR_CLEAR_PREFERRED },
} };

auto constexpr LogKeys = std::array<std::pair<std::string_view, tr_log_level>, 7>{ {
    { "critical", TR_LOG_CRITICAL },
    { "debug", TR_LOG_DEBUG },
    { "error", TR_LOG_ERROR },
    { "info", TR_LOG_INFO },
    { "off", TR_LOG_OFF },
    { "trace", TR_LOG_TRACE },
    { "warn", TR_LOG_WARN },
} };

auto constexpr PreallocationKeys = std::array<std::pair<std::string_view, tr_preallocation_mode>, 5>{ {
    { "off", TR_PREALLOCATE_NONE },
    { "none", TR_PREALLOCATE_NONE },
    { "fast", TR_PREALLOCATE_SPARSE },
    { "sparse", TR_PREALLOCATE_SPARSE },
    { "full", TR_PREALLOCATE_FULL },
} };

auto constexpr VerifyModeKeys = std::array<std::pair<std::string_view, tr_verify_added_mode>, 2>{ {
    { "fast", TR_VERIFY_ADDED_FAST },
    { "full", TR_VERIFY_ADDED_FULL },
} };

auto constexpr PreferredTransportKeys = std::
    array<std::pair<std::string_view, tr_preferred_transport>, TR_NUM_PREFERRED_TRANSPORT>{ {
        { "utp", TR_PREFER_UTP },
        { "tcp", TR_PREFER_TCP },
    } };
} // namespace

namespace libtransmission
{
template<>
std::optional<bool> VariantConverter::load<bool>(tr_variant const& src)
{
    if (auto val = src.get_if<bool>(); val != nullptr)
    {
        return *val;
    }

    return {};
}

template<>
tr_variant VariantConverter::save<bool>(bool const& val)
{
    return val;
}

// ---

template<>
std::optional<double> VariantConverter::load<double>(tr_variant const& src)
{
    if (auto val = src.get_if<double>(); val != nullptr)
    {
        return *val;
    }

    return {};
}

template<>
tr_variant VariantConverter::save<double>(double const& val)
{
    return val;
}

// ---

template<>
std::optional<tr_encryption_mode> VariantConverter::load<tr_encryption_mode>(tr_variant const& src)
{
    static constexpr auto Keys = EncryptionKeys;

    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [key, encryption] : Keys)
        {
            if (key == needle)
            {
                return encryption;
            }
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        for (auto const& [key, encryption] : Keys)
        {
            if (encryption == *val)
            {
                return encryption;
            }
        }
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_encryption_mode>(tr_encryption_mode const& val)
{
    return static_cast<int64_t>(val);
}

// ---

template<>
std::optional<tr_log_level> VariantConverter::load<tr_log_level>(tr_variant const& src)
{
    static constexpr auto Keys = LogKeys;

    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, log_level] : Keys)
        {
            if (needle == name)
            {
                return log_level;
            }
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        for (auto const& [name, log_level] : Keys)
        {
            if (log_level == *val)
            {
                return log_level;
            }
        }
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_log_level>(tr_log_level const& val)
{
    return static_cast<int64_t>(val);
}

// ---

template<>
std::optional<tr_mode_t> VariantConverter::load<tr_mode_t>(tr_variant const& src)
{
    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        if (auto const mode = tr_num_parse<uint32_t>(*val, nullptr, 8); mode)
        {
            return static_cast<tr_mode_t>(*mode);
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        return static_cast<tr_mode_t>(*val);
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_mode_t>(tr_mode_t const& val)
{
    return fmt::format("{:#03o}", val);
}

// ---

template<>
std::optional<tr_port> VariantConverter::load<tr_port>(tr_variant const& src)
{
    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        return tr_port::from_host(*val);
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_port>(tr_port const& val)
{
    return int64_t{ val.host() };
}

// ---

template<>
std::optional<tr_preallocation_mode> VariantConverter::load<tr_preallocation_mode>(tr_variant const& src)
{
    static constexpr auto Keys = PreallocationKeys;

    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, value] : Keys)
        {
            if (name == needle)
            {
                return value;
            }
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        for (auto const& [name, value] : Keys)
        {
            if (value == *val)
            {
                return value;
            }
        }
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_preallocation_mode>(tr_preallocation_mode const& val)
{
    return int64_t{ val };
}

// ---

template<>
std::optional<tr_preferred_transport> VariantConverter::load<tr_preferred_transport>(tr_variant const& src)
{
    static constexpr auto Keys = PreferredTransportKeys;

    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, value] : Keys)
        {
            if (name == needle)
            {
                return value;
            }
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        for (auto const& [name, value] : Keys)
        {
            if (value == *val)
            {
                return value;
            }
        }
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_preferred_transport>(tr_preferred_transport const& val)
{
    for (auto const& [key, value] : PreferredTransportKeys)
    {
        if (value == val)
        {
            return key;
        }
    }

    return static_cast<int64_t>(val);
}

// ---

template<>
std::optional<size_t> VariantConverter::load<size_t>(tr_variant const& src)
{
    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        return static_cast<size_t>(*val);
    }

    return {};
}

template<>
tr_variant VariantConverter::save<size_t>(size_t const& val)
{
    return uint64_t{ val };
}

// ---

template<>
std::optional<std::string> VariantConverter::load<std::string>(tr_variant const& src)
{
    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        return std::string{ *val };
    }

    return {};
}

template<>
tr_variant VariantConverter::save<std::string>(std::string const& val)
{
    return val;
}

// ---

template<>
std::optional<tr_tos_t> VariantConverter::load<tr_tos_t>(tr_variant const& src)
{
    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        return tr_tos_t::from_string(*val);
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        return tr_tos_t{ static_cast<int>(*val) };
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_tos_t>(tr_tos_t const& val)
{
    return val.toString();
}

// ---

template<>
std::optional<tr_verify_added_mode> VariantConverter::load<tr_verify_added_mode>(tr_variant const& src)
{
    static constexpr auto& Keys = VerifyModeKeys;

    if (auto const* val = src.get_if<std::string_view>(); val != nullptr)
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, value] : Keys)
        {
            if (name == needle)
            {
                return value;
            }
        }
    }

    if (auto const* val = src.get_if<int64_t>(); val != nullptr)
    {
        for (auto const& [name, value] : Keys)
        {
            if (value == *val)
            {
                return value;
            }
        }
    }

    return {};
}

template<>
tr_variant VariantConverter::save<tr_verify_added_mode>(tr_verify_added_mode const& val)
{
    for (auto const& [key, value] : VerifyModeKeys)
    {
        if (value == val)
        {
            return key;
        }
    }

    return static_cast<int64_t>(val);
}

} // namespace libtransmission
