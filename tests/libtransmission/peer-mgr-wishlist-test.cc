// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cstddef> // size_t
#include <map>
#include <memory>
#include <set>

#define LIBTRANSMISSION_PEER_MODULE

#include <libtransmission/transmission.h>

#include <libtransmission/bitfield.h>
#include <libtransmission/crypto-utils.h>
#include <libtransmission/peer-mgr-wishlist.h>

#include "gtest/gtest.h"

class PeerMgrWishlistTest : public ::testing::Test
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
        tr_piece_index_t piece_count_ = 0;
        bool is_sequential_download_ = false;
        tr_piece_index_t sequential_download_from_piece_ = 0;

        PeerMgrWishlistTest& parent_;

        explicit MockMediator(PeerMgrWishlistTest& parent)
            : parent_{ parent }
        {
        }

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
            return piece_count_;
        }

        [[nodiscard]] tr_priority_t priority(tr_piece_index_t piece) const override
        {
            return piece_priority_[piece];
        }

        [[nodiscard]] bool try_hotswap(tr_block_index_t /*block*/, tr_peer const* /*peer*/) const override
        {
            // Ignore hotswaps in tests
            return false;
        }

        [[nodiscard]] libtransmission::ObserverTag observe_files_wanted_changed(
            libtransmission::SimpleObservable<tr_torrent*, tr_file_index_t const*, tr_file_index_t, bool>::Observer observer)
            override
        {
            return parent_.files_wanted_changed_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_peer_disconnect(
            libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&, tr_bitfield const&>::Observer observer) override
        {
            return parent_.peer_disconnect_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_bad_piece(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) override
        {
            return parent_.got_bad_piece_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_bitfield(
            libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&>::Observer observer) override
        {
            return parent_.got_bitfield_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_choke(
            libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&>::Observer observer) override
        {
            return parent_.got_choke_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_have(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) override
        {
            return parent_.got_have_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_have_all(
            libtransmission::SimpleObservable<tr_torrent*>::Observer observer) override
        {
            return parent_.got_have_all_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_got_reject(
            libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_index_t>::Observer observer) override
        {
            return parent_.got_reject_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_piece_completed(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) override
        {
            return parent_.piece_completed_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_priority_changed(
            libtransmission::SimpleObservable<tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t>::Observer
                observer) override
        {
            return parent_.priority_changed_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_sent_cancel(
            libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_index_t>::Observer observer) override
        {
            return parent_.sent_cancel_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_sent_request(
            libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_span_t>::Observer observer) override
        {
            return parent_.sent_request_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_sequential_download_changed(
            libtransmission::SimpleObservable<tr_torrent*, bool>::Observer observer) override
        {
            return parent_.sequential_download_changed_.observe(std::move(observer));
        }

        [[nodiscard]] libtransmission::ObserverTag observe_sequential_download_from_piece_changed(
            libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t>::Observer observer) override
        {
            return parent_.sequential_download_from_piece_changed_.observe(std::move(observer));
        }
    };

    libtransmission::SimpleObservable<tr_torrent*, tr_file_index_t const*, tr_file_index_t, bool> files_wanted_changed_;
    libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&, tr_bitfield const&> peer_disconnect_;
    libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t> got_bad_piece_;
    libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&> got_bitfield_;
    libtransmission::SimpleObservable<tr_torrent*, tr_bitfield const&> got_choke_;
    libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t> got_have_;
    libtransmission::SimpleObservable<tr_torrent*> got_have_all_;
    libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_index_t> got_reject_;
    libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_index_t> sent_cancel_;
    libtransmission::SimpleObservable<tr_torrent*, tr_peer*, tr_block_span_t> sent_request_;
    libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t> piece_completed_;
    libtransmission::SimpleObservable<tr_torrent*, tr_file_index_t const*, tr_file_index_t, tr_priority_t> priority_changed_;
    libtransmission::SimpleObservable<tr_torrent*, bool> sequential_download_changed_;
    libtransmission::SimpleObservable<tr_torrent*, tr_piece_index_t> sequential_download_from_piece_changed_;

    static auto constexpr PeerHasAllPieces = [](tr_piece_index_t)
    {
        return true;
    };
};

TEST_F(PeerMgrWishlistTest, doesNotRequestPiecesThatAreNotWanted)
{
    auto mediator = MockMediator{ *this };

    // setup: three pieces, all missing
    mediator.piece_count_ = 3;
    mediator.block_span_[0] = { 0, 100 };
    mediator.block_span_[1] = { 100, 200 };
    mediator.block_span_[2] = { 200, 250 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // but we only want the first piece
    mediator.client_wants_piece_.insert(0);

    // we should only get the first piece back
    auto const spans = Wishlist{ mediator }.next(nullptr, 1000, PeerHasAllPieces);
    ASSERT_EQ(1U, std::size(spans));
    EXPECT_EQ(mediator.block_span_[0].begin, spans[0].begin);
    EXPECT_EQ(mediator.block_span_[0].end, spans[0].end);
}

TEST_F(PeerMgrWishlistTest, onlyRequestBlocksThePeerHas)
{
    auto mediator = MockMediator{ *this };

    // setup: three pieces, all missing
    mediator.piece_count_ = 3;
    mediator.block_span_[0] = { 0, 100 };
    mediator.block_span_[1] = { 100, 200 };
    mediator.block_span_[2] = { 200, 250 };

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
    auto const spans = Wishlist{ mediator }.next(nullptr, 250, IsPieceOne);
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
    auto mediator = MockMediator{ *this };

    // setup: three pieces, all missing
    mediator.piece_count_ = 3;
    mediator.block_span_[0] = { 0, 100 };
    mediator.block_span_[1] = { 100, 200 };
    mediator.block_span_[2] = { 200, 250 };

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
    sent_request_.emit(nullptr, nullptr, { 0, 10 });

    // even if we ask wishlist for all the blocks,
    // it should omit blocks [0..10) from the return set
    auto const spans = wishlist.next(nullptr, 250, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 250 };

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

        return Wishlist{ mediator }.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: four pieces, all missing
        mediator.piece_count_ = 4;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };
        mediator.block_span_[3] = { 300, 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // and we want all pieces
        for (tr_piece_index_t i = 0; i < 4; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // we enabled sequential download, from piece 2
        mediator.is_sequential_download_ = true;
        mediator.sequential_download_from_piece_ = 2;

        return Wishlist{ mediator }.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto mediator = MockMediator{ *this };

    // setup: three pieces, all missing
    mediator.piece_count_ = 3;
    mediator.block_span_[0] = { 0, 100 };
    mediator.block_span_[1] = { 100, 200 };
    mediator.block_span_[2] = { 200, 250 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        mediator.client_wants_piece_.insert(i);
    }

    // but we only ask for 10 blocks,
    // so that's how many we should get back
    static constexpr auto NumWanted = 10U;
    auto const spans = Wishlist{ mediator }.next(nullptr, NumWanted, PeerHasAllPieces);
    auto n_got = size_t{};
    for (auto const& [begin, end] : spans)
    {
        n_got += end - begin;
    }
    EXPECT_EQ(NumWanted, n_got);
}

TEST_F(PeerMgrWishlistTest, prefersHighPriorityPieces)
{
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // and the second piece is high priority
        mediator.piece_priority_[1] = TR_PRI_HIGH;

        return Wishlist{ mediator }.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, same size
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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

        return Wishlist{ mediator }.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // but some pieces are rarer than others
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 3;
        mediator.piece_replication_[2] = 2;

        return Wishlist{ mediator }.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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
        peer_disconnect_.emit(nullptr, have, tr_bitfield{ 300 });

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 1;

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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
        sent_request_.emit(nullptr, nullptr, { 0, 50 });
        sent_request_.emit(nullptr, nullptr, { 100, 150 });
        sent_request_.emit(nullptr, nullptr, { 200, 250 });

        // we request the rest of a random piece
        auto const random_piece = tr_rand_int(3U);
        sent_request_.emit(nullptr, nullptr, { (random_piece * 100U) + 50U, (random_piece + 1U) * 100U });

        // the random piece turns out to be corrupted, so all blocks should be missing again
        got_bad_piece_.emit(nullptr, random_piece);

        return std::pair{ wishlist.next(nullptr, n_wanted, PeerHasAllPieces), random_piece };
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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
        got_bitfield_.emit(nullptr, have);

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 3;
        // mediator.piece_replication_[1] = 3;

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we requested block 0 from someone, the wishlist should resort the
        // candidate list cache
        sent_request_.emit(nullptr, nullptr, { 0, 1 });

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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
        got_have_.emit(nullptr, 0);

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 3;

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        sent_request_.emit(nullptr, nullptr, { 0, 250 });

        // a peer sent a "Choke" message, which cancels some active requests
        tr_bitfield requested{ 300 };
        requested.set_span(0, 10);
        got_choke_.emit(nullptr, requested);

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
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
        got_have_all_.emit(nullptr);

        // this is what a real mediator should return at this point:
        // mediator.piece_replication_[0] = 2;
        // mediator.piece_replication_[1] = 3;
        // mediator.piece_replication_[2] = 4;

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        sent_request_.emit(nullptr, nullptr, { 0, 250 });

        // a peer sent some "Reject" messages, which cancels active requests
        auto rejected_bitfield = tr_bitfield{ 300 };
        for (tr_block_index_t i = 0, n = tr_rand_int(250U); i <= n; ++i)
        {
            auto const block = tr_rand_int(250U);
            rejected_bitfield.set(block);
            got_reject_.emit(nullptr, nullptr, block);
        }

        return std::pair{ wishlist.next(nullptr, n_wanted, PeerHasAllPieces), std::move(rejected_bitfield) };
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: two pieces, all missing
        mediator.piece_count_ = 2;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;

        // and we want everything
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 50 blocks of each piece
        sent_request_.emit(nullptr, nullptr, { 0, 50 });
        sent_request_.emit(nullptr, nullptr, { 100, 150 });

        // a peer sent a "Reject" messages, which cancels active requests
        auto const random_piece = tr_rand_int(2U);
        got_reject_.emit(nullptr, nullptr, mediator.block_span_[random_piece].begin);

        return std::pair{ wishlist.next(nullptr, n_wanted, PeerHasAllPieces), 1U - random_piece };
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we have active requests to the first 250 blocks
        sent_request_.emit(nullptr, nullptr, { 0, 250 });

        // we sent some "Cancel" messages
        auto cancelled_bitfield = tr_bitfield{ 300 };
        for (tr_block_index_t i = 0, n = tr_rand_int(250U); i <= n; ++i)
        {
            auto const block = tr_rand_int(250U);
            cancelled_bitfield.set(block);
            sent_cancel_.emit(nullptr, nullptr, block);
        }

        return std::pair{ wishlist.next(nullptr, n_wanted, PeerHasAllPieces), std::move(cancelled_bitfield) };
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peers has all pieces
        mediator.piece_replication_[0] = 2;
        mediator.piece_replication_[1] = 2;
        mediator.piece_replication_[2] = 2;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we sent "Request" messages
        sent_request_.emit(nullptr, nullptr, { 0, 120 });

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto mediator = MockMediator{ *this };

    // setup: three pieces, piece 0 is nearly complete
    mediator.piece_count_ = 3;
    mediator.block_span_[0] = { 0, 100 };
    mediator.block_span_[1] = { 100, 200 };
    mediator.block_span_[2] = { 200, 300 };

    // peer has all pieces
    mediator.piece_replication_[0] = 1;
    mediator.piece_replication_[1] = 1;
    mediator.piece_replication_[2] = 1;

    // and we want everything
    for (tr_piece_index_t i = 0; i < 3; ++i)
    {
        mediator.client_wants_piece_.insert(i);
    }

    // allow the wishlist to build its cache, it should have all 3 pieces
    // at this point
    auto wishlist = Wishlist{ mediator };

    // we just completed piece 0
    mediator.client_has_piece_.insert(0);
    piece_completed_.emit(nullptr, 0);

    // receiving a "piece_completed" signal removes the piece from the
    // wishlist's cache, its blocks should not be in the return set.
    auto const spans = wishlist.next(nullptr, 10, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // a file priority changed, the cache should be rebuilt.
        // let's say the file was in piece 1
        mediator.piece_priority_[1] = TR_PRI_HIGH;
        priority_changed_.emit(nullptr, nullptr, 0U, TR_PRI_HIGH);

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: three pieces, all missing
        mediator.piece_count_ = 3;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;

        // and we want everything
        for (tr_piece_index_t i = 0; i < 3; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // the sequential download setting was changed,
        // the cache should be rebuilt
        mediator.is_sequential_download_ = true;
        sequential_download_changed_.emit(nullptr, true);

        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: four pieces, all missing
        mediator.piece_count_ = 4;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };
        mediator.block_span_[3] = { 300, 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // and we want all pieces
        for (tr_piece_index_t i = 0; i < 4; ++i)
        {
            mediator.client_wants_piece_.insert(i);
        }

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we enabled sequential download, from piece 2
        mediator.is_sequential_download_ = true;
        sequential_download_changed_.emit(nullptr, true);
        mediator.sequential_download_from_piece_ = 2;
        sequential_download_from_piece_changed_.emit(nullptr, 2);

        // the sequential download setting was changed,
        // the candidate list should be resorted
        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
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
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: four pieces, all missing
        mediator.piece_count_ = 4;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };
        mediator.block_span_[3] = { 300, 400 };

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
        files_wanted_changed_.emit(nullptr, nullptr, 0, true);

        // a candidate should be inserted into the wishlist for
        // piece 2 and piece 3
        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
    };

    // We should request all 4 pieces here.
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
        EXPECT_EQ(350U, requested.count());
        EXPECT_NE(0U, requested.count(0, 100));
        EXPECT_NE(0U, requested.count(100, 200));
        EXPECT_NE(0U, requested.count(200, 300));
        EXPECT_NE(0U, requested.count(300, 400));
    }
}

TEST_F(PeerMgrWishlistTest, setFileWantedUpdatesCandidateListRemove)
{
    auto const get_spans = [this](size_t n_wanted)
    {
        auto mediator = MockMediator{ *this };

        // setup: four pieces, all missing
        mediator.piece_count_ = 4;
        mediator.block_span_[0] = { 0, 100 };
        mediator.block_span_[1] = { 100, 200 };
        mediator.block_span_[2] = { 200, 300 };
        mediator.block_span_[3] = { 300, 400 };

        // peer has all pieces
        mediator.piece_replication_[0] = 1;
        mediator.piece_replication_[1] = 1;
        mediator.piece_replication_[2] = 1;
        mediator.piece_replication_[3] = 1;

        // we initially only want the all 4 pieces
        mediator.client_wants_piece_.insert(0);
        mediator.client_wants_piece_.insert(1);
        mediator.client_wants_piece_.insert(2);
        mediator.client_wants_piece_.insert(3);

        // allow the wishlist to build its cache
        auto wishlist = Wishlist{ mediator };

        // we no longer want the file that consists of piece 2 and piece 3
        mediator.client_wants_piece_.erase(2);
        mediator.client_wants_piece_.erase(3);
        files_wanted_changed_.emit(nullptr, nullptr, 0, true);

        // the candidate objects for piece 2 and piece 3 should be removed
        return wishlist.next(nullptr, n_wanted, PeerHasAllPieces);
    };

    // We should request only the first 2 pieces here.
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
