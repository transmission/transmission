// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <cstdint>

#include "libtransmission/net.h"

inline tr_socket_address make_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    auto addr = tr_address{};
    addr.type = TR_AF_INET;
    addr.addr.addr4.s_addr = htonl(
        (static_cast<uint32_t>(a) << 24) | (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(c) << 8) |
        static_cast<uint32_t>(d));
    return tr_socket_address{ addr, tr_port::from_host(port) };
}
