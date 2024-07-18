// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

#include "libtransmission/quark.h"
#include "libtransmission/variant.h"

namespace libtransmission
{

class Settings
{
public:
    virtual ~Settings() = default;
    Settings(Settings const& settings) = default;
    Settings& operator=(Settings const& other) = default;
    Settings(Settings&& settings) noexcept = default;
    Settings& operator=(Settings&& other) noexcept = default;

    void load(tr_variant const& src);

    [[nodiscard]] tr_variant save() const;

protected:
    Settings();

    // convert from tr_variant to T
    template<typename T>
    using Load = bool (*)(tr_variant const& src, T* tgt);

    // convert from T to tr_variant
    template<typename T>
    using Save = tr_variant (*)(T const& src);

    template<typename T>
    void add_type_handler(Load<T> load_handler, Save<T> save_handler)
    {
        auto const key = std::type_index(typeid(T*));

        // wrap load_handler + save_handler with void* wrappers so that
        // they can be stored in the save_ and load_ maps
        load_.insert_or_assign(
            key,
            [load_handler](tr_variant const& src, void* tgt) { return load_handler(src, static_cast<T*>(tgt)); });
        save_.insert_or_assign(key, [save_handler](void const* src) { return save_handler(*static_cast<T const*>(src)); });
    }

    struct Field
    {
        template<typename T>
        Field(tr_quark key_in, T* ptr_in)
            : key{ key_in }
            , type{ typeid(T*) }
            , ptr{ ptr_in }
        {
        }

        tr_quark key;
        std::type_info const& type;
        void* ptr;
    };

    using Fields = std::vector<Field>;

    [[nodiscard]] virtual Fields fields() = 0;

private:
    std::unordered_map<std::type_index, std::function<tr_variant(void const* src)>> save_;
    std::unordered_map<std::type_index, std::function<bool(tr_variant const& src, void* tgt)>> load_;
};
} // namespace libtransmission
