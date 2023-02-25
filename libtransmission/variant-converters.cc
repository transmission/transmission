// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>

#include "transmission.h"

#include "log.h" // for tr_log_level
#include "net.h" // for tr_port
#include "utils.h" // for tr_strvStrip(), tr_strlower()
#include "variant.h"

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
} // namespace

namespace libtransmission
{
template<>
std::optional<bool> VariantConverter::load<bool>(tr_variant* src)
{
    if (auto val = bool{}; tr_variantGetBool(src, &val))
    {
        return val;
    }

    return {};
}

template<>
void VariantConverter::save<bool>(tr_variant* tgt, bool const& val)
{
    tr_variantInitBool(tgt, val);
}

// ---

template<>
std::optional<double> VariantConverter::load<double>(tr_variant* src)
{
    if (auto val = double{}; tr_variantGetReal(src, &val))
    {
        return val;
    }

    return {};
}

template<>
void VariantConverter::save<double>(tr_variant* tgt, double const& val)
{
    tr_variantInitReal(tgt, val);
}

// ---

template<>
std::optional<tr_encryption_mode> VariantConverter::load<tr_encryption_mode>(tr_variant* src)
{
    static constexpr auto Keys = EncryptionKeys;

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

template<>
void VariantConverter::save<tr_encryption_mode>(tr_variant* tgt, tr_encryption_mode const& val)
{
    tr_variantInitInt(tgt, val);
}

// ---

template<>
std::optional<tr_log_level> VariantConverter::load<tr_log_level>(tr_variant* src)
{
    static constexpr auto Keys = LogKeys;

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

template<>
void VariantConverter::save<tr_log_level>(tr_variant* tgt, tr_log_level const& val)
{
    tr_variantInitInt(tgt, val);
}

// ---

template<>
std::optional<tr_mode_t> VariantConverter::load<tr_mode_t>(tr_variant* src)
{
    if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
    {
        if (auto const mode = tr_parseNum<uint32_t>(val, nullptr, 8); mode)
        {
            return static_cast<tr_mode_t>(*mode);
        }
    }

    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return static_cast<tr_mode_t>(val);
    }

    return {};
}

template<>
void VariantConverter::save<tr_mode_t>(tr_variant* tgt, tr_mode_t const& val)
{
    tr_variantInitStr(tgt, fmt::format("{:#03o}", val));
}

// ---

template<>
std::optional<tr_port> VariantConverter::load<tr_port>(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return tr_port::fromHost(val);
    }

    return {};
}

template<>
void VariantConverter::save<tr_port>(tr_variant* tgt, tr_port const& val)
{
    tr_variantInitInt(tgt, val.host());
}

// ---

template<>
std::optional<tr_preallocation_mode> VariantConverter::load<tr_preallocation_mode>(tr_variant* src)
{
    static constexpr auto Keys = PreallocationKeys;

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

template<>
void VariantConverter::save<tr_preallocation_mode>(tr_variant* tgt, tr_preallocation_mode const& val)
{
    tr_variantInitInt(tgt, val);
}

// ---

template<>
std::optional<size_t> VariantConverter::load<size_t>(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return static_cast<size_t>(val);
    }

    return {};
}

template<>
void VariantConverter::save<size_t>(tr_variant* tgt, size_t const& val)
{
    tr_variantInitInt(tgt, val);
}

// ---

template<>
std::optional<std::string> VariantConverter::load<std::string>(tr_variant* src)
{
    if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
    {
        return std::string{ val };
    }

    return {};
}

template<>
void VariantConverter::save<std::string>(tr_variant* tgt, std::string const& val)
{
    tr_variantInitStr(tgt, val);
}

// ---

template<>
std::optional<tr_tos_t> VariantConverter::load<tr_tos_t>(tr_variant* src)
{
    if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
    {
        return tr_tos_t::from_string(val);
    }

    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return tr_tos_t{ static_cast<int>(val) };
    }

    return {};
}

template<>
void VariantConverter::save<tr_tos_t>(tr_variant* tgt, tr_tos_t const& val)
{
    tr_variantInitStr(tgt, val.toString());
}

// ---

template<>
std::optional<tr_verify_added_mode> VariantConverter::load<tr_verify_added_mode>(tr_variant* src)
{
    static constexpr auto& Keys = VerifyModeKeys;

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

template<>
void VariantConverter::save<tr_verify_added_mode>(tr_variant* tgt, tr_verify_added_mode const& val)
{
    for (auto const& [key, value] : VerifyModeKeys)
    {
        if (value == val)
        {
            tr_variantInitStrView(tgt, key);
            return;
        }
    }

    tr_variantInitInt(tgt, val);
}

} // namespace libtransmission
