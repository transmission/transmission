/*
 * This file Copyright (C) Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <array>
#include <cstdint>
#include <set>

#include "transmission.h"

#include "block-info.h"
#include "completion.h"
#include "crypto-utils.h"

#include "gtest/gtest.h"

using CompletionTest = ::testing::Test;

namespace
{

struct TestTorrent : public tr_completion::torrent_view
{
    std::set<tr_piece_index_t> dnd_pieces;

    [[nodiscard]] bool pieceIsDnd(tr_piece_index_t piece) const final
    {
        return dnd_pieces.count(piece) != 0;
    }
};

auto constexpr BlockSize = uint64_t{ 16 * 1024 };

} // namespace

TEST_F(CompletionTest, MagnetLink)
{
    auto torrent = TestTorrent{};
    auto block_info = tr_block_info{};
    auto completion = tr_completion(&torrent, block_info);

    EXPECT_FALSE(completion.hasAll());
    EXPECT_TRUE(completion.hasNone());
    EXPECT_FALSE(completion.hasBlocks({ 0, 1 }));
    EXPECT_FALSE(completion.hasBlocks({ 0, 1000 }));
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_FALSE(completion.isDone());
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
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };

    auto torrent = TestTorrent{};
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, block_info);
    EXPECT_FALSE(completion.blocks().hasAll());
    EXPECT_FALSE(completion.hasAll());
    EXPECT_EQ(0, completion.hasTotal());

    auto bitfield = tr_bitfield{ block_info.n_blocks };
    bitfield.setHasAll();

    // test that the bitfield did get replaced
    completion.setBlocks(bitfield);
    EXPECT_TRUE(completion.blocks().hasAll());
    EXPECT_TRUE(completion.hasAll());
    EXPECT_EQ(block_info.total_size, completion.hasTotal());
}

TEST_F(CompletionTest, hasBlock)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };
    auto completion = tr_completion(&torrent, block_info);

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

    auto completion = tr_completion(&torrent, block_info);
    EXPECT_FALSE(completion.hasBlocks(tr_block_range_t{ 0, 0 }));
    EXPECT_FALSE(completion.hasBlocks(tr_block_range_t{ 0, 1 }));

    completion.addBlock(0);
    EXPECT_TRUE(completion.hasBlocks(tr_block_range_t{ 0, 0 }));
    EXPECT_FALSE(completion.hasBlocks(tr_block_range_t{ 0, 1 }));
}

TEST_F(CompletionTest, hasNone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    auto completion = tr_completion(&torrent, block_info);
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
    auto completion = tr_completion(&torrent, block_info);
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
    for (size_t i = 1; i < block_info.n_blocks_in_piece; ++i)
    {
        completion.addBlock(i);
    }
    EXPECT_FALSE(completion.hasPiece(0));
    EXPECT_EQ(0, completion.hasValid());
    completion.addBlock(0);
    EXPECT_TRUE(completion.hasPiece(0));
    EXPECT_EQ(PieceSize, completion.hasValid());
}

TEST_F(CompletionTest, isDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that in blank-slate initial state, isDone() is false
    auto completion = tr_completion(&torrent, block_info);
    EXPECT_FALSE(completion.isDone());
    EXPECT_EQ(TR_LEECH, completion.status());
    EXPECT_EQ(block_info.total_size, completion.leftUntilDone());

    // check that we're done if we have all the blocks
    auto left = block_info.total_size;
    for (size_t i = 1; i < block_info.n_blocks; ++i)
    {
        completion.addBlock(i);
        left -= block_info.block_size;
        EXPECT_EQ(left, completion.leftUntilDone());
    }
    EXPECT_FALSE(completion.isDone());
    completion.addBlock(0);
    EXPECT_EQ(0, completion.leftUntilDone());
    EXPECT_TRUE(completion.isDone());
    EXPECT_EQ(TR_SEED, completion.status());

    // check that not having all the pieces (and we want all) means we're not done
    completion.removePiece(0);
    EXPECT_FALSE(completion.isDone());
    EXPECT_EQ(TR_LEECH, completion.status());

    // check that having all the pieces we want, even if it's not ALL pieces, means we're done
    torrent.dnd_pieces.insert(0);
    completion.invalidateSizeWhenDone();
    EXPECT_TRUE(completion.isDone());
    EXPECT_EQ(TR_PARTIAL_SEED, completion.status());

    // but if we decide we do want that missing piece after all, then we're not done
    torrent.dnd_pieces.erase(0);
    completion.invalidateSizeWhenDone();
    EXPECT_FALSE(completion.isDone());
    EXPECT_EQ(TR_LEECH, completion.status());
}

TEST_F(CompletionTest, percentCompleteAndDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 };
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that in blank-slate initial state, isDone() is false
    auto completion = tr_completion(&torrent, block_info);
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
    auto completion = tr_completion(&torrent, block_info);
    EXPECT_EQ(0, completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that adding the final piece adjusts by block_info.final_piece_size
    completion.addPiece(block_info.n_pieces - 1);
    EXPECT_EQ(block_info.final_piece_size, completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that adding a non-final piece adjusts by block_info.piece_size
    completion.addPiece(0);
    EXPECT_EQ(block_info.final_piece_size + block_info.piece_size, completion.hasTotal());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that removing the final piece adjusts by block_info.final_piece_size
    completion.removePiece(block_info.n_pieces - 1);
    EXPECT_EQ(block_info.piece_size, completion.hasValid());
    EXPECT_EQ(completion.hasValid(), completion.hasTotal());

    // check that removing a non-final piece adjusts by block_info.piece_size
    completion.removePiece(0);
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
    auto completion = tr_completion(&torrent, block_info);
    EXPECT_EQ(block_info.total_size, completion.leftUntilDone());

    // check that adding the final piece adjusts by block_info.final_piece_size
    completion.addPiece(block_info.n_pieces - 1);
    EXPECT_EQ(block_info.total_size - block_info.final_piece_size, completion.leftUntilDone());

    // check that adding a non-final piece adjusts by block_info.piece_size
    completion.addPiece(0);
    EXPECT_EQ(block_info.total_size - block_info.final_piece_size - block_info.piece_size, completion.leftUntilDone());

    // check that removing the final piece adjusts by block_info.final_piece_size
    completion.removePiece(block_info.n_pieces - 1);
    EXPECT_EQ(block_info.total_size - block_info.piece_size, completion.leftUntilDone());

    // check that dnd-flagging a piece we already have affects nothing
    torrent.dnd_pieces.insert(0);
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.total_size - block_info.piece_size, completion.leftUntilDone());
    torrent.dnd_pieces.clear();
    completion.invalidateSizeWhenDone();

    // check that dnd-flagging a piece we DON'T already have adjusts by block_info.piece_size
    torrent.dnd_pieces.insert(1);
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.total_size - block_info.piece_size * 2, completion.leftUntilDone());
    torrent.dnd_pieces.clear();
    completion.invalidateSizeWhenDone();

    // check that removing a non-final piece adjusts by block_info.piece_size
    completion.removePiece(0);
    EXPECT_EQ(block_info.total_size, completion.leftUntilDone());

    // check that adding a block adjusts by block_info.block_size
    completion.addBlock(0);
    EXPECT_EQ(block_info.total_size - block_info.block_size, completion.leftUntilDone());
}

TEST_F(CompletionTest, sizeWhenDone)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // check that adding or removing blocks or pieces does not affect sizeWhenDone
    auto completion = tr_completion(&torrent, block_info);
    EXPECT_EQ(block_info.total_size, completion.sizeWhenDone());
    completion.addBlock(0);
    EXPECT_EQ(block_info.total_size, completion.sizeWhenDone());
    completion.addPiece(0);
    EXPECT_EQ(block_info.total_size, completion.sizeWhenDone());
    completion.removePiece(0);
    EXPECT_EQ(block_info.total_size, completion.sizeWhenDone());

    // check that flagging complete pieces as dnd does not affect sizeWhenDone
    for (size_t i = 0; i < 32; ++i)
    {
        completion.addPiece(i);
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.total_size, completion.sizeWhenDone());

    // check that flagging missing pieces as dnd does not affect sizeWhenDone
    for (size_t i = 32; i < 48; ++i)
    {
        torrent.dnd_pieces.insert(i);
    }
    completion.invalidateSizeWhenDone();
    EXPECT_EQ(block_info.total_size - 16 * block_info.piece_size, completion.sizeWhenDone());
}

TEST_F(CompletionTest, createPieceBitfield)
{
    auto torrent = TestTorrent{};
    auto constexpr TotalSize = uint64_t{ BlockSize * 4096 } + 1;
    auto constexpr PieceSize = uint64_t{ BlockSize * 64 };
    auto const block_info = tr_block_info{ TotalSize, PieceSize };

    // make a completion object that has a random assortment of pieces
    auto completion = tr_completion(&torrent, block_info);
    auto buf = std::array<char, 64>{};
    EXPECT_TRUE(tr_rand_buffer(std::data(buf), std::size(buf)));
    for (uint64_t i = 0; i < block_info.n_pieces; ++i)
    {
        if (buf[i] % 2)
        {
            completion.addPiece(i);
        }
    }

    // serialize it to a raw bitfield, read it back into a bitfield,
    // and test that the new bitfield matches
    auto const pieces_raw_bitfield = completion.createPieceBitfield();
    tr_bitfield pieces{ block_info.n_pieces };
    pieces.setRaw(std::data(pieces_raw_bitfield), std::size(pieces_raw_bitfield));
    for (uint64_t i = 0; i < block_info.n_pieces; ++i)
    {
        EXPECT_EQ(completion.hasPiece(i), pieces.test(i));
    }
}

TEST_F(CompletionTest, countMissing)
{
}

TEST_F(CompletionTest, amountDone)
{
}

TEST_F(CompletionTest, status)
{
}
