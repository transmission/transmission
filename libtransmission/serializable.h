// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <new>
#include <optional>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <utility>

#include <small/map.hpp>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace libtransmission
{

/**
 * Registry for `tr_variant` <-> `T` converters.
 * Used by `Serializable` to serialize/deserialize fields in a class.
 */
class Serializers
{
public:
    template<typename T>
    static tr_variant serialize(T const& src)
    {
        return converter_storage<T>.serialize(src);
    }
    static tr_variant serialize(void const* const psrc, std::type_index const idx)
    {
        return converters.at(idx).second(psrc);
    }

    template<typename T>
    static bool deserialize(tr_variant const& src, T* const ptgt)
    {
        return converter_storage<T>.deserialize(src, ptgt);
    }
    static bool deserialize(tr_variant const& src, void* const vptgt, std::type_index const idx)
    {
        return converters.at(idx).first(src, vptgt);
    }

    template<typename T>
    using Deserialize = bool (*)(tr_variant const& src, T* ptgt);

    template<typename T>
    using Serialize = tr_variant (*)(T const& src);

    template<typename T>
    static void add_converter(Deserialize<T> deserialize, Serialize<T> serialize)
    {
        auto [key, val] = build_converter_entry<T>(deserialize, serialize);
        converters.try_emplace(std::move(key), std::move(val));
    }

private:
    using DeserializeFunc = bool (*)(tr_variant const& src, void* tgt);
    using SerializeFunc = tr_variant (*)(void const* src);
    using Converters = std::pair<DeserializeFunc, SerializeFunc>;

    template<typename T>
    struct ConverterStorage
    {
        Deserialize<T> deserialize = nullptr;
        Serialize<T> serialize = nullptr;
    };

    template<typename T>
    static inline ConverterStorage<T> converter_storage;

    template<typename T>
    static std::pair<std::type_index, Converters> build_converter_entry(Deserialize<T> deserialize, Serialize<T> serialize)
    {
        converter_storage<T> = ConverterStorage<T>{ deserialize, serialize };
        return { std::type_index{ typeid(T*) }, Converters{ &deserialize_impl<T>, &serialize_impl<T> } };
    }

    template<typename T>
    static bool deserialize_impl(tr_variant const& src, void* const tgt)
    {
        return converter_storage<T>.deserialize(src, static_cast<T*>(tgt));
    }

    template<typename T>
    static tr_variant serialize_impl(void const* const src)
    {
        return converter_storage<T>.serialize(*static_cast<T const*>(src));
    }

    using ConvertersMap = small::unordered_map<std::type_index, Converters>;
    static ConvertersMap converters;
};

/**
 * Base class for classes that are convertble to a `tr_variant`.
 * Settings classes use this to load and save state from `settings.json`.
 *
 * Subclasses must define a field named `fields` which is an iterable
 * collection of `Serializable::Field`. This is typically declared as a
 * `static const std::array<Field, N>`.
 *
 * If your subclass has a field with a bespoke type, it must use
 * `Serializers::add_converter()` to register how to serialize that type.
 */
template<typename Derived, typename Key = tr_quark>
class Serializable
{
public:
    /** Update this object's state from a tr_variant. */
    void load(tr_variant const& src)
    {
        auto const* map = src.get_if<tr_variant::Map>();
        if (map == nullptr)
        {
            return;
        }

        auto* const derived = static_cast<Derived*>(this);
        for (auto const& field : derived->fields)
        {
            if (auto const iter = map->find(field.key); iter != std::end(*map))
            {
                Serializers::deserialize(iter->second, field.get(derived), field.idx);
            }
        }
    }

    /** Alias for load() */
    void deserialize(tr_variant const& src)
    {
        load(src);
    }

    /**
     * Save this object's fields to a tr_variant.
     * @return a tr_variant::Map that holds the serialized form of this object.
     */
    [[nodiscard]] auto save() const
    {
        auto const* const derived = static_cast<Derived const*>(this);
        auto map = tr_variant::Map{ std::size(derived->fields) };
        for (auto const& field : derived->fields)
        {
            map.try_emplace(field.key, Serializers::serialize(field.get(derived), field.idx));
        }
        return map;
    }

    /** Alias for save() */
    [[nodiscard]] tr_variant::Map serialize() const
    {
        return save();
    }

    /**
     * Set a single property by key.
     * Example: `settings_.set(TR_KEY_filename, "foo.txt");`
     * @return true if the property was changed.
     */
    template<typename T>
    bool set(Key const& key_in, T val_in)
    {
        auto const idx_in = std::type_index{ typeid(T*) };
        auto* const derived = static_cast<Derived*>(this);
        for (auto const& field : derived->fields)
        {
            if (key_in == field.key && idx_in == field.idx)
            {
                if (T& val = *static_cast<T*>(field.get(derived)); val != val_in)
                {
                    val = val_in;
                    return true; // changed
                }

                return false; // found but unchanged
            }
        }
        return false; // not found
    }

    /**
     * Get a single property by key.
     * Example: `bool enabled = settings_.get<bool>(TR_KEY_incomplete_dir_enabled);`
     * @return a std::optional<> which has the value if the key was found.
     */
    template<typename T>
    [[nodiscard]] std::optional<T> get(Key const& key_in) const
    {
        auto const idx_in = std::type_index{ typeid(T*) };
        auto const* const derived = static_cast<Derived const*>(this);
        for (auto const& field : derived->fields)
        {
            if (key_in == field.key && idx_in == field.idx)
            {
                return *static_cast<T const*>(field.get(derived));
            }
        }
        return {};
    }

protected:
    struct Field
    {
        template<typename T>
        Field(Key key_in, T Derived::* ptr)
            : key{ std::move(key_in) }
            , idx{ typeid(T*) }
            , getter_{ &Field::template get_impl<T> }
            , const_getter_{ &Field::template get_const_impl<T> }
        {
            static_assert(sizeof(MemberStorage) >= sizeof(T Derived::*));
            static_assert(alignof(MemberStorage) >= alignof(T Derived::*));
            new (&storage_) T Derived::*(ptr);
        }

        void* get(Derived* self) const noexcept
        {
            return getter_(self, &storage_);
        }

        void const* get(Derived const* self) const noexcept
        {
            return const_getter_(self, &storage_);
        }

        Key key;
        std::type_index idx;

    private:
        using Getter = void* (*)(Derived*, void const*);
        using ConstGetter = void const* (*)(Derived const*, void const*);
        using MemberStorage = std::aligned_storage_t<sizeof(char Derived::*), alignof(char Derived::*)>;

        template<typename T>
        static void* get_impl(Derived* self, void const* opaque) noexcept
        {
            auto const member = *static_cast<T Derived::* const*>(opaque);
            return &(self->*member);
        }

        template<typename T>
        static void const* get_const_impl(Derived const* self, void const* opaque) noexcept
        {
            auto const member = *static_cast<T Derived::* const*>(opaque);
            return &(self->*member);
        }

        Getter getter_;
        ConstGetter const_getter_;
        MemberStorage storage_;
    };

private:
    friend Derived;

    Serializable() = default;
};
} // namespace libtransmission
