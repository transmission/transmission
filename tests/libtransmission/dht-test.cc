// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <fstream>
#include <memory>
#include <event2/event.h>

#include "transmission.h"

#include "dns-ev.h"
#include "dns.h"
#include "timer-ev.h"
#include "trevent.h" // for tr_evthread_init();

#include "gtest/gtest.h"
#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

class DhtTest : public SandboxedTest
{
protected:
    class MockDht final : public tr_dht::API
    {
    public:
        // int get_nodes(struct sockaddr_in* sin, int* num, struct sockaddr_in6* sin6, int* num6) override
        int get_nodes(struct sockaddr_in*, int*, struct sockaddr_in6*, int*) override
        {
            fmt::print("get_nodes\n");
            return 0;
        }

        //int nodes(int af, int* good_return, int* dubious_return, int* cached_return, int* incoming_return) override
        int nodes(int, int*, int*, int*, int*) override
        {
            fmt::print("nodes\n");
            return 0;
        }

        // int periodic( void const* buf, size_t buflen, sockaddr const* from, int fromlen, time_t* tosleep, dht_callback_t callback, void* closure) override
        int periodic(void const*, size_t, sockaddr const*, int, time_t*, dht_callback_t, void*) override
        {
            fmt::print("periodic\n");
            return 0;
        }

        int ping_node(struct sockaddr const* sa, int /*salen*/) override
        {
            auto addrport = tr_address::fromSockaddr(sa);
            auto const [addr, port] = *addrport;
            fmt::print("ping_node {:s}\n", addr.readable(port));
            pinged_.push_back(Pinged{ addr, port, tr_time() });
            return 0;
        }

        // int search(unsigned char const* id, int port, int af, dht_callback_t callback, void* closure) override
        int search(unsigned char const*, int, int, dht_callback_t, void*) override
        {
            fmt::print("search\n");
            return 0;
        }

        int uninit() override
        {
            fmt::print("uninit\n");
            return 0;
        }

        struct Pinged
        {
            tr_address address;
            tr_port port;
            time_t timestamp;
        };

        std::vector<Pinged> pinged_;
    };

    class MockTimer final : public libtransmission::Timer
    {
    public:
        MockTimer(std::unique_ptr<Timer> real_timer)
            : real_timer_{ std::move(real_timer) }
        {
        }

        void stop() override
        {
            real_timer_->stop();
        }

        void setCallback(std::function<void()> callback) override
        {
            fmt::print("set callback\n");
            real_timer_->setCallback(std::move(callback));
        }

        void setRepeating(bool repeating = true) override
        {
            fmt::print("set repeating {}\n", repeating);
            real_timer_->setRepeating(repeating);
        }

        void setInterval(std::chrono::milliseconds interval) override
        {
            fmt::print("setInterval requested {:d} using 10 msec\n", interval.count());
            real_timer_->setInterval(10ms);
        }

        void start() override
        {
            fmt::print("start()\n");
            real_timer_->start();
        }

        [[nodiscard]] std::chrono::milliseconds interval() const noexcept override
        {
            fmt::print("interval()\n");
            return real_timer_->interval();
        }

        [[nodiscard]] bool isRepeating() const noexcept override
        {
            fmt::print("isRepeating()\n");
            return real_timer_->isRepeating();
        }

    private:
        std::unique_ptr<Timer> const real_timer_;
    };

    class MockTimerMaker final : public libtransmission::TimerMaker
    {
    public:
        MockTimerMaker(struct event_base* evb)
            : real_timer_maker_{ evb }
        {
        }

        [[nodiscard]] std::unique_ptr<Timer> create() override
        {
            return std::make_unique<MockTimer>(real_timer_maker_.create());
        }

        EvTimerMaker real_timer_maker_;
    };

    class MockMediator final : public tr_dht::Mediator
    {
    public:
        MockMediator(struct event_base* event_base)
            : mock_timer_maker_{ event_base }
        {
        }

        [[nodiscard]] std::vector<tr_torrent_id_t> torrentsAllowingDHT() const override
        {
            return torrents_allowing_dht_;
        }

        [[nodiscard]] tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t) const override
        {
            return {};
        }

        [[nodiscard]] std::string_view configDir() const override
        {
            return config_dir_;
        }

        [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
        {
            return mock_timer_maker_;
        }

        [[nodiscard]] tr_dht::API& api() override
        {
            return mock_dht_;
        }

        // void addPex(tr_sha1_digest_t const&, tr_pex const* pex, size_t n_pex) override
        void addPex(tr_sha1_digest_t const&, tr_pex const*, size_t) override
        {
        }

        std::string config_dir_;
        std::vector<tr_torrent_id_t> torrents_allowing_dht_;
        std::map<tr_torrent_id_t, tr_sha1_digest_t> info_hashes_;
        MockDht mock_dht_;
        MockTimerMaker mock_timer_maker_;
    };

    [[nodiscard]] static std::pair<tr_address, tr_port> getSockaddr(std::string_view name, tr_port port)
    {
        auto hints = addrinfo{};
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_family = AF_UNSPEC;

        auto const szname = tr_urlbuf{ name };
        auto const port_str = std::to_string(port.host());
        addrinfo* info = nullptr;
        if (int const rc = getaddrinfo(szname.c_str(), std::data(port_str), &hints, &info); rc != 0)
        {
            tr_logAddWarn(fmt::format(
                _("Couldn't look up '{address}:{port}': {error} ({error_code})"),
                fmt::arg("address", name),
                fmt::arg("port", port.host()),
                fmt::arg("error", gai_strerror(rc)),
                fmt::arg("error_code", rc)));
            return {};
        }

        auto opt = tr_address::fromSockaddr(info->ai_addr);
        freeaddrinfo(info);
        if (opt)
        {
            return *opt;
        }

        return {};
    }

    void SetUp() override
    {
        SandboxedTest::SetUp();

        tr_evthread_init();
        event_base_ = event_base_new();
    }

    void TearDown() override
    {
        event_base_free(event_base_);
        event_base_ = nullptr;

        SandboxedTest::TearDown();
    }

    struct event_base* event_base_ = nullptr;
};

TEST_F(DhtTest, usesDhtBootstrapFile)
{
    auto constexpr Id = tr_torrent_id_t{ 1 };
    auto constexpr PeerPort = tr_port::fromHost(909);

    // Make the 'dht.bootstrap' file.
    // This a file with each line holding `${host} ${port}`
    // which tr-dht will try to ping as nodes
    auto constexpr BootstrapNodeName = "example.com"sv;
    auto constexpr BootstrapNodePort = tr_port::fromHost(8080);
    auto ofs = std::ofstream{ tr_pathbuf{ sandboxDir(), "/dht.bootstrap" } };
    ofs << BootstrapNodeName << ' ' << BootstrapNodePort.host() << std::endl;
    ofs.close();

    // make the mediator
    auto mediator = MockMediator{ event_base_ };
    mediator.info_hashes_[Id] = tr_randObj<tr_sha1_digest_t>();
    mediator.torrents_allowing_dht_ = { Id };
    mediator.config_dir_ = sandboxDir();

    // make the dht object
    auto constexpr Sock4 = tr_socket_t{ 404 };
    auto constexpr Sock6 = tr_socket_t{ 418 };
    auto dht = tr_dht::create(mediator, PeerPort, Sock4, Sock6);

    // We didn't create a 'dht.dat' file to load state from,
    // so 'dht.bootstrap' should be the first nodes in the bootstrap list.
    // Confirm that BootstrapNodeName gets pinged first.
    auto const expected = getSockaddr(BootstrapNodeName, BootstrapNodePort);
    auto& pinged = mediator.mock_dht_.pinged_;
    waitFor(
        event_base_,
        [&pinged]() { return !std::empty(pinged); },
        5s);
    ASSERT_EQ(1U, std::size(pinged));
    auto const actual = pinged.front();
    EXPECT_EQ(expected.first, actual.address);
    EXPECT_EQ(expected.second, actual.port);
    EXPECT_EQ(expected.first.readable(expected.second), actual.address.readable(actual.port));
}

} // namespace libtransmission::test
