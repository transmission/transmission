// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>

#include "libtransmission/net.h"

inline tr_socket_address makeIpv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    auto const buf = std::to_array({ a, b, c, d, static_cast<uint8_t>(port >> 8), static_cast<uint8_t>(port) });
    return tr_socket_address::from_compact_ipv4(reinterpret_cast<std::byte const*>(buf.data())).first;
}
