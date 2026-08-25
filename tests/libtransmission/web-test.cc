// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <cstdlib> // std::abort()
#include <future>
#include <iostream>
#include <thread>
#include <utility>

#include <libtransmission/web.h>

#include "test-fixtures.h"

using namespace std::literals;

class WebTest : public ::tr::test::TransmissionTest
{
protected:
    // A tr_web destructor asks its curl thread to stop and then joins it.
    // The request races the thread's decision to sleep, and roughly one
    // run in 1500 lands in the window where the request can be missed.
    // Keep this high enough that a stuck curl thread fails every run.
    static auto constexpr Reps = 10000;

    // How long the reps get before the test calls the curl thread stuck.
    // The slowest CI machines take thirty times longer than the fastest.
    // This only has to be short enough to beat ctest's own timeout.
    static auto constexpr StuckBudget = std::chrono::seconds{ 120 };

    // Run func on a helper thread and end the process if it overruns.
    // A curl thread that never notices the shutdown request leaves the
    // helper blocked in join(), and that helper can't be cancelled.
    // Aborting turns a stalled CI job into a reported failure.
    // func counts the reps it finishes so that the message can tell a
    // stuck curl thread apart from a slow machine.
    template<typename Func>
    static void runWithinBudget(Func&& func)
    {
        auto n_done = std::atomic<int>{};
        auto promise = std::promise<void>{};
        auto future = promise.get_future();
        auto thread = std::thread{ [&n_done, promise = std::move(promise), func = std::forward<Func>(func)]() mutable
                                   {
                                       func(n_done);
                                       promise.set_value();
                                   } };

        if (future.wait_for(StuckBudget) == std::future_status::timeout)
        {
            std::cerr << "tr_web did not finish within " << StuckBudget.count() << " seconds (" << n_done.load()
                      << " reps done)\n";
            thread.detach();
            std::abort();
        }

        thread.join();
    }
};

TEST_F(WebTest, destroyWhileIdleDoesNotHang)
{
    runWithinBudget(
        [](std::atomic<int>& n_done)
        {
            auto mediator = tr_web::Mediator{};

            for (auto i = 0; i < Reps; ++i)
            {
                auto const web = tr_web::create(mediator);
                EXPECT_TRUE(web->is_idle());
                ++n_done;
            }
        });
}

TEST_F(WebTest, startShutdownThenDestroyDoesNotHang)
{
    runWithinBudget(
        [](std::atomic<int>& n_done)
        {
            auto mediator = tr_web::Mediator{};

            for (auto i = 0; i < Reps; ++i)
            {
                auto const web = tr_web::create(mediator);
                web->startShutdown(0ms);
                ++n_done;
            }
        });
}

TEST_F(WebTest, destroyWithTaskInFlightDoesNotHang)
{
    // A tr_web with work in flight takes the other path out of the loop,
    // where the curl thread cancels the task instead of waiting for one.
    runWithinBudget(
        [](std::atomic<int>& n_done)
        {
            auto mediator = tr_web::Mediator{};
            auto const web = tr_web::create(mediator);

            // TEST-NET-1 (RFC 5737) has no route, so this fetch is still
            // in flight when the tr_web is destroyed.
            web->fetch({ "http://192.0.2.1/"sv, [](tr_web::FetchResponse const&) {}, nullptr, 60s });
            ++n_done;
        });
}
