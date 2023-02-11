// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <memory>

#include <event2/event.h>

#include "transmission.h"

#include "dns-ev.h"
#include "dns.h"
#include "trevent.h" // for tr_evthread_init();

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

class EvDnsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ::testing::Test::SetUp();

        tr_evthread_init();
        event_base_ = event_base_new();
    }

    void TearDown() override
    {
        event_base_free(event_base_);
        event_base_ = nullptr;

        ::testing::Test::TearDown();
    }

    struct event_base* event_base_ = nullptr;
};

TEST_F(EvDnsTest, canLookup)
{
    auto dns = EvDns{ event_base_, tr_time };
    auto done = false;

    dns.lookup(
        "example.com",
        [&done](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_GT(expires_at, tr_time());
            done = true;
        });

    waitFor(event_base_, [&done]() { return done; });
    EXPECT_TRUE(done);
}

TEST_F(EvDnsTest, canRequestWhilePending)
{
    auto dns = EvDns{ event_base_, tr_time };
    auto n_done = size_t{ 0 };

    dns.lookup(
        "example.com",
        [&n_done](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_GT(expires_at, tr_time());
            ++n_done;
        });

    dns.lookup(
        "example.com",
        [&n_done](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_GT(expires_at, tr_time());
            ++n_done;
        });

    // wait for both callbacks to be called
    waitFor(event_base_, [&n_done]() { return n_done >= 2U; });
    EXPECT_EQ(2U, n_done);
}

TEST_F(EvDnsTest, canCancel)
{
    auto dns = EvDns{ event_base_, tr_time };
    auto n_done = size_t{ 0 };
    static auto constexpr Name = "example.com"sv;

    auto tag = dns.lookup(
        Name,
        [&n_done](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            ++n_done;
            // we cancelled this req, so `ai` and `ailen` should be zeroed out
            EXPECT_EQ(nullptr, ai);
            EXPECT_EQ(0, ailen);
            EXPECT_EQ(0, expires_at);
        });

    dns.lookup(
        Name,
        [&n_done](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            ++n_done;

            // this one did _not_ get cancelled so it should be OK
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_GT(expires_at, tr_time());
        });

    dns.cancel(tag);

    // wait for both callbacks to be called
    waitFor(event_base_, [&n_done]() { return n_done >= 2U; });
    EXPECT_EQ(2U, n_done);
}

TEST_F(EvDnsTest, doesCacheEntries)
{
    auto dns = EvDns{ event_base_, tr_time };
    static auto constexpr Name = "example.com"sv;

    struct sockaddr const* ai_addr = nullptr;

    dns.lookup(
        Name,
        [&ai_addr](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_GT(expires_at, tr_time());
            ai_addr = ai;
        });

    // wait for the lookup
    waitFor(event_base_, [&ai_addr]() { return ai_addr != nullptr; });
    ASSERT_NE(nullptr, ai_addr);

    auto second_callback_called = false;
    dns.lookup(
        Name,
        [&ai_addr, &second_callback_called](struct sockaddr const* ai, socklen_t ailen, time_t expires_at)
        {
            EXPECT_NE(nullptr, ai);
            EXPECT_GT(ailen, 0);
            EXPECT_EQ(ai_addr, ai);
            EXPECT_GT(expires_at, tr_time());
            second_callback_called = true;
        });
    // since it's cached, the callback should have been invoked
    // without waiting for the event loop
    EXPECT_TRUE(second_callback_called);

    // confirm that `cached()` returns the cached value immediately
    auto res = dns.cached(Name);
    EXPECT_TRUE(res);
    EXPECT_EQ(ai_addr, res->first);
    EXPECT_GT(res->second, 0);
}

} // namespace libtransmission::test
