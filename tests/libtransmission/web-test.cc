// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <atomic>
#include <chrono>
#include <cstddef> // size_t
#include <utility>

#include <libtransmission/web.h>

#include <gtest/gtest.h>

#include "test-fixtures.h"

using namespace std::literals;

using WebTest = ::testing::Test;

TEST_F(WebTest, fetchAfterShutdownStartedStillInvokesCallback)
{
    auto mediator = tr_web::Mediator{};
    auto web = tr_web::create(mediator);
    web->startShutdown(15s);

    auto n_responses = size_t{};
    auto status = long{ -1 };
    void* user_data = nullptr;
    auto marker = int{};
    web->fetch(
        { "http://127.0.0.1/"sv,
          [&](tr_web::FetchResponse const& response)
          {
              status = response.status;
              user_data = response.user_data;
              ++n_responses;
          },
          &marker });

    // the callers of a refused fetch must still be sent a response,
    // otherwise ones that hold state until their callback is invoked
    // (e.g. tr_ip_cache) block shutdown until the deadline expires.
    // the default Mediator::run() is synchronous, so no need to wait
    EXPECT_EQ(1U, n_responses);
    EXPECT_EQ(0L, status);
    EXPECT_EQ(&marker, user_data);
}

TEST_F(WebTest, startShutdownCancelsFlaggedTasksPromptly)
{
    auto mediator = tr_web::Mediator{};
    auto web = tr_web::create(mediator);

    auto n_responses = std::atomic<size_t>{};
    // 192.0.2.1 is TEST-NET-1 (RFC 5737): the connection attempt can
    // neither succeed nor finish quickly, like a black-holed endpoint
    auto options = tr_web::FetchOptions{ "http://192.0.2.1/"sv,
                                         [&n_responses](tr_web::FetchResponse const& /*response*/) { ++n_responses; },
                                         nullptr };
    options.cancel_on_shutdown = true;
    web->fetch(std::move(options));

    web->startShutdown(30s);

    // the flagged task must be canceled as soon as shutdown starts
    // instead of using up the 30s shutdown grace period
    EXPECT_TRUE(tr::test::waitFor([&]() { return n_responses > 0U && web->is_idle(); }, 10s));
}
