// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <array>
#include <cstddef> // size_t
#include <map>
#include <set>

#define LIBTRANSMISSION_PEER_MODULE

#include "tr/torrent/transmission.h"

#include "tr/torrent/bitfield.h"
#include "tr/torrent/crypto-utils.h"
#include "tr/torrent/peer-mgr-wishlist.h"

#include "test-fixtures.h"

class PeerMgrWishlistTest : public ::tr::test::TransmissionTest
{
protected:
    struct MockMediator final : public Wishlist::Mediator
    {
        mutable std::map<tr_block_index_t, uint8_t> active_request_count_;
        mutable std::map<tr_piece_index_t, tr_block_span_t> block_span_;
        mutable std::map<tr_piece_index_t, tr_priority_t> piece_priority_;
        mutable std::map<tr_piece_index_t, size_t> piece_replication_;
        mutable std::set<tr_block_index_t> client_has_block_;
        mutable std::set<tr_piece_index_t> client_has_piece_;
        mutable std::set<tr_piece_index_t> client_wants_piece_;
        bool is_sequential_download_ = false;
        tr_piece_index_t sequential_download_from_piece_ = 0;

        MockMediator() = default;

        [[nodiscard]] bool client_has_block(tr_block_index_t block) const override
        {
            return client_has_block_.count(block) != 0;
        }

        [[nodiscard]] bool client_has_piece(tr_piece_index_t piece) const override
        {
            return client_has_piece_.count(piece) != 0;
        }

        [[nodiscard]] bool client_wants_piece(tr_piece_index_t piece) const override
        {
            return client_wants_piece_.count(piece) != 0;
        }

        [[nodiscard]] bool is_sequential_download() const override
        {
            return is_sequential_download_;
        }

        [[nodiscard]] tr_piece_index_t sequential_download_from_piece() const override
        {
            return sequential_download_from_piece_;
        }

        [[nodiscard]] size_t count_piece_replication(tr_piece_index_t piece) const override
        {
            return piece_replication_[piece];
        }

        [[nodiscard]] tr_block_span_t block_span(tr_piece_index_t piece) const override
        {
            return block_span_[piece];
        }

        [[nodiscard]] tr_piece_index_t piece_count() const override
        {
            return std::size(block_span_);
        }

        [[nodiscard]] tr_priority_t priority(tr_piece_index_t piece) const override
        {
            return piece_priority_[piece];
        }
    };

    static auto constexpr PeerHasAllPieces = [](tr_piece_index_t)
    {
        return true;
    };
};

TEST_F(PeerMgrWishlistTest, doesNotRequestPiecesThatAreNotWanted)
{
    auto mediator = MockMediator{};

    // setup: three pieces, all missing
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 250 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // but we only want the first piece
    mediator.client_wants_piece_.insert(0);

    // we should only get the first piece back
    auto const spans = Wishlist{ mediator }.next(1000, PeerHasAllPieces);
    ASSERT_EQ(1U, std::size(spans));
    EXPECT_EQ(mediator.block_span_[0].begin, spans[0].begin);
    EXPECT_EQ(mediator.block_span_[0].end, spans[0].end);
}

TEST_F(PeerMgrWishlistTest, doesNotRequestPiecesThatClientHas)
{
    auto mediator = MockMediator{};

    // setup: three pieces
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 250 };

    // we have pieces 0, 1
    mediator.client_has_piece_.insert(0);
    mediator.client_has_piece_.insert(1);

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // we want all three pieces
    mediator.client_wants_piece_.insert(0);
    mediator.client_wants_piece_.insert(1);
    mediator.client_wants_piece_.insert(2);

    // we should only get piece 2
    auto const spans = Wishlist{ mediator }.next(1000, PeerHasAllPieces);
    ASSERT_EQ(1U, std::size(spans));
    EXPECT_EQ(mediator.block_span_[2].begin, spans[0].begin);
    EXPECT_EQ(mediator.block_span_[2].end, spans[0].end);
}

TEST_F(PeerMgrWishlistTest, onlyRequestBlocksThePeerHas)
{
    auto mediator = MockMediator{};

    // setup: three pieces, all missing
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 250 };

    // peer has piece 1
    mediator.piece_replication_[0] = 0;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 0;

    // and we want all three pieces
    mediator.client_wants_piece_.insert(0);
    mediator.client_wants_piece_.insert(1);
    mediator.client_wants_piece_.insert(2);

    // but the peer only has the second piece, we don't want to
    // request blocks other than these
    static auto constexpr IsPieceOne = [](tr_piece_index_t p)
    {
        return p == 1U;
    };

    // even if we ask wishlist for more blocks than what the peer has,
    // it should only return blocks [100..200)
    auto const spans = Wishlist{ mediator }.next(250, IsPieceOne);
    auto requested = tr_bitfield{ 250 };
    for (auto const& [begin, end] : spans)
    {
        requested.set_span(begin, end);
    }
    EXPECT_EQ(100U, requested.count());
    EXPECT_EQ(0U, requested.count(0, 100));
    EXPECT_EQ(100U, requested.count(100, 200));
    EXPECT_EQ(0U, requested.count(200, 250));
}

TEST_F(PeerMgrWishlistTest, doesNotRequestSameBlockTwice)
{
    auto mediator = MockMediator{};

    // setup: three pieces, all missing
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 250 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // and we want all three pieces
    mediator.client_wants_piece_.insert(0);
    mediator.client_wants_piece_.insert(1);
    mediator.client_wants_piece_.insert(2);

    // allow the wishlist to build its cache
    auto wishlist = Wishlist{ mediator };

    // but we've already requested blocks [0..10) from this peer,
    // so we don't want to send repeated requests
    wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 10 });

    // even if we ask wishlist for all the blocks,
    // it should omit blocks [0..10) from the return set
    auto const spans = wishlist.next(250, PeerHasAllPieces);
    auto requested = tr_bitfield{ 250 };
    for (auto const& [begin, end] : spans)
    {
        requested.set_span(begin, end);
    }
    EXPECT_EQ(240U, requested.count());
    EXPECT_EQ(0U, requested.count(0, 10));
    EXPECT_EQ(240U, requested.count(10, 250));
}

TEST_F(PeerMgrWishlistTest, sequentialDownload)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 250 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want all three pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);

        // we enabled sequential download
        mediator.is_sequential_download_ = true;

        return Wishlist{ mediator }.next(n_wanted, PeerHasAllPieces);
    };

    // when we ask for blocks, apart from the last piece,
    // which will be returned first because it is smaller,
    // we should get pieces in order
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 250 };
        auto const spans = get_spans(100);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(50U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 200));
        EXPECT_EQ(50U, requested.count(200, 250));
    }

    // Same premise as previous test, but ask for more blocks.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 250 };
        auto const spans = get_spans(200);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(200U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(50U, requested.count(100, 200));
        EXPECT_EQ(50U, requested.count(200, 250));
    }
}

TEST_F(PeerMgrWishlistTest, sequentialDownloadFromPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: four pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };
        mediator.block_span_[3] = { .begin = 300, .end = 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // and we want all pieces
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // we enabled sequential download, from piece 2
        mediator.is_sequential_download_ = true;
        mediator.sequential_download_from_piece_ = 2;

        return Wishlist{ mediator }.next(n_wanted, PeerHasAllPieces);
    };

    // First and last piece come first in sequential download mode regardless
    // of "sequential download from piece", piece 2 comes next.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;

    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 400 };
        auto const spans = get_spans(300);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(300U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 200));
        // piece 2 should be downloaded before piece 1
        EXPECT_EQ(100U, requested.count(200, 300));
        EXPECT_EQ(100U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, doesNotRequestTooManyBlocks)
{
    auto mediator = MockMediator{};

    // setup: three pieces, all missing
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 250 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // and we want everything
    for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
    {
        mediator.client_wants_piece_.insert(i);
    }

    // but we only ask for 10 blocks,
    // so that's how many we should get back
    static constexpr auto NumWanted = 10U;
    auto const spans = Wishlist{ mediator }.next(NumWanted, PeerHasAllPieces);
    auto n_got = size_t{};
    for (auto const& [begin, end] : spans)
    {
        n_got += end - begin;
    }
    EXPECT_EQ(NumWanted, n_got);
}

TEST_F(PeerMgrWishlistTest, prefersHighPriorityPieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // and the second piece is high priority
        mediator.piece_priority_[1] = TR_PRI_HIGH;

        return Wishlist{ mediator }.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist should pick the high priority piece's blocks first.
    //
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(10);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(10U, requested.count());
        EXPECT_EQ(0U, requested.count(0, 100));
        EXPECT_EQ(10U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, prefersNearlyCompletePieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, same size
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // but some pieces are closer to completion than others
        static auto constexpr MissingBlockCount = std::array{ 10U, 20U, 100U };
        static_assert(std::size(MissingBlockCount) == 3);
        for (tr_piece_index_t piece = 0; piece < 3; ++piece)
        {
            auto const& [begin, end] = mediator.block_span_[piece];
            auto const have_end = end - MissingBlockCount[piece];

            for (tr_piece_index_t i = begin; i < have_end; ++i)
            {
                mediator.client_has_block_.insert(i);
            }
        }

        return Wishlist{ mediator }.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to get pieces completed ASAP, so it
    // should pick the ones with the fewest missing blocks first.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(10);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(10U, requested.count());
        EXPECT_EQ(10U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the second piece is also the second-closest to completion,
    // those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(20);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(20U, requested.count());
        EXPECT_EQ(10U, requested.count(0, 100));
        EXPECT_EQ(10U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, prefersRarerPieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // but some pieces are rarer than others
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 3;
        mediator.piece_replication_[2] = 2;

        return Wishlist{ mediator }.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to request rarer pieces, so it
    // should pick the ones with the smallest replication.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(100);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the third piece is the second-rarest, those blocks
    // should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 200));
        EXPECT_EQ(50U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, peerDisconnectDecrementsReplication)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // all pieces had the same rarity
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a peer that has only the first piece disconnected, now the
        // first piece should be the rarest piece according to the cache
        auto have = tr_bitfield{ 3 };
        have.set(0);
        wishlist.on_peer_disconnect(have, tr_bitfield{ 300 });

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 1;

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to request rarer pieces, so it
    // should pick the ones with the smallest replication.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(100);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the second and third piece are the second-rarest,
    // those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(50U, requested.count(100, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotBadPieceResetsPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // all pieces had the same rarity
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we already requested 50 blocks each from every piece
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 50 });
        wishlist.on_sent_request(tr_block_span_t{ .begin = 100, .end = 150 });
        wishlist.on_sent_request(tr_block_span_t{ .begin = 200, .end = 250 });

        // we request the rest of a random piece
        auto const random_piece = tr_rand_int(3U);
        wishlist.on_sent_request(tr_block_span_t{ .begin = (random_piece * 100U) + 50U, .end = (random_piece + 1U) * 100U });

        // the random piece turns out to be corrupted, so all blocks should be missing again
        wishlist.on_got_bad_piece(random_piece);

        return std::pair{ wishlist.next(n_wanted, PeerHasAllPieces), random_piece };
    };

    // The wishlist should request the bad piece last, since it now became
    // the piece with the most unrequested blocks.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const [spans, bad_piece] = get_spans(101);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(101U, requested.count());
        EXPECT_EQ(1U, requested.count(bad_piece * size_t{ 100U }, (bad_piece + 1U) * size_t{ 100U }));
    }
}

TEST_F(PeerMgrWishlistTest, gotBitfieldIncrementsReplication)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // all pieces had the same rarity
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a peer with first 2 pieces connected and sent a bitfield, now the
        // third piece should be the rarest piece according to the cache
        auto have = tr_bitfield{ 3 };
        have.set_span(0, 2);
        wishlist.on_got_bitfield(have);

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 3;
        // mediator.piece_replication_[1] = 3;

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to request rarer pieces, so it
    // should pick the ones with the smallest replication.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(100);
        auto requested = tr_bitfield{ 300 };
        for (auto const& span : spans)
        {
            requested.set_span(span.begin, span.end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(0U, requested.count(0, 200));
        EXPECT_EQ(100U, requested.count(200, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the first and second piece are the second-rarest,
    // those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& span : spans)
        {
            requested.set_span(span.begin, span.end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(50U, requested.count(0, 200));
        EXPECT_EQ(100U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, sentRequestsResortsPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we requested block 0 from someone, the wishlist should resort the
        // candidate list cache
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 1 });

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to get pieces completed ASAP, so it
    // should pick the ones with the fewest unrequested blocks first.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(100);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(99U, requested.count(0, 100));
        EXPECT_EQ(1U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the first and second piece are the second nearest
    // to completion, those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(99U, requested.count(0, 100));
        EXPECT_EQ(51U, requested.count(100, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotBlockResortsPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we received block 0 from someone, the wishlist should resort the
        // candidate list cache
        wishlist.on_got_block(0);

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to get pieces completed ASAP, so it
    // should pick the ones with the fewest unrequested blocks first.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(100);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(100U, requested.count());
        EXPECT_EQ(99U, requested.count(0, 100));
        EXPECT_EQ(1U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the first and second piece are the second nearest
    // to completion, those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(99U, requested.count(0, 100));
        EXPECT_EQ(51U, requested.count(100, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotHaveIncrementsReplication)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // all pieces had the same rarity
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a peer sent a "Have" message for the first piece, now the
        // first piece should be the least rare piece according to the cache
        wishlist.on_got_have(0);

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 3;

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to request rarer pieces, so it
    // should pick the ones with the smallest replication.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(200);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(200U, requested.count());
        EXPECT_EQ(0U, requested.count(0, 100));
        EXPECT_EQ(200U, requested.count(100, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    // Since the first and second piece are the second-rarest,
    // those blocks should be next in line.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(250);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(250U, requested.count());
        EXPECT_EQ(50U, requested.count(0, 100));
        EXPECT_EQ(200U, requested.count(100, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotChokeResetsRequestedBlocks)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 250 });

        // a peer sent a "Choke" message, which cancels some active requests
        tr_bitfield requested{ 300 };
        requested.set_span(0, 10);
        wishlist.on_got_choke(requested);

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist only picks blocks with no active requests, which are
    // [0, 10) and [250, 300).
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(300);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(60U, requested.count());
        EXPECT_EQ(10U, requested.count(0, 10));
        EXPECT_EQ(0U, requested.count(10, 250));
        EXPECT_EQ(50U, requested.count(250, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotHaveAllDoesNotAffectOrder)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // all pieces have different rarity
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 3;

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a peer sent a "Have All" message, this should not affect the piece order
        wishlist.on_got_have_all();

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 2;
        // mediator.piece_replication_[1] = 3;
        // mediator.piece_replication_[2] = 4;

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist prefers to request rarer pieces, so it
    // should pick the ones with the smallest replication.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(150);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(50U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(250);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(250U, requested.count());
        EXPECT_EQ(200U, requested.count(0, 200));
        EXPECT_EQ(50U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, gotRejectResetsBlock)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 250 });

        // a peer sent some "Reject" messages, which cancels active requests
        auto rejected_bitfield = tr_bitfield{ 300 };
        for (tr_block_index_t i = 0, n = tr_rand_int(250U); i <= n; ++i)
        {
            auto const block = tr_rand_int(250U);
            rejected_bitfield.set(block);
            wishlist.on_got_reject(block);
        }

        return std::pair{ wishlist.next(n_wanted, PeerHasAllPieces), std::move(rejected_bitfield) };
    };

    // wishlist only picks blocks with no active requests, which are
    // [250, 300) and some other random blocks.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const [spans, expected] = get_spans(300);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(50U + expected.count(), requested.count());
        EXPECT_EQ(50U, requested.count(250, 300));
        for (tr_block_index_t i = 0; i < 250; ++i)
        {
            EXPECT_EQ(expected.test(i), requested.test(i));
        }
    }
}

TEST_F(PeerMgrWishlistTest, gotRejectResortsPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: two pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;

        // and we want everything
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 50 blocks of each piece
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 50 });
        wishlist.on_sent_request(tr_block_span_t{ .begin = 100, .end = 150 });

        // a peer sent a "Reject" messages, which cancels active requests
        auto const random_piece = tr_rand_int(2U);
        wishlist.on_got_reject(mediator.block_span_[random_piece].begin);

        return std::pair{ wishlist.next(n_wanted, PeerHasAllPieces), 1U - random_piece };
    };

    // wishlist prioritises pieces that have fewer unrequested blocks.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const [spans, expected_piece] = get_spans(1);
        auto requested = tr_bitfield{ 200 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(1U, requested.count());
        EXPECT_TRUE(requested.test((expected_piece * 100U) + 50U));
    }
}

TEST_F(PeerMgrWishlistTest, sentCancelResetsBlocks)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 250 });

        // we sent some "Cancel" messages
        auto cancelled_bitfield = tr_bitfield{ 300 };
        for (tr_block_index_t i = 0, n = tr_rand_int(250U); i <= n; ++i)
        {
            auto const block = tr_rand_int(250U);
            cancelled_bitfield.set(block);
            wishlist.on_sent_cancel(block);
        }

        return std::pair{ wishlist.next(n_wanted, PeerHasAllPieces), std::move(cancelled_bitfield) };
    };

    // wishlist only picks blocks with no active requests, which are
    // [250, 300) and some other random blocks.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const [spans, expected] = get_spans(300);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(50U + expected.count(), requested.count());
        EXPECT_EQ(50U, requested.count(250, 300));
        for (tr_block_index_t i = 0; i < 250; ++i)
        {
            EXPECT_EQ(expected.test(i), requested.test(i));
        }
    }
}

TEST_F(PeerMgrWishlistTest, doesNotRequestBlockAfterBlockCompleted)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we sent "Request" messages
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 120 });

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist only picks blocks with no active requests, which are
    // [0, 120).
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(300);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(180U, requested.count());
        EXPECT_EQ(0U, requested.count(0, 120));
        EXPECT_EQ(180U, requested.count(120, 300));
    }
}

TEST_F(PeerMgrWishlistTest, doesNotRequestPieceAfterPieceCompleted)
{
    auto mediator = MockMediator{};

    // setup: three pieces, piece 0 is nearly complete
    mediator.block_span_[0] = { .begin = 0, .end = 100 };
    mediator.block_span_[1] = { .begin = 100, .end = 200 };
    mediator.block_span_[2] = { .begin = 200, .end = 300 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // and we want everything
    for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
    {
        mediator.client_wants_piece_.insert(i);
    }

    // allow the wishlist to build its cache, it should have all 3 pieces
    // at this point
    auto wishlist = Wishlist{ mediator };

    // we just completed piece 0
    wishlist.on_sent_request(mediator.block_span_[0]);
    for (auto [block, end] = mediator.block_span_[0]; block < end; ++block)
    {
        mediator.client_has_block_.insert(block);
        wishlist.on_got_block(block);
    }
    mediator.client_has_piece_.insert(0);
    wishlist.on_piece_completed(0);

    // receiving a "piece_completed" signal removes the piece from the
    // wishlist's cache, its blocks should not be in the return set.
    auto const spans = wishlist.next(10, PeerHasAllPieces);
    auto requested = tr_bitfield{ 300 };
    for (auto const& [begin, end] : spans)
    {
        requested.set_span(begin, end);
    }
    EXPECT_EQ(10U, requested.count());
    EXPECT_EQ(0U, requested.count(0, 100));
    EXPECT_EQ(10U, requested.count(100, 300));
}

TEST_F(PeerMgrWishlistTest, settingPriorityResortsCandidates)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a file priority changed, the cache should be rebuilt.
        // let's say the file was in piece 1
        mediator.piece_priority_[1] = TR_PRI_HIGH;
        wishlist.on_priority_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // wishlist should pick the high priority piece's blocks first.
    //
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto const spans = get_spans(10);
        auto requested = tr_bitfield{ 300 };
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(10U, requested.count());
        EXPECT_EQ(0U, requested.count(0, 100));
        EXPECT_EQ(10U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, settingSequentialDownloadResortsCandidates)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: three pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // the sequential download setting was changed,
        // the cache should be rebuilt
        mediator.is_sequential_download_ = true;
        wishlist.on_sequential_download_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // we should get pieces in sequential order when we ask for blocks,
    // except the last piece should follow immediately after the first piece
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 300 };
        auto spans = get_spans(150);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(150U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 200));
        EXPECT_EQ(50U, requested.count(200, 300));
    }

    // Same premise as previous test, but ask for more blocks.
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 300 };
        auto spans = get_spans(250);
        for (auto const& span : spans)
        {
            requested.set_span(span.begin, span.end);
        }
        EXPECT_EQ(250U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(50U, requested.count(100, 200));
        EXPECT_EQ(100U, requested.count(200, 300));
    }
}

TEST_F(PeerMgrWishlistTest, sequentialDownloadFromPieceResortsCandidates)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: four pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };
        mediator.block_span_[3] = { .begin = 300, .end = 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // and we want all pieces
        for (tr_piece_index_t i = 0; i < mediator.piece_count(); ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we enabled sequential download, from piece 2
        mediator.is_sequential_download_ = true;
        wishlist.on_sequential_download_changed();
        mediator.sequential_download_from_piece_ = 2;
        wishlist.on_sequential_download_from_piece_changed();

        // the sequential download setting was changed,
        // the candidate list should be resorted
        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // First and last piece come first in sequential download mode regardless
    // of "sequential download from piece", piece 2 comes next.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;

    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 400 };
        auto const spans = get_spans(300);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(300U, requested.count());
        EXPECT_EQ(100U, requested.count(0, 100));
        EXPECT_EQ(0U, requested.count(100, 200));
        // piece 2 should be downloaded before piece 1
        EXPECT_EQ(100U, requested.count(200, 300));
        EXPECT_EQ(100U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, setFileWantedUpdatesCandidateListAdd)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: four pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };
        mediator.block_span_[3] = { .begin = 300, .end = 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially only want the first 2 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // now we want the file that consists of piece 2 and piece 3 also
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);
        wishlist.on_files_wanted_changed();

        // a candidate should be inserted into the wishlist for
        // piece 2 and piece 3
        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // We should request all 4 pieces here.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 400 };
        auto const spans = get_spans(400);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(400U, requested.count());
        EXPECT_NE(0U, requested.count(0, 100));
        EXPECT_NE(0U, requested.count(100, 200));
        EXPECT_NE(0U, requested.count(200, 300));
        EXPECT_NE(0U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, setFileWantedUpdatesCandidateListAddHad)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: four pieces
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };
        mediator.block_span_[3] = { .begin = 300, .end = 400 };

        // we have pieces 2, 3
        mediator.client_has_piece_.insert(2);
        mediator.client_has_piece_.insert(3);

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially wanted the first 2 pieces only
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // now we want piece 2 and piece 3
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);
        wishlist.on_files_wanted_changed();

        // the candidate list should remain unchanged
        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // We should only request pieces 0, 1 here.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 400 };
        auto const spans = get_spans(350);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(200U, requested.count());
        EXPECT_NE(0U, requested.count(0, 100));
        EXPECT_NE(0U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
        EXPECT_EQ(0U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, setFileWantedUpdatesCandidateListRemove)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: four pieces, all missing
        mediator.block_span_[0] = { .begin = 0, .end = 100 };
        mediator.block_span_[1] = { .begin = 100, .end = 200 };
        mediator.block_span_[2] = { .begin = 200, .end = 300 };
        mediator.block_span_[3] = { .begin = 300, .end = 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we no longer want the file that consists of piece 2 and piece 3
        mediator.client_wants_piece_.erase(2);
        mediator.client_wants_piece_.erase(3);
        wishlist.on_files_wanted_changed();

        // the candidate objects for piece 2 and piece 3 should be removed
        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // We should request only the first 2 pieces here.
    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 400 };
        auto const spans = get_spans(400);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(200U, requested.count());
        EXPECT_NE(0U, requested.count(0, 100));
        EXPECT_NE(0U, requested.count(100, 200));
        EXPECT_EQ(0U, requested.count(200, 300));
        EXPECT_EQ(0U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrent)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get 1 span, instead of
        // [0, 34), [33, 67), [66, 134)
        EXPECT_EQ(std::size(spans), 1);

        // Since the spans might overlap if we didn't handle unaligned
        // torrents correctly, we might not get all 134 blocks if there
        // is a bug
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_TRUE(requested.has_all());
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentPartiallyCompletedPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // We have the first and last 10 blocks of piece 1
        for (tr_block_index_t block = 33; block < 43; ++block)
        {
            mediator.client_has_block_.insert(block);
        }
        for (tr_block_index_t block = 57; block < 67; ++block)
        {
            mediator.client_has_block_.insert(block);
        }

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get 3 spans
        // [0, 33), [43, 57), [67, 134)
        EXPECT_EQ(std::size(spans), 3);

        // If we didn't handle the overlapping spans correctly, we might mistakenly
        // erase blocks from the unrequested set despite not needing to.
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(114U, requested.count());
        EXPECT_EQ(33U, requested.count(0, 34));
        EXPECT_FALSE(requested.test(33));
        EXPECT_EQ(14U, requested.count(34, 67));
        EXPECT_EQ(14U, requested.count(43, 57));
        EXPECT_EQ(33U, requested.count(67, 100));
        EXPECT_FALSE(requested.test(66));
        EXPECT_EQ(34U, requested.count(100, 134));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentPartiallyCompleted)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // We have pieces 0, 2, 3
        mediator.client_has_piece_.insert(0);
        mediator.client_has_piece_.insert(2);
        mediator.client_has_piece_.insert(3);

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [33, 67), not any shrunken spans like [34, 67)
        EXPECT_EQ(std::size(spans), 1);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(34U, requested.count());
        EXPECT_EQ(34U, requested.count(33, 67));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentGotBadPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // requested all blocks and "download" piece 1,
        // as well as parts of piece 0 and piece 2 that
        // is next to piece 1
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 134 });
        for (auto block = mediator.block_span_[0].end - 10; block < mediator.block_span_[1].end + 10; ++block)
        {
            mediator.client_has_block_.insert(block);
            wishlist.on_got_block(block);
        }

        // piece 1 turned out to be corrupt, needs to be re-downloaded
        for (auto [block, end] = mediator.block_span_[1]; block < end; ++block)
        {
            mediator.client_has_block_.erase(block);
        }
        wishlist.on_got_bad_piece(1);

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [33, 67), not any shrunken spans like [34, 67)
        EXPECT_EQ(std::size(spans), 1);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(34U, requested.count());
        EXPECT_EQ(34U, requested.count(33, 67));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentGotBadPieceSurroundingCompleted)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // pieces 0, 2 completed normally, piece 3 has pending requests
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 134 });
        for (tr_block_index_t block = 0; block < 120; ++block)
        {
            mediator.client_has_block_.insert(block);
            wishlist.on_got_block(block);
        }
        mediator.client_has_piece_.insert(0);
        wishlist.on_piece_completed(0);
        mediator.client_has_piece_.insert(2);
        wishlist.on_piece_completed(2);

        // piece 1 turned out to be corrupt, needs to be re-downloaded
        for (auto [block, end] = mediator.block_span_[1]; block < end; ++block)
        {
            mediator.client_has_block_.erase(block);
        }
        wishlist.on_got_bad_piece(1);

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [33, 67), not any shrunken spans like [34, 67)
        EXPECT_EQ(std::size(spans), 1);
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(34U, requested.count());
        EXPECT_EQ(34U, requested.count(33, 67));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentGot2ConsectutiveBadPieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // pieces 0, 3 completed normally
        wishlist.on_sent_request(tr_block_span_t{ .begin = 0, .end = 134 });
        for (tr_block_index_t block = 0; block < 134; ++block)
        {
            mediator.client_has_block_.insert(block);
            wishlist.on_got_block(block);
        }
        mediator.client_has_piece_.insert(0);
        wishlist.on_piece_completed(0);
        mediator.client_has_piece_.insert(3);
        wishlist.on_piece_completed(3);

        // pieces 1, 2 turned out to be corrupt, need to be re-downloaded
        for (auto [block, end] = mediator.block_span_[1]; block < end; ++block)
        {
            mediator.client_has_block_.erase(block);
        }
        wishlist.on_got_bad_piece(1);
        for (auto [block, end] = mediator.block_span_[2]; block < end; ++block)
        {
            mediator.client_has_block_.erase(block);
        }
        wishlist.on_got_bad_piece(2);

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(67);

        // We should get 1 pan [33, 100),
        // not [33, 67), [66, 100)
        EXPECT_EQ(std::size(spans), 1);

        // Since the spans might overlap if we didn't handle unaligned
        // torrents correctly, we might not get all 67 blocks if there
        // is a bug
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(67U, requested.count());
        EXPECT_EQ(67U, requested.count(33, 100));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentPartiallyWanted)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we want pieces 0, 2
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(2);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [0, 34), [66, 100)
        EXPECT_EQ(std::size(spans), 2);

        // If we don't handle overlapping spans correctly, we might get
        // incorrectly shrunken spans and the download will never complete
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(68, requested.count());
        EXPECT_EQ(34, requested.count(0, 34));
        EXPECT_EQ(34, requested.count(66, 100));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentDeselectedPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially wanted all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we don't want piece 1 anymore
        tr_file_index_t constexpr Deselected = 1;
        mediator.client_wants_piece_.erase(Deselected);
        wishlist.on_files_wanted_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [0, 34), [66, 134)
        EXPECT_EQ(std::size(spans), 2);

        // If we don't handle overlapping spans correctly, we might get
        // incorrectly shrunken spans and the download will never complete
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(102, requested.count());
        EXPECT_EQ(34, requested.count(0, 34));
        EXPECT_EQ(68, requested.count(66, 134));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentDeselected2ConsecutivePieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially wanted all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we don't want pieces 1, 2 anymore
        auto constexpr Deselected = std::array<tr_file_index_t, 2>{ 1, 2 };
        for (auto const idx : Deselected)
        {
            mediator.client_wants_piece_.erase(idx);
        }
        wishlist.on_files_wanted_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get [0, 34), [100, 134)
        EXPECT_EQ(std::size(spans), 2);

        // If we don't handle overlapping spans correctly, we might get
        // incorrectly shrunken spans and the download will never complete
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_EQ(68, requested.count());
        EXPECT_EQ(34, requested.count(0, 34));
        EXPECT_EQ(34, requested.count(100, 134));
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentSelectedPiece)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially wanted all pieces except piece 1
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we want piece 1 now
        tr_file_index_t constexpr Selected = 1;
        mediator.client_wants_piece_.insert(Selected);
        wishlist.on_files_wanted_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get 1 span
        EXPECT_EQ(std::size(spans), 1);

        // If we don't handle overlapping spans correctly, we might get
        // incorrectly shrunken spans and the download will never complete
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_TRUE(requested.has_all());
    }
}

TEST_F(PeerMgrWishlistTest, unalignedTorrentSelected2ConsecutivePieces)
{
    auto const get_spans = [](size_t n_wanted)
    {
        auto mediator = MockMediator{};

        // setup: 4 pieces, (100 / 3 * 16) KiB each, all missing
        // N.B. only the boundary of piece 2 and 3 is aligned
        mediator.block_span_[0] = { .begin = 0, .end = 34 };
        mediator.block_span_[1] = { .begin = 33, .end = 67 };
        mediator.block_span_[2] = { .begin = 66, .end = 100 };
        mediator.block_span_[3] = { .begin = 100, .end = 134 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially wanted pieces 0, 3
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we don't want pieces 1, 2 anymore
        auto constexpr Selected = std::array<tr_file_index_t, 2>{ 1, 2 };
        for (auto const idx : Selected)
        {
            mediator.client_wants_piece_.insert(idx);
        }
        wishlist.on_files_wanted_changed();

        return wishlist.next(n_wanted, PeerHasAllPieces);
    };

    // NB: when all other things are equal in the wishlist, pieces are
    // picked at random so this test -could- pass even if there's a bug.
    // So test several times to shake out any randomness
    static auto constexpr NumRuns = 1000;
    for (int run = 0; run < NumRuns; ++run)
    {
        auto requested = tr_bitfield{ 134 };
        auto const spans = get_spans(134);

        // We should get 1 span
        EXPECT_EQ(std::size(spans), 1);

        // If we don't handle overlapping spans correctly, we might get
        // incorrectly shrunken spans and the download will never complete
        for (auto const& [begin, end] : spans)
        {
            requested.set_span(begin, end);
        }
        EXPECT_TRUE(requested.has_all());
    }
}
