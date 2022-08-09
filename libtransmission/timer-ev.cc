// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <memory>

#include <event2/event.h>

#include "tr-assert.h"
#include "timer-ev.h"

using namespace std::literals;

namespace libtransmission
{

class EvTimer final : public Timer
{
public:
    EvTimer(struct event_base* base)
        : base_{ base }
    {
        setRepeating(is_repeating_);
    }

    EvTimer(EvTimer&&) = delete;
    EvTimer(EvTimer const&) = delete;
    EvTimer& operator=(EvTimer&&) = delete;
    EvTimer& operator=(EvTimer const&) = delete;

    ~EvTimer() override
    {
        stop();
        event_free(evtimer_);
    }

    void stop() override
    {
        evtimer_del(evtimer_);
    }

    void start() override
    {
        restart();
    }

    void setCallback(std::function<void()> callback) override
    {
        callback_ = std::move(callback);
    }

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
    {
        return interval_;
    }

    void setInterval(std::chrono::milliseconds interval) override
    {
        TR_ASSERT_MSG(interval.count() > 0 || !is_repeating_, "repeating timers must have a postiive interval");

        interval_ = interval;

        // if evtimer_ is already running, update its interval
        if (auto const is_pending = event_pending(evtimer_, EV_TIMEOUT, nullptr); is_pending != 0)
        {
            restart();
        }
    }

    [[nodiscard]] bool isRepeating() const noexcept override
    {
        return is_repeating_;
    }

    void setRepeating(bool repeating) override
    {
        is_repeating_ = repeating;

        if (evtimer_ != nullptr)
        {
            event_del(evtimer_);
            event_free(evtimer_);
        }

        evtimer_ = repeating ? event_new(base_, -1, EV_TIMEOUT | EV_PERSIST, onTimer, this) :
                               event_new(base_, -1, EV_TIMEOUT, onTimer, this);
    }

private:
    void restart()
    {
        stop();

        using namespace std::chrono;
        auto const secs = duration_cast<seconds>(interval_);
        auto tv = timeval{};
        tv.tv_sec = secs.count();
        tv.tv_usec = duration_cast<microseconds>(interval_ - secs).count();
        evtimer_add(evtimer_, &tv);
    }

    static void onTimer(evutil_socket_t /*unused*/, short /*unused*/, void* vself)
    {
        static_cast<EvTimer*>(vself)->handleTimer();
    }

    void handleTimer()
    {
        TR_ASSERT(callback_);
        callback_();
    }

    struct event_base* const base_;
    struct event* evtimer_ = nullptr;

    std::function<void()> callback_;
    std::chrono::milliseconds interval_ = 100ms;
    bool is_repeating_ = true;
};

std::unique_ptr<Timer> EvTimerMaker::create()
{
    return std::make_unique<EvTimer>(event_base_);
}

} // namespace libtransmission
