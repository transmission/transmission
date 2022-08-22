// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>

#include "transmission.h"

#include "session.h"
#include "tr-lpd.h"

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission
{
namespace test
{

using LpdTest = SessionTest;

namespace
{

class MyMediator final : public tr_lpd::Mediator
{
public:
    MyMediator() = default;
    ~MyMediator() override = default;

    [[nodiscard]] tr_port port() const override
    {
        return port_;
    }

    [[nodiscard]] bool allowsLPD() const override
    {
        return allows_lpd_;
    }

    [[nodiscard]] std::vector<TorrentInfo> torrents() const override
    {
        return torrents_;
    }

    void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_at) override
    {
        for (auto& tor : torrents_)
        {
            if (tor.info_hash_str == info_hash_str)
            {
                tor.announce_at = announce_at;
                break;
            }
        }
    }

    bool onPeerFound(std::string_view info_hash_str, tr_address address, tr_port port) override
    {
        auto found = Found{};
        found.info_hash_str = info_hash_str;
        found.address = address;
        found.port = port;
        found_.emplace_back(found);
        return found_returns_;
    }

    tr_port port_ = tr_port::fromHost(51413);
    bool allows_lpd_ = true;
    std::vector<TorrentInfo> torrents_;
    bool found_returns_ = true;

    struct Found
    {
        std::string info_hash_str;
        tr_address address;
        tr_port port;
    };

    std::vector<Found> found_;
};

} // namespace

TEST_F(LpdTest, HelloWorld)
{
    auto mediator = MyMediator{};
    auto lpd = tr_lpd::create(mediator, session_->timerMaker(), session_->eventBase());
    EXPECT_TRUE(lpd);
    EXPECT_EQ(0U, std::size(mediator.found_));
}

TEST_F(LpdTest, CanAnnounceAndRead)
{
    auto mediator_a = MyMediator{};
    auto lpd_a = tr_lpd::create(mediator_a, session_->timerMaker(), session_->eventBase());
    EXPECT_TRUE(lpd_a);

    auto mediator_b = MyMediator{};
    auto tor = tr_lpd::Mediator::TorrentInfo{};
    tor.info_hash_str = "B26C81363AC1A236765385A702AEC107A49581B5"sv;
    tor.activity = TR_STATUS_SEED;
    tor.allows_lpd = true;
    tor.announce_at = 0;
    mediator_b.torrents_.push_back(tor);
    auto lpd_b = tr_lpd::create(mediator_b, session_->timerMaker(), session_->eventBase());

    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 10s);
    EXPECT_EQ(1U, std::size(mediator_a.found_));
    if (!std::empty(mediator_a.found_))
    {
        EXPECT_EQ(mediator_a.port_, mediator_a.found_.front().port);
        EXPECT_EQ(tor.info_hash_str, mediator_a.found_.front().info_hash_str);
    }
    EXPECT_EQ(0U, std::size(mediator_b.found_));
}

} // namespace test
} // namespace libtransmission
