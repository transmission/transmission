// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

#include <uv.h>

#include "libtransmission/timer.h"
#include "libtransmission/timer-libuv.h"
#include "libtransmission/tr-assert.h"

using namespace std::literals;

namespace libtransmission
{

class UvTimer final : public Timer
{
public:
    explicit UvTimer(uv_loop_s* loop)
        : loop_{ loop }
    {
        uv_timer_ = new uv_timer_t;
        uv_timer_init(loop_, uv_timer_);
        uv_timer_->data = this;
    }

    UvTimer(UvTimer&&) = delete;
    UvTimer(UvTimer const&) = delete;
    UvTimer& operator=(UvTimer&&) = delete;
    UvTimer& operator=(UvTimer const&) = delete;

    ~UvTimer() override
    {
        stop();
        if (uv_timer_ != nullptr)
        {
            uv_close(
                reinterpret_cast<uv_handle_t*>(uv_timer_),
                [](uv_handle_t* handle) { delete reinterpret_cast<uv_timer_t*>(handle); });

            uv_timer_ = nullptr;
        }
    }

    void stop() override
    {
        if (!is_running_)
        {
            return;
        }

        uv_timer_stop(uv_timer_);

        is_running_ = false;
    }

    void start() override
    {
        if (is_running_)
        {
            return;
        }

        using namespace std::chrono;
        auto const timeout_ms = static_cast<uint64_t>(interval_.count());
        auto const repeat_ms = is_repeating_ ? timeout_ms : 0U;

        uv_timer_start(uv_timer_, &UvTimer::onTimer, timeout_ms, repeat_ms);
        is_running_ = true;
    }

    void set_callback(std::function<void()> callback) override
    {
        callback_ = std::move(callback);
    }

    [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
    {
        return interval_;
    }

    void set_interval(std::chrono::milliseconds interval) override
    {
        TR_ASSERT_MSG(interval.count() > 0 || !is_repeating(), "repeating timers must have a positive interval");

        if (interval_ == interval)
        {
            return;
        }

        interval_ = interval;
        applyChanges();
    }

    [[nodiscard]] bool is_repeating() const noexcept override
    {
        return is_repeating_;
    }

    void set_repeating(bool repeating) override
    {
        if (is_repeating_ == repeating)
        {
            return;
        }

        is_repeating_ = repeating;
        applyChanges();
    }

private:
    void applyChanges()
    {
        auto const was_running = isRunning();

        if (was_running)
        {
            stop();
        }

        if (was_running)
        {
            start();
        }
    }

    static void onTimer(uv_timer_t* handle)
    {
        static_cast<UvTimer*>(handle->data)->handleTimer();
    }

    void handleTimer()
    {
        is_running_ = is_repeating_;

        TR_ASSERT(callback_);
        callback_();
    }

    [[nodiscard]] constexpr bool isRunning() const noexcept
    {
        return is_running_;
    }

    std::chrono::milliseconds interval_ = 100ms;
    bool is_repeating_ = false;
    bool is_running_ = false;
    std::function<void()> callback_;

    uv_loop_s* const loop_;
    uv_timer_t* uv_timer_ = nullptr;
};

std::unique_ptr<Timer> UvTimerMaker::create()
{
    return std::make_unique<UvTimer>(uv_loop_);
}

} // namespace libtransmission
