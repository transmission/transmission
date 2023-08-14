// This file Copyright (C) 2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <chrono>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/crypto-utils.h> // tr_rand_obj()
#include <libtransmission/session.h>
#include <libtransmission/tr-lpd.h>

#include "test-fixtures.h"

using namespace std::literals;

namespace libtransmission::test
{

using LpdTest = SessionTest;

namespace
{

class MyMediator final : public tr_lpd::Mediator
{
public:
    explicit MyMediator(tr_session& session)
        : session_{ session }
    {
    }

    [[nodiscard]] tr_address bind_address(tr_address_type /* type */) const override
    {
        return {};
    }

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

    [[nodiscard]] libtransmission::TimerMaker& timerMaker() override
    {
        return session_.timerMaker();
    }

    void setNextAnnounceTime(std::string_view info_hash_str, time_t announce_after) override
    {
        for (auto& tor : torrents_)
        {
            if (tor.info_hash_str == info_hash_str)
            {
                tor.announce_after = announce_after;
                break;
            }
        }
    }

    bool onPeerFound(std::string_view info_hash_str, tr_address /*address*/, tr_port /*port*/) override
    {
        found_.insert(std::string{ info_hash_str });
        return found_returns_;
    }

    tr_session& session_;
    tr_port port_ = tr_port::fromHost(51413);
    bool allows_lpd_ = true;
    std::vector<TorrentInfo> torrents_;
    std::set<std::string> found_;
    bool found_returns_ = true;
};

auto makeRandomHash()
{
    return tr_sha1::digest(tr_rand_obj<std::array<char, 256>>());
}

auto makeRandomHashString()
{
    return tr_strupper(tr_sha1_to_string(makeRandomHash()));
}

} // namespace

TEST_F(LpdTest, HelloWorld)
{
    auto mediator = MyMediator{ *session_ };
    auto lpd = tr_lpd::create(mediator, session_->eventBase());
    EXPECT_TRUE(lpd);
    EXPECT_EQ(0U, std::size(mediator.found_));
}

TEST_F(LpdTest, DISABLED_CanAnnounceAndRead)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->eventBase());
    EXPECT_TRUE(lpd_a);

    auto const info_hash_str = makeRandomHashString();
    auto info = tr_lpd::Mediator::TorrentInfo{};
    info.info_hash_str = info_hash_str;
    info.activity = TR_STATUS_SEED;
    info.allows_lpd = true;
    info.announce_after = 0; // never announced

    auto mediator_b = MyMediator{ *session_ };
    mediator_b.torrents_.push_back(info);
    auto lpd_b = tr_lpd::create(mediator_b, session_->eventBase());

    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);
    EXPECT_EQ(1U, mediator_a.found_.count(info_hash_str));
    EXPECT_EQ(0U, mediator_b.found_.count(info_hash_str));
}

TEST_F(LpdTest, DISABLED_canMultiAnnounce)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->eventBase());
    EXPECT_TRUE(lpd_a);

    auto info_hash_strings = std::array<std::string, 2>{};
    auto infos = std::array<tr_lpd::Mediator::TorrentInfo, 2>{};
    auto mediator_b = MyMediator{ *session_ };
    for (size_t i = 0; i < std::size(info_hash_strings); ++i)
    {
        auto& info_hash_string = info_hash_strings[i];
        auto& info = infos[i];

        info_hash_string = makeRandomHashString();

        info.info_hash_str = info_hash_string;
        info.activity = TR_STATUS_SEED;
        info.allows_lpd = true;
        info.announce_after = 0; // never announced
    }

    for (auto const& info : infos)
    {
        mediator_b.torrents_.push_back(info);
    }

    auto lpd_b = tr_lpd::create(mediator_b, session_->eventBase());
    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);

    for (auto const& info : infos)
    {
        EXPECT_EQ(1U, mediator_a.found_.count(std::string{ info.info_hash_str }));
    }
}

TEST_F(LpdTest, DISABLED_DoesNotReannounceTooSoon)
{
    auto mediator_a = MyMediator{ *session_ };
    auto lpd_a = tr_lpd::create(mediator_a, session_->eventBase());
    EXPECT_TRUE(lpd_a);

    // similar to canMultiAnnounce...
    auto info_hash_strings = std::array<std::string, 2>{};
    auto infos = std::array<tr_lpd::Mediator::TorrentInfo, 2>{};
    auto mediator_b = MyMediator{ *session_ };
    for (size_t i = 0; i < std::size(info_hash_strings); ++i)
    {
        auto& info_hash_string = info_hash_strings[i];
        auto& info = infos[i];

        info_hash_string = makeRandomHashString();

        info.info_hash_str = info_hash_string;
        info.activity = TR_STATUS_SEED;
        info.allows_lpd = true;
        info.announce_after = 0; // never announced
    }

    // ...except one torrent has already been announced
    // and doesn't need to be reannounced until later
    auto const now = time(nullptr);
    infos[0].announce_after = now + 60;

    for (auto const& info : infos)
    {
        mediator_b.torrents_.push_back(info);
    }

    auto lpd_b = tr_lpd::create(mediator_b, session_->eventBase());
    waitFor([&mediator_a]() { return !std::empty(mediator_a.found_); }, 1s);

    for (auto& info : infos)
    {
        auto const expected_count = info.announce_after <= now ? 1U : 0U;
        EXPECT_EQ(expected_count, mediator_a.found_.count(std::string{ info.info_hash_str }));
    }
}

} // namespace libtransmission::test
