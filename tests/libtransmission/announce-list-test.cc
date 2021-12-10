/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>
#include <set>

#include "transmission.h"

#include "announce-list.h"

#include "gtest/gtest.h"

using AnnounceListTest = ::testing::Test;
using namespace std::literals;

TEST_F(AnnounceListTest, canAdd)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Tier, Announce));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.full);
    EXPECT_EQ("https://example.org/scrape"sv, tracker.scrape.full);
    EXPECT_EQ(Tier, tracker.tier);
    EXPECT_EQ("example.org:443"sv, tracker.host);
}

TEST_F(AnnounceListTest, groupsSiblingsIntoSameTier)
{
    auto constexpr Tier1 = tr_tracker_tier_t{ 1 };
    auto constexpr Tier2 = tr_tracker_tier_t{ 2 };
    auto constexpr Tier3 = tr_tracker_tier_t{ 3 };
    auto constexpr Announce1 = "https://example.org/announce"sv;
    auto constexpr Announce2 = "http://example.org/announce"sv;
    auto constexpr Announce3 = "udp://example.org:999/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Tier1, Announce1));
    EXPECT_EQ(2, announce_list.add(Tier2, Announce2));
    EXPECT_EQ(3, announce_list.add(Tier3, Announce3));

    EXPECT_EQ(3, std::size(announce_list));
    EXPECT_EQ(Tier1, announce_list.at(0).tier);
    EXPECT_EQ(Tier1, announce_list.at(1).tier);
    EXPECT_EQ(Tier1, announce_list.at(2).tier);
    EXPECT_EQ(Announce1, announce_list.at(1).announce.full);
    EXPECT_EQ(Announce2, announce_list.at(0).announce.full);
    EXPECT_EQ(Announce3, announce_list.at(2).announce.full);
    EXPECT_EQ("example.org:443"sv, announce_list.at(1).host);
    EXPECT_EQ("example.org:80"sv, announce_list.at(0).host);
    EXPECT_EQ("example.org:999"sv, announce_list.at(2).host);
}

TEST_F(AnnounceListTest, canAddWithoutScrape)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/foo"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Tier, Announce));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.full);
    EXPECT_EQ(TR_KEY_NONE, tracker.scrape_interned);
    EXPECT_EQ(Tier, tracker.tier);
}

TEST_F(AnnounceListTest, canAddUdp)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "udp://example.org/"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Tier, Announce));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.full);
    EXPECT_EQ("udp://example.org/"sv, tracker.scrape.full);
    EXPECT_EQ(Tier, tracker.tier);
}

TEST_F(AnnounceListTest, canNotAddDuplicateAnnounce)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Tier, Announce));
    EXPECT_EQ(1, announce_list.size());
    EXPECT_EQ(1, announce_list.add(Tier, Announce));
    EXPECT_EQ(1, announce_list.size());

    auto constexpr Announce2 = "https://example.org:443/announce"sv;
    EXPECT_EQ(1, announce_list.add(Tier, Announce2));
    EXPECT_EQ(1, announce_list.size());
}

TEST_F(AnnounceListTest, canNotAddInvalidUrl)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "telnet://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(0, announce_list.add(Tier, Announce));
    EXPECT_EQ(0, announce_list.size());
}

TEST_F(AnnounceListTest, canSet)
{
    auto constexpr Urls = std::array<char const*, 3>{
        "https://www.example.com/a/announce",
        "https://www.example.com/b/announce",
        "https://www.example.com/c/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 3>{ 1, 2, 3 };

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(3, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(3, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(0).tier);
    EXPECT_EQ(Tiers[1], announce_list.at(1).tier);
    EXPECT_EQ(Tiers[2], announce_list.at(2).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.full);
    EXPECT_EQ(Urls[1], announce_list.at(1).announce.full);
    EXPECT_EQ(Urls[2], announce_list.at(2).announce.full);
}

TEST_F(AnnounceListTest, canSetUnsortedWithBackupsInTiers)
{
    auto constexpr Urls = std::array<char const*, 6>{
        "https://www.backup-a.com/announce",  "https://www.backup-b.com/announce",  "https://www.backup-c.com/announce",
        "https://www.primary-a.com/announce", "https://www.primary-b.com/announce", "https://www.primary-c.com/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 6>{ 0, 1, 2, 0, 1, 2 };

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(6, announce_list.set(std::data(Urls), std::data(Tiers), 6));
    EXPECT_EQ(6, announce_list.size());
    EXPECT_EQ(0, announce_list.at(0).tier);
    EXPECT_EQ(0, announce_list.at(1).tier);
    EXPECT_EQ(1, announce_list.at(2).tier);
    EXPECT_EQ(1, announce_list.at(3).tier);
    EXPECT_EQ(2, announce_list.at(4).tier);
    EXPECT_EQ(2, announce_list.at(5).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.full);
    EXPECT_EQ(Urls[3], announce_list.at(1).announce.full);
    EXPECT_EQ(Urls[1], announce_list.at(2).announce.full);
    EXPECT_EQ(Urls[4], announce_list.at(3).announce.full);
    EXPECT_EQ(Urls[2], announce_list.at(4).announce.full);
    EXPECT_EQ(Urls[5], announce_list.at(5).announce.full);

    // confirm that each has a unique id
    auto ids = std::set<tr_tracker_id_t>{};
    for (size_t i = 0, n = std::size(announce_list); i < n; ++i)
    {
        ids.insert(announce_list.at(i).id);
    }
    EXPECT_EQ(std::size(announce_list), std::size(ids));
}

TEST_F(AnnounceListTest, canSetExceptDuplicate)
{
    auto constexpr Urls = std::array<char const*, 3>{
        "https://www.example.com/a/announce",
        "https://www.example.com/b/announce",
        "https://www.example.com/b/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 3>{ 3, 2, 1 };

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(2, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(2, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(1).tier);
    EXPECT_EQ(Tiers[1], announce_list.at(0).tier);
    EXPECT_EQ(Urls[0], announce_list.at(1).announce.full);
    EXPECT_EQ(Urls[1], announce_list.at(0).announce.full);
}

TEST_F(AnnounceListTest, canSetExceptInvalid)
{
    auto constexpr Urls = std::array<char const*, 3>{
        "https://www.example.com/a/announce",
        "telnet://www.example.com/b/announce",
        "https://www.example.com/c/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 3>{ 1, 2, 3 };

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(2, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(2, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(0).tier);
    EXPECT_EQ(Tiers[2], announce_list.at(1).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.full);
    EXPECT_EQ(Urls[2], announce_list.at(1).announce.full);
}

TEST_F(AnnounceListTest, canRemoveById)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Tier, Announce);
    EXPECT_EQ(1, std::size(announce_list));
    auto const id = announce_list.at(0).id;

    EXPECT_EQ(0, announce_list.remove(id));
    EXPECT_EQ(0, std::size(announce_list));
}

TEST_F(AnnounceListTest, canNotRemoveByInvalidId)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Tier, Announce);
    EXPECT_EQ(1, std::size(announce_list));
    auto const id = announce_list.at(0).id;

    EXPECT_EQ(1, announce_list.remove(id + 1));
    EXPECT_EQ(1, std::size(announce_list));
    EXPECT_EQ(Announce, announce_list.at(0).announce.full);
}

TEST_F(AnnounceListTest, canRemoveByAnnounce)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Tier, Announce);
    EXPECT_EQ(1, std::size(announce_list));

    EXPECT_EQ(0, announce_list.remove(Announce));
    EXPECT_EQ(0, std::size(announce_list));
}

TEST_F(AnnounceListTest, canNotRemoveByInvalidAnnounce)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Tier, Announce);
    EXPECT_EQ(1, std::size(announce_list));

    EXPECT_EQ(1, announce_list.remove("https://www.not-example.com/announce"sv));
    EXPECT_EQ(1, std::size(announce_list));
}
