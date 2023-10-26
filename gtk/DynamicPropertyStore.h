// This file Copyright © 2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include "Utils.h"

#include <glibmm/object.h>
#include <glibmm/value.h>
#include <glibmm/wrap.h>

#include <array>
#include <functional>
#include <type_traits>

template<typename ObjectT, typename PropertyT>
class DynamicPropertyStore
{
public:
    using ObjectType = ObjectT;
    using PropertyType = PropertyT;

    using PropertyIdType = guint;
    static_assert(std::is_same_v<std::underlying_type_t<PropertyType>, PropertyIdType>);

    struct PropertyInfo
    {
        template<typename MethodT>
        using ValueType = std::invoke_result_t<MethodT, ObjectType>;

        PropertyIdType id = 0;
        GParamSpec* spec = nullptr;
        std::function<void(ObjectType const&, Glib::ValueBase&)> getter;

        PropertyInfo() = default;

        template<typename MethodT>
        PropertyInfo(PropertyType index, char const* name, char const* nick, char const* blurb, MethodT getter_method)
            : id(static_cast<PropertyIdType>(index))
            , spec(gtr_get_param_spec<ValueType<MethodT>>(name, nick, blurb))
            , getter([getter_method](ObjectType const& object, Glib::ValueBase& value)
                     { static_cast<Glib::Value<ValueType<MethodT>>&>(value).set((object.*getter_method)()); })
        {
        }
    };

    static inline auto const PropertyCount = static_cast<PropertyIdType>(PropertyType::N_PROPS);

public:
    static DynamicPropertyStore& get() noexcept
    {
        static auto instance = DynamicPropertyStore();
        return instance;
    }

    void install(GObjectClass* cls, std::initializer_list<PropertyInfo> properties)
    {
        cls->get_property = &DynamicPropertyStore::get_property_vfunc;

        g_assert(properties_.size() == properties.size() + 1);
        std::move(properties.begin(), properties.end(), properties_.begin() + 1);

        for (auto id = PropertyIdType{ 1 }; id < PropertyCount; ++id)
        {
            g_assert(id == properties_[id].id);
            g_object_class_install_property(cls, id, properties_[id].spec);
        }
    }

    void get_value(ObjectType const& object, PropertyType index, Glib::ValueBase& value) const
    {
        get_property(index).getter(object, value);
    }

    void notify_changed(ObjectType& object, PropertyType index) const
    {
        g_object_notify_by_pspec(object.gobj(), get_property(index).spec);
    }

private:
    PropertyInfo const& get_property(PropertyType index) const noexcept
    {
        auto const id = static_cast<PropertyIdType>(index);
        g_assert(id > 0);
        g_assert(id < PropertyCount);
        return properties_[id];
    }

    static void get_property_vfunc(GObject* object, PropertyIdType id, GValue* value, GParamSpec* /*param_spec*/)
    {
        if (id <= 0 || id >= PropertyCount)
        {
            return;
        }

        if (auto const* const typed_object = dynamic_cast<ObjectType const*>(Glib::wrap_auto(object)); typed_object != nullptr)
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            get().get_value(*typed_object, PropertyType{ id }, *reinterpret_cast<Glib::ValueBase*>(value));
        }
    }

private:
    std::array<PropertyInfo, PropertyCount> properties_ = {};
};
