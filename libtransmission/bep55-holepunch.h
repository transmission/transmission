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

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "libtransmission/net.h"
#include "libtransmission/tr-buffer.h"

namespace bep55
{
inline auto constexpr Ipv4CompactSize = tr_socket_address::CompactSockAddrBytes[TR_AF_INET];
inline auto constexpr Ipv6CompactSize = tr_socket_address::CompactSockAddrBytes[TR_AF_INET6];

// Message types (BEP 55 §Protocol Extension)
enum MsgType : uint8_t
{
    MsgRendezvous = 0,
    MsgConnect = 1,
    MsgError = 2,
};

// BEP 10 LTEP extension message ID for ut_holepunch.
// ID 2 is free between UT_PEX_ID (1) and UT_METADATA_ID (3).
inline auto constexpr LtepExtensionId = uint8_t{ 2 };

// Address types
enum AddrType : uint8_t
{
    AddrIPv4 = 0,
    AddrIPv6 = 1,
};

// Error codes (4-byte err_code field)
// NOLINTNEXTLINE(performance-enum-size)
enum ErrorCode : uint32_t
{
    ErrNoSuchPeer = 1,
    ErrNotConnected = 2,
    ErrNoSupport = 3,
    ErrNoSelf = 4,
};

// Wire format sizes (non-error messages, no err_code)
inline auto constexpr PayloadMinIPv4 = sizeof(MsgType) + sizeof(AddrType) + Ipv4CompactSize;
inline auto constexpr PayloadMinIPv6 = sizeof(MsgType) + sizeof(AddrType) + Ipv6CompactSize;

// Wire format sizes (all messages with err_code, including zeroed for non-error)
inline auto constexpr PayloadFullIPv4 = PayloadMinIPv4 + sizeof(ErrorCode);
inline auto constexpr PayloadFullIPv6 = PayloadMinIPv6 + sizeof(ErrorCode);

struct HolepunchMessage
{
    uint8_t msg_type = {};
    tr_socket_address socket_address{};
    uint32_t err_code = {};
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

} // namespace bep55
