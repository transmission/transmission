// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <utility>

namespace libtransmission
{

class Timer
{
public:
    Timer() = default;
    virtual ~Timer() = default;

    Timer(Timer&&) = delete;
    Timer(Timer const&) = delete;
    Timer& operator=(Timer&&) = delete;
    Timer& operator=(Timer const&) = delete;

    virtual void stop() = 0;
    virtual void set_callback(std::function<void()> callback) = 0;
    virtual void set_repeating(bool repeating = true) = 0;
    virtual void set_interval(std::chrono::milliseconds) = 0;
    virtual void start() = 0;

    [[nodiscard]] virtual std::chrono::milliseconds interval() const noexcept = 0;
    [[nodiscard]] virtual bool is_repeating() const noexcept = 0;

    void start(std::chrono::milliseconds msec)
    {
        set_interval(msec);
        start();
    }

    void start_repeating(std::chrono::milliseconds msec)
    {
        set_repeating();
        start(msec);
    }

    void start_single_shot(std::chrono::milliseconds msec)
    {
        set_repeating(false);
        start(msec);
    }

    using CStyleCallback = void (*)(void* user_data);
    void set_callback(CStyleCallback callback, void* user_data)
    {
        set_callback([user_data, callback]() { callback(user_data); });
    }
};

class TimerMaker
{
public:
    virtual ~TimerMaker() = default;
    [[nodiscard]] virtual std::unique_ptr<Timer> create() = 0;

    [[nodiscard]] std::unique_ptr<Timer> create(std::function<void()> callback)
    {
        auto timer = create();
        timer->set_callback(std::move(callback));
        return timer;
    }

    [[nodiscard]] std::unique_ptr<Timer> create(Timer::CStyleCallback callback, void* user_data)
    {
        auto timer = create();
        timer->set_callback(callback, user_data);
        return timer;
    }
};

} // namespace libtransmission
