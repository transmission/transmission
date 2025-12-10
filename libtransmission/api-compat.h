// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/quark.h>

struct tr_variant;

namespace libtransmission::api_compat
{
// Nothing to see here.
// Exposed as public only so tr_variant can see it & declare it a friend.
namespace detail
{
struct CloneState;
[[nodiscard]] tr_variant convert_impl(tr_variant const& self, CloneState& state);
} // namespace detail

enum class Style
{
    // Tr4: mixed-case keys, bespoke RPC
    LegacyRpc,

    // Tr4: mixed-case keys
    LegacySettings,

    // Tr5: snake_case keys everywhere + jsonrpc for RPC
    Current,

    // Use case for exporting to legacy: users can use the
    // same settings.json in 4.0.x and 4.1.x.
    // TODO: when we bump to 5.0.0, change this to `Current`
    DefaultSettingsExportStyle = LegacySettings,
};

[[nodiscard]] tr_variant convert(tr_variant const& src, Style tgt_style);

[[nodiscard]] tr_quark convert(tr_quark src, Style tgt_style);

} // namespace libtransmission::api_compat
