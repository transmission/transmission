// This file Copyright © 2022 Mnemosyne LLC.
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

///

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

///

namespace EncryptionHelpers
{
// clang-format off
static auto constexpr Keys = std::array<std::pair<std::string_view, tr_encryption_mode>, 3>{{
    { "required", TR_ENCRYPTION_REQUIRED },
    { "preferred", TR_ENCRYPTION_PREFERRED },
    { "allowed", TR_CLEAR_PREFERRED }
}};
// clang-format on
}

template<>
std::optional<tr_encryption_mode> VariantConverter::load<tr_encryption_mode>(tr_variant* src)
{
    using namespace EncryptionHelpers;

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
    using namespace EncryptionHelpers;

    for (auto const& [key, value] : Keys)
    {
        if (value == val)
        {
            tr_variantInitStrView(tgt, key);
            return;
        }
    }
}

///

template<>
std::optional<int> VariantConverter::load<int>(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return static_cast<int>(val);
    }

    return {};
}

template<>
void VariantConverter::save<int>(tr_variant* tgt, int const& val)
{
    tr_variantInitInt(tgt, val);
}

///

namespace LogLevelHelpers
{
// clang-format off
static auto constexpr Keys = std::array<std::pair<std::string_view, tr_log_level>, 7>{ {
    { "critical", TR_LOG_CRITICAL },
    { "debug", TR_LOG_DEBUG },
    { "error", TR_LOG_ERROR },
    { "info", TR_LOG_INFO },
    { "off", TR_LOG_OFF },
    { "trace", TR_LOG_TRACE },
    { "warn", TR_LOG_WARN },
}};
// clang-format on
} // namespace LogLevelHelpers

template<>
std::optional<tr_log_level> VariantConverter::load<tr_log_level>(tr_variant* src)
{
    using namespace LogLevelHelpers;

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
    using namespace LogLevelHelpers;

    for (auto const& [key, value] : Keys)
    {
        if (value == val)
        {
            tr_variantInitStrView(tgt, key);
            return;
        }
    }
}

///

template<>
std::optional<mode_t> VariantConverter::load<mode_t>(tr_variant* src)
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

template<>
void VariantConverter::save<mode_t>(tr_variant* tgt, mode_t const& val)
{
    tr_variantInitStr(tgt, fmt::format("{:03o}", val));
}

///

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

///

namespace PreallocationModeHelpers
{
// clang-format off
static auto constexpr Keys = std::array<std::pair<std::string_view, tr_preallocation_mode>, 5>{{
    { "off", TR_PREALLOCATE_NONE },
    { "none", TR_PREALLOCATE_NONE },
    { "fast", TR_PREALLOCATE_SPARSE },
    { "sparse", TR_PREALLOCATE_SPARSE },
    { "full", TR_PREALLOCATE_FULL },
}};
// clang-format on
} // namespace PreallocationModeHelpers

template<>
std::optional<tr_preallocation_mode> VariantConverter::load<tr_preallocation_mode>(tr_variant* src)
{
    using namespace PreallocationModeHelpers;

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
    using namespace PreallocationModeHelpers;

    for (auto const& [key, value] : Keys)
    {
        if (value == val)
        {
            tr_variantInitStrView(tgt, key);
            return;
        }
    }
}

///

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

///

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

} // namespace libtransmission
