// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <string_view>

#include "libtransmission/string-utils.h"
#include "libtransmission/variant.h"

namespace tr_webtorrent_ice_servers
{
[[nodiscard]] inline tr_variant::Vector parse(std::string_view value)
{
    auto result = tr_variant::Vector{};
    auto server = std::string_view{};
    while (tr_strv_sep(&value, &server, ','))
    {
        server = tr_strv_strip(server);
        if (!server.empty())
        {
            result.emplace_back(server);
        }
    }
    return result;
}
} // namespace tr_webtorrent_ice_servers
