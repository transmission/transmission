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
    void add_type_handler(Load<T> load, Save<T> save)
    {
        auto const key = std::type_index(typeid(T*));

        // wrap load + save with void* wrappers so that
        // they can be stored in the save_ and load_ maps

        load_[key] = [load](tr_variant const& src, void* tgt)
        {
            return load(src, static_cast<T*>(tgt));
        };

        save_[key] = [save](void const* src)
        {
            return save(*static_cast<T const*>(src));
        };
    }

    class Field
    {
    public:
        template<typename T>
        Field(tr_quark key_in, T* ptr_in)
            : key{ key_in }
            , type{ typeid(T*) }
            , ptr{ ptr_in }
        {
        }

    private:
        friend Settings;

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
