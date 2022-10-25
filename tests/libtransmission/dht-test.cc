// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <utility>

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

namespace
{
auto constexpr IdLength = size_t{ 20U };
auto constexpr MockTimerInterval = 10ms;
} // namespace

class DhtTest : public SandboxedTest
{
protected:
    // Helper for creating a mock dht.dat state file
    struct MockStateFile
    {
        // Fake data to be written to the test state file

        std::array<char, IdLength> id_ = tr_randObj<std::array<char, IdLength>>();

        std::vector<std::pair<tr_address, tr_port>> ipv4_nodes_ = {
            std::make_pair(*tr_address::fromString("10.10.10.1"), tr_port::fromHost(128)),
            std::make_pair(*tr_address::fromString("10.10.10.2"), tr_port::fromHost(129)),
            std::make_pair(*tr_address::fromString("10.10.10.3"), tr_port::fromHost(130)),
            std::make_pair(*tr_address::fromString("10.10.10.4"), tr_port::fromHost(131)),
            std::make_pair(*tr_address::fromString("10.10.10.5"), tr_port::fromHost(132))
        };

        std::vector<std::pair<tr_address, tr_port>> ipv6_nodes_ = {
            std::make_pair(*tr_address::fromString("1002:1035:4527:3546:7854:1237:3247:3217"), tr_port::fromHost(6881)),
            std::make_pair(*tr_address::fromString("1002:1035:4527:3546:7854:1237:3247:3218"), tr_port::fromHost(6882)),
            std::make_pair(*tr_address::fromString("1002:1035:4527:3546:7854:1237:3247:3219"), tr_port::fromHost(6883)),
            std::make_pair(*tr_address::fromString("1002:1035:4527:3546:7854:1237:3247:3220"), tr_port::fromHost(6884)),
            std::make_pair(*tr_address::fromString("1002:1035:4527:3546:7854:1237:3247:3221"), tr_port::fromHost(6885))
        };

        [[nodiscard]] auto nodesString() const
        {
            auto str = std::string{};
            for (auto const& [addr, port] : ipv4_nodes_)
            {
                str += addr.readable(port);
                str += ',';
            }
            for (auto const& [addr, port] : ipv6_nodes_)
            {
                str += addr.readable(port);
                str += ',';
            }
            return str;
        }

        void save(std::string_view sandbox_dir) const
        {
            auto const dat_file = tr_pathbuf{ sandbox_dir, "/dht.dat" };

            auto dict = tr_variant{};
            tr_variantInitDict(&dict, 3U);
            tr_variantDictAddRaw(&dict, TR_KEY_id, std::data(id_), std::size(id_));
            auto compact = std::vector<std::byte>{};
            for (auto const& [addr, port] : ipv4_nodes_)
            {
                addr.toCompact4(std::back_inserter(compact), port);
            }
            tr_variantDictAddRaw(&dict, TR_KEY_nodes, std::data(compact), std::size(compact));
            compact.clear();
            for (auto const& [addr, port] : ipv6_nodes_)
            {
                addr.toCompact6(std::back_inserter(compact), port);
            }
            tr_variantDictAddRaw(&dict, TR_KEY_nodes6, std::data(compact), std::size(compact));
            tr_variantToFile(&dict, TR_VARIANT_FMT_BENC, dat_file);
            tr_variantClear(&dict);
        }
    };

    // A fake libdht for the tests to call
    class MockDht final : public tr_dht::API
    {
    public:
        int get_nodes(struct sockaddr_in* /*sin*/, int* /*max*/, struct sockaddr_in6* /*sin6*/, int* /*max6*/) override
        {
            fmt::print("get_nodes\n");
            return 0;
        }

        int nodes(int /*af*/, int* good, int* dubious, int* cached, int* incoming) override
        {
            fmt::print("nodes\n");

            if (good != nullptr)
            {
                *good = good_;
            }

            if (dubious != nullptr)
            {
                *dubious = dubious_;
            }

            if (cached != nullptr)
            {
                *cached = cached_;
            }

            if (incoming != nullptr)
            {
                *incoming = incoming_;
            }

            return 0;
        }

        int periodic(
            void const* /*buf*/,
            size_t /*buflen*/,
            sockaddr const /*from*/*,
            int /*fromlen*/,
            time_t* /*tosleep*/,
            dht_callback_t /*callback*/,
            void* /*closure*/) override
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

        int search(unsigned char const* /*id*/, int /*port*/, int /*af*/, dht_callback_t /*callback*/, void* /*closure*/)
            override
        {
            fmt::print("search\n");
            return 0;
        }

        int init(int dht_socket, int dht_socket6, unsigned const char* id, unsigned const char* /*v*/) override
        {
            inited_ = true;
            fmt::print("init\n");
            dht_socket_ = dht_socket;
            dht_socket6_ = dht_socket6;
            std::copy_n(id, std::size(id_), std::begin(id_));
            return 0;
        }

        int uninit() override
        {
            inited_ = false;
            fmt::print("uninit\n");
            return 0;
        }

        struct Pinged
        {
            tr_address address;
            tr_port port;
            time_t timestamp;
        };

        int good_ = 0;
        int dubious_ = 0;
        int cached_ = 0;
        int incoming_ = 0;
        bool inited_ = false;
        std::vector<Pinged> pinged_;
        std::array<char, IdLength> id_ = {};
        int dht_socket_ = TR_BAD_SOCKET;
        int dht_socket6_ = TR_BAD_SOCKET;
    };

    // Creates real timers, but with shortened intervals so that tests can run faster
    class MockTimer final : public libtransmission::Timer
    {
    public:
        explicit MockTimer(std::unique_ptr<Timer> real_timer)
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
            real_timer_->setInterval(MockTimerInterval);
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

    // Creates MockTimers
    class MockTimerMaker final : public libtransmission::TimerMaker
    {
    public:
        explicit MockTimerMaker(struct event_base* evb)
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
        explicit MockMediator(struct event_base* event_base)
            : mock_timer_maker_{ event_base }
        {
        }

        [[nodiscard]] std::vector<tr_torrent_id_t> torrentsAllowingDHT() const override
        {
            return torrents_allowing_dht_;
        }

        [[nodiscard]] tr_sha1_digest_t torrentInfoHash(tr_torrent_id_t /*id*/) const override
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

        void addPex(tr_sha1_digest_t const& /*info_hash*/, tr_pex const* /*pex*/, size_t /*n_pex*/) override
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

    // Arbitrary values. Several tests requires socket/port values
    // to be provided but they aren't central to the tests, so they're
    // declared here with "Arbitrary" in the name to make that clear.
    static auto constexpr ArbitrarySock4 = tr_socket_t{ 404 };
    static auto constexpr ArbitrarySock6 = tr_socket_t{ 418 };
    static auto constexpr ArbitraryPeerPort = tr_port::fromHost(909);
};

TEST_F(DhtTest, initsWithCorrectSockets)
{
    static auto constexpr Sock4 = tr_socket_t{ 1000 };
    static auto constexpr Sock6 = tr_socket_t{ 2000 };

    // Make the DHT
    auto mediator = MockMediator{ event_base_ };
    mediator.config_dir_ = sandboxDir();
    auto dht = tr_dht::create(mediator, ArbitraryPeerPort, Sock4, Sock6);

    // Confirm that dht_init() was called with the right sockets
    EXPECT_EQ(Sock4, mediator.mock_dht_.dht_socket_);
    EXPECT_EQ(Sock6, mediator.mock_dht_.dht_socket6_);
}

TEST_F(DhtTest, callsUninitOnDestruct)
{
    auto mediator = MockMediator{ event_base_ };
    mediator.config_dir_ = sandboxDir();
    EXPECT_FALSE(mediator.mock_dht_.inited_);

    {
        auto dht = tr_dht::create(mediator, ArbitraryPeerPort, ArbitrarySock4, ArbitrarySock6);
        EXPECT_TRUE(mediator.mock_dht_.inited_);

        // dht goes out-of-scope here
    }

    EXPECT_FALSE(mediator.mock_dht_.inited_);
}

TEST_F(DhtTest, loadsStateFromStateFile)
{
    auto const state_file = MockStateFile{};
    state_file.save(sandboxDir());

    // Make the DHT
    auto mediator = MockMediator{ event_base_ };
    mediator.config_dir_ = sandboxDir();
    auto dht = tr_dht::create(mediator, ArbitraryPeerPort, ArbitrarySock4, ArbitrarySock6);

    // Wait for all the state nodes to be pinged
    auto& pinged = mediator.mock_dht_.pinged_;
    auto const n_expected_nodes = std::size(state_file.ipv4_nodes_) + std::size(state_file.ipv6_nodes_);
    waitFor(event_base_, [&pinged, n_expected_nodes]() { return std::size(pinged) >= n_expected_nodes; });
    auto actual_nodes_str = std::string{};
    for (auto const& [addr, port, timestamp] : pinged)
    {
        actual_nodes_str += addr.readable(port);
        actual_nodes_str += ',';
    }

    /// Confirm that the state was loaded

    // dht_init() should have been called with the state file's id
    EXPECT_EQ(state_file.id_, mediator.mock_dht_.id_);

    // dht_ping_nodedht_init() should have been called with state file's nodes
    EXPECT_EQ(state_file.nodesString(), actual_nodes_str);
}

TEST_F(DhtTest, stopsBootstrappingWhenSwarmHealthIsGoodEnough)
{
    auto const state_file = MockStateFile{};
    state_file.save(sandboxDir());

    // Make the DHT
    auto mediator = MockMediator{ event_base_ };
    mediator.config_dir_ = sandboxDir();
    auto dht = tr_dht::create(mediator, ArbitraryPeerPort, ArbitrarySock4, ArbitrarySock6);

    // Wait for N pings to occur...
    auto& mock_dht = mediator.mock_dht_;
    static auto constexpr TurnGoodAfterNthPing = size_t{ 3 };
    waitFor(event_base_, [&mock_dht]() { return std::size(mock_dht.pinged_) == TurnGoodAfterNthPing; });
    EXPECT_EQ(TurnGoodAfterNthPing, std::size(mock_dht.pinged_));

    // Now fake the swarm data that should cause bootstrapping to end
    mock_dht.good_ = 50;
    mock_dht.incoming_ = 10;

    // Now test to see if bootstrapping is done.
    // There's not public API for `isBootstrapping()`,
    // so to test this we just a moment to confirm that no more bootstrap nodes are pinged.
    waitFor(
        event_base_,
        []() { return false; },
        MockTimerInterval * 10);

    // Confirm that the number of nodes pinged is unchanged,
    // indicating that boostrapping is done
    EXPECT_EQ(TurnGoodAfterNthPing, std::size(mock_dht.pinged_));
}

TEST_F(DhtTest, savesStateIfSwarmIsGood)
{
}

TEST_F(DhtTest, doesNotSaveStateIfSwarmIsBad)
{
}

TEST_F(DhtTest, usesBootstrapFile)
{
    // Make the 'dht.bootstrap' file.
    // This a file with each line holding `${host} ${port}`
    // which tr-dht will try to ping as nodes
    static auto constexpr BootstrapNodeName = "example.com"sv;
    static auto constexpr BootstrapNodePort = tr_port::fromHost(8080);
    if (auto ofs = std::ofstream{ tr_pathbuf{ sandboxDir(), "/dht.bootstrap" } }; ofs)
    {
        ofs << BootstrapNodeName << ' ' << BootstrapNodePort.host() << std::endl;
        ofs.close();
    }

    // Make the DHT
    auto mediator = MockMediator{ event_base_ };
    mediator.config_dir_ = sandboxDir();
    auto dht = tr_dht::create(mediator, ArbitraryPeerPort, ArbitrarySock4, ArbitrarySock6);

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

TEST_F(DhtTest, pingsAddedNodes)
{
}

TEST_F(DhtTest, announcesTorrents)
{
}

TEST_F(DhtTest, honorsPeriodicSleepTime)
{
}

// auto constexpr Id = tr_torrent_id_t{ 1 };
// mediator.info_hashes_[Id] = tr_randObj<tr_sha1_digest_t>();
// mediator.torrents_allowing_dht_ = { Id };

} // namespace libtransmission::test
