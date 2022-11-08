// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include "transmission.h"

#include "timer-ev.h"
#include "utils-ev.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

class TimerTest : public ::testing::Test
{
protected:
    // setup + teardown to manage an event_base

    void SetUp() override
    {
        ::testing::Test::SetUp();
        evbase_.reset(event_base_new());
    }

    void TearDown() override
    {
        evbase_.reset();
        ::testing::Test::TearDown();
    }

    static auto constexpr AsMSec = [](auto val)
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(val);
    };

    static void EXPECT_TIME(
        std::chrono::milliseconds expected,
        std::chrono::milliseconds actual,
        std::chrono::milliseconds allowed_deviation)
    {
        auto const lower_bound = expected - allowed_deviation;
        EXPECT_LT(lower_bound, actual) << "lower_bound:" << lower_bound.count() << " actual:" << actual.count();

        auto const upper_bound = expected + allowed_deviation;
        EXPECT_LT(actual, upper_bound) << "actual:" << actual.count() << " upper_bound:" << upper_bound.count();
    }

    // This checks that `actual` is in the bounds of [expected/2 ... expected*1.5]
    // to confirm that the timer didn't kick too close to the previous or next interval.
    static void EXPECT_INTERVAL(std::chrono::milliseconds expected, std::chrono::milliseconds actual)
    {
        EXPECT_TIME(expected, actual, expected / 2);
    }

    evhelpers::evbase_unique_ptr evbase_;
};

TEST_F(TimerTest, canInstantiate)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);
}

TEST_F(TimerTest, singleShotCallsCallback)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);

    auto called = false;
    auto callback = [&called]()
    {
        called = true;
    };
    timer->setCallback(callback);

    static auto constexpr Interval = 50ms;
    timer->startSingleShot(Interval);

    waitFor(evbase_.get(), [&called] { return called; });
    EXPECT_TRUE(called);
}

TEST_F(TimerTest, repeatingCallsCallback)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);

    auto called = false;
    auto callback = [&called]()
    {
        called = true;
    };
    timer->setCallback(callback);

    static auto constexpr Interval = 50ms;
    timer->startRepeating(Interval);

    waitFor(evbase_.get(), [&called] { return called; });
    EXPECT_TRUE(called);
}

TEST_F(TimerTest, singleShotHonorsInterval)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);

    auto called = false;
    auto callback = [&called]()
    {
        called = true;
    };
    timer->setCallback(callback);

    auto const begin_time = std::chrono::steady_clock::now();
    static auto constexpr Interval = 50ms;
    timer->startSingleShot(Interval);
    EXPECT_FALSE(timer->isRepeating());
    EXPECT_EQ(Interval, timer->interval());
    waitFor(evbase_.get(), [&called] { return called; });
    auto const end_time = std::chrono::steady_clock::now();

    EXPECT_TRUE(called);
    EXPECT_INTERVAL(Interval, AsMSec(end_time - begin_time));
}

TEST_F(TimerTest, repeatingHonorsInterval)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);

    auto n_calls = size_t{ 0U };
    auto callback = [&n_calls]()
    {
        ++n_calls;
    };
    timer->setCallback(callback);

    auto const begin_time = std::chrono::steady_clock::now();
    static auto constexpr Interval = 50ms;
    static auto constexpr DesiredLoops = 3;
    timer->startRepeating(Interval);
    EXPECT_TRUE(timer->isRepeating());
    EXPECT_EQ(Interval, timer->interval());
    waitFor(evbase_.get(), [&n_calls] { return n_calls >= DesiredLoops; });
    auto const end_time = std::chrono::steady_clock::now();

    EXPECT_INTERVAL(Interval * DesiredLoops, AsMSec(end_time - begin_time));
    EXPECT_EQ(DesiredLoops, n_calls);
}

TEST_F(TimerTest, startAgainDoesRestart)
{
    auto timer_maker = EvTimerMaker{ evbase_.get() };
    auto timer = timer_maker.create();
    EXPECT_TRUE(timer);

    auto n_calls = size_t{ 0U };
    auto callback = [&n_calls]()
    {
        ++n_calls;
    };
    timer->setCallback(callback);

    auto const test = [this, &n_calls, &timer](auto interval)
    {
        auto const next = n_calls + 1;
        auto const begin_time = std::chrono::steady_clock::now();
        timer->startSingleShot(interval);
        waitFor(evbase_.get(), [&n_calls, next]() { return n_calls >= next; });
        auto const end_time = std::chrono::steady_clock::now();

        EXPECT_INTERVAL(interval, AsMSec(end_time - begin_time));
    };

    test(50ms);
    test(100ms);
    test(50ms);
}

} // namespace libtransmission::test
