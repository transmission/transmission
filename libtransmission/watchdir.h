// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string_view>

struct event_base;

namespace libtransmission
{

class TimerMaker;

class Watchdir
{
public:
    enum class Action : uint8_t
    {
        Done,
        Retry
    };

    using Callback = std::function<Action(std::string_view dirname, std::string_view basename)>;

    Watchdir() = default;
    virtual ~Watchdir() = default;
    Watchdir(Watchdir&&) = delete;
    Watchdir(Watchdir const&) = delete;
    Watchdir& operator=(Watchdir&&) = delete;
    Watchdir& operator=(Watchdir const&) = delete;

    [[nodiscard]] virtual std::string_view dirname() const noexcept = 0;

    [[nodiscard]] static auto generic_rescan_interval() noexcept
    {
        return generic_rescan_interval_;
    }

    static void set_generic_rescan_interval(std::chrono::milliseconds interval) noexcept
    {
        generic_rescan_interval_ = interval;
    }

    [[nodiscard]] static std::unique_ptr<Watchdir> create(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        struct event_base* evbase);

    [[nodiscard]] static std::unique_ptr<Watchdir> create_generic(
        std::string_view dirname,
        Callback callback,
        libtransmission::TimerMaker& timer_maker,
        std::chrono::milliseconds rescan_interval = generic_rescan_interval_);

private:
    // TODO: Re-enable after setting readability-identifier-naming.PrivateMemberSuffix to _
    // NOLINTNEXTLINE(readability-identifier-naming)
    static inline auto generic_rescan_interval_ = std::chrono::milliseconds{ 1000 };
};

} // namespace libtransmission
