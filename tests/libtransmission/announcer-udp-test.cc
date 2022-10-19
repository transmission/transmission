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

    // https://www.bittorrent.org/beps/bep_0015.html
    static auto constexpr ProtocolId = uint64_t{ 0x41727101980ULL };
    static auto constexpr ConnectAction = uint32_t{ 0 };
    // static auto constexpr AnnounceAction = uint32_t{ 1 };
    static auto constexpr ScrapeAction = uint32_t{ 2 };
    static auto constexpr ErrorAction = uint32_t{ 3 };
};

TEST_F(AnnouncerUdpTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);
}

TEST_F(AnnouncerUdpTest, canScrape)
{
    static auto constexpr ScrapeUrl = "https://127.0.0.1/scrape"sv;

    // build the expected reponse
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = randomFilled<tr_sha1_digest_t>();
    expected_response.rows[0].seeders = 1;
    expected_response.rows[0].leechers = 2;
    expected_response.rows[0].downloads = 3;
    expected_response.rows[0].downloaders = 0;
    expected_response.scrape_url = ScrapeUrl;
    expected_response.min_request_interval = 0;

    // build the request
    auto request = buildScrapeRequestFromResponse(expected_response);

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);

    // tell announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // announcer should be attempting to send a connect request.
    // inspect it for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto connect_transaction_id = parseConnectionRequest(sent);

    // send a connection response
    auto const connection_id = randomFilled<uint64_t>();
    auto buf = libtransmission::Buffer{};
    buf.addUint32(ConnectAction);
    buf.addUint32(connect_transaction_id);
    buf.addUint64(connection_id);
    auto response_size = std::size(buf);
    auto arr = std::array<uint8_t, 128>{};
    buf.toBuf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // announcer should now send a scrape request.
    // inspect it for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    auto const [scrape_transaction_id, info_hashes] = parseScrapeRequest(sent, connection_id);
    expectEqual(request, info_hashes);

    // send a scrape response
    buf.clear();
    buf.addUint32(ScrapeAction);
    buf.addUint32(scrape_transaction_id);
    buf.addUint32(expected_response.rows[0].seeders);
    buf.addUint32(expected_response.rows[0].downloads);
    buf.addUint32(expected_response.rows[0].leechers);
    response_size = std::size(buf);
    buf.toBuf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleScrapeError)
{
    static auto constexpr ScrapeUrl = "https://127.0.0.1/scrape"sv;

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
    expected_response.scrape_url = ScrapeUrl;
    expected_response.min_request_interval = 0;
    expected_response.errmsg = "Unrecognized info-hash";

    // build the request
    auto request = buildScrapeRequestFromResponse(expected_response);

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);

    // tell announcer to scrape
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        request,
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // announcer should be attempting to send a connect request.
    // inspect it for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // send a connection response
    auto const connection_id = randomFilled<uint64_t>();
    auto buf = libtransmission::Buffer{};
    buf.addUint32(ConnectAction);
    buf.addUint32(transaction_id);
    buf.addUint64(connection_id);
    auto response_size = std::size(buf);
    EXPECT_EQ(16, response_size);
    auto arr = std::array<uint8_t, 128>{};
    buf.toBuf(std::data(arr), response_size);
    EXPECT_TRUE(announcer->handleMessage(std::data(arr), response_size));

    // announcer should now send a scrape request.
    // inspect it for validity.
    sent = waitForAnnouncerToSendMessage(mediator);
    EXPECT_EQ(connection_id, sent.toUint64());
    EXPECT_EQ(ScrapeAction, sent.toUint32());
    transaction_id = sent.toUint32();
    auto tmp_hash = tr_sha1_digest_t{};
    sent.toBuf(std::data(tmp_hash), std::size(tmp_hash));
    EXPECT_EQ(request.info_hash[0], tmp_hash);

    // have the tracker send an "unable to scrape" error response
    EXPECT_TRUE(sendError(*announcer, transaction_id, expected_response.errmsg));

    // confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
}

TEST_F(AnnouncerUdpTest, canHandleConnectError)
{
    static auto constexpr ScrapeUrl = "https://127.0.0.1/scrape"sv;

    // build the response we'd expect for a connect failre:
    auto expected_response = tr_scrape_response{};
    expected_response.did_connect = true;
    expected_response.did_timeout = false;
    expected_response.row_count = 1;
    expected_response.rows[0].info_hash = randomFilled<tr_sha1_digest_t>();
    expected_response.rows[0].seeders = -1; // -1 here & on next lines means error
    expected_response.rows[0].leechers = -1;
    expected_response.rows[0].downloads = -1;
    expected_response.rows[0].downloaders = 0;
    expected_response.scrape_url = ScrapeUrl;
    expected_response.min_request_interval = 0;
    expected_response.errmsg = "Unable to Connect";

    // build the announcer
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);

    // give the announcer a scripe task
    auto response = std::optional<tr_scrape_response>{};
    announcer->scrape(
        buildScrapeRequestFromResponse(expected_response),
        [](tr_scrape_response const* resp, void* vresponse)
        { *static_cast<std::optional<tr_scrape_response>*>(vresponse) = *resp; },
        &response);

    // announcer should be attempting to send a connect request.
    // inspect it for validity.
    auto sent = waitForAnnouncerToSendMessage(mediator);
    auto transaction_id = parseConnectionRequest(sent);

    // have the tracker send an "unable to connect" error response
    EXPECT_TRUE(sendError(*announcer, transaction_id, expected_response.errmsg));

    // confirm that announcer processed the response
    EXPECT_TRUE(response);
    expectEqual(expected_response, *response);
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

/* pick a number small enough for common tracker software:
 *  - ocelot has no upper bound
 *  - opentracker has an upper bound of 64
 *  - udp protocol has an upper bound of 74
 *  - xbtt has no upper bound
 *
 * This is only an upper bound: if the tracker complains about
 * length, announcer will incrementally lower the batch size.
 */
auto inline constexpr TR_MULTISCRAPE_MAX = 60;

struct tr_scrape_request
{
    /* the scrape URL */
    tr_interned_string scrape_url;

    /* the name to use when deep logging is enabled */
    char log_name[128];

    /* info hashes of the torrents to scrape */
    std::array<tr_sha1_digest_t, TR_MULTISCRAPE_MAX> info_hash;

    /* how many hashes to use in the info_hash field */
    int info_hash_count = 0;
};

struct tr_scrape_response_row
{
    /* the torrent's info_hash */
    tr_sha1_digest_t info_hash;

    /* how many peers are seeding this torrent */
    int seeders = 0;

    /* how many peers are downloading this torrent */
    int leechers = 0;

    /* how many times this torrent has been downloaded */
    int downloads = 0;

    /* the number of active downloaders in the swarm.
     * this is a BEP 21 extension that some trackers won't support.
     * http://www.bittorrent.org/beps/bep_0021.html#tracker-scrapes  */
    int downloaders = 0;
};

struct tr_scrape_response
{
    /* whether or not we managed to connect to the tracker */
    bool did_connect = false;

    /* whether or not the scrape timed out */
    bool did_timeout = false;

    /* how many info hashes are in the 'rows' field */
    int row_count;

    /* the individual torrents' scrape results */
    std::array<tr_scrape_response_row, TR_MULTISCRAPE_MAX> rows;

    /* the raw scrape url */
    tr_interned_string scrape_url;

    /* human-readable error string on failure, or nullptr */
    std::string errmsg;

    /* minimum interval (in seconds) allowed between scrapes.
     * this is an unofficial extension that some trackers won't support. */
    int min_request_interval;
};

using tr_scrape_response_func = void (*)(tr_scrape_response const* response, void* user_data);

#endif
