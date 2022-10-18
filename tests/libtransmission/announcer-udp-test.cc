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
#include "dns.h"
#include "crypto-utils.h"

#include "test-fixtures.h"

using namespace std::literals;
using AnnouncerUdpTest = ::testing::Test;

class MockDns final : public libtransmission::Dns
{
public:
    ~MockDns() = default;

    [[nodiscard]] std::optional<std::pair<struct sockaddr const*, socklen_t>> cached(
        std::string_view /*address*/,
        Hints /*hints*/ = {}) const override
    {
        return {};
    }

    Tag lookup(std::string_view address, Callback&& callback, Hints /*hints*/) override
    {
        fmt::print("looking up {:s}\n", address);
        auto const addr = tr_address::fromString(address); // mock has no actual DNS, just parsing e.g. inet_pton
        auto [ss, sslen] = addr->toSockaddr(Port);
        fmt::print("got {} family {} sslen {}\n", addr->readable(Port), ss.ss_family, sslen);
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
        auto const [addr, port] = *target;
        fmt::print("sending {:d} bytes to {:s}\n", buflen, addr.readable(port));
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
            : buf_{ buf, buf + buflen }
            , sslen_{ salen }
        {
            std::memcpy(&ss_, sa, salen);
        }

        std::vector<unsigned char> buf_;
        sockaddr_storage ss_;
        socklen_t sslen_ = {};
    };

    std::deque<Sent> sent_;

    std::unique_ptr<event_base, void (*)(event_base*)> const event_base_;

    MockDns dns_;
    // depends-on: event_base_
    // libtransmission::EvDns dns_;
};

static auto constexpr ProtocolId = uint64_t{ 0x41727101980ULL };

TEST_F(AnnouncerUdpTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);
}

TEST_F(AnnouncerUdpTest, canScrape)
{
    static auto constexpr ScrapeUrl = "https://127.0.0.1/scrape"sv;
    static auto constexpr LogName = "test";

    auto info_hash = tr_sha1_digest_t{};
    tr_rand_buffer(std::data(info_hash), std::size(info_hash));

    // build the request
    auto request = tr_scrape_request{};
    request.scrape_url = ScrapeUrl;
    tr_strlcpy(request.log_name, LogName, sizeof(request.log_name));
    request.info_hash[0] = info_hash;
    request.info_hash_count = 1;

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
    libtransmission::test::waitFor(mediator.eventBase(), [&mediator]() { return !std::empty(mediator.sent_); });

    // announcer should be attempting to send a connect request.
    // inspect it for validity.
    auto sent = mediator.sent_.front();
    EXPECT_EQ(16U, sent.buf_.size());
    auto const* walk = std::data(sent.buf_);
    // first arg is a 64-bit magic number protocol id
    auto tmp64 = tr_htonll(ProtocolId);
    EXPECT_EQ(0, std::memcmp(&tmp64, walk, sizeof(tmp64)));
    walk += sizeof(tmp64);
    // second arg is a 32-bit action, where 0 is connect
    auto tmp32 = htonl(0);
    EXPECT_EQ(0, std::memcmp(&tmp32, walk, sizeof(tmp32)));
    walk += sizeof(tmp32);
    // third arg is a client-chosen transaction id
    walk += sizeof(tmp32);
    mediator.sent_.pop_front();
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
