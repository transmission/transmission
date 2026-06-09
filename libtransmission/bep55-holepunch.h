// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// BEP 55: Holepunch extension
// https://www.bittorrent.org/beps/bep_0055.html
//
// Provides encode/decode helpers for ut_holepunch messages.
// The encoder always emits the 12/24-byte form (with trailing err_code zeroed
// for non-error messages), which is the only encoding interoperable with both
// libtorrent and anacrolix. The decoder accepts both strict BEP-55 (8/20 bytes
// for non-error) and anacrolix (12/24 bytes always) forms.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

#include "libtransmission/net.h"
#include "libtransmission/tr-assert.h"

namespace bep55
{

// Message types (BEP 55 §Protocol Extension)
auto constexpr MsgRendezvous = uint8_t{ 0 };
auto constexpr MsgConnect = uint8_t{ 1 };
auto constexpr MsgError = uint8_t{ 2 };

// BEP 10 LTEP extension message ID for ut_holepunch.
// ID 2 is free between UT_PEX_ID (1) and UT_METADATA_ID (3).
auto constexpr LtepExtensionId = uint8_t{ 2 };

// Address types
auto constexpr AddrIPv4 = uint8_t{ 0 };
auto constexpr AddrIPv6 = uint8_t{ 1 };

// Error codes (4-byte err_code field)
auto constexpr ErrNoSuchPeer = uint32_t{ 1 };
auto constexpr ErrNotConnected = uint32_t{ 2 };
auto constexpr ErrNoSupport = uint32_t{ 3 };
auto constexpr ErrNoSelf = uint32_t{ 4 };

// Wire format sizes (non-error messages, no err_code)
auto constexpr PayloadMinIPv4 = size_t{ 8 }; // msg_type(1) + addr_type(1) + addr(4) + port(2)
auto constexpr PayloadMinIPv6 = size_t{ 20 }; // msg_type(1) + addr_type(1) + addr(16) + port(2)

// Wire format sizes (all messages with err_code, including zeroed for non-error)
auto constexpr PayloadFullIPv4 = size_t{ 12 }; // PayloadMinIPv4 + err_code(4)
auto constexpr PayloadFullIPv6 = size_t{ 24 }; // PayloadMinIPv6 + err_code(4)

// Sum of non-address fixed fields: msg_type(1) + addr_type(1) + port(2) = 4.
// Note: port sits after addr on the wire; this constant is a size convenience, not a layout prefix.
auto constexpr HeaderSize = size_t{ 4 };

struct HolepunchMessage
{
    uint8_t msg_type = 0;
    tr_socket_address socket_address{};
    uint32_t err_code = 0;
};

// Decode a BEP 55 ut_holepunch payload.
//
// Accepts both strict BEP-55 lengths (8/20 for rendezvous/connect, 12/24 for
// error) and anacrolix lengths (12/24 always with err_code).
//
// For rendezvous/connect: a trailing 4-byte zero error code is accepted;
// a trailing non-zero error code is rejected.
// For error: the 4-byte error code is required.
[[nodiscard]] std::optional<HolepunchMessage> decode(std::string_view payload) noexcept;

// Encode a BEP 55 ut_holepunch payload.
//
// Always emits the 12/24-byte form (with trailing err_code zeroed for
// non-error messages). This is the only single encoding accepted by all
// three major implementations (libtorrent, anacrolix, Transmission).
//
// Wire layout:
//   msg_type  (1 byte)
//   addr_type (1 byte)
//   addr      (4 bytes for IPv4, 16 bytes for IPv6)
//   port      (2 bytes, network byte order)
//   err_code  (4 bytes, network byte order, zero for non-error)
[[nodiscard]] std::string encode(uint8_t msg_type, tr_socket_address const& addr, uint32_t err_code = 0);

[[nodiscard]] inline std::string encode(uint8_t msg_type, tr_address const& addr, tr_port port, uint32_t err_code = 0)
{
    return encode(msg_type, tr_socket_address{ addr, port }, err_code);
}

} // namespace bep55
