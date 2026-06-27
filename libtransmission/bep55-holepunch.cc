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

bool encode(
    tr::BufferWriter<std::byte>& payload,
    MsgType const msg_type,
    tr_socket_address const& addr,
    ErrorCode const err_code)
{
    if (!is_valid_msg_type(msg_type))
    {
        return false;
    }

    if (!addr.is_valid())
    {
        return false;
    }

    payload.add_uint8(msg_type);
    payload.add_uint8(addr.address().is_ipv4() ? AddrIPv4 : AddrIPv6);

    auto const [paddr, addr_len] = payload.reserve_space(addr.address().is_ipv4() ? Ipv4CompactSize : Ipv6CompactSize);
    addr.to_compact(paddr);
    payload.commit_space(addr_len);

    payload.add_uint32(err_code);

    return true;
}

} // namespace bep55
