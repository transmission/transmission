// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> // for std::memcpy()
#include <deque>
#include <memory>
#include <vector>

#include <fmt/format.h>

#include "transmission.h"

#include "announcer.h"
#include "tr-buffer.h"
#include "dns.h"
#include "crypto-utils.h"

#include "test-fixtures.h"

using namespace std::literals;

class AnnouncerUdpTest : public ::testing::Test
{
private:
    void SetUp() override
    {
        ::testing::Test::SetUp();
        tr_timeUpdate(time(nullptr));
    }

protected:
    class MockDns final : public libtransmission::Dns
    {
    public:
        ~MockDns() override = default;

        [[nodiscard]] std::optional<std::pair<struct sockaddr const*, socklen_t>> cached(
            std::string_view /*address*/,
            Hints /*hints*/ = {}) const override
        {
            return {};
        }

        Tag lookup(std::string_view address, Callback&& callback, Hints /*hints*/) override
        {
            auto const addr = tr_address::fromString(address); // mock has no actual DNS, just parsing e.g. inet_pton
            auto [ss, sslen] = addr->toSockaddr(Port);
            callback(reinterpret_cast<sockaddr const*>(&ss), sslen, tr_time() + 3600); // 1hr ttl
            return {};
        }

        void cancel(Tag /*tag*/) override
        {
        }

        static auto constexpr Port = tr_port::fromHost(443);
    };

    class MockMediator final : public tr_announcer_udp::Mediator
    {
    public:
        MockMediator()
            : event_base_{ event_base_new(), event_base_free }
        // , dns_{ event_base_.get(), tr_time }
        {
        }

        void sendto(void const* buf, size_t buflen, sockaddr const* sa, socklen_t salen) override
        {
            auto target = tr_address::fromSockaddr(sa);
            ASSERT_TRUE(target);
            // auto const [addr, port] = *target;
            // fmt::print("sending {:d} bytes to {:s}\n", buflen, addr.readable(port));
            sent_.emplace_back(static_cast<char const*>(buf), buflen, sa, salen);
        }

        [[nodiscard]] auto* eventBase()
        {
            return event_base_.get();
        }

        [[nodiscard]] libtransmission::Dns& dns() override
        {
            return dns_;
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

        MockDns dns_;
        // depends-on: event_base_
        // libtransmission::EvDns dns_;
    };

    static void expectEqual(tr_scrape_response const& expected, tr_scrape_response const& actual)
    {
        EXPECT_EQ(expected.did_connect, actual.did_connect);
        EXPECT_EQ(expected.did_timeout, actual.did_timeout);
        EXPECT_EQ(expected.errmsg, actual.errmsg);
        EXPECT_EQ(expected.min_request_interval, actual.min_request_interval);
        EXPECT_EQ(expected.scrape_url, actual.scrape_url);

        EXPECT_EQ(expected.row_count, actual.row_count);
        for (size_t i = 0; i < std::min(expected.row_count, actual.row_count); ++i)
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
        for (size_t i = 0; i < std::min(size_t(expected.info_hash_count), std::size(actual)); ++i)
        {
            EXPECT_EQ(expected.info_hash[i], actual[i]);
        }
    }

    template<typename T>
    [[nodiscard]] static auto randomFilled()
    {
        auto tmp = T{};
        tr_rand_buffer(&tmp, sizeof(tmp));
        return tmp;
    }

    [[nodiscard]] static uint32_t parseConnectionRequest(libtransmission::Buffer& buf)
    {
        EXPECT_EQ(ProtocolId, buf.toUint64());
        EXPECT_EQ(ConnectAction, buf.toUint32());
        return buf.toUint32();
    }

    [[nodiscard]] static auto buildScrapeRequestFromResponse(tr_scrape_response const& response)
    {
        auto request = tr_scrape_request{};
        request.scrape_url = response.scrape_url;
        request.info_hash_count = response.row_count;
        for (size_t i = 0; i < request.info_hash_count; ++i)
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
        response.rows[0].info_hash = randomFilled<tr_sha1_digest_t>();
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
        EXPECT_EQ(expected_connection_id, buf.toUint64());
        EXPECT_EQ(ScrapeAction, buf.toUint32());
        auto const transaction_id = buf.toUint32();
        auto info_hashes = std::vector<tr_sha1_digest_t>{};
        while (!std::empty(buf))
        {
            auto tmp = tr_sha1_digest_t{};
            buf.toBuf(std::data(tmp), std::size(tmp));
            info_hashes.emplace_back(tmp);
        }
        return std::make_pair(transaction_id, info_hashes);
    }

    [[nodiscard]] static auto waitForAnnouncerToSendMessage(MockMediator& mediator)
    {
        EXPECT_FALSE(std::empty(mediator.sent_));
        libtransmission::test::waitFor(mediator.eventBase(), [&mediator]() { return !std::empty(mediator.sent_); });
        auto buf = libtransmission::Buffer(mediator.sent_.back().buf_);
        mediator.sent_.pop_back();
        return buf;
    }

    [[nodiscard]] static bool sendError(tr_announcer_udp& announcer, uint32_t transaction_id, std::string_view errmsg)
    {
        auto buf = libtransmission::Buffer{};
        buf.addUint32(ErrorAction);
        buf.addUint32(transaction_id);
        buf.add(errmsg);

        auto const response_size = std::size(buf);
        auto arr = std::array<uint8_t, 256>{};
        buf.toBuf(std::data(arr), response_size);

        return announcer.handleMessage(std::data(arr), response_size);
    }

    [[nodiscard]] static auto sendConnectionResponse(tr_announcer_udp& announcer, uint32_t transaction_id)
    {
        auto const connection_id = randomFilled<uint64_t>();
        auto buf = libtransmission::Buffer{};
        buf.addUint32(ConnectAction);
        buf.addUint32(transaction_id);
        buf.addUint64(connection_id);

        auto arr = std::array<uint8_t, 128>{};
        auto response_size = std::size(buf);
        buf.toBuf(std::data(arr), response_size);
        EXPECT_TRUE(announcer.handleMessage(std::data(arr), response_size));

        return connection_id;
    }

    // https://www.bittorrent.org/beps/bep_0015.html
    static auto constexpr ProtocolId = uint64_t{ 0x41727101980ULL };
    static auto constexpr ConnectAction = uint32_t{ 0 };
    // static auto constexpr AnnounceAction = uint32_t{ 1 };
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

    // tell announcer to scrape
    auto [request, expected_response] = buildSimpleScrapeRequestAndResponse();
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

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
    buf.addUint32(ScrapeAction);
    buf.addUint32(scrape_transaction_id);
    buf.addUint32(expected_response.rows[0].seeders);
    buf.addUint32(expected_response.rows[0].downloads);
    buf.addUint32(expected_response.rows[0].leechers);
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.toBuf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);

    // Now scrape again.
    // Since the timestamp hasn't changed, the connection should be good
    // and announcer-udp should skip the `connect` step, going straight to the scrape.
    response.reset();
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    std::tie(scrape_transaction_id, info_hashes) = parseScrapeRequest(sent, connection_id);
    expectEqual(request, info_hashes);
}

TEST_F(AnnouncerUdpTest, canMultiScrape)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);

    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 2;
    expected_response.rows[0] = { randomFilled<tr_sha1_digest_t>(), 1, 2, 3, 0 };
    expected_response.rows[1] = { randomFilled<tr_sha1_digest_t>(), 4, 5, 6, 0 };
    expected_response.scrape_url = DefaultScrapeUrl;
    expected_response.min_request_interval = 0;

    auto request = buildScrapeRequestFromResponse(expected_response);
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

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
    buf.addUint32(ScrapeAction);
    buf.addUint32(scrape_transaction_id);
    for (size_t i = 0; i < expected_response.row_count; ++i)
    {
        buf.addUint32(expected_response.rows[i].seeders);
        buf.addUint32(expected_response.rows[i].downloads);
        buf.addUint32(expected_response.rows[i].leechers);
    }
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.toBuf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // Confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleScrapeError)
{
    // build the expected reponse
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = randomFilled<tr_sha1_digest_t>();
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

    // tell announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

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
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleConnectError)
{
    // build the response we'd expect for a connect failure
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = randomFilled<tr_sha1_digest_t>();
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

    // tell the announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        buildScrapeRequestFromResponse(expected_response),
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // Have the tracker respond to the request with an "unable to connect" error
    EXPECT_TRUE(sendError(*announcer, transaction_id, expected_response.errmsg));

    // Confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, handleMessageReturnsFalseOnInvalidMessage)
{
    // build a simple scrape request
    auto request = tr_scrape_request{};
    request.scrape_url = DefaultScrapeUrl;
    request.info_hash_count = 1;
    request.info_hash[0] = randomFilled<tr_sha1_digest_t>();

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);

    // tell the announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // The announcer should have sent a UDP connection request.
    // Inspect that request for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // send a connection response but with an *invalid* transaction id
    auto buf = libtransmission::Buffer{};
    buf.addUint32(ConnectAction);
    buf.addUint32(transaction_id + 1);
    buf.addUint64(randomFilled<uint64_t>());
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 256>{};
    buf.toBuf(std::data(arr), response_size);
    EXPECT_FALSE(announcer->handleMessage(std::data(arr), response_size));

    // send a connection response but with an *invalid* action
    buf.clear();
    buf.addUint32(ScrapeAction);
    buf.addUint32(transaction_id);
    buf.addUint64(randomFilled<uint64_t>());
    response_size = std::size(buf);
    buf.toBuf(std::data(arr), response_size);
    EXPECT_FALSE(announcer->handleMessage(std::data(arr), response_size));

    // but after discarding invalid messages,
    // a valid connection response should still work
    auto const connection_id = sendConnectionResponse(*announcer, transaction_id);
    EXPECT_NE(0, connection_id);
}

TEST_F(AnnouncerUdpTest, canAnnounce)
{

#if 0
Offset  Size    Name    Value
0       64-bit integer  connection_id
8       32-bit integer  action          1 // announce
12      32-bit integer  transaction_id
16      20-byte string  info_hash
36      20-byte string  peer_id
56      64-bit integer  downloaded
64      64-bit integer  left
72      64-bit integer  uploaded
80      32-bit integer  event           0 // 0: none; 1: completed; 2: started; 3: stopped
84      32-bit integer  IP address      0 // default
88      32-bit integer  key
92      32-bit integer  num_want        -1 // default
96      16-bit integer  port
98
#endif
}

TEST_F(AnnouncerUdpTest, announceUsesIPAddress)
{
}

#if 0
class tr_announcer_udp
{
public:
    class Mediator
    {
    public:
        virtual ~Mediator() noexcept = default;
        virtual void sendto(void const* buf, size_t buflen, struct sockaddr const* addr, size_t addrlen) = 0;
        [[nodiscard]] virtual libtransmission::Dns& dns() = 0;
        [[nodiscard]] virtual std::optional<tr_address> announceIP() const = 0;
    };

    virtual ~tr_announcer_udp() noexcept = default;

    [[nodiscard]] static std::unique_ptr<tr_announcer_udp> create(Mediator&);

    [[nodiscard]] virtual bool isIdle() const noexcept = 0;

    virtual void announce(tr_announce_request const& request, tr_announce_response_func response_func, void* user_data) = 0;

    virtual void scrape(tr_scrape_request const& request, tr_scrape_response_func response_func, void* user_data) = 0;

    virtual void upkeep() = 0;

    virtual void startShutdown() = 0;

    // @brief process an incoming udp message if it's a tracker response.
    // @return true if msg was a tracker response; false otherwise
    virtual bool handleMessage(uint8_t const* msg, size_t msglen) = 0;
};


struct tr_announce_request
{
    tr_announce_event event = {};
    bool partial_seed = false;

    /* the port we listen for incoming peers on */
    tr_port port;

    /* per-session key */
    int key = 0;

    /* the number of peers we'd like to get back in the response */
    int numwant = 0;

    /* the number of bytes we uploaded since the last 'started' event */
    uint64_t up = 0;

    /* the number of good bytes we downloaded since the last 'started' event */
    uint64_t down = 0;

    /* the number of bad bytes we downloaded since the last 'started' event */
    uint64_t corrupt = 0;

    /* the total size of the torrent minus the number of bytes completed */
    uint64_t leftUntilComplete = 0;

    /* the tracker's announce URL */
    tr_interned_string announce_url;

    /* key generated by and returned from an http tracker.
     * see tr_announce_response.tracker_id_str */
    std::string tracker_id;

    /* the torrent's peer id.
     * this changes when a torrent is stopped -> restarted. */
    tr_peer_id_t peer_id;

    /* the torrent's info_hash */
    tr_sha1_digest_t info_hash;

    /* the name to use when deep logging is enabled */
    char log_name[128];
};

struct tr_announce_response
{
    /* the torrent's info hash */
    tr_sha1_digest_t info_hash = {};

    /* whether or not we managed to connect to the tracker */
    bool did_connect = false;

    /* whether or not the scrape timed out */
    bool did_timeout = false;

    /* preferred interval between announces.
     * transmission treats this as the interval for periodic announces */
    int interval = 0;

    /* minimum interval between announces. (optional)
     * transmission treats this as the min interval for manual announces */
    int min_interval = 0;

    /* how many peers are seeding this torrent */
    int seeders = -1;

    /* how many peers are downloading this torrent */
    int leechers = -1;

    /* how many times this torrent has been downloaded */
    int downloads = -1;

    /* IPv4 peers that we acquired from the tracker */
    std::vector<tr_pex> pex;

    /* IPv6 peers that we acquired from the tracker */
    std::vector<tr_pex> pex6;

    /* human-readable error string on failure, or nullptr */
    std::string errmsg;

    /* human-readable warning string or nullptr */
    std::string warning;

    /* key generated by and returned from an http tracker.
     * if this is provided, subsequent http announces must include this. */
    std::string tracker_id;

    /* tracker extension that returns the client's public IP address.
     * https://www.bittorrent.org/beps/bep_0024.html */
    std::optional<tr_address> external_ip;
};

using tr_announce_response_func = void (*)(tr_announce_response const* response, void* userdata);
#endif
