// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#define LIBTRANSMISSION_PEER_MODULE

#include <algorithm>
#include <type_traits>

#include <libtransmission/transmission.h>

#include <libtransmission/peer-mgr-active-requests.h>

#include "gtest/gtest.h"

class PeerMgrActiveRequestsTest : public ::testing::Test
{
protected:
    tr_peer* peer_a_ = reinterpret_cast<tr_peer*>(0xCAFE);
    tr_peer* peer_b_ = reinterpret_cast<tr_peer*>(0xDEAD);
    tr_peer* peer_c_ = reinterpret_cast<tr_peer*>(0xBEEF);
};

// consider: making it a templated class so that tr_peer can be replaced with X

TEST_F(PeerMgrActiveRequestsTest, requestsAreNotAddedTwice)
{
    auto requests = ActiveRequests{};

    auto const block = tr_block_index_t{ 100 };
    auto const peer = static_cast<tr_peer*>(nullptr);
    auto const when = time_t{};
    EXPECT_TRUE(requests.add(block, peer, when));
    EXPECT_FALSE(requests.add(block, peer, when));
    EXPECT_FALSE(requests.add(block, peer, when));
    EXPECT_FALSE(requests.add(block, peer, when + 1));
}

TEST_F(PeerMgrActiveRequestsTest, requestsMadeAreCounted)
{
    auto requests = ActiveRequests{};

    auto const block = tr_block_index_t{ 100 };
    auto const peer = static_cast<tr_peer*>(nullptr);
    auto const when = time_t{};

    EXPECT_EQ(0U, requests.count(block));
    EXPECT_EQ(0U, requests.count(peer));
    EXPECT_EQ(0U, requests.size());

    EXPECT_TRUE(requests.add(block, peer, when));

    EXPECT_EQ(1U, requests.count(block));
    EXPECT_EQ(1U, requests.count(peer));
    EXPECT_EQ(1U, requests.size());
}

TEST_F(PeerMgrActiveRequestsTest, requestsAreRemoved)
{
    auto requests = ActiveRequests{};

    auto const block = tr_block_index_t{ 100 };
    auto const peer = static_cast<tr_peer*>(nullptr);
    auto const when = time_t{};

    EXPECT_TRUE(requests.add(block, peer, when));
    EXPECT_EQ(1U, requests.count(block));
    EXPECT_EQ(1U, requests.count(peer));
    EXPECT_EQ(1U, requests.size());

    EXPECT_TRUE(requests.remove(block, peer));
    EXPECT_EQ(0U, requests.count(block));
    EXPECT_EQ(0U, requests.count(peer));
    EXPECT_EQ(0U, requests.size());

    EXPECT_FALSE(requests.remove(block, peer));
    EXPECT_EQ(0U, requests.count(block));
    EXPECT_EQ(0U, requests.count(peer));
    EXPECT_EQ(0U, requests.size());
}

TEST_F(PeerMgrActiveRequestsTest, peersAreRemoved)
{
    auto requests = ActiveRequests{};

    auto const block = tr_block_index_t{ 100 };
    auto const peer = static_cast<tr_peer*>(nullptr);
    auto const when = time_t{};

    // setup: add a request
    EXPECT_TRUE(requests.add(block, peer, when));
    EXPECT_EQ(1U, requests.count(block));
    EXPECT_EQ(1U, requests.count(peer));
    EXPECT_EQ(1U, requests.size());

    // try removing requests for that block (should remove the 1 active request)
    auto const removed = requests.remove(block);
    EXPECT_EQ(std::vector<tr_peer*>{ peer }, removed);
    EXPECT_EQ(0U, requests.count(block));
    EXPECT_EQ(0U, requests.count(peer));
    EXPECT_EQ(0U, requests.size());

    // try removing requests for that block again (should remove nothing)
    EXPECT_EQ(std::vector<tr_peer*>{}, requests.remove(block));
}

TEST_F(PeerMgrActiveRequestsTest, multiplePeersAreRemoved)
{
    // setup
    auto requests = ActiveRequests{};
    auto const block_a = tr_block_index_t{ 128 };
    auto const when_a = 100;
    EXPECT_TRUE(requests.add(block_a, peer_a_, when_a));
    auto const block_b = block_a;
    auto const when_b = 200;
    EXPECT_TRUE(requests.add(block_b, peer_b_, when_b));
    auto const block_c = tr_block_index_t{ 256 };
    auto const when_c = when_b;
    EXPECT_TRUE(requests.add(block_c, peer_c_, when_c));
    EXPECT_EQ(block_a, block_b);
    EXPECT_EQ(2U, requests.count(block_a));
    EXPECT_EQ(1U, requests.count(block_c));
    EXPECT_EQ(3U, requests.size());

    // now remove block_a, which was req'd by peer_a_ and peer_b_
    auto expected = std::vector<tr_peer*>{ peer_a_, peer_b_ };
    std::sort(std::begin(expected), std::end(expected));
    auto removed = requests.remove(block_a);
    std::sort(std::begin(removed), std::end(removed));
    EXPECT_EQ(expected, removed);
}

TEST_F(PeerMgrActiveRequestsTest, multipleBlocksAreRemoved)
{
    // setup
    auto requests = ActiveRequests{};
    auto const block_a1 = tr_block_index_t{ 128 };
    auto const when_a1 = 300;
    EXPECT_TRUE(requests.add(block_a1, peer_a_, when_a1));
    auto const block_a2 = tr_block_index_t{ 256 };
    auto const when_a2 = 400;
    EXPECT_TRUE(requests.add(block_a2, peer_a_, when_a2));
    EXPECT_EQ(2U, requests.size());
    EXPECT_EQ(2U, requests.count(peer_a_));
    EXPECT_EQ(1U, requests.count(block_a1));
    EXPECT_EQ(0U, requests.count(peer_b_));
    EXPECT_EQ(0U, requests.count(tr_block_index_t{ 512 }));

    // confirm that removing peer_a_ removes all of its requests
    auto expected = std::vector<tr_block_index_t>{ block_a1, block_a2 };
    std::sort(std::begin(expected), std::end(expected));
    auto removed = requests.remove(peer_a_);
    std::sort(std::begin(removed), std::end(removed));
    EXPECT_EQ(expected, removed);
    EXPECT_EQ(0U, requests.size());
    EXPECT_EQ(0U, requests.count(peer_a_));
    EXPECT_EQ(0U, requests.count(block_a1));
}

TEST_F(PeerMgrActiveRequestsTest, sentBefore)
{
    // setup
    auto requests = ActiveRequests{};
    auto const block_a1 = tr_block_index_t{ 128 };
    auto const when_a1 = 300;
    EXPECT_TRUE(requests.add(block_a1, peer_a_, when_a1));
    auto const block_a2 = tr_block_index_t{ 256 };
    auto const when_a2 = 400;
    EXPECT_TRUE(requests.add(block_a2, peer_a_, when_a2));
    EXPECT_EQ(2U, requests.size());
    EXPECT_EQ(2U, requests.count(peer_a_));
    EXPECT_EQ(1U, requests.count(block_a1));

    // test that the timestamps are counted correctly
    EXPECT_EQ(0U, std::size(requests.sentBefore(when_a1 - 1)));
    EXPECT_EQ(0U, std::size(requests.sentBefore(when_a1)));
    EXPECT_EQ(1U, std::size(requests.sentBefore(when_a1 + 1)));
    EXPECT_EQ(1U, std::size(requests.sentBefore(when_a2 - 1)));
    EXPECT_EQ(1U, std::size(requests.sentBefore(when_a2)));
    EXPECT_EQ(2U, std::size(requests.sentBefore(when_a2 + 1)));

    // test that the returned block + peer pairs are correct
    auto items = requests.sentBefore(when_a1 + 1);
    ASSERT_EQ(1U, std::size(items));
    EXPECT_EQ(block_a1, items[0].first);
    EXPECT_EQ(peer_a_, items[0].second);
}
