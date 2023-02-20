// This file Copyright © 2022-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include "timer.h"

extern "C"
{
    struct event_base;
}

namespace libtransmission
{

class EvTimerMaker final : public TimerMaker
{
public:
    explicit EvTimerMaker(event_base* base) noexcept
        : event_base_{ base }
    {
    }

    [[nodiscard]] std::unique_ptr<Timer> create() override;

private:
    event_base* const event_base_;
};

} // namespace libtransmission
