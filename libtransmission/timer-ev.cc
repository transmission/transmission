// This file Copyright 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <memory>
#include <utility>

#include <event2/event.h>

#include "tr-assert.h"
#include "timer-ev.h"

using namespace std::literals;

namespace
{

struct EventDeleter
{
    void operator()(struct event* event)
    {
        if (event != nullptr)
        {
            event_del(event);
            event_free(event);
        }
    }
};

} // namespace

namespace libtransmission
{

class EvTimer final : public Timer
{
public:
    explicit EvTimer(struct event_base* base)
        : base_{ base }
    {
        setRepeating(is_repeating_);
    }

    EvTimer(EvTimer&&) = delete;
    EvTimer(EvTimer const&) = delete;
    EvTimer& operator=(EvTimer&&) = delete;
    EvTimer& operator=(EvTimer const&) = delete;

    ~EvTimer() override = default;

    void stop() override
    {
        evtimer_.reset();
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
        TR_ASSERT_MSG(interval.count() > 0 || !is_repeating_, "repeating timers must have a positive interval");

        interval_ = interval;

        if (evtimer_) // update the timer if it's already running
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
        evtimer_.reset();
    }

private:
    void restart()
    {
        evtimer_.reset(event_new(base_, -1, EV_TIMEOUT | (isRepeating() ? EV_PERSIST : 0), onTimer, this));

        using namespace std::chrono;
        auto const secs = duration_cast<seconds>(interval_);
        auto tv = timeval{};
        tv.tv_sec = secs.count();
        tv.tv_usec = static_cast<decltype(tv.tv_usec)>(duration_cast<microseconds>(interval_ - secs).count());
        evtimer_add(evtimer_.get(), &tv);
    }

    static void onTimer(evutil_socket_t /*unused*/, short /*unused*/, void* vself)
    {
        static_cast<EvTimer*>(vself)->handleTimer();
    }

    void handleTimer() const
    {
        TR_ASSERT(callback_);
        callback_();
    }

    struct event_base* const base_;
    std::unique_ptr<struct event, EventDeleter> evtimer_;

    std::function<void()> callback_;
    std::chrono::milliseconds interval_ = 100ms;
    bool is_repeating_ = true;
};

std::unique_ptr<Timer> EvTimerMaker::create()
{
    return std::make_unique<EvTimer>(event_base_);
}

} // namespace libtransmission
