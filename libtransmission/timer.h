// This file Copyright © 2022-2023 Mnemosyne LLC.
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
    virtual void setCallback(std::function<void()> callback) = 0;
    virtual void setRepeating(bool repeating = true) = 0;
    virtual void setInterval(std::chrono::milliseconds) = 0;
    virtual void start() = 0;

    [[nodiscard]] virtual std::chrono::milliseconds interval() const noexcept = 0;
    [[nodiscard]] virtual bool isRepeating() const noexcept = 0;

    void start(std::chrono::milliseconds msec)
    {
        setInterval(msec);
        start();
    }

    void startRepeating(std::chrono::milliseconds msec)
    {
        setRepeating();
        start(msec);
    }

    void startSingleShot(std::chrono::milliseconds msec)
    {
        setRepeating(false);
        start(msec);
    }

    using CStyleCallback = void (*)(void* user_data);
    void setCallback(CStyleCallback callback, void* user_data)
    {
        setCallback([user_data, callback]() { callback(user_data); });
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
        timer->setCallback(std::move(callback));
        return timer;
    }

    [[nodiscard]] std::unique_ptr<Timer> create(Timer::CStyleCallback callback, void* user_data)
    {
        auto timer = create();
        timer->setCallback(callback, user_data);
        return timer;
    }
};

} // namespace libtransmission
