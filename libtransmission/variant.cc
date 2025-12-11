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

#include "libtransmission/error.h"
#include "libtransmission/log.h"
#include "libtransmission/quark.h"
#include "libtransmission/tr-assert.h"
#include "libtransmission/utils.h"
#include "libtransmission/variant.h"

using namespace std::literals;

namespace
{
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

// Specialisations for int64_t and bool could have been inline and constexpr,
// but aren't because https://gcc.gnu.org/bugzilla/show_bug.cgi?id=85282

template<>
[[nodiscard]] std::optional<int64_t> tr_variant::value_if() noexcept
{
    switch (index())
    {
    case IntIndex:
        return *get_if<IntIndex>();

    case BoolIndex:
        return *get_if<BoolIndex>() ? 1 : 0;

    default:
        return {};
    }
}

template<>
[[nodiscard]] std::optional<bool> tr_variant::value_if() noexcept
{
    switch (index())
    {
    case BoolIndex:
        return *get_if<BoolIndex>();

    case IntIndex:
        if (auto const val = *get_if<IntIndex>(); val == 0 || val == 1)
        {
            return val != 0;
        }
        break;

    case StringIndex:
        if (auto const val = *get_if<StringIndex>(); val == "true"sv)
        {
            return true;
        }
        else if (val == "false"sv)
        {
            return false;
        }
        break;

    default:
        break;
    }

    return {};
}

template<>
[[nodiscard]] std::optional<double> tr_variant::value_if() noexcept
{
    switch (index())
    {
    case DoubleIndex:
        return *get_if<DoubleIndex>();

    case IntIndex:
        return static_cast<double>(*get_if<IntIndex>());

    case StringIndex:
        return tr_num_parse<double>(*get_if<StringIndex>());

    default:
        return {};
    }
}

// ---

tr_variant::StringHolder::StringHolder(std::string&& str) noexcept
    : str_{ std::move(str) }
{
    sv_ = str_;
}

tr_variant::StringHolder::StringHolder(StringHolder&& that) noexcept
{
    *this = std::move(that);
}

void tr_variant::StringHolder::set_unmanaged(std::string_view sv)
{
    str_.clear();
    sv_ = sv;
}

tr_variant::StringHolder& tr_variant::StringHolder::operator=(StringHolder&& that) noexcept
{
    auto const managed = std::data(that.sv_) == std::data(that.str_);
    std::swap(str_, that.str_);
    sv_ = managed ? str_ : that.sv_;
    return *this;
}

// ---

tr_variant& tr_variant::merge(tr_variant const& that)
{
    that.visit(
        [this](auto const& value)
        {
            using ValueType = std::decay_t<decltype(value)>;

            if constexpr (
                std::is_same_v<ValueType, std::monostate> || std::is_same_v<ValueType, std::nullptr_t> ||
                std::is_same_v<ValueType, bool> || std::is_same_v<ValueType, int64_t> || std::is_same_v<ValueType, double> ||
                std::is_same_v<ValueType, std::string_view>)
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
                if (index() != MapIndex)
                {
                    val_.emplace<Map>();
                }

                if (auto* dest = this->template get_if<MapIndex>(); dest != nullptr)
                {
                    dest->reserve(std::size(*dest) + std::size(value));
                    for (auto const& [key, child] : value)
                    {
                        (*dest)[tr_quark_convert(key)].merge(child);
                    }
                }
            }
        });

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

bool tr_variantListRemove(tr_variant* const var, size_t pos)
{
    if (auto* const vec = var != nullptr ? var->get_if<tr_variant::VectorIndex>() : nullptr;
        vec != nullptr && pos < std::size(*vec))
    {
        vec->erase(std::begin(*vec) + pos);
        return true;
    }

    return false;
}

bool tr_variantGetInt(tr_variant const* const var, int64_t* setme)
{
    return value_if(var, setme);
}

bool tr_variantGetStrView(tr_variant const* const var, std::string_view* setme)
{
    return value_if(var, setme);
}

bool tr_variantGetRaw(tr_variant const* v, std::byte const** setme_raw, size_t* setme_len)
{
    if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
    {
        *setme_raw = reinterpret_cast<std::byte const*>(std::data(sv));
        *setme_len = std::size(sv);
        return true;
    }

    return false;
}

bool tr_variantGetRaw(tr_variant const* v, uint8_t const** setme_raw, size_t* setme_len)
{
    if (auto sv = std::string_view{}; tr_variantGetStrView(v, &sv))
    {
        *setme_raw = reinterpret_cast<uint8_t const*>(std::data(sv));
        *setme_len = std::size(sv);
        return true;
    }

    return false;
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

bool tr_variantDictFindBool(tr_variant* const var, tr_quark key, bool* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetBool(child, setme);
}

bool tr_variantDictFindReal(tr_variant* const var, tr_quark key, double* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetReal(child, setme);
}

bool tr_variantDictFindStrView(tr_variant* const var, tr_quark key, std::string_view* setme)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetStrView(child, setme);
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

bool tr_variantDictFindRaw(tr_variant* const var, tr_quark key, uint8_t const** setme_raw, size_t* setme_len)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

bool tr_variantDictFindRaw(tr_variant* const var, tr_quark key, std::byte const** setme_raw, size_t* setme_len)
{
    auto const* const child = tr_variantDictFind(var, key);
    return tr_variantGetRaw(child, setme_raw, setme_len);
}

// ---

void tr_variantInitList(tr_variant* initme, size_t n_reserve)
{
    auto vec = tr_variant::Vector{};
    vec.reserve(n_reserve);
    *initme = std::move(vec);
}

void tr_variantListReserve(tr_variant* const var, size_t n_reserve)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Vector>());

    if (auto* const vec = var != nullptr ? var->get_if<tr_variant::VectorIndex>() : nullptr; vec != nullptr)
    {
        vec->reserve(std::size(*vec) + n_reserve);
    }
}

void tr_variantInitDict(tr_variant* initme, size_t n_reserve)
{
    *initme = tr_variant::Map{ n_reserve };
}

void tr_variantDictReserve(tr_variant* const var, size_t n_reserve)
{
    TR_ASSERT(var != nullptr);
    TR_ASSERT(var->holds_alternative<tr_variant::Map>());

    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr; map != nullptr)
    {
        map->reserve(std::size(*map) + n_reserve);
    }
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

tr_variant* tr_variantListAddInt(tr_variant* const var, int64_t const value)
{
    return vec_add(var, value);
}

tr_variant* tr_variantListAddReal(tr_variant* const var, double const value)
{
    return vec_add(var, value);
}

tr_variant* tr_variantListAddBool(tr_variant* const var, bool const value)
{
    return vec_add(var, value);
}

tr_variant* tr_variantListAddStr(tr_variant* const var, std::string_view const value)
{
    return vec_add(var, std::string{ value });
}

tr_variant* tr_variantListAddStrView(tr_variant* const var, std::string_view value)
{
    return vec_add(var, tr_variant::unmanaged_string(value));
}

tr_variant* tr_variantListAddRaw(tr_variant* const var, void const* value, size_t n_bytes)
{
    return vec_add(var, tr_variant::make_raw(value, n_bytes));
}

tr_variant* tr_variantListAddList(tr_variant* const var, size_t const n_reserve)
{
    return vec_add(var, tr_variant::make_vector(n_reserve));
}

tr_variant* tr_variantListAddDict(tr_variant* const var, size_t const n_reserve)
{
    return vec_add(var, tr_variant::make_map(n_reserve));
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

tr_variant* tr_variantDictAddBool(tr_variant* const var, tr_quark key, bool val)
{
    return dict_set(var, key, val);
}

tr_variant* tr_variantDictAddReal(tr_variant* const var, tr_quark const key, double const val)
{
    return dict_set(var, key, val);
}

tr_variant* tr_variantDictAddStr(tr_variant* const var, tr_quark const key, std::string_view const val)
{
    return dict_set(var, key, val);
}

tr_variant* tr_variantDictAddRaw(tr_variant* const var, tr_quark const key, void const* const value, size_t const n_bytes)
{
    return dict_set(var, key, std::string{ static_cast<char const*>(value), n_bytes });
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

bool tr_variantDictRemove(tr_variant* const var, tr_quark key)
{
    if (auto* const map = var != nullptr ? var->get_if<tr_variant::MapIndex>() : nullptr; map != nullptr)
    {
        return map->erase(key) != 0U;
    }

    return false;
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
