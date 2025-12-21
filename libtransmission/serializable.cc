// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef> // size_t
#include <cstdint> // int64_t, uint32_t
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include <small/set.hpp>
#include <small/vector.hpp>

#include "libtransmission/transmission.h"

#include "libtransmission/log.h" // for tr_log_level
#include "libtransmission/net.h" // for tr_port
#include "libtransmission/open-files.h" // for tr_open_files::Preallocation
#include "libtransmission/peer-io.h" // tr_preferred_transport
#include "libtransmission/serializable.h"
#include "libtransmission/utils.h" // for tr_strv_strip(), tr_strlower()
#include "libtransmission/variant.h"
#include "libtransmission/tr-assert.h"

using namespace std::literals;

namespace libtransmission
{
namespace
{
template<typename T, size_t N>
using Lookup = std::array<std::pair<std::string_view, T>, N>;

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

auto constexpr EncryptionKeys = Lookup<tr_encryption_mode, 3U>{ {
    { "required", TR_ENCRYPTION_REQUIRED },
    { "preferred", TR_ENCRYPTION_PREFERRED },
    { "allowed", TR_CLEAR_PREFERRED },
} };

bool to_encryption_mode(tr_variant const& src, tr_encryption_mode* tgt)
{
    static constexpr auto& Keys = EncryptionKeys;

    if (auto const val = src.value_if<std::string_view>())
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [key, encryption] : Keys)
        {
            if (key == needle)
            {
                *tgt = encryption;
                return true;
            }
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        for (auto const& [key, encryption] : Keys)
        {
            if (encryption == *val)
            {
                *tgt = encryption;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_encryption_mode(tr_encryption_mode const& val)
{
    return static_cast<int64_t>(val);
}

// ---

auto constexpr LogKeys = Lookup<tr_log_level, 7U>{ {
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
    static constexpr auto& Keys = LogKeys;

    if (auto const val = src.value_if<std::string_view>())
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, log_level] : Keys)
        {
            if (needle == name)
            {
                *tgt = log_level;
                return true;
            }
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        for (auto const& [name, log_level] : Keys)
        {
            if (log_level == *val)
            {
                *tgt = log_level;
                return true;
            }
        }
    }

    return false;
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

auto constexpr PreallocationKeys = Lookup<tr_open_files::Preallocation, 5U>{ {
    { "off", tr_open_files::Preallocation::None },
    { "none", tr_open_files::Preallocation::None },
    { "fast", tr_open_files::Preallocation::Sparse },
    { "sparse", tr_open_files::Preallocation::Sparse },
    { "full", tr_open_files::Preallocation::Full },
} };

bool to_preallocation_mode(tr_variant const& src, tr_open_files::Preallocation* tgt)
{
    static constexpr auto& Keys = PreallocationKeys;

    if (auto const val = src.value_if<std::string_view>())
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, value] : Keys)
        {
            if (name == needle)
            {
                *tgt = value;
                return true;
            }
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        for (auto const& [name, value] : Keys)
        {
            if (value == static_cast<tr_open_files::Preallocation>(*val))
            {
                *tgt = value;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_preallocation_mode(tr_open_files::Preallocation const& val)
{
    return static_cast<int64_t>(val);
}

// ---

auto constexpr PreferredTransportKeys = Lookup<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT>{ {
    { "utp", TR_PREFER_UTP },
    { "tcp", TR_PREFER_TCP },
} };

bool to_preferred_transport(
    tr_variant const& src,
    small::max_size_vector<tr_preferred_transport, TR_NUM_PREFERRED_TRANSPORT>* tgt)
{
    static auto constexpr LoadSingle = [](tr_variant const& var)
    {
        static constexpr auto& Keys = PreferredTransportKeys;

        if (auto const val = var.value_if<std::string_view>())
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

        if (auto const val = var.value_if<int64_t>())
        {
            if (auto const i = *val; i >= 0 && i < TR_NUM_PREFERRED_TRANSPORT)
            {
                return static_cast<tr_preferred_transport>(i);
            }
        }

        return TR_NUM_PREFERRED_TRANSPORT;
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
        for (auto const& [key, value] : PreferredTransportKeys)
        {
            if (value == ele)
            {
                return key;
            }
        }

        return ele;
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

bool to_optional_string(tr_variant const& src, std::optional<std::string>* tgt)
{
    if (src.holds_alternative<std::nullptr_t>())
    {
        tgt->reset();
        return true;
    }

    if (auto const val = src.value_if<std::string_view>())
    {
        *tgt = std::string{ *val };
        return true;
    }

    return false;
}

tr_variant from_optional_string(std::optional<std::string> const& val)
{
    return val ? tr_variant{ *val } : nullptr;
}

// ---

bool to_tos_t(tr_variant const& src, tr_tos_t* tgt)
{
    if (auto const val = src.value_if<std::string_view>())
    {
        if (auto const tos = tr_tos_t::from_string(*val); tos)
        {
            *tgt = *tos;
            return true;
        }

        return false;
    }

    if (auto const val = src.value_if<int64_t>())
    {
        *tgt = tr_tos_t{ static_cast<int>(*val) };
        return true;
    }

    return false;
}

tr_variant from_tos_t(tr_tos_t const& val)
{
    return val.toString();
}

// ---

auto constexpr VerifyModeKeys = Lookup<tr_verify_added_mode, 2U>{ {
    { "fast", TR_VERIFY_ADDED_FAST },
    { "full", TR_VERIFY_ADDED_FULL },
} };

bool to_verify_added_mode(tr_variant const& src, tr_verify_added_mode* tgt)
{
    static constexpr auto& Keys = VerifyModeKeys;

    if (auto const val = src.value_if<std::string_view>())
    {
        auto const needle = tr_strlower(tr_strv_strip(*val));

        for (auto const& [name, value] : Keys)
        {
            if (name == needle)
            {
                *tgt = value;
                return true;
            }
        }
    }

    if (auto const val = src.value_if<int64_t>())
    {
        for (auto const& [name, value] : Keys)
        {
            if (value == *val)
            {
                *tgt = value;
                return true;
            }
        }
    }

    return false;
}

tr_variant from_verify_added_mode(tr_verify_added_mode const& val)
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
} // unnamed namespace

Serializers::ConvertersMap Serializers::converters = { {
    Serializers::build_converter_entry(to_bool, from_bool),
    Serializers::build_converter_entry(to_double, from_double),
    Serializers::build_converter_entry(to_encryption_mode, from_encryption_mode),
    Serializers::build_converter_entry(to_log_level, from_log_level),
    Serializers::build_converter_entry(to_mode_t, from_mode_t),
    Serializers::build_converter_entry(to_msec, from_msec),
    Serializers::build_converter_entry(to_optional_string, from_optional_string),
    Serializers::build_converter_entry(to_port, from_port),
    Serializers::build_converter_entry(to_preallocation_mode, from_preallocation_mode),
    Serializers::build_converter_entry(to_preferred_transport, from_preferred_transport),
    Serializers::build_converter_entry(to_size_t, from_size_t),
    Serializers::build_converter_entry(to_string, from_string),
    Serializers::build_converter_entry(to_tos_t, from_tos_t),
    Serializers::build_converter_entry(to_verify_added_mode, from_verify_added_mode),
} };

} // namespace libtransmission
