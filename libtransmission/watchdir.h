// This file Copyright 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

extern "C"
{
    struct event_base;
}

namespace libtransmission
{

class TimerMaker;

class Watchdir
{
public:
    Watchdir() = default;
    virtual ~Watchdir() = default;
    Watchdir(Watchdir&&) = delete;
    Watchdir(Watchdir const&) = delete;
    Watchdir& operator=(Watchdir&&) = delete;
    Watchdir& operator=(Watchdir const&) = delete;

    [[nodiscard]] virtual std::string_view dirname() const noexcept = 0;

    enum class Action
    {
        Done,
        Retry
    };

    using Callback = std::function<Action(std::string_view dirname, std::string_view basename)>;

    [[nodiscard]] static auto genericRescanInterval() noexcept
    {
        return generic_rescan_interval;
    }

    static void setGenericRescanInterval(std::chrono::milliseconds interval) noexcept
    {
        generic_rescan_interval = interval;
    }

    [[nodiscard]] static std::unique_ptr<Watchdir> create(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        struct event_base* evbase);

    [[nodiscard]] static std::unique_ptr<Watchdir> createGeneric(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval = generic_rescan_interval);

private:
    static inline auto generic_rescan_interval = std::chrono::milliseconds{ 1000 };
};

} // namespace libtransmission
