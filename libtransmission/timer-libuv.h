// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <memory>

#include "libtransmission/timer.h"

extern "C"
{
    struct uv_loop_s;
}

namespace libtransmission
{

class UvTimerMaker final : public TimerMaker
{
public:
    explicit UvTimerMaker(uv_loop_s* loop) noexcept
        : uv_loop_{ loop }
    {
    }

    [[nodiscard]] std::unique_ptr<Timer> create() override;

private:
    uv_loop_s* const uv_loop_;
};

} // namespace libtransmission
