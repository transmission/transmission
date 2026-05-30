// This file Copyright © Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/transmission.h> /* tr_variant, tr_session */
#include <libtransmission/quark.h>
#include <libtransmission/serializer.h>
#include <libtransmission/variant.h>

#include <cstdint> // int64_t
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] tr_variant::Map& gtr_pref_get_map();

template<typename T>
[[nodiscard]] std::optional<T> gtr_pref_get(tr_quark const key)
{
    using namespace tr::serializer;
    auto const& map = gtr_pref_get_map();
    auto const iter = map.find(key);
    return iter != std::end(map) ? to_value<T>(iter->second) : std::nullopt;
}

template<typename T>
void gtr_pref_set(tr_quark const key, T const& val)
{
    using namespace tr::serializer;
    auto& map = gtr_pref_get_map();
    map.insert_or_assign(key, to_variant(val));
}

void gtr_pref_init(std::string_view config_dir);

template<std::integral T>
T gtr_pref_int_get(tr_quark const key)
{
    return gtr_pref_get_map().value_if<T>(key).value_or(T{});
}

template<std::integral T>
void gtr_pref_int_set(tr_quark key, T value)
{
    gtr_pref_get_map().insert_or_assign(key, value);
}

double gtr_pref_double_get(tr_quark key);
void gtr_pref_double_set(tr_quark key, double value);

bool gtr_pref_flag_get(tr_quark key);
void gtr_pref_flag_set(tr_quark key, bool value);

std::vector<std::string> gtr_pref_strv_get(tr_quark key);

std::string gtr_pref_string_get(tr_quark key);
void gtr_pref_string_set(tr_quark key, std::string_view value);

void gtr_pref_save(tr_session* /*session*/);
tr_variant& gtr_pref_get_all();
