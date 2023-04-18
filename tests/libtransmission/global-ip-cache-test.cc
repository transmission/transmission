// This file Copyright © 2023-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <ctime>
#include <memory>
#include <string>
#include <utility>

#include "libtransmission/global-ip-cache.h"
#include "libtransmission/net.h"
#include "libtransmission/timer.h"
#include "libtransmission/web.h"

#include "test-fixtures.h"

class GlobalIPTest : public ::testing::Test
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

        void setCallback(std::function<void()> /* callback */) override
        {
        }

        void setRepeating(bool /* is_repeating */ = true) override
        {
        }

        void setInterval(std::chrono::milliseconds /* msec */) override
        {
        }

        void start() override
        {
        }

        [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
        {
            return {};
        }

        [[nodiscard]] bool isRepeating() const noexcept override
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
        global_ip_cache_ = std::make_unique<tr_global_ip_cache>(*web_, timer_maker_);
    }

    void TearDown() override
    {
        ::testing::Test::SetUp();
        global_ip_cache_->try_shutdown();
    }

    MockMediator mediator_;
    std::unique_ptr<tr_web> web_ = tr_web::create(mediator_);

    MockTimerMaker timer_maker_;

    std::unique_ptr<tr_global_ip_cache> global_ip_cache_;
};

TEST_F(GlobalIPTest, bindAddr)
{
    static auto const Ipv4Tests = std::array<std::pair<std::string, std::string_view>, 4>{
        { { "8.8.8.8"s, "8.8.8.8"sv },
          { "192.168.133.133"s, "192.168.133.133"sv },
          { "2001:1890:1112:1::20"s, "0.0.0.0"sv },
          { "asdasd"s, "0.0.0.0"sv } }
    };
    static auto const Ipv6Tests = std::array<std::pair<std::string, std::string_view>, 4>{
        { { "fd12:3456:789a:1::1"s, "fd12:3456:789a:1::1"sv },
          { "192.168.133.133"s, "::"sv },
          { "2001:1890:1112:1::20"s, "2001:1890:1112:1::20"sv },
          { "asdasd"s, "::"sv } }
    };

    for (auto const& [addr_str, expected] : Ipv4Tests)
    {
        global_ip_cache_->set_settings_bind_addr(TR_AF_INET, addr_str);
        auto const addr = global_ip_cache_->bind_addr(TR_AF_INET);
        EXPECT_EQ(addr.display_name(), expected);
    }
    for (auto const& [addr_str, expected] : Ipv6Tests)
    {
        global_ip_cache_->set_settings_bind_addr(TR_AF_INET6, addr_str);
        auto const addr = global_ip_cache_->bind_addr(TR_AF_INET6);
        EXPECT_EQ(addr.display_name(), expected);
    }
}