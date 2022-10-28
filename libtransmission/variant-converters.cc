// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>

#include "transmission.h"

#include "utils.h" // for tr_strvStrip(), tr_strlower()
#include "variant.h"
#include "variant-converters.h"

using namespace std::literals;

namespace libtransmission
{

std::optional<bool> VariantConverter<bool>::load(tr_variant* src)
{
    if (auto val = bool{}; tr_variantGetBool(src, &val))
    {
        return val;
    }

    return {};
}

void VariantConverter<bool>::save(tr_variant* tgt, bool const& val)
{
    tr_variantInitBool(tgt, val);
}

///

std::optional<double> VariantConverter<double>::load(tr_variant* src)
{
    if (auto val = double{}; tr_variantGetReal(src, &val))
    {
        return val;
    }

    return {};
}

void VariantConverter<double>::save(tr_variant* tgt, double const& val)
{
    tr_variantInitReal(tgt, val);
}

///

std::optional<tr_encryption_mode> VariantConverter<tr_encryption_mode>::load(tr_variant* src)
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

void VariantConverter<tr_encryption_mode>::save(tr_variant* tgt, tr_encryption_mode const& val)
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

///

std::optional<int> VariantConverter<int>::load(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return static_cast<int>(val);
    }

    return {};
}

void VariantConverter<int>::save(tr_variant* tgt, int val)
{
    tr_variantInitInt(tgt, val);
}

///

std::optional<tr_log_level> VariantConverter<tr_log_level>::load(tr_variant* src)
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

void VariantConverter<tr_log_level>::save(tr_variant* tgt, tr_log_level val)
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

///

std::optional<mode_t> VariantConverter<mode_t>::load(tr_variant* src)
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

void VariantConverter<mode_t>::save(tr_variant* tgt, mode_t const& val)
{
    tr_variantInitStr(tgt, fmt::format("{:03o}", val));
}

///

std::optional<tr_port> VariantConverter<tr_port>::load(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return tr_port::fromHost(val);
    }

    return {};
}

void VariantConverter<tr_port>::save(tr_variant* tgt, tr_port const& val)
{
    tr_variantInitInt(tgt, val.host());
}

///

std::optional<tr_preallocation_mode> VariantConverter<tr_preallocation_mode>::load(tr_variant* src)
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

void VariantConverter<tr_preallocation_mode>::save(tr_variant* tgt, tr_preallocation_mode val)
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

///

std::optional<size_t> VariantConverter<size_t>::load(tr_variant* src)
{
    if (auto val = int64_t{}; tr_variantGetInt(src, &val))
    {
        return static_cast<size_t>(val);
    }

    return {};
}

void VariantConverter<size_t>::save(tr_variant* tgt, size_t const& val)
{
    tr_variantInitInt(tgt, val);
}

///

std::optional<std::string> VariantConverter<std::string>::load(tr_variant* src)
{
    if (auto val = std::string_view{}; tr_variantGetStrView(src, &val))
    {
        return std::string{ val };
    }

    return {};
}

void VariantConverter<std::string>::save(tr_variant* tgt, std::string const& val)
{
    tr_variantInitStr(tgt, val);
}

} // namespace libtransmission
