// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint> // uint8_t

#include "libtransmission/quark.h"

struct tr_variant;

namespace libtransmission::api_compat
{
enum class Style : uint8_t
{
    Tr4, // bespoke RPC, mixed-case keys,
    Tr5, // jsonrpc, all snake_case keys
};

[[nodiscard]] Style get_export_settings_style();

void convert(tr_variant& var, Style tgt_style);
void convert_incoming_data(tr_variant& var);
void convert_outgoing_data(tr_variant& var);

} // namespace libtransmission::api_compat
