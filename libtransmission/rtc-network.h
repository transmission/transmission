// This file is part of Transmission.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef __TRANSMISSION__
#error only libtransmission should #include this header.
#endif

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wextra-semi"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <rtc/description.hpp>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "libtransmission/error.h"
#include "libtransmission/net.h"

namespace tr_rtc_network
{
[[nodiscard]] inline tr_error bind_address(
    std::string_view ipv4,
    std::string_view ipv6,
    std::size_t peer_sequence,
    std::optional<std::string>& selected)
{
    selected.reset();

    auto const parse = [](std::string_view value, bool want_ipv4, std::optional<std::string>& address) -> tr_error
    {
        if (value.empty())
        {
            return {};
        }

        auto const parsed = tr_address::from_string(value);
        if (!parsed || parsed->is_ipv4() != want_ipv4)
        {
            return { EINVAL, want_ipv4 ? "Invalid WebRTC IPv4 bind address" : "Invalid WebRTC IPv6 bind address" };
        }
        if (!parsed->is_any())
        {
            address = parsed->display_name();
        }
        return {};
    };

    auto v4 = std::optional<std::string>{};
    if (auto error = parse(ipv4, true, v4); error)
    {
        return error;
    }

    auto v6 = std::optional<std::string>{};
    if (auto error = parse(ipv6, false, v6); error)
    {
        return error;
    }

    if (v4 && v6)
    {
        selected = peer_sequence % 2U == 1U ? std::move(v4) : std::move(v6);
    }
    else
    {
        selected = v4 ? std::move(v4) : std::move(v6);
    }
    return {};
}

template<typename IsBlocked>
[[nodiscard]] std::size_t filter_blocklisted_candidates(rtc::Description& description, IsBlocked const& is_blocked)
{
    auto const ended = description.ended();
    auto candidates = description.extractCandidates();
    auto const original_size = candidates.size();
    std::erase_if(
        candidates,
        [&](rtc::Candidate const& candidate)
        {
            // Parsed SDP candidates may not be resolved yet; inspect the numeric address directly.
            auto const raw = candidate.candidate();
            auto offset = std::size_t{ 0 };
            for (int field = 0; field < 4; ++field)
            {
                offset = raw.find(' ', offset);
                if (offset == std::string::npos)
                {
                    return false;
                }
                ++offset;
            }
            auto const end = raw.find(' ', offset);
            auto const address = tr_address::from_string(std::string_view{ raw }.substr(offset, end - offset));
            return address && std::invoke(is_blocked, *address);
        });
    auto const removed = original_size - candidates.size();
    description.addCandidates(std::move(candidates));
    if (ended)
    {
        description.endCandidates();
    }
    return removed;
}
} // namespace tr_rtc_network
