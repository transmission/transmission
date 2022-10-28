// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <array>
#include <cstddef> // size_t
#include <optional>
#include <string>
#include <string_view>
#include <utility> // for std::pair

#include "transmission.h"

#include "log.h" // for tr_log_level
#include "net.h" // for tr_port

namespace libtransmission
{

template<typename T>
struct VariantConverter
{
public:
    static std::optional<T> load(tr_variant* src);
    static void save(tr_variant* tgt, T const& val);
};

template<>
struct VariantConverter<bool>
{
    static std::optional<bool> load(tr_variant* src);
    static void save(tr_variant* tgt, bool const& val);
};

template<>
struct VariantConverter<double>
{
    static std::optional<double> load(tr_variant* src);
    static void save(tr_variant* tgt, double const& val);
};

template<>
struct VariantConverter<tr_encryption_mode>
{
    static std::optional<tr_encryption_mode> load(tr_variant* src);
    static void save(tr_variant* tgt, tr_encryption_mode const& val);

private:
    // clang-format off
    static auto constexpr Keys = std::array<std::pair<std::string_view, tr_encryption_mode>, 3>{{
        { "required", TR_ENCRYPTION_REQUIRED },
        { "preferred", TR_ENCRYPTION_PREFERRED },
        { "allowed", TR_CLEAR_PREFERRED }
    }};
    // clang-format on
};

template<>
struct VariantConverter<int>
{
    static std::optional<int> load(tr_variant* src);
    static void save(tr_variant* tgt, int val);
};

template<>
struct VariantConverter<tr_log_level>
{
    static std::optional<tr_log_level> load(tr_variant* src);
    static void save(tr_variant* tgt, tr_log_level val);

private:
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
};

template<>
struct VariantConverter<mode_t>
{
    static std::optional<mode_t> load(tr_variant* src);
    static void save(tr_variant* tgt, mode_t const& val);
};

template<>
struct VariantConverter<tr_port>
{
    static std::optional<tr_port> load(tr_variant* src);
    static void save(tr_variant* tgt, tr_port const& val);
};

template<>
struct VariantConverter<tr_preallocation_mode>
{
    static std::optional<tr_preallocation_mode> load(tr_variant* src);
    static void save(tr_variant* tgt, tr_preallocation_mode val);

private:
    // clang-format off
    static auto constexpr Keys = std::array<std::pair<std::string_view, tr_preallocation_mode>, 5>{{
        { "off", TR_PREALLOCATE_NONE },
        { "none", TR_PREALLOCATE_NONE },
        { "fast", TR_PREALLOCATE_SPARSE },
        { "sparse", TR_PREALLOCATE_SPARSE },
        { "full", TR_PREALLOCATE_FULL },
    }};
    // clang-format on
};

template<>
struct VariantConverter<size_t>
{
    static std::optional<size_t> load(tr_variant* src);
    static void save(tr_variant* tgt, size_t const& val);
};

template<>
struct VariantConverter<std::string>
{
    static std::optional<std::string> load(tr_variant* src);
    static void save(tr_variant* tgt, std::string const& val);
};

} // namespace libtransmission
