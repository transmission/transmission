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

namespace libtransmission
{

template<>
struct VariantConverter<bool>
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
struct VariantConverter<double>
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
struct VariantConverter<tr_encryption_mode>
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
struct VariantConverter<int>
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
struct VariantConverter<tr_log_level>
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
struct VariantConverter<mode_t>
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
struct VariantConverter<tr_port>
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
struct VariantConverter<tr_preallocation_mode>
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
struct VariantConverter<size_t>
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
struct VariantConverter<std::string>
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

void SessionSettings::load(tr_variant* src)
{
#define V(key, field, type, default_value) \
    if (auto* const child = tr_variantDictFind(src, key); child != nullptr) \
    { \
        if (auto val = VariantConverter<decltype(field)>::load(child); val) \
        { \
            this->field = *val; \
        } \
    }
    SESSION_SETTINGS_FIELDS(V);
#undef V
}

void SessionSettings::save(tr_variant* tgt) const
{
#define V(key, field, type, default_value) \
    tr_variantDictRemove(tgt, key); \
    VariantConverter<decltype(field)>::save(tr_variantDictAdd(tgt, key), field);
    SESSION_SETTINGS_FIELDS(V)
#undef V
}

} // namespace libtransmission
