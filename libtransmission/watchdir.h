// This file Copyright 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <functional>
#include <memory>
#include <string_view>

#include "timer.h"

extern "C"
{
    struct event_base;
}

class tr_watchdir
{
public:
    tr_watchdir() = default;
    virtual ~tr_watchdir() = default;
    tr_watchdir(tr_watchdir&&) = delete;
    tr_watchdir(tr_watchdir const&) = delete;
    tr_watchdir& operator=(tr_watchdir&&) = delete;
    tr_watchdir& operator=(tr_watchdir const&) = delete;

    [[nodiscard]] virtual std::string_view dirname() const noexcept = 0;

    enum class Action
    {
        Done,
        Retry
    };

    using Callback = std::function<Action(std::string_view dirname, std::string_view basename)>;

    [[nodiscard]] static auto genericRescanInterval() noexcept
    {
        return generic_rescan_interval_;
    }

    static void setGenericRescanInterval(std::chrono::milliseconds interval) noexcept
    {
        generic_rescan_interval_ = interval;
    }

    static std::unique_ptr<tr_watchdir> create(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        struct event_base* evbase);

    static std::unique_ptr<tr_watchdir> createGeneric(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval = generic_rescan_interval_);

private:
    static constexpr std::chrono::milliseconds DefaultGenericRescanInterval{ 1000 };
    static std::chrono::milliseconds generic_rescan_interval_;
};
