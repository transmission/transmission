// This file Copyright © 2015-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#ifndef LIBTRANSMISSION_WATCHDIR_MODULE
#error only the wathcdir module should #include this header.
#endif

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef> // for size_t
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "timer.h"
#include "watchdir.h"

namespace libtransmission::impl
{
// base class for concrete tr_watchdirs
class BaseWatchdir : public Watchdir
{
public:
    BaseWatchdir(std::string_view dirname, Callback callback, TimerMaker& timer_maker)
        : dirname_{ dirname }
        , callback_{ std::move(callback) }
        , retry_timer_{ timer_maker.create() }
    {
        retry_timer_->setCallback([this]() { onRetryTimer(); });
    }

    BaseWatchdir(BaseWatchdir&&) = delete;
    BaseWatchdir(BaseWatchdir const&) = delete;
    BaseWatchdir& operator=(BaseWatchdir&&) = delete;
    BaseWatchdir& operator=(BaseWatchdir const&) = delete;

    [[nodiscard]] std::string_view dirname() const noexcept override
    {
        return dirname_;
    }

    [[nodiscard]] constexpr auto timeoutDuration() const noexcept
    {
        return timeout_duration_;
    }

    constexpr void setTimeoutDuration(std::chrono::seconds timeout_duration) noexcept
    {
        timeout_duration_ = timeout_duration;
    }

    [[nodiscard]] constexpr auto retryDuration() const noexcept
    {
        return retry_duration_;
    }

    void setRetryDuration(std::chrono::milliseconds retry_duration) noexcept
    {
        retry_duration_ = retry_duration;

        for (auto& [basename, info] : pending_)
        {
            setNextKickTime(info);
        }
    }

protected:
    void scan();
    void processFile(std::string_view basename);

private:
    using Timestamp = std::chrono::time_point<std::chrono::steady_clock>;

    struct Pending
    {
        size_t strikes = 0U;
        Timestamp first_kick_at = {};
        Timestamp last_kick_at = {};
        Timestamp next_kick_at = {};
    };

    void setNextKickTime(Pending& item)
    {
        item.next_kick_at = item.last_kick_at + retry_duration_;
    }

    [[nodiscard]] auto nextKickTime() const
    {
        auto next_time = std::optional<Timestamp>{};

        for (auto const& [name, info] : pending_)
        {
            if (!next_time || info.next_kick_at < *next_time)
            {
                next_time = info.next_kick_at;
            }
        }

        return next_time;
    }

    void restartTimerIfPending()
    {
        if (auto next_kick_time = nextKickTime(); next_kick_time)
        {
            using namespace std::chrono;
            auto const now = steady_clock::now();
            auto duration = duration_cast<milliseconds>(*next_kick_time - now);
            retry_timer_->startSingleShot(duration);
        }
    }

    void onRetryTimer()
    {
        using namespace std::chrono;
        auto const now = steady_clock::now();

        auto tmp = decltype(pending_){};
        std::swap(tmp, pending_);
        for (auto const& [basename, info] : tmp)
        {
            if (info.next_kick_at <= now)
            {
                processFile(basename);
            }
            else
            {
                pending_.try_emplace(basename, info);
            }
        }

        restartTimerIfPending();
    }

    std::string const dirname_;
    Callback const callback_;
    std::unique_ptr<Timer> const retry_timer_;

    std::map<std::string, Pending, std::less<>> pending_;
    std::set<std::string, std::less<>> handled_;
    std::chrono::milliseconds retry_duration_ = std::chrono::seconds{ 5 };
    std::chrono::seconds timeout_duration_ = std::chrono::seconds{ 15 };
};

} // namespace libtransmission::impl
