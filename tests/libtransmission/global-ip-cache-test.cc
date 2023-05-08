// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <chrono>
#include <ctime>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <libtransmission/global-ip-cache.h>
#include <libtransmission/net.h>
#include <libtransmission/timer.h>
#include <libtransmission/web.h>

#include "gtest/gtest.h"

using namespace std::literals;

class GlobalIPCacheTest : public ::testing::Test
{
protected:
    class MockMediator final : public tr_web::Mediator
    {
        [[nodiscard]] time_t now() const override
        {
            return std::time(nullptr);
        }
    };

    class MockTimer final : public libtransmission::Timer
    {
        void stop() override
        {
        }

        void set_callback(std::function<void()> /* callback */) override
        {
        }

        void set_repeating(bool /* is_repeating */ = true) override
        {
        }

        void set_interval(std::chrono::milliseconds /* msec */) override
        {
        }

        void start() override
        {
        }

        [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
        {
            return {};
        }

        [[nodiscard]] bool is_repeating() const noexcept override
        {
            return {};
        }
    };

    class MockTimerMaker final : public libtransmission::TimerMaker
    {
    public:
        [[nodiscard]] std::unique_ptr<libtransmission::Timer> create() override
        {
            return std::make_unique<MockTimer>();
        }
    };

    void SetUp() override
    {
        ::testing::Test::SetUp();
        web_->startShutdown(std::chrono::milliseconds::max()); // Prevent sending actual HTTP requests
        global_ip_cache_ = std::make_unique<tr_global_ip_cache>(*web_, timer_maker_);
    }

    void TearDown() override
    {
        ::testing::Test::TearDown();
        global_ip_cache_->try_shutdown();
    }

    std::unique_ptr<tr_global_ip_cache> global_ip_cache_;

    MockMediator mediator_{};
    std::unique_ptr<tr_web> web_ = tr_web::create(mediator_);

    MockTimerMaker timer_maker_{};
};

TEST_F(GlobalIPCacheTest, bindAddr)
{
    static constexpr auto Ipv4Tests = std::array<std::pair<std::string_view, std::string_view>, 4>{
        { { "8.8.8.8"sv, "8.8.8.8"sv },
          { "192.168.133.133"sv, "192.168.133.133"sv },
          { "2001:1890:1112:1::20"sv, "0.0.0.0"sv },
          { "asdasd"sv, "0.0.0.0"sv } }
    };
    static constexpr auto Ipv6Tests = std::array<std::pair<std::string_view, std::string_view>, 4>{
        { { "fd12:3456:789a:1::1"sv, "fd12:3456:789a:1::1"sv },
          { "192.168.133.133"sv, "::"sv },
          { "2001:1890:1112:1::20"sv, "2001:1890:1112:1::20"sv },
          { "asdasd"sv, "::"sv } }
    };

    // IPv4
    for (auto const& [addr_str, expected] : Ipv4Tests)
    {
        global_ip_cache_->set_settings_bind_addr(TR_AF_INET, addr_str);
        auto const addr = global_ip_cache_->bind_addr(TR_AF_INET);
        EXPECT_EQ(addr.display_name(), expected);
    }

    // IPv6
    for (auto const& [addr_str, expected] : Ipv6Tests)
    {
        global_ip_cache_->set_settings_bind_addr(TR_AF_INET6, addr_str);
        auto const addr = global_ip_cache_->bind_addr(TR_AF_INET6);
        EXPECT_EQ(addr.display_name(), expected);
    }
}

TEST_F(GlobalIPCacheTest, setGlobalAddr)
{
    auto constexpr AddrStr = std::array{ "8.8.8.8"sv,
                                         "192.168.133.133"sv,
                                         "172.16.241.133"sv,
                                         "2001:1890:1112:1::20"sv,
                                         "fd12:3456:789a:1::1"sv };
    auto constexpr IPv4Tests = std::array{ true, false, false, false, false };
    auto constexpr IPv6Tests = std::array{ false, false, false, true, false };

    static_assert(std::size(AddrStr) == std::size(IPv4Tests));
    static_assert(std::size(AddrStr) == std::size(IPv6Tests));

    // IPv4
    for (std::size_t i = 0; i < std::size(AddrStr); ++i)
    {
        auto const addr = tr_address::from_string(AddrStr[i]);
        ASSERT_TRUE(addr);
        EXPECT_EQ(global_ip_cache_->set_global_addr(TR_AF_INET, *addr), IPv4Tests[i]);
        if (IPv4Tests[i])
        {
            EXPECT_EQ(global_ip_cache_->global_addr(TR_AF_INET)->display_name(), AddrStr[i]);
        }
    }

    // IPv6
    for (std::size_t i = 0; i < std::size(AddrStr); ++i)
    {
        auto const addr = tr_address::from_string(AddrStr[i]);
        ASSERT_TRUE(addr);
        EXPECT_EQ(global_ip_cache_->set_global_addr(TR_AF_INET6, *addr), IPv6Tests[i]);
        if (IPv6Tests[i])
        {
            EXPECT_EQ(global_ip_cache_->global_addr(TR_AF_INET6)->display_name(), AddrStr[i]);
        }
    }
}

TEST_F(GlobalIPCacheTest, globalSourceIPv4)
{
    global_ip_cache_->set_settings_bind_addr(TR_AF_INET, "0.0.0.0"s);
    auto const addr = global_ip_cache_->global_source_addr(TR_AF_INET);
    if (!addr)
    {
        GTEST_SKIP() << "globalSourceIPv4 did not return an address, either:\n"
                     << "1. globalSourceIPv4 is broken\n"
                     << "2. Your system does not support IPv4\n"
                     << "3. You don't have IPv4 connectivity to public internet";
    }
    EXPECT_TRUE(addr->is_ipv4());
}

TEST_F(GlobalIPCacheTest, globalSourceIPv6)
{
    global_ip_cache_->set_settings_bind_addr(TR_AF_INET6, "::"s);
    auto const addr = global_ip_cache_->global_source_addr(TR_AF_INET6);
    if (!addr)
    {
        GTEST_SKIP() << "globalSourceIPv6 did not return an address, either:\n"
                     << "1. globalSourceIPv6 is broken\n"
                     << "2. Your system does not support IPv6\n"
                     << "3. You don't have IPv6 connectivity to public internet";
    }
    EXPECT_TRUE(addr->is_ipv6());
}
