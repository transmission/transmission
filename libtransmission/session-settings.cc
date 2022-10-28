// This file Copyright © 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <fmt/format.h>

#include "transmission.h"

#include "session-settings.h"
#include "variant-converters.h"
#include "variant.h"

namespace libtransmission
{

void SessionSettings::load(tr_variant* src)
{
#define V(key, field, type, default_value) \
    if (auto* const child = tr_variantDictFind(src, key); child != nullptr) \
    { \
        if (auto val = VariantConverter<decltype(field)>::load(child); val) \
        { \
            this->field = *val; \
        } \
    }
    SESSION_SETTINGS_FIELDS(V);
#undef V
}

void SessionSettings::save(tr_variant* tgt) const
{
#define V(key, field, type, default_value) \
    tr_variantDictRemove(tgt, key); \
    VariantConverter<decltype(field)>::save(tr_variantDictAdd(tgt, key), field);
    SESSION_SETTINGS_FIELDS(V)
#undef V
}

} // namespace libtransmission
