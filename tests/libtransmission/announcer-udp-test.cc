// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstring> // for std::memcpy()
#include <deque>
#include <memory>
#include <vector>

#include "transmission.h"

#include "announcer.h"
#include "dns-ev.h"

#include "test-fixtures.h"

using namespace std::literals;
using AnnouncerUdpTest = ::testing::Test;

class MockMediator final : public tr_announcer_udp::Mediator
{
public:
    MockMediator()
        : event_base_{ event_base_new(), event_base_free }
        , dns_{ event_base_.get(), tr_time }
    {
    }

    void sendto(void const* buf, size_t buflen, sockaddr const* sa, socklen_t salen) override
    {
        sent_.emplace_back(static_cast<char const*>(buf), buflen, sa, salen);
    }

    [[nodiscard]] libtransmission::Dns& dns() override
    {
        return dns_;
    }

    [[nodiscard]] std::optional<tr_address> announceIP() const override
    {
        return {};
    }

private:
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

    // depends-on: event_base_
    libtransmission::EvDns dns_;
};

TEST_F(AnnouncerUdpTest, canInstantiate)
{
    auto mediator = MockMediator{};
    auto announcer = tr_announcer_udp::create(mediator);
    EXPECT_TRUE(announcer);
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
#endif
