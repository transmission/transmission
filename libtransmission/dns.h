// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <string_view>

#include "transmission.h"

#include "net.h"

namespace libtransmission
{

class Dns
{
public:
    virtual ~Dns() = default;

    using Callback = std::function<void(struct sockaddr const*, socklen_t salen, time_t expires_at)>;
    using Tag = unsigned int;

    class Hints
    {
    public:
        Hints()
        {
        }

        int ai_family = AF_UNSPEC;
        int ai_socktype = SOCK_DGRAM;
        int ai_protocol = IPPROTO_UDP;

        [[nodiscard]] constexpr int compare(Hints const& that) const noexcept // <=>
        {
            if (ai_family != that.ai_family)
            {
                return ai_family < that.ai_family ? -1 : 1;
            }

            if (ai_socktype != that.ai_socktype)
            {
                return ai_socktype < that.ai_socktype ? -1 : 1;
            }

            if (ai_protocol != that.ai_protocol)
            {
                return ai_protocol < that.ai_protocol ? -1 : 1;
            }

            return 0;
        }

        [[nodiscard]] constexpr bool operator<(Hints const& that) const noexcept
        {
            return compare(that) < 0;
        }
    };

    [[nodiscard]] virtual std::optional<std::pair<struct sockaddr const*, socklen_t>> cached(
        std::string_view address,
        Hints hints = {}) const = 0;

    virtual Tag lookup(std::string_view address, Callback&& callback, Hints hints = {}) = 0;

    virtual void cancel(Tag) = 0;
};

} // namespace libtransmission
