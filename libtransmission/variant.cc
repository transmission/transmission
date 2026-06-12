// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <variant>

#ifdef _WIN32
#include <share.h>
#endif

#include <fmt/format.h>

#define LIBTRANSMISSION_VARIANT_MODULE

#include "libtransmission/api-compat.h"
#include "libtransmission/error.h"
#include "libtransmission/file-utils.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
void merge_maps(tr_variant::Map& dest, tr_variant::Map const& src)
{
    dest.reserve(std::size(dest) + std::size(src));
    for (auto const& [key, child] : src)
    {
        dest[key].merge(child);
    }
}

template<typename T>
[[nodiscard]] bool value_if(tr_variant const* const var, T* const setme)
{
    if (var != nullptr)
    {
        if (auto val = var->value_if<T>())
        {
            if (setme)
            {
                *setme = *val;
            }
            return true;
        }
    }

    return false;
}

template<typename T>
[[nodiscard]] tr_variant* dict_set(tr_variant* const var, tr_quark const key, T&& val)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr; map != nullptr)
    {
        return &map->insert_or_assign(key, std::forward<T>(val)).first;
    }

    return {};
}

template<typename T>
[[nodiscard]] tr_variant* vec_add(tr_variant* const var, T&& val)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Vector>());

    if (auto* const vec = var != nullptr ? var->get_if<tr_variant::VectorIndex>() : nullptr; vec != nullptr)
    {
        return &vec->emplace_back(std::forward<T>(val));
    }

    return {};
}
} // namespace

// ---

template<>
[[nodiscard]] std::optional<double> tr_variant::value_if() const noexcept
{
    switch (index())
    {
    case DoubleIndex:
        return *get_if<DoubleIndex>();

    case IntIndex:
        return static_cast<double>(*get_if<IntIndex>());

    case StringIndex:
    case StringViewIndex:
        if (auto const sv = value_if<std::string_view>())
        {
            return tr_num_parse<double>(*sv);
        }
        break;

    default:
        break;
    }

    return {};
}

// ---

tr_variant tr_variant::clone() const
{
    auto ret = tr_variant{};
    ret.merge(*this);
    return ret;
}

tr_variant::Map& tr_variant::Map::merge(tr_variant::Map const& that)
{
    merge_maps(*this, that);
    return *this;
}

tr_variant& tr_variant::merge(tr_variant const& that)
{
    that.visit(
        [this](auto const& value)
        {
            using ValueType = std::remove_cvref_t<decltype(value)>;

            if constexpr (
                std::is_same_v<ValueType, std::monostate> || std::is_same_v<ValueType, std::nullptr_t> ||
                std::is_same_v<ValueType, bool> || std::is_same_v<ValueType, int64_t> || std::is_same_v<ValueType, double> ||
                std::is_same_v<ValueType, std::string_view> || std::is_same_v<ValueType, std::string>)
            {
                *this = value;
            }
            else if constexpr (std::is_same_v<ValueType, Vector>)
            {
                auto& dest = val_.emplace<Vector>();
                dest.resize(std::size(value));
                for (size_t i = 0; i < std::size(value); ++i)
                {
                    dest[i].merge(value[i]);
                }
            }
            else if constexpr (std::is_same_v<ValueType, Map>)
            {
                this->merge(value);
            }
        });

    return *this;
}

tr_variant& tr_variant::merge(Map const& that)
{
    if (index() != MapIndex)
    {
        val_.emplace<Map>();
    }

    if (auto* const dest = this->template get_if<MapIndex>(); dest != nullptr)
    {
        merge_maps(*dest, that);
    }

    return *this;
}

// ---

tr_variant* tr_variantDictFind(tr_variant* const var, tr_quark key)
{
    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr; map != nullptr)
    {
        if (auto iter = map->find(key); iter != std::end(*map))
        {
            return &iter->second;
        }
    }

    return {};
}

tr_variant* tr_variantListChild(tr_variant* const var, size_t pos)
{
    if (auto* const vec = var != nullptr ? var->get_if<tr_variant::VectorIndex>() : nullptr; vec != nullptr)
    {
        if (pos < std::size(*vec))
        {
            return &vec->at(pos);
        }
    }

    return {};
}

bool tr_variantGetInt(tr_variant const* const var, int64_t* setme)
{
    return value_if(var, setme);
}

bool tr_variantGetStrView(tr_variant const* const var, std::string_view* setme)
{
    return value_if(var, setme);
}

bool tr_variantGetBool(tr_variant const* const var, bool* setme)
{
    return value_if(var, setme);
}

bool tr_variantGetReal(tr_variant const* const var, double* setme)
{
    return value_if(var, setme);
}

bool tr_variantDictFindInt(tr_variant* const var, tr_quark key, int64_t* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetInt(child, setme);
}

bool tr_variantDictFindList(tr_variant* const var, tr_quark key, tr_variant** setme)
{
    if (auto* const res = tr_variantDictFind(var, key); res != nullptr && res->holds_alternative<tr_variant::Vector>())
    {
        *setme = res;
        return true;
    }

    return false;
}

bool tr_variantDictFindDict(tr_variant* const var, tr_quark key, tr_variant** setme)
{
    if (auto* const res = tr_variantDictFind(var, key); res != nullptr && res->holds_alternative<tr_variant::Map>())
    {
        *setme = res;
        return true;
    }

    return false;
}

// ---

void tr_variantInitList(tr_variant* initme, size_t n_reserve)
{
    auto vec = tr_variant::Vector{};
    vec.reserve(n_reserve);
    *initme = std::move(vec);
}

void tr_variantInitDict(tr_variant* initme, size_t n_reserve)
{
    *initme = tr_variant::Map{ n_reserve };
}

tr_variant* tr_variantListAdd(tr_variant* const var)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Vector>());

    if (auto* const vec = var != nullptr ? var->get_if<tr_variant::VectorIndex>() : nullptr; vec != nullptr)
    {
        return &vec->emplace_back();
    }

    return nullptr;
}

tr_variant* tr_variantDictAdd(tr_variant* const var, tr_quark key)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr; map != nullptr)
    {
        return &(*map)[key];
    }

    return {};
}

tr_variant* tr_variantDictAddInt(tr_variant* const var, tr_quark const key, int64_t const val)
{
    return dict_set(var, key, val);
}

tr_variant* tr_variantDictAddList(tr_variant* const var, tr_quark const key, size_t const n_reserve)
{
    return dict_set(var, key, tr_variant::make_vector(n_reserve));
}

tr_variant* tr_variantDictAddStrView(tr_variant* const var, tr_quark const key, std::string_view const val)
{
    return dict_set(var, key, tr_variant::unmanaged_string(val));
}

tr_variant* tr_variantDictAddDict(tr_variant* const var, tr_quark key, size_t n_reserve)
{
    return dict_set(var, key, tr_variant::make_map(n_reserve));
}

// ---

bool tr_variantDictChild(tr_variant* const var, size_t pos, tr_quark* key, tr_variant** setme_value)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr;
        map != nullptr && pos < std::size(*map))
    {
        auto iter = std::begin(*map);
        std::advance(iter, pos);
        *key = iter->first;
        *setme_value = &iter->second;
        return true;
    }

    return false;
}

void tr_variantMergeDicts(tr_variant* const tgt, tr_variant const* const src)
{
    TR_ASSERT(tgt != nullptr);
    TR_ASSERT(src != nullptr);
    tgt->merge(*src);
}

// ---

std::optional<tr_variant> tr_variant_serde::parse(std::string_view input)
{
    error_ = {};
    return type_ == Type::Json ? parse_json(input) : parse_benc(input);
}

[[nodiscard]] std::optional<tr_variant> tr_variant_serde::parse_file(std::string_view filename)
{
    TR_ASSERT_MSG(!parse_inplace_, "not supported in from_file()");
    parse_inplace_ = false;

    if (auto buf = std::vector<char>{}; tr_file_read(filename, buf, &error_))
    {
        return parse(buf);
    }

    return {};
}

std::string tr_variant_serde::to_string(tr_variant const& var) const
{
    return type_ == Type::Json ? to_json_string(var) : to_benc_string(var);
}

bool tr_variant_serde::to_file(tr_variant const& var, std::string_view filename)
{
    tr_file_save(filename, to_string(var), &error_);

    if (error_)
    {
        tr_logAddError(
            fmt::format(
                fmt::runtime(_("Couldn't save '{path}': {error} ({error_code})")),
                fmt::arg("path", filename),
                fmt::arg("error", error_.message()),
                fmt::arg("error_code", error_.code())));
        return false;
    }

    return true;
}
