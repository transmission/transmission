// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <variant>

#include "libtransmission/settings.h"
#include "libtransmission/variant.h"

namespace libtransmission
{
namespace
{
struct LoadVisitor
{
    explicit constexpr LoadVisitor(tr_variant const& src)
        : src_{ src }
    {
    }

    template<typename T>
    void operator()(T* const tgt)
    {
        if (auto val = VariantConverter::load<T>(src_))
        {
            *tgt = *val;
        }
    }

private:
    tr_variant const& src_;
};

struct SaveVisitor
{
    constexpr SaveVisitor(tr_variant::Map& tgt, tr_quark key)
        : tgt_{ tgt }
        , key_{ key }
    {
    }

    template<typename T>
    void operator()(T const* const src)
    {
        tgt_.try_emplace(key_, VariantConverter::save<T>(*src));
    }

private:
    tr_variant::Map& tgt_;
    tr_quark key_;
};
} // unnamed namespace

void Settings::load(tr_variant const& src)
{
    auto const* map = src.get_if<tr_variant::Map>();
    if (map == nullptr)
    {
        return;
    }

    for (auto& [key, prop_vptr] : fields())
    {
        if (auto const iter = map->find(key); iter != std::end(*map))
        {
            std::visit(LoadVisitor{ iter->second }, prop_vptr);
        }
    }
}

tr_variant Settings::save() const
{
    auto const fields = const_cast<Settings*>(this)->fields();

    auto map = tr_variant::Map{};
    map.reserve(std::size(fields));

    for (auto const& [key, prop_vptr] : fields)
    {
        std::visit(SaveVisitor{ map, key }, prop_vptr);
    }

    return map;
}
} // namespace libtransmission
