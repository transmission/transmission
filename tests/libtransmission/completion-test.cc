// This file Copyright (C) 2021-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <array>
#include <cstdint>
#include <set>

#include <libtransmission/transmission.h>

#include <libtransmission/block-info.h>
#include <libtransmission/crypto-utils.h> // for tr_rand_obj()
#include <libtransmission/completion.h>

#include "gtest/gtest.h"

using CompletionTest = ::testing::Test;

namespace
{

struct TestTorrent : public tr_completion::torrent_view
{
    std::set<tr_piece_index_t> dnd_pieces;

    [[nodiscard]] bool pieceIsWanted(tr_piece_index_t piece) const final
    {
        return dnd_pieces.count(piece) == 0;
    }
};

auto constexpr BlockSize = uint64_t{ 16 } * 1024U;

} // namespace

TEST_F(CompletionTest, MagnetLink)
{
    auto torrent = TestTorrent{};
    auto block_info = tr_block_info{};
    auto completion = tr_completion(&torrent, &block_info);

    EXPECT_FALSE(completion.hasAll());
    EXPECT_TRUE(completion.hasNone());
    EXPECT_FALSE(completion.hasBlocks({ 0, 1 }));
    EXPECT_FALSE(completion.hasBlocks({ 0, 1000 }));
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_DOUBLE_EQ(0.0, completion.percentDone());
    EXPECT_DOUBLE_EQ(0.0, completion.percentComplete());
    EXPECT_EQ(TR_LEECH, completion.status());
    EXPECT_EQ(0, completion.hasTotal());
    EXPECT_EQ(0, completion.hasValid());
    EXPECT_EQ(0, completion.leftUntilDone());
    EXPECT_EQ(0, completion.sizeWhenDone());
}

TEST_F(CompletionTest, setBlocks)
{
    auto constexpr TotalSize = uint64_t{ BlockSize * 64 * 50000 }; // 50GB
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };

    auto torrent = TestTorrent{};
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_FALSE(completion.blocks().hasAll());
    EXPECT_FALSE(completion.hasAll());
    EXPECT_EQ(0, completion.hasTotal());

    auto bitfield = tr_bitfield{ block_info.blockCount() };
    bitfield.setHasAll();

    // test that the bitfield did get replaced
    completion.setBlocks(bitfield);
    EXPECT_TRUE(completion.blocks().hasAll());
    EXPECT_TRUE(completion.hasAll());
    EXPECT_EQ(block_info.totalSize(), completion.hasTotal());
}

TEST_F(CompletionTest, hasBlock)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);

    EXPECT_FALSE(completion.hasBlock(0));
    EXPECT_FALSE(completion.hasBlock(1));

    completion.addBlock(0);
    EXPECT_TRUE(completion.hasBlock(0));
    EXPECT_FALSE(completion.hasBlock(1));

    completion.addPiece(0);
    EXPECT_TRUE(completion.hasBlock(0));
    EXPECT_TRUE(completion.hasBlock(1));
}

TEST_F(CompletionTest, hasBlocks)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_FALSE(completion.hasBlocks({ 0, 1 }));
    EXPECT_FALSE(completion.hasBlocks({ 0, 2 }));

    completion.addBlock(0);
    EXPECT_TRUE(completion.hasBlocks({ 0, 1 }));
    EXPECT_FALSE(completion.hasBlocks({ 0, 2 }));
}

TEST_F(CompletionTest, hasNone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_TRUE(completion.hasNone());

    completion.addBlock(0);
    EXPECT_FALSE(completion.hasNone());
}

TEST_F(CompletionTest, hasPiece)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that the initial state does not have it
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_FALSE(completion.hasPiece(1));
    EXPECT_EQ(0, completion.hasValid());

    // check that adding a piece means we have it
    completion.addPiece(0);
    EXPECT_TRUE(completion.hasPiece(0));
    EXPECT_FALSE(completion.hasPiece(1));
    EXPECT_EQ(PieceSize, completion.hasValid());

    // check that removing a piece means we don't have it
    completion.removePiece(0);
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_FALSE(completion.hasPiece(1));
    EXPECT_EQ(0, completion.hasValid());

    // check that adding all the blocks in a piece means we have it
    for (tr_block_index_t i = 1, n = block_info.pieceLoc(1).block; i < n; ++i)
    {
        completion.addBlock(i);
    }
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_EQ(0, completion.hasValid());
    completion.addBlock(0);
    EXPECT_TRUE(completion.hasPiece(0));
    EXPECT_EQ(PieceSize, completion.hasValid());
}

TEST_F(CompletionTest, percentCompleteAndDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that in blank-slate initial state, isDone() is false
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_DOUBLE_EQ(0.0, completion.percentComplete());
    EXPECT_DOUBLE_EQ(0.0, completion.percentDone());

    // add half the pieces
    for (size_t i = 0; i < 32; ++i)
    {
        completion.addPiece(i);
    }
    EXPECT_DOUBLE_EQ(0.5, completion.percentComplete());
    EXPECT_DOUBLE_EQ(0.5, completion.percentDone());

    // but marking some of the pieces we have as unwanted
    // should not change percentDone
    for (size_t i = 0; i < 16; ++i)
    {
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_DOUBLE_EQ(0.5, completion.percentComplete());
    EXPECT_DOUBLE_EQ(0.5, completion.percentDone());

    // but marking some of the pieces we DON'T have as unwanted
    // SHOULD change percentDone
    for (size_t i = 32; i < 48; ++i)
    {
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_DOUBLE_EQ(0.5, completion.percentComplete());
    EXPECT_DOUBLE_EQ(2.0 / 3.0, completion.percentDone());
}

TEST_F(CompletionTest, hasTotalAndValid)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that the initial blank-slate state has nothing
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_EQ(0, completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that adding the final piece adjusts by block_info.final_piece_size
    completion.setHasPiece(block_info.pieceCount() - 1, true);
    EXPECT_EQ(block_info.pieceSize(block_info.pieceCount() - 1), completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that adding a non-final piece adjusts by block_info.pieceSize()
    completion.setHasPiece(0, true);
    EXPECT_EQ(block_info.pieceSize(block_info.pieceCount() - 1) + block_info.pieceSize(), completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that removing the final piece adjusts by block_info.final_piece_size
    completion.setHasPiece(block_info.pieceCount() - 1, false);
    EXPECT_EQ(block_info.pieceSize(), completion.hasValid());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that removing a non-final piece adjusts by block_info.pieceSize()
    completion.setHasPiece(0, false);
    EXPECT_EQ(0, completion.hasValid());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that adding an incomplete piece adjusts hasTotal but not hasValid
    completion.addBlock(0);
    EXPECT_EQ(0, completion.hasValid());
    EXPECT_EQ(BlockSize, completion.hasTotal());
}

TEST_F(CompletionTest, leftUntilDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that the initial blank-slate state has nothing
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_EQ(block_info.totalSize(), completion.leftUntilDone());

    // check that adding the final piece adjusts by block_info.final_piece_size
    completion.addPiece(block_info.pieceCount() - 1);
    EXPECT_EQ(block_info.totalSize() - block_info.pieceSize(block_info.pieceCount() - 1), completion.leftUntilDone());

    // check that adding a non-final piece adjusts by block_info.pieceSize()
    completion.addPiece(0);
    EXPECT_EQ(
        block_info.totalSize() - block_info.pieceSize(block_info.pieceCount() - 1) - block_info.pieceSize(),
        completion.leftUntilDone());

    // check that removing the final piece adjusts by block_info.final_piece_size
    completion.removePiece(block_info.pieceCount() - 1);
    EXPECT_EQ(block_info.totalSize() - block_info.pieceSize(), completion.leftUntilDone());

    // check that dnd-flagging a piece we already have affects nothing
    torrent.dnd_pieces.insert(0);
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.totalSize() - block_info.pieceSize(), completion.leftUntilDone());
    torrent.dnd_pieces.clear();
    completion.invalidateSizeWhenDone();

    // check that dnd-flagging a piece we DON'T already have adjusts by block_info.pieceSize()
    torrent.dnd_pieces.insert(1);
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.totalSize() - block_info.pieceSize() * uint64_t{ 2U }, completion.leftUntilDone());
    torrent.dnd_pieces.clear();
    completion.invalidateSizeWhenDone();

    // check that removing a non-final piece adjusts by block_info.pieceSize()
    completion.removePiece(0);
    EXPECT_EQ(block_info.totalSize(), completion.leftUntilDone());

    // check that adding a block adjusts by block_info.block_size
    completion.addBlock(0);
    EXPECT_EQ(block_info.totalSize() - tr_block_info::BlockSize, completion.leftUntilDone());
}

TEST_F(CompletionTest, sizeWhenDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 64 * 50000 }; // 50GB
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that adding or removing blocks or pieces does not affect sizeWhenDone
    auto completion = tr_completion(&torrent, &block_info);
    EXPECT_EQ(block_info.totalSize(), completion.sizeWhenDone());
    completion.addBlock(0);
    EXPECT_EQ(block_info.totalSize(), completion.sizeWhenDone());
    completion.addPiece(0);
    EXPECT_EQ(block_info.totalSize(), completion.sizeWhenDone());
    completion.removePiece(0);
    EXPECT_EQ(block_info.totalSize(), completion.sizeWhenDone());

    // check that flagging complete pieces as dnd does not affect sizeWhenDone
    for (size_t i = 0; i < 32; ++i)
    {
        completion.addPiece(i);
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.totalSize(), completion.sizeWhenDone());

    // check that flagging missing pieces as dnd does not affect sizeWhenDone
    for (size_t i = 32; i < 48; ++i)
    {
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.totalSize() - uint64_t{ 16U } * block_info.pieceSize(), completion.sizeWhenDone());
}

TEST_F(CompletionTest, createPieceBitfield)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // make a completion object that has a random assortment of pieces
    auto completion = tr_completion(&torrent, &block_info);
    auto buf = tr_rand_obj<std::array<char, 65>>();
    ASSERT_EQ(std::size(buf), block_info.pieceCount());
    for (uint64_t i = 0; i < block_info.pieceCount(); ++i)
    {
        if ((buf[i] % 2) != 0)
        {
            completion.addPiece(i);
        }
    }

    // serialize it to a raw bitfield, read it back into a bitfield,
    // and test that the new bitfield matches
    auto const pieces_raw_bitfield = completion.createPieceBitfield();
    tr_bitfield pieces{ size_t{ block_info.pieceCount() } };
    pieces.setRaw(std::data(pieces_raw_bitfield), std::size(pieces_raw_bitfield));
    for (uint64_t i = 0; i < block_info.pieceCount(); ++i)
    {
        EXPECT_EQ(completion.hasPiece(i), pieces.test(i));
    }
}

TEST_F(CompletionTest, setHasPiece)
{
}

TEST_F(CompletionTest, countMissingBytesInPiece)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);

    EXPECT_EQ(block_info.pieceSize(0), completion.countMissingBytesInPiece(0));
    completion.addBlock(0);
    EXPECT_EQ(block_info.pieceSize(0) - tr_block_info::BlockSize, completion.countMissingBytesInPiece(0));
    completion.addPiece(0);
    EXPECT_EQ(0U, completion.countMissingBytesInPiece(0));

    auto const final_piece = block_info.pieceCount() - 1;
    auto const final_block = block_info.blockCount() - 1;
    EXPECT_EQ(block_info.pieceSize(final_piece), completion.countMissingBytesInPiece(final_piece));
    completion.addBlock(final_block);
    EXPECT_EQ(1U, block_info.pieceSize(block_info.pieceCount() - 1));
    EXPECT_TRUE(completion.hasPiece(final_piece));
    EXPECT_EQ(0U, completion.countMissingBytesInPiece(final_piece));
}

TEST_F(CompletionTest, amountDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);

    // make bins s.t. each bin is a single piece
    auto bins = std::array<float, TotalSize / PieceSize>{};

    for (tr_piece_index_t piece = 0; piece < block_info.pieceCount(); ++piece)
    {
        completion.removePiece(piece);
    }
    completion.amountDone(std::data(bins), std::size(bins));
    std::for_each(std::begin(bins), std::end(bins), [](float bin) { EXPECT_DOUBLE_EQ(0.0, bin); });

    // one block
    completion.addBlock(0);
    completion.amountDone(std::data(bins), std::size(bins));
    EXPECT_DOUBLE_EQ(0.0, bins[1]);

    // one piece
    completion.addPiece(0);
    completion.amountDone(std::data(bins), std::size(bins));
    EXPECT_DOUBLE_EQ(1.0, bins[0]);
    EXPECT_DOUBLE_EQ(0.0, bins[1]);

    // all pieces
    for (tr_piece_index_t piece = 0; piece < block_info.pieceCount(); ++piece)
    {
        completion.addPiece(piece);
    }
    completion.amountDone(std::data(bins), std::size(bins));
    std::for_each(std::begin(bins), std::end(bins), [](float bin) { EXPECT_DOUBLE_EQ(1.0, bin); });

    // don't do anything if fed bad input
    auto const backup = bins;
    completion.amountDone(std::data(bins), 0);
    EXPECT_EQ(backup, bins);
}

TEST_F(CompletionTest, countHasBytesInSpan)
{
    // set up a fake torrent
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);

    // torrent is complete
    auto blocks = tr_bitfield{ block_info.blockCount() };
    blocks.setHasAll();
    completion.setBlocks(blocks);

    EXPECT_EQ(TotalSize, completion.countHasBytesInSpan({ 0, TotalSize }));
    EXPECT_EQ(TotalSize, completion.countHasBytesInSpan({ 0, TotalSize + 1 }));
    // test span that's entirely in a single block
    EXPECT_EQ(1, completion.countHasBytesInSpan({ 16, 17 }));
    EXPECT_EQ(16, completion.countHasBytesInSpan({ 16, 32 }));
    // test edge cases on block boundary
    EXPECT_EQ(1, completion.countHasBytesInSpan({ BlockSize - 1, BlockSize }));
    EXPECT_EQ(1, completion.countHasBytesInSpan({ BlockSize, BlockSize + 1 }));
    EXPECT_EQ(2, completion.countHasBytesInSpan({ BlockSize - 1, BlockSize + 1 }));
    // test edge cases on piece boundary
    EXPECT_EQ(1, completion.countHasBytesInSpan({ PieceSize - 1, PieceSize }));
    EXPECT_EQ(1, completion.countHasBytesInSpan({ PieceSize, PieceSize + 1 }));
    EXPECT_EQ(2, completion.countHasBytesInSpan({ PieceSize - 1, PieceSize + 1 }));

    // test span that has a middle block
    EXPECT_EQ(BlockSize * 3, completion.countHasBytesInSpan({ 0, BlockSize * 3 }));
    EXPECT_EQ(BlockSize * 2, completion.countHasBytesInSpan({ BlockSize / 2, BlockSize * 2 + BlockSize / 2 }));

    // test span where first block is missing
    blocks.unset(0);
    completion.setBlocks(blocks);
    EXPECT_EQ(BlockSize * 2, completion.countHasBytesInSpan({ 0, BlockSize * 3 }));
    EXPECT_EQ(BlockSize * 1.5, completion.countHasBytesInSpan({ BlockSize / 2, BlockSize * 2 + BlockSize / 2 }));
    // test span where final block is missing
    blocks.setHasAll();
    blocks.unset(2);
    completion.setBlocks(blocks);
    EXPECT_EQ(BlockSize * 2, completion.countHasBytesInSpan({ 0, BlockSize * 3 }));
    EXPECT_EQ(BlockSize * 1.5, completion.countHasBytesInSpan({ BlockSize / 2, BlockSize * 2 + BlockSize / 2 }));
}

TEST_F(CompletionTest, wantNone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, &block_info);

    // we have some data
    completion.addBlock(0);

    // and want nothing
    for (tr_piece_index_t i = 0, n = block_info.blockCount(); i < n; ++i)
    {
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();

    EXPECT_LE(completion.hasTotal(), completion.sizeWhenDone());
    EXPECT_EQ(completion.hasTotal(), block_info.BlockSize);
    EXPECT_EQ(completion.sizeWhenDone(), block_info.BlockSize);
    EXPECT_LE(completion.leftUntilDone(), completion.sizeWhenDone());
    EXPECT_EQ(completion.leftUntilDone(), 0);

    // but we magically get a block anyway
    completion.addBlock(1);

    EXPECT_LE(completion.hasTotal(), completion.sizeWhenDone());
    EXPECT_EQ(completion.hasTotal(), 2 * block_info.BlockSize);
    EXPECT_EQ(completion.sizeWhenDone(), 2 * block_info.BlockSize);
    EXPECT_LE(completion.leftUntilDone(), completion.sizeWhenDone());
    EXPECT_EQ(completion.leftUntilDone(), 0);
}
