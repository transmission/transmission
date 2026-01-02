// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <optional>
#include <string_view>

#include "libtransmission/quark.h"

using namespace transmission::symbol;

std::optional<tr_quark> tr_quark_lookup(std::string_view const str)
{
    return StringInterner::instance().get(str);
}

tr_quark tr_quark_new(std::string_view const str)
{
    return StringInterner::instance().get_or_intern(str);
}

std::string_view tr_quark_get_string_view(tr_quark const quark)
{
    return quark.sv();
}
