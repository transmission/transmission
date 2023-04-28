// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> // for std::memcpy()
#include <deque>
#include <memory>
#include <vector>

#include <fmt/format.h>

#define LIBTRANSMISSION_ANNOUNCER_MODULE

#include <libtransmission/transmission.h>

#include <libtransmission/announcer.h>
#include <libtransmission/announcer-common.h>
#include <libtransmission/crypto-utils.h> // for tr_rand_obj()
#include <libtransmission/peer-mgr.h> // for tr_pex
#include <libtransmission/timer-ev.h>
#include <libtransmission/tr-buffer.h>

#include "test-fixtures.h"

using namespace std::literals;

class AnnouncerUdpTest : public ::testing::Test
{
private:
    void SetUp() override
    {
        tr_net_init();

        ::testing::Test::SetUp();
        tr_timeUpdate(time(nullptr));
    }

protected:
    class MockMediator final : public tr_announcer_udp::Mediator
    {
    public:
        MockMediator()
            : event_base_{ event_base_new(), event_base_free }
        {
        }

        void sendto(void const* buf, size_t buflen, sockaddr const* sa, socklen_t salen) override
        {
            auto target = tr_address::from_sockaddr(sa);
            ASSERT_TRUE(target);
            sent_.emplace_back(static_cast<char const*>(buf), buflen, sa, salen);
        }

        [[nodiscard]] auto* eventBase()
        {
            return event_base_.get();
        }

        [[nodiscard]] std::optional<tr_address> announceIP() const override
        {
            return {};
        }

        struct Sent
        {
            Sent() = default;

            Sent(char const* buf, size_t buflen, sockaddr const* sa, socklen_t salen)
                : sslen_{ salen }
            {
                buf_.insert(std::end(buf_), buf, buf + buflen);
                std::memcpy(&ss_, sa, salen);
            }

            std::vector<char> buf_;
            sockaddr_storage ss_ = {};
            socklen_t sslen_ = {};
        };

        std::deque<Sent> sent_;

        std::unique_ptr<event_base, void (*)(event_base*)> const event_base_;
    };

    static void expectEqual(tr_scrape_response const& expected, tr_scrape_response const& actual)
    {
        EXPECT_EQ(expected.did_connect, actual.did_connect);
        EXPECT_EQ(expected.did_timeout, actual.did_timeout);
        EXPECT_EQ(expected.errmsg, actual.errmsg);
        EXPECT_EQ(expected.min_request_interval, actual.min_request_interval);
        EXPECT_EQ(expected.scrape_url, actual.scrape_url);

        EXPECT_EQ(expected.row_count, actual.row_count);
        for (int i = 0; i < std::min(expected.row_count, actual.row_count); ++i)
        {
            EXPECT_EQ(expected.rows[i].info_hash, actual.rows[i].info_hash);
            EXPECT_EQ(expected.rows[i].seeders, actual.rows[i].seeders);
            EXPECT_EQ(expected.rows[i].leechers, actual.rows[i].leechers);
            EXPECT_EQ(expected.rows[i].downloads, actual.rows[i].downloads);
            EXPECT_EQ(expected.rows[i].downloaders, actual.rows[i].downloaders);
        }
    }

    static void expectEqual(tr_scrape_request const& expected, std::vector<tr_sha1_digest_t> const& actual)
    {
        EXPECT_EQ(expected.info_hash_count, std::size(actual));
        for (size_t i = 0; i < std::min(static_cast<size_t>(expected.info_hash_count), std::size(actual)); ++i)
        {
            EXPECT_EQ(expected.info_hash[i], actual[i]);
        }
    }

    [[nodiscard]] static uint32_t parseConnectionRequest(libtransmission::Buffer& buf)
    {
        EXPECT_EQ(ProtocolId, buf.to_uint64());
        EXPECT_EQ(ConnectAction, buf.to_uint32());
        return buf.to_uint32();
    }

    [[nodiscard]] static auto buildScrapeRequestFromResponse(tr_scrape_response const& response)
    {
        auto request = tr_scrape_request{};
        request.scrape_url = response.scrape_url;
        request.info_hash_count = response.row_count;
        for (int i = 0; i < request.info_hash_count; ++i)
        {
            request.info_hash[i] = response.rows[i].info_hash;
        }
        return request;
    }

    [[nodiscard]] static auto buildSimpleScrapeRequestAndResponse()
    {
        auto response = tr_scrape_response{};
        response.did_connect = true;
        response.did_timeout = false;
        response.row_count = 1;
        response.rows[0].info_hash = tr_rand_obj<tr_sha1_digest_t>();
        response.rows[0].seeders = 1;
        response.rows[0].leechers = 2;
        response.rows[0].downloads = 3;
        response.rows[0].downloaders = 0;
        response.scrape_url = DefaultScrapeUrl;
        response.min_request_interval = 0;

        return std::make_pair(buildScrapeRequestFromResponse(response), response);
    }

    [[nodiscard]] static auto parseScrapeRequest(libtransmission::Buffer& buf, uint64_t expected_connection_id)
    {
        EXPECT_EQ(expected_connection_id, buf.to_uint64());
        EXPECT_EQ(ScrapeAction, buf.to_uint32());
        auto const transaction_id = buf.to_uint32();
        auto info_hashes = std::vector<tr_sha1_digest_t>{};
        while (!std::empty(buf))
        {
            auto tmp = tr_sha1_digest_t{};
            buf.to_buf(std::data(tmp), std::size(tmp));
            info_hashes.emplace_back(tmp);
        }
        return std::make_pair(transaction_id, info_hashes);
    }

    [[nodiscard]] static auto waitForAnnouncerToSendMessage(MockMediator& mediator)
    {
        libtransmission::test::waitFor(mediator.eventBase(), [&mediator]() { return !std::empty(mediator.sent_); });
        auto buf = libtransmission::Buffer(mediator.sent_.back().buf_);
        mediator.sent_.pop_back();
        return buf;
    }

    [[nodiscard]] static bool sendError(tr_announcer_udp& announcer, uint32_t transaction_id, std::string_view errmsg)
    {
        auto buf = libtransmission::Buffer{};
        buf.add_uint32(ErrorAction);
        buf.add_uint32(transaction_id);
        buf.add(errmsg);

        auto const response_size = std::size(buf);
        auto arr = std::array<uint8_t, 256>{};
        buf.to_buf(std::data(arr), response_size);

        return announcer.handleMessage(std::data(arr), response_size);
    }

    [[nodiscard]] static auto sendConnectionResponse(tr_announcer_udp& announcer, uint32_t transaction_id)
    {
        auto const connection_id = tr_rand_obj<uint64_t>();
        auto buf = libtransmission::Buffer{};
        buf.add_uint32(ConnectAction);
        buf.add_uint32(transaction_id);
        buf.add_uint64(connection_id);

        auto arr = std::array<uint8_t, 128>{};
        auto response_size = std::size(buf);
        buf.to_buf(std::data(arr), response_size);
        EXPECT_TRUE(announcer.handleMessage(std::data(arr), response_size));

        return connection_id;
    }

    struct UdpAnnounceReq
    {
        uint64_t connection_id = 0;
        uint32_t action = 0; // 1: announce
        uint32_t transaction_id = 0;
        tr_sha1_digest_t info_hash = {};
        tr_peer_id_t peer_id = {};
        uint64_t downloaded = 0;
        uint64_t left = 0;
        uint64_t uploaded = 0;
        uint32_t event = 0; // 0: none; 1: completed; 2: started; 3: stopped
        uint32_t ip_address = 0;
        uint32_t key;
        uint32_t num_want = static_cast<uint32_t>(-1); // default
        uint16_t port;
    };

    static void expectEqual(tr_announce_request const& expected, UdpAnnounceReq const& actual)
    {
        EXPECT_EQ(AnnounceAction, actual.action);
        EXPECT_EQ(expected.info_hash, actual.info_hash);
        EXPECT_EQ(expected.peer_id, actual.peer_id);
        EXPECT_EQ(expected.down, actual.downloaded);
        EXPECT_EQ(expected.leftUntilComplete, actual.left);
        EXPECT_EQ(expected.up, actual.uploaded);
        // EXPECT_EQ(foo, actual.event); ; // 0: none; 1: completed; 2: started; 3: stopped // FIXME
        // EXPECT_EQ(foo, actual.ip_address); // FIXME
        EXPECT_EQ(expected.key, static_cast<int>(actual.key));
        EXPECT_EQ(expected.numwant, static_cast<int>(actual.num_want));
        EXPECT_EQ(expected.port.host(), actual.port);
    }

    static void expectEqual(tr_announce_response const& expected, tr_announce_response const& actual)
    {
        EXPECT_EQ(actual.info_hash, expected.info_hash);
        EXPECT_EQ(actual.did_connect, expected.did_connect);
        EXPECT_EQ(actual.did_timeout, expected.did_timeout);
        EXPECT_EQ(actual.interval, expected.interval);
        EXPECT_EQ(actual.min_interval, expected.min_interval);
        EXPECT_EQ(actual.seeders, expected.seeders);
        EXPECT_EQ(actual.leechers, expected.leechers);
        EXPECT_EQ(actual.downloads, expected.downloads);
        EXPECT_EQ(actual.pex, expected.pex);
        EXPECT_EQ(actual.pex6, expected.pex6);
        EXPECT_EQ(actual.errmsg, expected.errmsg);
        EXPECT_EQ(actual.warning, expected.warning);
        EXPECT_EQ(actual.tracker_id, expected.tracker_id);
        EXPECT_EQ(actual.external_ip, expected.external_ip);
    }

    [[nodiscard]] static auto parseAnnounceRequest(libtransmission::Buffer& buf, uint64_t connection_id)
    {
        auto req = UdpAnnounceReq{};
        req.connection_id = buf.to_uint64();
        req.action = buf.to_uint32();
        req.transaction_id = buf.to_uint32();
        buf.to_buf(std::data(req.info_hash), std::size(req.info_hash));
        buf.to_buf(std::data(req.peer_id), std::size(req.peer_id));
        req.downloaded = buf.to_uint64();
        req.left = buf.to_uint64();
        req.uploaded = buf.to_uint64();
        req.event = buf.to_uint32();
        req.ip_address = buf.to_uint32();
        req.key = buf.to_uint32();
        req.num_want = buf.to_uint32();
        req.port = buf.to_uint16();

        EXPECT_EQ(AnnounceAction, req.action);
        EXPECT_EQ(connection_id, req.connection_id);

        return req;
    }

    // emulate the upkeep timer that tr_announcer runs in production
    static auto createUpkeepTimer(MockMediator& mediator, std::unique_ptr<tr_announcer_udp>& announcer)
    {
        auto timer_maker = libtransmission::EvTimerMaker{ mediator.eventBase() };
        auto timer = timer_maker.create();
        timer->setCallback([&announcer]() { announcer->upkeep(); });
        timer->startRepeating(200ms);
        return timer;
    }

    // https://www.bittorrent.org/beps/bep_0015.html
    static auto constexpr ProtocolId = uint64_t{ 0x41727101980ULL };
    static auto constexpr ConnectAction = uint32_t{ 0 };
    static auto constexpr AnnounceAction = uint32_t{ 1 };
    static auto constexpr ScrapeAction = uint32_t{ 2 };
    static auto constexpr ErrorAction = uint32_t{ 3 };

    static auto constexpr DefaultScrapeUrl = "https://127.0.0.1/scrape"sv;
};

TEST_F(AnnouncerUdpTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);
}

TEST_F(AnnouncerUdpTest, canScrape)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    // tell announcer to scrape
    auto [request, expected_response] = buildSimpleScrapeRequestAndResponse();
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto connect_transaction_id = parseConnectionRequest(sent);

    // Have the tracker respond to the request
    auto const connection_id = sendConnectionResponse(*announcer, connect_transaction_id);

    // The announcer should have sent a UDP scrape request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    auto [scrape_transaction_id, info_hashes] = parseScrapeRequest(sent, connection_id);
    expectEqual(request, info_hashes);

    // Have the tracker respond to the request
    auto buf = libtransmission::Buffer{};
    buf.add_uint32(ScrapeAction);
    buf.add_uint32(scrape_transaction_id);
    buf.add_uint32(expected_response.rows[0].seeders);
    buf.add_uint32(expected_response.rows[0].downloads);
    buf.add_uint32(expected_response.rows[0].leechers);
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.to_buf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // confirm that announcer processed the response
    EXPECT_TRUE(response.has_value());
    assert(response.has_value());
    expectEqual(expected_response, *response);

    // Now scrape again.
    // Since the timestamp hasn't changed, the connection should be good
    // and announcer-udp should skip the `connect` step, going straight to the scrape.
    response.reset();
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    std::tie(scrape_transaction_id, info_hashes) = parseScrapeRequest(sent, connection_id);
    expectEqual(request, info_hashes);
}

TEST_F(AnnouncerUdpTest, canDestructCleanlyEvenWhenBusy)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    // tell announcer to scrape
    auto [request, expected_response] = buildSimpleScrapeRequestAndResponse();
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto const connect_transaction_id = parseConnectionRequest(sent);
    EXPECT_NE(0U, connect_transaction_id);

    // now just end the test before responding to the request.
    // the announcer and mediator will go out-of-scope & be destroyed.
}

TEST_F(AnnouncerUdpTest, canMultiScrape)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 2;
    expected_response.rows[0] = { tr_rand_obj<tr_sha1_digest_t>(), 1, 2, 3, 0 };
    expected_response.rows[1] = { tr_rand_obj<tr_sha1_digest_t>(), 4, 5, 6, 0 };
    expected_response.scrape_url = DefaultScrapeUrl;
    expected_response.min_request_interval = 0;

    auto request = buildScrapeRequestFromResponse(expected_response);
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // Announcer will request a connection. Verify and grant the request
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto connect_transaction_id = parseConnectionRequest(sent);
    auto const connection_id = sendConnectionResponse(*announcer, connect_transaction_id);

    // The announcer should have sent a UDP scrape request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    auto [scrape_transaction_id, info_hashes] = parseScrapeRequest(sent, connection_id);
    expectEqual(request, info_hashes);

    // Have the tracker respond to the request
    auto buf = libtransmission::Buffer{};
    buf.add_uint32(ScrapeAction);
    buf.add_uint32(scrape_transaction_id);
    for (int i = 0; i < expected_response.row_count; ++i)
    {
        buf.add_uint32(expected_response.rows[i].seeders);
        buf.add_uint32(expected_response.rows[i].downloads);
        buf.add_uint32(expected_response.rows[i].leechers);
    }
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.to_buf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // Confirm that announcer processed the response
    EXPECT_TRUE(response.has_value());
    assert(response.has_value());
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleScrapeError)
{
    // build the expected response
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = tr_rand_obj<tr_sha1_digest_t>();
    expected_response.rows[0].seeders = -1;
    expected_response.rows[0].leechers = -1;
    expected_response.rows[0].downloads = -1;
    expected_response.rows[0].downloaders = 0;
    expected_response.scrape_url = DefaultScrapeUrl;
    expected_response.min_request_interval = 0;
    expected_response.errmsg = "Unrecognized info-hash";

    // build the request
    auto request = buildScrapeRequestFromResponse(expected_response);

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    // tell announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto connect_transaction_id = parseConnectionRequest(sent);

    // Have the tracker respond to the request
    auto const connection_id = sendConnectionResponse(*announcer, connect_transaction_id);

    // The announcer should have sent a UDP scrape request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    auto const [scrape_transaction_id, info_hashes] = parseScrapeRequest(sent, connection_id);

    // Have the tracker respond to the request with an "unable to scrape" error
    EXPECT_TRUE(sendError(*announcer, scrape_transaction_id, expected_response.errmsg));

    // confirm that announcer processed the response
    EXPECT_TRUE(response.has_value());
    assert(response.has_value());
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleConnectError)
{
    // build the response we'd expect for a connect failure
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = tr_rand_obj<tr_sha1_digest_t>();
    expected_response.rows[0].seeders = -1; // -1 here & on next lines means error
    expected_response.rows[0].leechers = -1;
    expected_response.rows[0].downloads = -1;
    expected_response.rows[0].downloaders = 0;
    expected_response.scrape_url = DefaultScrapeUrl;
    expected_response.min_request_interval = 0;
    expected_response.errmsg = "Unable to Connect";

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    // tell the announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        buildScrapeRequestFromResponse(expected_response),
        [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // Have the tracker respond to the request with an "unable to connect" error
    EXPECT_TRUE(sendError(*announcer, transaction_id, expected_response.errmsg));

    // Confirm that announcer processed the response
    EXPECT_TRUE(response.has_value());
    assert(response.has_value());
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, handleMessageReturnsFalseOnInvalidMessage)
{
    // build a simple scrape request
    auto request = tr_scrape_request{};
    request.scrape_url = DefaultScrapeUrl;
    request.info_hash_count = 1;
    request.info_hash[0] = tr_rand_obj<tr_sha1_digest_t>();

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    // tell the announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(request, [&response](tr_scrape_response const& resp) { response = resp; });

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // send a connection response but with an *invalid* transaction id
    auto buf = libtransmission::Buffer{};
    buf.add_uint32(ConnectAction);
    buf.add_uint32(transaction_id + 1);
    buf.add_uint64(tr_rand_obj<uint64_t>());
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.to_buf(std::data(arr), response_size);
    EXPECT_FALSE(announcer->handleMessage(std::data(arr), response_size));

    // send a connection response but with an *invalid* action
    buf.clear();
    buf.add_uint32(ScrapeAction);
    buf.add_uint32(transaction_id);
    buf.add_uint64(tr_rand_obj<uint64_t>());
    response_size = std::size(buf);
    buf.to_buf(std::data(arr), response_size);
    EXPECT_FALSE(announcer->handleMessage(std::data(arr), response_size));

    // but after discarding invalid messages,
    // a valid connection response should still work
    auto const connection_id = sendConnectionResponse(*announcer, transaction_id);
    EXPECT_NE(0, connection_id);
}

TEST_F(AnnouncerUdpTest, canAnnounce)
{
    static auto constexpr Interval = uint32_t{ 3600 };
    static auto constexpr Leechers = uint32_t{ 10 };
    static auto constexpr Seeders = uint32_t{ 20 };
    auto const addresses = std::array<std::pair<tr_address, tr_port>, 3>{
        std::make_pair(tr_address::from_string("10.10.10.5").value_or(tr_address{}), tr_port::fromHost(128)),
        std::make_pair(tr_address::from_string("192.168.1.2").value_or(tr_address{}), tr_port::fromHost(2021)),
        std::make_pair(tr_address::from_string("192.168.1.3").value_or(tr_address{}), tr_port::fromHost(2022)),
    };

    auto request = tr_announce_request{};
    request.event = TR_ANNOUNCE_EVENT_STARTED;
    request.port = tr_port::fromHost(80);
    request.key = 0xCAFE;
    request.numwant = 20;
    request.up = 1;
    request.down = 2;
    request.corrupt = 3;
    request.leftUntilComplete = 100;
    request.announce_url = "https://127.0.0.1/announce";
    request.tracker_id = "fnord";
    request.peer_id = tr_peerIdInit();
    request.info_hash = tr_rand_obj<tr_sha1_digest_t>();

    auto expected_response = tr_announce_response{};
    expected_response.info_hash = request.info_hash;
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.interval = Interval;
    expected_response.min_interval = 0; // not specified in UDP announce
    expected_response.seeders = Seeders;
    expected_response.leechers = Leechers;
    expected_response.downloads = -1; // not specified in UDP announce
    expected_response.pex = std::vector<tr_pex>{ tr_pex{ addresses[0].first, addresses[0].second },
                                                 tr_pex{ addresses[1].first, addresses[1].second },
                                                 tr_pex{ addresses[2].first, addresses[2].second } };
    expected_response.pex6 = {};
    expected_response.errmsg = {};
    expected_response.warning = {};
    expected_response.tracker_id = {}; // not specified in UDP announce
    expected_response.external_ip = {};

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    auto upkeep_timer = createUpkeepTimer(mediator, announcer);

    auto response = std::optional<tr_announce_response>{};
    announcer->announce(request, [&response](tr_announce_response const& resp) { response = resp; });

    // Announcer will request a connection. Verify and grant the request
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto connect_transaction_id = parseConnectionRequest(sent);
    auto const connection_id = sendConnectionResponse(*announcer, connect_transaction_id);

    // The announcer should have sent a UDP announce request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    auto udp_ann_req = parseAnnounceRequest(sent, connection_id);
    expectEqual(request, udp_ann_req);

    // Have the tracker respond to the request
    auto buf = libtransmission::Buffer{};
    buf.add_uint32(AnnounceAction);
    buf.add_uint32(udp_ann_req.transaction_id);
    buf.add_uint32(expected_response.interval);
    buf.add_uint32(expected_response.leechers);
    buf.add_uint32(expected_response.seeders);
    for (auto const& [addr, port] : addresses)
    {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
        buf.add(&addr.addr.addr4.s_addr, sizeof(addr.addr.addr4.s_addr));
        buf.add_uint16(port.host());
    }

    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 512>{};
    buf.to_buf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // Confirm that announcer processed the response
    EXPECT_TRUE(response.has_value());
    assert(response.has_value());
    expectEqual(expected_response, *response);
}
