// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/bep55-holepunch.h"
#include "libtransmission/net.h"
#include "libtransmission/tr-buffer.h"

namespace bep55
{
namespace
{

// Sum of non-address fixed fields: msg_type(1) + addr_type(1) + port(2) = 4.
// Note: port sits after addr on the wire; this constant is a size convenience, not a layout prefix.
auto constexpr HeaderSize = size_t{ 4 };

[[nodiscard]] constexpr bool is_valid_msg_type(uint8_t const msg_type) noexcept
{
    return msg_type == MsgRendezvous || msg_type == MsgConnect || msg_type == MsgError;
}

[[nodiscard]] std::optional<tr_socket_address> decode_sockaddr(tr::BufferReader<std::byte>& payload)
{
    switch (payload.to_uint8())
    {
    case AddrIPv4:
        if (payload.size() >= Ipv4CompactSize)
        {
            auto&& addr = tr_socket_address::from_compact_ipv4(payload.data()).first;
            payload.drain(Ipv4CompactSize);
            return addr;
        }
        return std::nullopt;

    case AddrIPv6:
        if (payload.size() >= Ipv6CompactSize)
        {
            auto&& addr = tr_socket_address::from_compact_ipv6(payload.data()).first;
            payload.drain(Ipv6CompactSize);
            return addr;
        }
        return std::nullopt;

    default:
        return std::nullopt;
    }
}
} // namespace

std::optional<HolepunchMessage> decode(tr::BufferReader<std::byte>& payload) noexcept
{
    if (payload.size() < PayloadMinIPv4)
    {
        return std::nullopt;
    }

    HolepunchMessage msg{};
    msg.msg_type = payload.to_uint8();

    if (!is_valid_msg_type(msg.msg_type))
    {
        return std::nullopt;
    }

    if (auto sockaddr = decode_sockaddr(payload))
    {
        msg.socket_address = std::move(*sockaddr);
    }
    else
    {
        return std::nullopt;
    }

    if (msg.msg_type == MsgError)
    {
        if (payload.size() < sizeof(ErrorCode))
        {
            return std::nullopt;
        }

        msg.err_code = payload.to_uint32();
    }
    // rendezvous / connect: accept trailing 4-byte zero error code
    // (anacrolix compat); reject trailing non-zero error code.
    else if (payload.size() == sizeof(ErrorCode))
    {
        if (payload.to_uint32() != 0)
        {
            return std::nullopt;
        }
    }
    // invalid payload size
    else if (!payload.empty())
    {
        return std::nullopt;
    }

    return msg;
}

std::string encode(uint8_t msg_type, tr_socket_address const& addr, uint32_t err_code)
{
    auto const& address = addr.address();
    auto const port = addr.port();
    auto const is_v4 = address.is_ipv4();

    auto const addr_len = is_v4 ? size_t{ 4 } : size_t{ 16 };
    auto const total = HeaderSize + addr_len + 4; // HeaderSize(4) + addr + err_code(4)

    auto buf = std::string(total, '\0');
    auto* out = reinterpret_cast<std::byte*>(buf.data());

    out[0] = static_cast<std::byte>(msg_type);
    out[1] = static_cast<std::byte>(is_v4 ? AddrIPv4 : AddrIPv6);

    if (is_v4)
    {
        std::memcpy(out + 2, &address.addr.addr4.s_addr, 4);
    }
    else
    {
        std::memcpy(out + 2, &address.addr.addr6.s6_addr, 16);
    }

    auto const nport = port.network();
    std::memcpy(out + 2 + addr_len, &nport, sizeof(nport));

    auto const nerr_code = htonl(err_code);
    std::memcpy(out + 2 + addr_len + 2, &nerr_code, sizeof(nerr_code));

    return buf;
}

} // namespace bep55
