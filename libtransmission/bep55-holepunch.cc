// This file Copyright © Transmission authors and contributors.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "libtransmission/bep55-holepunch.h"

#include <cstring>

namespace bep55
{

std::optional<HolepunchMessage> decode(std::string_view payload) noexcept
{
    auto const len = payload.size();
    auto const* data = reinterpret_cast<std::byte const*>(payload.data());

    if (len < PayloadMinIPv4)
    {
        return std::nullopt;
    }

    HolepunchMessage msg{};
    msg.msg_type = static_cast<uint8_t>(data[0]);
    auto const addr_type = static_cast<uint8_t>(data[1]);

    if (msg.msg_type != MsgRendezvous && msg.msg_type != MsgConnect && msg.msg_type != MsgError)
    {
        return std::nullopt;
    }

    if (addr_type != AddrIPv4 && addr_type != AddrIPv6)
    {
        return std::nullopt;
    }

    auto const addr_len = (addr_type == AddrIPv4) ? size_t{ 4 } : size_t{ 16 };
    auto const min_len = HeaderSize + addr_len; // without err_code
    auto const full_len = min_len + 4; // with err_code

    if (len < min_len)
    {
        return std::nullopt;
    }

    auto addr = tr_address{};

    if (addr_type == AddrIPv4)
    {
        addr.type = TR_AF_INET;
        std::memcpy(&addr.addr.addr4.s_addr, data + 2, 4);
    }
    else
    {
        addr.type = TR_AF_INET6;
        std::memcpy(&addr.addr.addr6.s6_addr, data + 2, 16);
    }

    uint16_t nport{};
    std::memcpy(&nport, data + 2 + addr_len, sizeof(nport));
    auto const port = tr_port::from_network(nport);

    msg.socket_address = tr_socket_address{ addr, port };

    if (msg.msg_type == MsgError)
    {
        if (len != full_len)
        {
            return std::nullopt;
        }

        uint32_t nerr_code{};
        std::memcpy(&nerr_code, data + min_len, sizeof(nerr_code));
        msg.err_code = ntohl(nerr_code);
    }
    else
    {
        // rendezvous / connect: accept trailing 4-byte zero error code
        // (anacrolix compat); reject trailing non-zero error code.
        if (len == full_len)
        {
            uint32_t nerr_code{};
            std::memcpy(&nerr_code, data + min_len, sizeof(nerr_code));

            if (ntohl(nerr_code) != 0)
            {
                return std::nullopt;
            }
        }
        else if (len != min_len)
        {
            return std::nullopt;
        }
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
