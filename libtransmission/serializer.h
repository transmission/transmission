// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cmath>
#include <array>
#include <cstddef>
#include <iterator>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <typeinfo>
#include <utility>

#include <fmt/core.h>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace libtransmission::serializer
{

// These type traits are used for serialize() and deserialize() to sniff
// out containers that support `push_back()`, `insert()`, `reserve()`, etc.
// Example uses: (de)serializing std::vector<T>, QStringList, small::set<T>
namespace detail
{

// NOLINTBEGIN(readability-identifier-naming)
// use std-style naming for these traits

// TODO(c++20): use std::remove_cvref_t (P0550R2) when GCC >= 9.1
template<typename T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

// Type trait: is C a container with push_back (but not a string)?
template<typename C, typename = void>
inline constexpr bool is_push_back_range_v = false;

template<typename C>
inline constexpr bool is_push_back_range_v<
    C,
    std::void_t<
        typename C::value_type,
        decltype(std::begin(std::declval<C const&>())),
        decltype(std::end(std::declval<C const&>())),
        decltype(std::declval<C&>().push_back(
            std::declval<typename C::value_type const&>()))>> = !std::is_same_v<C, std::basic_string<typename C::value_type>>;

// Type trait: is C a container with insert (like std::set)?
template<typename C, typename = void>
inline constexpr bool is_insert_range_v = false;

template<typename C>
inline constexpr bool is_insert_range_v<
    C,
    std::void_t<
        typename C::value_type,
        decltype(std::begin(std::declval<C const&>())),
        decltype(std::end(std::declval<C const&>())),
        decltype(std::declval<C&>().insert(std::declval<typename C::value_type const&>()))>> = true;

// Type trait: is C a std::array?
template<typename C>
inline constexpr bool is_std_array_v = false;

template<typename T, std::size_t N>
inline constexpr bool is_std_array_v<std::array<T, N>> = true;

template<typename C>
inline constexpr bool is_optional_v = false;

template<typename T>
inline constexpr bool is_optional_v<std::optional<T>> = true;

template<typename C>
tr_variant from_push_back_range(C const& src);

template<typename C>
bool to_push_back_range(tr_variant const& src, C* ptgt);

template<typename C>
tr_variant from_insert_range(C const& src);

template<typename C>
bool to_insert_range(tr_variant const& src, C* ptgt);

template<typename C>
tr_variant from_array(C const& src);

template<typename C>
bool to_array(tr_variant const& src, C* ptgt);

template<typename T>
tr_variant from_optional(std::optional<T> const& src);

template<typename T>
bool to_optional(tr_variant const& src, std::optional<T>* ptgt);

// Call reserve() if available, otherwise no-op
template<typename C>
auto reserve_if_possible(C& c, std::size_t n) -> decltype(c.reserve(n), void())
{
    c.reserve(n);
}
template<typename C>
void reserve_if_possible(C& /*c*/, ...) // NOLINT(cert-dcl50-cpp)
{
}

// NOLINTEND(readability-identifier-naming)
} // namespace detail

/**
 * Registry for `tr_variant` <-> `T` converters.
 * Used by the `serializable` helpers to load/save fields in a class.
 */
class Converters
{
public:
    template<typename T>
    using Deserialize = bool (*)(tr_variant const& src, T* ptgt);

    template<typename T>
    using Serialize = tr_variant (*)(T const& src);

    template<typename T>
    static tr_variant serialize(T const& src)
    {
        if (converter_storage<T>.serialize != nullptr)
        {
            return converter_storage<T>.serialize(src);
        }

        if constexpr (detail::is_push_back_range_v<T>)
        {
            return detail::from_push_back_range(src);
        }
        else if constexpr (detail::is_insert_range_v<T>)
        {
            return detail::from_insert_range(src);
        }
        else if constexpr (detail::is_std_array_v<T>)
        {
            return detail::from_array(src);
        }
        else if constexpr (detail::is_optional_v<T>)
        {
            return detail::from_optional(src);
        }
        else
        {
            fmt::print(stderr, "ERROR: No serializer registered for type '{}'\n", typeid(T).name());
            return {};
        }
    }

    template<typename T>
    static bool deserialize(tr_variant const& src, T* const ptgt)
    {
        if (converter_storage<T>.deserialize != nullptr)
        {
            return converter_storage<T>.deserialize(src, ptgt);
        }

        if constexpr (detail::is_push_back_range_v<T>)
        {
            return detail::to_push_back_range(src, ptgt);
        }
        else if constexpr (detail::is_insert_range_v<T>)
        {
            return detail::to_insert_range(src, ptgt);
        }
        else if constexpr (detail::is_std_array_v<T>)
        {
            return detail::to_array(src, ptgt);
        }
        else if constexpr (detail::is_optional_v<T>)
        {
            return detail::to_optional(src, ptgt);
        }
        else
        {
            fmt::print(stderr, "ERROR: No deserializer registered for type '{}'\n", typeid(T).name());
            return false;
        }
    }

    // register a new tr_variant<->T converter.
    template<typename T>
    static void add(Deserialize<T> deserialize, Serialize<T> serialize)
    {
        converter_storage<T> = { deserialize, serialize };
    }

    static void ensure_default_converters();

private:
    template<typename T>
    struct ConverterStorage
    {
        Deserialize<T> deserialize = nullptr;
        Serialize<T> serialize = nullptr;
    };

    template<typename T>
    static inline ConverterStorage<T> converter_storage;
};

template<typename T>
[[nodiscard]] std::optional<T> to_value(tr_variant const& var)
{
    if (auto ret = T{}; Converters::deserialize<T>(var, &ret))
    {
        return ret;
    }

    return {};
}

template<typename T>
[[nodiscard]] tr_variant to_variant(T const& val)
{
    return Converters::serialize<T>(val);
}

// ---

/**
 * Helpers for converting structured types to/from a `tr_variant`.
 *
 * Types opt in by declaring a `fields` tuple, typically:
 * `static constexpr auto fields = std::tuple{ Field<&T::member>{key}, ... }`.
 */

/**
 * Describes a single serializable field in a struct.
 *
 * @tparam MemberPtr Pointer-to-member, e.g. `&MyStruct::my_field`
 * @tparam Key       Key type used for lookup in the variant map (default: tr_quark)
 *
 * Example:
 *   Field<&Settings::port>{ TR_KEY_peer_port }
 */
template<auto MemberPtr, typename Key = tr_quark>
struct Field;

template<typename Owner, typename T, T Owner::* MemberPtr, typename Key>
struct Field<MemberPtr, Key>
{
    Key const key;

    explicit constexpr Field(Key key_in) noexcept
        : key{ std::move(key_in) }
    {
    }

    template<typename Derived>
    constexpr T& ref(Derived& derived) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        return static_cast<Owner&>(derived).*MemberPtr;
    }

    template<typename Derived>
    constexpr T const& ref(Derived const& derived) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        return static_cast<Owner const&>(derived).*MemberPtr;
    }

    template<typename Derived>
    void load(Derived* derived, tr_variant::Map const& map) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        if (auto const iter = map.find(key); iter != std::end(map))
        {
            (void)Converters::deserialize(iter->second, &(static_cast<Owner*>(derived)->*MemberPtr));
        }
    }

    template<typename Derived>
    void save(Derived const* derived, tr_variant::Map& map) const
    {
        static_assert(std::is_base_of_v<Owner, Derived>);
        map.try_emplace(key, Converters::serialize(static_cast<Owner const*>(derived)->*MemberPtr));
    }
};

/**
 * Load fields from a variant map into a target object.
 * Missing keys are silently ignored (fields retain their existing values).
 * If `src` is not a map, this is a no-op.
 *
 * @param tgt    The object to populate
 * @param fields A tuple of Field<> descriptors
 * @param src    The source variant (expected to be a Map)
 */
template<typename T, typename Fields>
void load(T& tgt, Fields const& fields, tr_variant const& src)
{
    if (auto const* map = src.get_if<tr_variant::Map>())
    {
        std::apply([&tgt, map](auto const&... field) { (field.load(&tgt, *map), ...); }, fields);
    }
}

/**
 * Save an object's fields to a variant map.
 *
 * @param src    The object to serialize
 * @param fields A tuple of Field<> descriptors
 * @return       A tr_variant::Map containing the serialized fields
 */
template<typename T, typename Fields>
[[nodiscard]] tr_variant::Map save(T const& src, Fields const& fields)
{
    auto map = tr_variant::Map{ std::tuple_size_v<detail::remove_cvref_t<Fields>> };
    std::apply([&src, &map](auto const&... field) { (field.save(&src, map), ...); }, fields);
    return map;
}

/**
 * Set a field's value iff the value changes.
 *
 * If a match of type `Value` is found for `key` *and* its value differs from `val`,
 * Then the field is assigned and `true` is returned.
 *
 * @return true iff the key was found and the field changed.
 */
template<typename T, typename Fields, typename Key, typename Value>
bool set_if_changed(T& tgt, Fields const& fields, Key const key, Value val)
{
    bool key_found = false;
    bool changed = false;

    std::apply(
        [&tgt, key, &val, &key_found, &changed](auto const&... field)
        {
            auto const try_one = [&tgt, key, &val, &key_found, &changed](auto const& f)
            {
                if (changed || f.key != key)
                {
                    return;
                }

                key_found = true;

                auto& current = f.ref(tgt);
                using CurT = std::decay_t<decltype(current)>;
                if constexpr (std::is_same_v<CurT, std::decay_t<Value>>)
                {
                    bool equal = false;
                    if constexpr (std::is_floating_point_v<CurT>)
                    {
                        auto const diff = std::fabs(current - val);
                        auto const abs_current = std::fabs(current);
                        auto const abs_val = std::fabs(val);
                        auto const max_abs = abs_current > abs_val ? abs_current : abs_val;
                        auto const scale = max_abs > CurT{ 1 } ? max_abs : CurT{ 1 };
                        equal = diff <= std::numeric_limits<CurT>::epsilon() * scale;
                    }
                    else
                    {
                        equal = (current == val);
                    }

                    if (!equal)
                    {
                        current = std::move(val);
                        changed = true;
                    }
                }
            };

            (try_one(field), ...);
        },
        fields);

    return key_found && changed;
}

/**
 * Check whether a key exists in a `fields` tuple.
 */
template<typename Fields, typename Key>
[[nodiscard]] bool constexpr contains(Fields const& fields, Key const key)
{
    if constexpr (std::tuple_size_v<detail::remove_cvref_t<Fields>> == 0)
    {
        return false;
    }

    return std::apply([key](auto const&... field) { return ((field.key == key) || ...); }, fields);
}

/**
 * Get a field's value.
 *
 * @return the value if the field is found and its type exactly matches Value.
 */
template<typename Value, typename T, typename Fields, typename Key>
[[nodiscard]] std::optional<Value> get_value(T const& src, Fields const& fields, Key const key)
{
    auto ret = std::optional<Value>{};

    std::apply(
        [&src, key, &ret](auto const&... field)
        {
            auto const try_one = [&src, key, &ret](auto const& f)
            {
                if (ret.has_value() || f.key != key)
                {
                    return;
                }

                auto const& current = f.ref(src);
                if constexpr (std::is_same_v<std::decay_t<decltype(current)>, Value>)
                {
                    ret = current;
                }
            };

            (try_one(field), ...);
        },
        fields);

    return ret;
}

// ---

// N.B. This second `detail` block contains the implementations of
// to_push_back_range, from_push_back_range, etc., which were forward-
// declared above. They must be defined after `Converters` because
// they call Converters::serialize/deserialize for each element.
namespace detail
{

template<typename C>
tr_variant from_push_back_range(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src)
    {
        ret.emplace_back(Converters::serialize(elem));
    }
    return ret;
}

template<typename C>
bool to_push_back_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    auto tmp = C{};
    reserve_if_possible(tmp, std::size(*vec));

    for (auto const& elem : *vec)
    {
        typename C::value_type value{};
        if (!Converters::deserialize(elem, &value))
        {
            return false;
        }
        tmp.push_back(std::move(value));
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename C>
tr_variant from_insert_range(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src)
    {
        ret.emplace_back(Converters::serialize(elem));
    }
    return ret;
}

template<typename C>
bool to_insert_range(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    auto tmp = C{};

    for (auto const& elem : *vec)
    {
        typename C::value_type value{};
        if (!Converters::deserialize(elem, &value))
        {
            return false;
        }
        tmp.insert(std::move(value));
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename C>
tr_variant from_array(C const& src)
{
    auto ret = tr_variant::Vector{};
    ret.reserve(std::size(src));
    for (auto const& elem : src)
    {
        ret.emplace_back(Converters::serialize(elem));
    }
    return ret;
}

template<typename C>
bool to_array(tr_variant const& src, C* const ptgt)
{
    auto const* const vec = src.get_if<tr_variant::Vector>();
    if (vec == nullptr)
    {
        return false;
    }

    if (std::size(*vec) != std::size(*ptgt))
    {
        return false; // Array size mismatch
    }

    auto tmp = C{};
    for (std::size_t i = 0; i < std::size(*vec); ++i)
    {
        if (!Converters::deserialize((*vec)[i], &tmp[i]))
        {
            return false;
        }
    }

    *ptgt = std::move(tmp);
    return true;
}

template<typename T>
tr_variant from_optional(std::optional<T> const& src)
{
    static_assert(!is_optional_v<T>);
    return src ? Converters::serialize(*src) : nullptr;
}

template<typename T>
bool to_optional(tr_variant const& src, std::optional<T>* ptgt)
{
    static_assert(!is_optional_v<T>);
    if (src.index() == tr_variant::NullIndex)
    {
        ptgt->reset();
        return true;
    }

    if (auto val = to_value<T>(src))
    {
        *ptgt = std::move(val);
        return true;
    }

    return false;
}

} // namespace detail
} // namespace libtransmission::serializer
