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
#include "timer.h"
#include "trevent.h" // for tr_evthread_init();

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

class DhtTest : public ::testing::Test
{
protected:
    class MockDht final : public tr_dht::API
    {
        int get_nodes(struct sockaddr_in* sin, int* num, struct sockaddr_in6* sin6, int* num6) override
        {
            return 0;
        }

        int nodes(int af, int* good_return, int* dubious_return, int* cached_return, int* incoming_return) override
        {
            return 0;
        }

        int periodic(
            void const* buf,
            size_t buflen,
            struct sockaddr const* from,
            int fromlen,
            time_t* tosleep,
            dht_callback_t callback,
            void* closure) override
        {
            return 0;
        }

        int ping_node(struct sockaddr const* sa, int salen) override
        {
            auto addrport = tr_address::fromSockaddr(sa);
            auto const [addr, port] = *addrport;
            fmt::print("ping_node {:s}\n", addr.readable(port));
            return 0;
        }

        int search(unsigned char const* id, int port, int af, dht_callback_t callback, void* closure) override
        {
            return 0;
        }

        int uninit() override
        {
            return 0;
        }
    };

    class MockTimer final : public libtransmission::Timer
    {
    public:
        void stop() override
        {
        }

        void setCallback(std::function<void()> callback) override
        {
            callback_ = std::move(callback);
        }

        void setRepeating(bool repeating = true) override
        {
            repeating_ = repeating;
        }

        void setInterval(std::chrono::milliseconds interval) override
        {
            fmt::print("interval {:d} msec\n", interval.count());
            interval_ = interval;
        }

        void start() override
        {
        }

        [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
        {
            return interval_;
        }

        [[nodiscard]] bool isRepeating() const noexcept override
        {
            return false; // FIXME
        }

    private:
        std::function<void()> callback_;
        bool repeating_ = false;
        std::chrono::milliseconds interval_ = {};
    };

    class MockTimerMaker final : public libtransmission::TimerMaker
    {
    public:
        [[nodiscard]] std::unique_ptr<Timer> create() override
        {
            return std::make_unique<MockTimer>();
        }
    };

    class MockMediator final : public tr_dht::Mediator
    {
    public:
        [[nodiscard]] std::vector<tr_torrent_id_t> torrentsAllowingDHT() const override
        {
            return {};
        }

        [[nodiscard]] tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t) const override
        {
            return {};
        }

        [[nodiscard]] std::string_view configDir() const override
        {
            return "/tmp";
        }

        [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
        {
            return mock_timer_maker_;
        }

        [[nodiscard]] tr_dht::API& api() override
        {
            return mock_dht_;
        }

        void addPex(tr_sha1_digest_t const&, tr_pex const* pex, size_t n_pex) override
        {
        }

    private:
        MockDht mock_dht_;
        MockTimerMaker mock_timer_maker_;
    };

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

TEST_F(DhtTest, helloWorld)
{
    auto constexpr Port = tr_port::fromHost(909);
    auto mediator = MockMediator{};
    auto dht = tr_dht::create(mediator, Port, 1, 2);
}

} // namespace libtransmission::test
