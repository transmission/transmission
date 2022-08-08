// This file Copyright © 2015-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifndef LIBTRANSMISSION_WATCHDIR_MODULE
#error only the libtransmission watchdir module should #include this header.
#endif

#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>

#include <event2/event.h>

#include "watchdir.h"

// base class for concrete tr_watchdirs
class tr_watchdir_base : public tr_watchdir
{
public:
    tr_watchdir_base(std::string_view dirname, Callback callback, event_base* event_base, TimeFunc current_time_func)
        : dirname_{ dirname }
        , callback_{ std::move(callback) }
        , event_base_{ event_base }
        , current_time_func_{ current_time_func }
    {
    }

    virtual ~tr_watchdir_base() override = default;
    tr_watchdir_base(tr_watchdir_base&&) = delete;
    tr_watchdir_base(tr_watchdir_base const&) = delete;
    tr_watchdir_base& operator=(tr_watchdir_base&&) = delete;
    tr_watchdir_base& operator=(tr_watchdir_base const&) = delete;

    [[nodiscard]] std::string_view dirname() const noexcept override
    {
        return dirname_;
    }

    [[nodiscard]] constexpr auto retryLimit() const noexcept
    {
        return retry_limit_;
    }

    constexpr void setRetryLimit(size_t retry_limit) noexcept
    {
        retry_limit_ = retry_limit;
    }

    [[nodiscard]] constexpr auto retryMultiplierMsec() const noexcept
    {
        return retry_multiplier_msec_;
    }

    void setRetryMultiplierSecs(size_t retry_multiplier_msec) noexcept
    {
        retry_multiplier_msec_ = retry_multiplier_msec;

        for (auto& [basename, info] : pending_)
        {
            setNextKickTime(info);
        }
    }

protected:
    void scan();
    void processFile(std::string_view basename);

    [[nodiscard]] constexpr event_base* eventBase() noexcept
    {
        return &*event_base_;
    }

    struct FreeEvent
    {
        void operator()(event* ev)
        {
            event_del(ev);
            event_free(ev);
        }
    };

    using WrappedEvent = std::unique_ptr<struct event, FreeEvent>;

private:
    struct Pending
    {
        size_t strikes = 0U;
        time_t last_kick_at = 0U;
        time_t next_kick_at = 0U;
    };

    void setNextKickTime(Pending& info)
    {
        info.next_kick_at = info.last_kick_at + (static_cast<size_t>(info.strikes * retry_multiplier_msec_)) / 1000U;
    }

    [[nodiscard]] auto nextKickTime() const
    {
        auto next_time = std::optional<time_t>{};

        for (auto const& [name, info] : pending_)
        {
            if (!next_time || info.next_kick_at < *next_time)
            {
                next_time = info.next_kick_at;
            }
        }

        return next_time;
    }

    void updateRetryTimer();

    static void onRetryTimer(evutil_socket_t /*unused*/, short /*unused*/, void* vself)
    {
        static_cast<tr_watchdir_base*>(vself)->retry();
    }
    void retry()
    {
        auto const now = current_time_func_();

        auto tmp = decltype(pending_){};
        std::swap(tmp, pending_);
        for (auto const& [basename, info] : tmp)
        {
            if (info.next_kick_at <= now)
            {
                processFile(basename);
            }
        }

        updateRetryTimer();
    }

    std::string const dirname_;
    Callback const callback_;
    event_base* const event_base_;
    TimeFunc const current_time_func_;
    WrappedEvent const retry_timer_{ evtimer_new(event_base_, onRetryTimer, this) };

    std::map<std::string, Pending, std::less<>> pending_;
    std::set<std::string, std::less<>> handled_;
    size_t retry_limit_ = 3U;
    size_t retry_multiplier_msec_ = 5000U;
};
