/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <algorithm>
#include <type_traits>

#define LIBTRANSMISSION_PEER_MODULE

#include "transmission.h"

#include "peer-mgr-wishlist.h"

#include "gtest/gtest.h"

class PeerMgrWishlistTest : public ::testing::Test
{
protected:
    struct MockPeerInfo : public Wishlist::PeerInfo
    {
        mutable std::map<tr_block_index_t, size_t> active_request_count_;
        mutable std::map<tr_piece_index_t, size_t> missing_block_count_;
        mutable std::map<tr_piece_index_t, tr_block_range_t> block_range_;
        mutable std::map<tr_piece_index_t, tr_priority_t> piece_priority_;
        mutable std::set<tr_block_index_t> can_request_block_;
        mutable std::set<tr_piece_index_t> can_request_piece_;
        tr_piece_index_t piece_count_ = 0;
        bool is_endgame_ = false;

        [[nodiscard]] bool clientCanRequestBlock(tr_block_index_t block) const final
        {
            return can_request_block_.count(block) != 0;
        }

        [[nodiscard]] bool clientCanRequestPiece(tr_piece_index_t piece) const final
        {
            return can_request_piece_.count(piece) != 0;
        }

        [[nodiscard]] bool isEndgame() const final
        {
            return is_endgame_;
        }

        [[nodiscard]] size_t countActiveRequests(tr_block_index_t block) const final
        {
            return active_request_count_[block];
        }

        [[nodiscard]] size_t countMissingBlocks(tr_piece_index_t piece) const final
        {
            return missing_block_count_[piece];
        }

        [[nodiscard]] tr_block_range_t blockRange(tr_piece_index_t piece) const final
        {
            return block_range_[piece];
        }

        [[nodiscard]] tr_piece_index_t countAllPieces() const final
        {
            return piece_count_;
        }

        [[nodiscard]] tr_priority_t priority(tr_piece_index_t piece) const final
        {
            return piece_priority_[piece];
        }
    };
};

TEST_F(PeerMgrWishlistTest, doesNotRequestPiecesThatCannotBeRequested)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, all missing
    peer_info.piece_count_ = 3;
    peer_info.missing_block_count_[0] = 100;
    peer_info.missing_block_count_[1] = 100;
    peer_info.missing_block_count_[2] = 50;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 250 };

    // but we only want the first piece
    peer_info.can_request_piece_.insert(0);
    for (tr_block_index_t i = peer_info.block_range_[0].first; i <= peer_info.block_range_[0].last; ++i)
    {
        peer_info.can_request_block_.insert(i);
    }

    // we should only get the first piece back
    auto ranges = wishlist.next(peer_info, 1000);
    ASSERT_EQ(1, std::size(ranges));
    EXPECT_EQ(peer_info.block_range_[0].first, ranges[0].first);
    EXPECT_EQ(peer_info.block_range_[0].last, ranges[0].last);
}

TEST_F(PeerMgrWishlistTest, doesNotRequestBlocksThatCannotBeRequested)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, all missing
    peer_info.piece_count_ = 3;
    peer_info.missing_block_count_[0] = 100;
    peer_info.missing_block_count_[1] = 100;
    peer_info.missing_block_count_[2] = 50;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 249 };

    // and we want all three pieces
    peer_info.can_request_piece_.insert(0);
    peer_info.can_request_piece_.insert(1);
    peer_info.can_request_piece_.insert(2);

    // but we've already requested blocks [0..10) from someone else,
    // so we don't want to send repeat requests
    for (tr_block_index_t i = 10; i < 250; ++i)
    {
        peer_info.can_request_block_.insert(i);
    }

    // even if we ask wishlist for more blocks than exist,
    // it should omit blocks 1-10 from the return set
    auto ranges = wishlist.next(peer_info, 1000);
    auto requested = tr_bitfield(250);
    for (auto const& range : ranges)
    {
        requested.setRange(range.first, range.last + 1);
    }
    EXPECT_EQ(240, requested.count());
    EXPECT_EQ(0, requested.count(0, 10));
    EXPECT_EQ(240, requested.count(10, 250));
}

TEST_F(PeerMgrWishlistTest, doesNotRequestTooManyBlocks)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, all missing
    peer_info.piece_count_ = 3;
    peer_info.missing_block_count_[0] = 100;
    peer_info.missing_block_count_[1] = 100;
    peer_info.missing_block_count_[2] = 50;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 249 };

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        peer_info.can_request_piece_.insert(i);
    }
    for (tr_block_index_t i = 0; i < 250; ++i)
    {
        peer_info.can_request_block_.insert(i);
    }

    // but we only ask for 10 blocks,
    // so that's how many we should get back
    auto const n_wanted = 10;
    auto ranges = wishlist.next(peer_info, n_wanted);
    auto n_got = size_t{};
    for (auto const& range : ranges)
    {
        n_got += range.last + 1 - range.first;
    }
    EXPECT_EQ(n_wanted, n_got);
}

TEST_F(PeerMgrWishlistTest, prefersHighPriorityPieces)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, all missing
    peer_info.piece_count_ = 3;
    peer_info.missing_block_count_[0] = 100;
    peer_info.missing_block_count_[1] = 100;
    peer_info.missing_block_count_[2] = 100;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 299 };

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        peer_info.can_request_piece_.insert(i);
    }
    for (tr_block_index_t i = 0; i < 299; ++i)
    {
        peer_info.can_request_block_.insert(i);
    }

    // and the second piece is high priority
    peer_info.piece_priority_[1] = TR_PRI_HIGH;

    // wishlist should pick the high priority piece's blocks first.
    //
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    auto const num_runs = 1000;
    for (int run = 0; run < num_runs; ++run)
    {
        auto const n_wanted = 10;
        auto ranges = wishlist.next(peer_info, n_wanted);
        auto n_got = size_t{};
        for (auto const& range : ranges)
        {
            for (auto block = range.first; block <= range.last; ++block)
            {
                EXPECT_LE(peer_info.block_range_[1].first, block);
                EXPECT_LE(block, peer_info.block_range_[1].last);
            }
            n_got += range.last + 1 - range.first;
        }
        EXPECT_EQ(n_wanted, n_got);
    }
}

TEST_F(PeerMgrWishlistTest, onlyRequestsDupesDuringEndgame)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, all missing
    peer_info.piece_count_ = 3;
    peer_info.missing_block_count_[0] = 100;
    peer_info.missing_block_count_[1] = 100;
    peer_info.missing_block_count_[2] = 100;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 299 };

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        peer_info.can_request_piece_.insert(i);
    }
    for (tr_block_index_t i = 0; i < 300; ++i)
    {
        peer_info.can_request_block_.insert(i);
    }

    // and we've already requested blocks [0-150)
    for (tr_block_index_t i = 0; i < 150; ++i)
    {
        peer_info.active_request_count_[i] = 1;
    }

    // even if we ask wishlist to list more blocks than exist,
    // those first 150 should be omitted from the return list
    auto ranges = wishlist.next(peer_info, 1000);
    auto requested = tr_bitfield(300);
    for (auto const& range : ranges)
    {
        requested.setRange(range.first, range.last + 1);
    }
    EXPECT_EQ(150, requested.count());
    EXPECT_EQ(0, requested.count(0, 150));
    EXPECT_EQ(150, requested.count(150, 300));

    // BUT during endgame it's OK to request dupes,
    // so then we _should_ see the first 150 in the list
    peer_info.is_endgame_ = true;
    ranges = wishlist.next(peer_info, 1000);
    requested = tr_bitfield(300);
    for (auto const& range : ranges)
    {
        requested.setRange(range.first, range.last + 1);
    }
    EXPECT_EQ(300, requested.count());
    EXPECT_EQ(150, requested.count(0, 150));
    EXPECT_EQ(150, requested.count(150, 300));
}

TEST_F(PeerMgrWishlistTest, prefersNearlyCompletePieces)
{
    auto peer_info = MockPeerInfo{};
    auto wishlist = Wishlist{};

    // setup: three pieces, same size
    peer_info.piece_count_ = 3;
    peer_info.block_range_[0] = { 0, 99 };
    peer_info.block_range_[1] = { 100, 199 };
    peer_info.block_range_[2] = { 200, 299 };

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        peer_info.can_request_piece_.insert(i);
    }

    // but some pieces are closer to completion than others
    peer_info.missing_block_count_[0] = 10;
    peer_info.missing_block_count_[1] = 20;
    peer_info.missing_block_count_[2] = 100;
    for (tr_piece_index_t piece = 0; piece < 3; ++piece)
    {
        auto const& range = peer_info.block_range_[piece];
        auto const& n_missing = peer_info.missing_block_count_[piece];

        for (size_t i = 0; i < n_missing; ++i)
        {
            peer_info.can_request_block_.insert(range.first + i);
        }
    }

    // wishlist prefers to get pieces completed ASAP, so it
    // should pick the ones with the fewest missing blocks first.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    auto const num_runs = 1000;
    for (int run = 0; run < num_runs; ++run)
    {
        auto const ranges = wishlist.next(peer_info, 10);
        auto requested = tr_bitfield(300);
        for (auto const& range : ranges)
        {
            requested.setRange(range.first, range.last + 1);
        }
        EXPECT_EQ(10, requested.count());
        EXPECT_EQ(10, requested.count(0, 100));
        EXPECT_EQ(0, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the second piece is also the second-closest to completion,
    // those blocks should be next in line.
    for (int run = 0; run < num_runs; ++run)
    {
        auto const ranges = wishlist.next(peer_info, 20);
        auto requested = tr_bitfield(300);
        for (auto const& range : ranges)
        {
            requested.setRange(range.first, range.last + 1);
        }
        EXPECT_EQ(20, requested.count());
        EXPECT_EQ(10, requested.count(0, 100));
        EXPECT_EQ(10, requested.count(100, 200));
        EXPECT_EQ(0, requested.count(200, 300));
    }
}
