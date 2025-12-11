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
namespace detail
{
// This is only public so that tr_variant can see it & declare it as a friend.
struct CloneState;
[[nodiscard]] tr_variant convert_impl(tr_variant const& self, CloneState& state);
} // namespace detail

enum class Style : uint8_t
{
    Tr4, // bespoke RPC, mixed-case keys,
    Tr5, // jsonrpc, all snake_case keys
};

[[nodiscard]] tr_variant convert(tr_variant const& src, Style tgt_style);

[[nodiscard]] Style get_export_settings_style();

[[nodiscard]] tr_variant convert_incoming_data(tr_variant const& src);
[[nodiscard]] tr_variant convert_outgoing_data(tr_variant const& src);

} // namespace libtransmission::api_compat

/**
 * Get the replacement quark from old deprecated quarks.
 *
 * Note: Temporary shim just for the transition period to snake_case.
 */
[[nodiscard]] tr_quark tr_quark_convert(tr_quark quark);
