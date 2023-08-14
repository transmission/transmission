// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstdlib>
#include <set>
#include <string_view>
#include <vector>

#include <libtransmission/transmission.h>

#include <libtransmission/announce-list.h>
#include <libtransmission/error.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/tr-strbuf.h>
#include <libtransmission/variant.h>

#include "test-fixtures.h"

#include "gtest/gtest.h"

using AnnounceListTest = ::testing::Test;
using namespace std::literals;

TEST_F(AnnounceListTest, canAdd)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(1, announce_list.add(Announce, Tier));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.sv());
    EXPECT_EQ("https://example.org/scrape"sv, tracker.scrape.sv());
    EXPECT_EQ(Tier, tracker.tier);
    EXPECT_EQ("example.org:443"sv, tracker.host.sv());
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
    EXPECT_TRUE(announce_list.add(Announce1, Tier1));
    EXPECT_TRUE(announce_list.add(Announce2, Tier2));
    EXPECT_TRUE(announce_list.add(Announce3, Tier3));

    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ(Tier1, announce_list.at(0).tier);
    EXPECT_EQ(Tier1, announce_list.at(1).tier);
    EXPECT_EQ(Tier1, announce_list.at(2).tier);
    EXPECT_EQ(Announce1, announce_list.at(0).announce.sv());
    EXPECT_EQ(Announce2, announce_list.at(1).announce.sv());
    EXPECT_EQ(Announce3, announce_list.at(2).announce.sv());
    EXPECT_EQ("example.org:443"sv, announce_list.at(0).host.sv());
    EXPECT_EQ("example.org:80"sv, announce_list.at(1).host.sv());
    EXPECT_EQ("example.org:999"sv, announce_list.at(2).host.sv());
}

TEST_F(AnnounceListTest, canAddWithoutScrape)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/foo"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce, Tier));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.sv());
    EXPECT_TRUE(std::empty(tracker.scrape));
    EXPECT_EQ(Tier, tracker.tier);
}

TEST_F(AnnounceListTest, canAddUdp)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "udp://example.org/"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce, Tier));
    auto const tracker = announce_list.at(0);
    EXPECT_EQ(Announce, tracker.announce.sv());
    EXPECT_EQ("udp://example.org/"sv, tracker.scrape.sv());
    EXPECT_EQ(Tier, tracker.tier);
}

TEST_F(AnnounceListTest, canNotAddDuplicateAnnounce)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "https://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce, Tier));
    EXPECT_EQ(1U, announce_list.size());
    EXPECT_FALSE(announce_list.add(Announce, Tier));
    EXPECT_EQ(1U, announce_list.size());

    auto constexpr Announce2 = "https://example.org:443/announce"sv;
    EXPECT_FALSE(announce_list.add(Announce2, Tier));
    EXPECT_EQ(1U, announce_list.size());
}

TEST_F(AnnounceListTest, canNotAddInvalidUrl)
{
    auto constexpr Tier = tr_tracker_tier_t{ 2 };
    auto constexpr Announce = "telnet://example.org/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_FALSE(announce_list.add(Announce, Tier));
    EXPECT_EQ(0U, announce_list.size());
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
    EXPECT_EQ(3U, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(3U, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(0).tier);
    EXPECT_EQ(Tiers[1], announce_list.at(1).tier);
    EXPECT_EQ(Tiers[2], announce_list.at(2).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.sv());
    EXPECT_EQ(Urls[1], announce_list.at(1).announce.sv());
    EXPECT_EQ(Urls[2], announce_list.at(2).announce.sv());
}

TEST_F(AnnounceListTest, canSetUnsortedWithBackupsInTiers)
{
    auto constexpr Urls = std::array<char const*, 6>{
        "https://www.backup-a.com/announce",  "https://www.backup-b.com/announce",  "https://www.backup-c.com/announce",
        "https://www.primary-a.com/announce", "https://www.primary-b.com/announce", "https://www.primary-c.com/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 6>{ 0, 1, 2, 0, 1, 2 };

    auto announce_list = tr_announce_list{};
    EXPECT_EQ(6U, announce_list.set(std::data(Urls), std::data(Tiers), 6));
    EXPECT_EQ(6U, announce_list.size());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ(0U, announce_list.at(1).tier);
    EXPECT_EQ(1U, announce_list.at(2).tier);
    EXPECT_EQ(1U, announce_list.at(3).tier);
    EXPECT_EQ(2U, announce_list.at(4).tier);
    EXPECT_EQ(2U, announce_list.at(5).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.sv());
    EXPECT_EQ(Urls[3], announce_list.at(1).announce.sv());
    EXPECT_EQ(Urls[1], announce_list.at(2).announce.sv());
    EXPECT_EQ(Urls[4], announce_list.at(3).announce.sv());
    EXPECT_EQ(Urls[2], announce_list.at(4).announce.sv());
    EXPECT_EQ(Urls[5], announce_list.at(5).announce.sv());

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
    EXPECT_EQ(2U, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(2U, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(1).tier);
    EXPECT_EQ(Tiers[1], announce_list.at(0).tier);
    EXPECT_EQ(Urls[0], announce_list.at(1).announce.sv());
    EXPECT_EQ(Urls[1], announce_list.at(0).announce.sv());
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
    EXPECT_EQ(2U, announce_list.set(std::data(Urls), std::data(Tiers), 3));
    EXPECT_EQ(2U, announce_list.size());
    EXPECT_EQ(Tiers[0], announce_list.at(0).tier);
    EXPECT_EQ(Tiers[2], announce_list.at(1).tier);
    EXPECT_EQ(Urls[0], announce_list.at(0).announce.sv());
    EXPECT_EQ(Urls[2], announce_list.at(1).announce.sv());
}

TEST_F(AnnounceListTest, canRemoveById)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Announce, Tier);
    EXPECT_EQ(1U, std::size(announce_list));
    auto const id = announce_list.at(0).id;

    EXPECT_TRUE(announce_list.remove(id));
    EXPECT_EQ(0U, std::size(announce_list));
}

TEST_F(AnnounceListTest, canNotRemoveByInvalidId)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Announce, Tier);
    EXPECT_EQ(1U, std::size(announce_list));
    auto const id = announce_list.at(0).id;

    EXPECT_FALSE(announce_list.remove(id + 1));
    EXPECT_EQ(1U, std::size(announce_list));
    EXPECT_EQ(Announce, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, canRemoveByAnnounce)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Announce, Tier);
    EXPECT_EQ(1U, std::size(announce_list));

    EXPECT_TRUE(announce_list.remove(Announce));
    EXPECT_EQ(0U, std::size(announce_list));
}

TEST_F(AnnounceListTest, canNotRemoveByInvalidAnnounce)
{
    auto constexpr Announce = "https://www.example.com/announce"sv;
    auto constexpr Tier = tr_tracker_tier_t{ 1 };

    auto announce_list = tr_announce_list{};
    announce_list.add(Announce, Tier);
    EXPECT_EQ(1U, std::size(announce_list));

    EXPECT_FALSE(announce_list.remove("https://www.not-example.com/announce"sv));
    EXPECT_EQ(1U, std::size(announce_list));
}

TEST_F(AnnounceListTest, canReplace)
{
    auto constexpr Tier = tr_tracker_tier_t{ 1 };
    auto constexpr Announce1 = "https://www.example.com/1/announce"sv;
    auto constexpr Announce2 = "https://www.example.com/2/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce1, Tier));
    EXPECT_TRUE(announce_list.replace(announce_list.at(0).id, Announce2));
    EXPECT_EQ(Announce2, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, canReplaceWithDiffQuery)
{
    auto constexpr Tier = tr_tracker_tier_t{ 1 };
    auto constexpr Announce1 = "https://www.example.com/1/announce"sv;
    auto constexpr Announce2 = "https://www.example.com/2/announce?pass=1999"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce1, Tier));
    EXPECT_TRUE(announce_list.replace(announce_list.at(0).id, Announce2));
    EXPECT_EQ(Announce2, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, canNotReplaceInvalidId)
{
    auto constexpr Tier = tr_tracker_tier_t{ 1 };
    auto constexpr Announce1 = "https://www.example.com/1/announce"sv;
    auto constexpr Announce2 = "https://www.example.com/2/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce1, Tier));
    EXPECT_FALSE(announce_list.replace(announce_list.at(0).id + 1, Announce2));
    EXPECT_EQ(Announce1, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, canNotReplaceWithInvalidAnnounce)
{
    auto constexpr Tier = tr_tracker_tier_t{ 1 };
    auto constexpr Announce1 = "https://www.example.com/1/announce"sv;
    auto constexpr Announce2 = "telnet://www.example.com/2/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce1, Tier));
    EXPECT_FALSE(announce_list.replace(announce_list.at(0).id, Announce2));
    EXPECT_EQ(Announce1, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, canNotReplaceWithDuplicate)
{
    auto constexpr Tier = tr_tracker_tier_t{ 1 };
    auto constexpr Announce = "https://www.example.com/announce"sv;

    auto announce_list = tr_announce_list{};
    EXPECT_TRUE(announce_list.add(Announce, Tier));
    EXPECT_FALSE(announce_list.replace(announce_list.at(0).id, Announce));
    EXPECT_EQ(Announce, announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, announceToScrape)
{
    struct ScrapeTest
    {
        std::string_view announce;
        std::string_view expected_scrape;
    };

    auto constexpr Tests = std::array<ScrapeTest, 3>{ {
        { "https://www.example.com/announce"sv, "https://www.example.com/scrape"sv },
        { "https://www.example.com/foo"sv, ""sv },
        { "udp://www.example.com:999/"sv, "udp://www.example.com:999/"sv },
    } };

    for (auto const test : Tests)
    {
        auto const scrape = tr_announce_list::announceToScrape(tr_quark_new(test.announce));
        EXPECT_EQ(tr_quark_new(test.expected_scrape), scrape);
    }
}

TEST_F(AnnounceListTest, save)
{
    auto constexpr Urls = std::array<char const*, 3>{
        "https://www.example.com/a/announce",
        "https://www.example.com/b/announce",
        "https://www.example.com/c/announce",
    };
    auto constexpr Tiers = std::array<tr_tracker_tier_t, 3>{ 0, 1, 2 };

    // first, set up a scratch torrent
    auto constexpr* const OriginalFile = LIBTRANSMISSION_TEST_ASSETS_DIR "/Android-x86 8.1 r6 iso.torrent";
    auto original_content = std::vector<char>{};
    auto const test_file = tr_pathbuf{ ::testing::TempDir(), "transmission-announce-list-test.torrent"sv };
    tr_error* error = nullptr;
    EXPECT_TRUE(tr_loadFile(OriginalFile, original_content, &error));
    EXPECT_EQ(nullptr, error) << *error;
    EXPECT_TRUE(tr_saveFile(test_file.sv(), original_content, &error));
    EXPECT_EQ(nullptr, error) << *error;

    // make an announce_list for it
    auto announce_list = tr_announce_list();
    EXPECT_TRUE(announce_list.add(Urls[0], Tiers[0]));
    EXPECT_TRUE(announce_list.add(Urls[1], Tiers[1]));
    EXPECT_TRUE(announce_list.add(Urls[2], Tiers[2]));

    // try saving to a nonexistent torrent file
    EXPECT_FALSE(announce_list.save("/this/path/does/not/exist", &error));
    EXPECT_NE(nullptr, error);
    EXPECT_NE(0, error->code);
    tr_error_clear(&error);

    // now save to a real torrent file
    EXPECT_TRUE(announce_list.save(std::string{ test_file.sv() }, &error));
    EXPECT_EQ(nullptr, error) << *error;

    // load the original
    auto original_tm = tr_torrent_metainfo{};
    EXPECT_TRUE(original_tm.parseBenc({ std::data(original_content), std::size(original_content) }));

    // load the scratch that we saved to
    auto modified_tm = tr_torrent_metainfo{};
    EXPECT_TRUE(modified_tm.parseTorrentFile(test_file.sv()));

    // test that non-announce parts of the metainfo are the same
    EXPECT_EQ(original_tm.name(), modified_tm.name());
    EXPECT_EQ(original_tm.fileCount(), modified_tm.fileCount());
    EXPECT_EQ(original_tm.dateCreated(), modified_tm.dateCreated());
    EXPECT_EQ(original_tm.pieceCount(), modified_tm.pieceCount());

    // test that the saved version has the updated announce list
    EXPECT_TRUE(std::equal(
        std::begin(announce_list),
        std::end(announce_list),
        std::begin(modified_tm.announceList()),
        std::end(modified_tm.announceList())));

    // cleanup
    (void)std::remove(test_file.c_str());
}

TEST_F(AnnounceListTest, SingleAnnounce)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text = "https://www.example.com/a/announce";
    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(1U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
}

TEST_F(AnnounceListTest, parseThreeTier)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(1U, announce_list.at(1).tier);
    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(2U, announce_list.at(2).tier);
    EXPECT_EQ(fmt::format("{:s}\n", Text), announce_list.toString());
}

TEST_F(AnnounceListTest, parseThreeTierWithTrailingLf)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(1U, announce_list.at(1).tier);
    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(2U, announce_list.at(2).tier);
    EXPECT_EQ(Text, announce_list.toString());
}

TEST_F(AnnounceListTest, parseThreeTierWithExcessLf)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\n"
        "\n"
        "\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(1U, announce_list.at(1).tier);
    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(2U, announce_list.at(2).tier);

    auto constexpr ExpectedText =
        "https://www.example.com/a/announce\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;
    EXPECT_EQ(ExpectedText, announce_list.toString());
}

TEST_F(AnnounceListTest, parseThreeTierWithWhitespace)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce  \n"
        "\n"
        "  \n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "\n"
        "\n"
        "  https://www.example.com/c/announce\n"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(1U, announce_list.at(1).tier);
    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(2U, announce_list.at(2).tier);

    auto constexpr ExpectedText =
        "https://www.example.com/a/announce\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;
    EXPECT_EQ(ExpectedText, announce_list.toString());
}

TEST_F(AnnounceListTest, parseThreeTierCrLf)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\r\n"
        "\r\n"
        "https://www.example.com/b/announce\r\n"
        "\r\n"
        "https://www.example.com/c/announce\r\n"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(3U, std::size(announce_list));
    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(1U, announce_list.at(1).tier);
    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(2U, announce_list.at(2).tier);

    auto constexpr ExpectedText =
        "https://www.example.com/a/announce\n"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;
    EXPECT_EQ(ExpectedText, announce_list.toString());
}

TEST_F(AnnounceListTest, parseMultiTrackerInTier)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"
        "https://www.example.com/d/announce\n"
        "https://www.example.com/e/announce\n"
        "https://www.example.com/f/announce\n"
        "\n"
        "https://www.example.com/g/announce\n"
        "https://www.example.com/h/announce\n"
        "https://www.example.com/i/announce\n"sv;

    EXPECT_TRUE(announce_list.parse(Text));
    EXPECT_EQ(9U, std::size(announce_list));

    EXPECT_EQ("https://www.example.com/a/announce", announce_list.at(0).announce.sv());
    EXPECT_EQ(0U, announce_list.at(0).tier);
    EXPECT_EQ("https://www.example.com/b/announce", announce_list.at(1).announce.sv());
    EXPECT_EQ(0U, announce_list.at(1).tier);

    EXPECT_EQ("https://www.example.com/c/announce", announce_list.at(2).announce.sv());
    EXPECT_EQ(1U, announce_list.at(2).tier);
    EXPECT_EQ("https://www.example.com/d/announce", announce_list.at(3).announce.sv());
    EXPECT_EQ(1U, announce_list.at(3).tier);
    EXPECT_EQ("https://www.example.com/e/announce", announce_list.at(4).announce.sv());
    EXPECT_EQ(1U, announce_list.at(4).tier);
    EXPECT_EQ("https://www.example.com/f/announce", announce_list.at(5).announce.sv());
    EXPECT_EQ(1U, announce_list.at(5).tier);

    EXPECT_EQ("https://www.example.com/g/announce", announce_list.at(6).announce.sv());
    EXPECT_EQ(2U, announce_list.at(6).tier);
    EXPECT_EQ("https://www.example.com/h/announce", announce_list.at(7).announce.sv());
    EXPECT_EQ(2U, announce_list.at(7).tier);
    EXPECT_EQ("https://www.example.com/i/announce", announce_list.at(8).announce.sv());
    EXPECT_EQ(2U, announce_list.at(8).tier);

    EXPECT_EQ(Text, announce_list.toString());
}

TEST_F(AnnounceListTest, parseInvalidUrl)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "siojfaiojf"
        "\n"
        "https://www.example.com/b/announce\n"
        "\n"
        "https://www.example.com/c/announce\n"sv;
    EXPECT_FALSE(announce_list.parse(Text));
}

TEST_F(AnnounceListTest, parseDuplicateUrl)
{
    auto announce_list = tr_announce_list{};

    auto constexpr Text =
        "https://www.example.com/a/announce\n"
        "\r\n"
        "https://www.example.com/a/announce"sv;

    EXPECT_FALSE(announce_list.parse(Text));
}

TEST_F(AnnounceListTest, addAnnounceListWithSingleTracker)
{
    auto constexpr Trackers =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"sv;
    auto announce_list = tr_announce_list{};
    announce_list.parse(Trackers);

    auto constexpr AddStr = "https://www.baz.com/announce"sv;
    auto tmp = tr_announce_list{};
    tmp.parse(AddStr);

    announce_list.add(tmp);

    auto constexpr Expected =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"
        "\n"
        "https://www.baz.com/announce\n"sv;
    EXPECT_EQ(Expected, announce_list.toString());
}

TEST_F(AnnounceListTest, addAnnounceWithSingleTier)
{
    auto constexpr Trackers =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"sv;
    auto announce_list = tr_announce_list{};
    announce_list.parse(Trackers);

    auto constexpr AddStr =
        "https://www.baz.com/announce\n"
        "https://www.qux.com/announce\n"sv;
    auto tmp = tr_announce_list{};
    tmp.parse(AddStr);

    announce_list.add(tmp);

    auto constexpr Expected =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"
        "\n"
        "https://www.baz.com/announce\n"
        "https://www.qux.com/announce\n"sv;
    EXPECT_EQ(Expected, announce_list.toString());
}

TEST_F(AnnounceListTest, addAnnounceListWithMultiTier)
{
    auto constexpr Trackers =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"sv;
    auto announce_list = tr_announce_list{};
    announce_list.parse(Trackers);

    auto constexpr AddStr =
        "https://www.baz.com/announce\n"
        "\n"
        "https://www.qux.com/announce\n"sv;
    auto tmp = tr_announce_list{};
    tmp.parse(AddStr);

    announce_list.add(tmp);

    auto constexpr Expected =
        "https://www.foo.com/announce\n"
        "\n"
        "https://www.bar.com/announce\n"
        "\n"
        "https://www.baz.com/announce\n"
        "\n"
        "https://www.qux.com/announce\n"sv;
    EXPECT_EQ(Expected, announce_list.toString());
}
