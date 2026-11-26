// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

// Selected ICE endpoints are actual transport peer addresses, not TCP dial targets.
// Both libjuice and libnice return address:port (IPv6 may omit square brackets).
#pragma once

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>
#include <system_error>

#include "libtransmission/net.h"

namespace tr_rtc_endpoint
{
[[nodiscard]] inline std::optional<tr_socket_address> parse(std::string_view selected)
{
    auto const separator = selected.rfind(':');
    if (separator == std::string_view::npos)
    {
        return {};
    }

    auto host = selected.substr(0, separator);
    if (host.size() >= 2U && host.front() == '[' && host.back() == ']')
    {
        host.remove_prefix(1U);
        host.remove_suffix(1U);
    }

    auto const ip = tr_address::from_string(host);
    if (!ip || ip->is_any() || ip->is_ipv4_multicast() || ip->is_ipv6_multicast())
    {
        return {};
    }

    auto const port_string = selected.substr(separator + 1U);
    auto port = unsigned{ 0 };
    auto const [end, error] = std::from_chars(port_string.data(), port_string.data() + port_string.size(), port);
    if (error != std::errc{} || end != port_string.data() + port_string.size() || port == 0U || port > UINT16_MAX)
    {
        return {};
    }

    return tr_socket_address{ *ip, tr_port::from_host(static_cast<uint16_t>(port)) };
}
} // namespace tr_rtc_endpoint
